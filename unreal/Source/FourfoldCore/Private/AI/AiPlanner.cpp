// Fourfold core - port of game/actors/ai_planner.gd.
#include "AI/AiPlanner.h"

#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ff {

bool ai_kit_has(const AiKit& k, int element) {
	for (const auto& kv : k)
		if (kv.first == element) return true;
	return false;
}

std::vector<int>* ai_kit_subs(AiKit& k, int element) {
	for (auto& kv : k)
		if (kv.first == element) return &kv.second;
	return nullptr;
}

void AiParams::merge(const Dict& d) {
	for (const auto& kv : d) {
		const std::string& k = kv.first;
		const Value& v = kv.second;
		if (k == "reaction") reaction = vnum(v);
		else if (k == "counter") counter = vnum(v);
		else if (k == "misjudge") misjudge = vnum(v);
		else if (k == "timing_err") timing_err = vnum(v);
		else if (k == "aggression") aggression = vnum(v);
		else if (k == "tiers") tiers = vint(v);
		else if (k == "chain") chain = vint(v);
		else if (k == "env") env = v.truthy();
		else if (k == "punish") punish = v.truthy();
		else if (k == "weave") weave = v.truthy();
		else if (k == "recent") recent = v.as_dict();
		else if (k == "only") {
			only.clear();
			for (const Value& x : v.as_array()) only.push_back(vstr(x));
		}
	}
}

bool AiOption::has_reason(const std::string& r) const { return std::find(reasons.begin(), reasons.end(), r) != reasons.end(); }

Dict AiOption::to_dict() const {
	Array rs;
	for (const std::string& r : reasons) rs.append(r);
	return D({{"id", id}, {"element", element}, {"sub", sub}, {"slot", slot}, {"press", press}, {"tier", tier}, {"hold", hold},
	          {"perfect", perfect}, {"p", p}, {"outcome", outcome}, {"band", band}, {"ratio", ratio}, {"value", value},
	          {"utility", utility}, {"kind", kind}, {"press_in", press_in}, {"release_in", release_in}, {"guard_for", guard_for},
	          {"gesture", gesture}, {"label", label}, {"mode", mode}, {"score", score}, {"reasons", rs}, {"aim_body", aim_body},
	          {"aim", aim}, {"weave", weave}});
}

namespace AiPlanner {

const char* const COUNTER_SLOTS[9] = {"guard", "push", "sink", "tech", "strike", "thrust", "ground", "sweep", "evade_hold"};
const char* const OFFENSE_SLOTS[5] = {"strike", "thrust", "ground", "sweep", "tech"};

std::vector<std::string> counter_slots() { return std::vector<std::string>(COUNTER_SLOTS, COUNTER_SLOTS + 9); }
std::vector<std::string> offense_slots() { return std::vector<std::string>(OFFENSE_SLOTS, OFFENSE_SLOTS + 5); }
std::vector<std::string> attack_slots() { return {"strike", "thrust", "ground", "sweep"}; }

namespace {
struct NamedValue {
	const char* k;
	double v;
};
const NamedValue kOutcomeValue[] = {
    {"reclaim", 1.0}, {"redirect", 0.9}, {"reflect", 0.9}, {"capture", 0.8}, {"sink", 0.8}, {"transform", 0.8}, {"absorb", 0.8},
    {"extinguish", 0.8}, {"ground", 0.8}, {"neutralize", 0.8}, {"disrupt", 0.8}, {"shatter", 0.75}, {"block", 0.6}, {"deflect", 0.6},
    {"disperse", 0.6}, {"clash", 0.55}, {"push", 0.4}, {"heat", 0.2}, {"bend", 0.2}, {"weaken", 0.15}, {"slow", 0.15},
    {"pass", -1.0}, {"amplify", -1.0}, {"overwhelm", -1.0}, {"conduct", -1.0}, {"fail", -1.0}, {"", -1.0},
};
const NamedValue kKitValue[] = {
    {"air_catch", 1.0}, {"air_spit", 1.0}, {"air_compress", 0.8}, {"air_contest", 0.7}, {"air_cool", 0.55}, {"air_cut", 0.8},
    {"air_infuse", 0.6}, {"air_pop", 0.75}, {"air_shatter", 0.75}, {"air_shrink", 0.5}, {"air_slow_bend", 0.3}, {"air_snuff", 0.8},
    {"air_spatter", -0.5}, {"air_split", 0.5}, {"air_still", 0.8},
    {"earth_absorb_face", 0.8}, {"earth_bolt_grit", 0.8}, {"earth_crust", 0.8}, {"earth_drag", 0.45}, {"earth_embed", 0.6},
    {"earth_face_heat", 0.8}, {"earth_feed_face", 0.8}, {"earth_glass_beads", 0.8}, {"earth_glass_ground", 0.8},
    {"earth_glassify", 0.8}, {"earth_glaze", 0.8}, {"earth_magnet_catch", 1.0}, {"earth_melt_in", 0.8}, {"earth_mud", 0.8},
    {"earth_plate_bolt", 0.4}, {"earth_plate_heat", 0.45}, {"earth_quench", 0.8}, {"earth_ram_blocked", 0.3},
    {"earth_ram_both", 0.3}, {"earth_ram_push", 0.9}, {"earth_rod_ground", 0.8}, {"earth_rod_melt", 0.4}, {"earth_set", 0.8},
    {"earth_smother", 0.8}, {"earth_spike_stop", 0.8}, {"earth_stick", 0.8}, {"earth_wrap", 0.9},
    {"fire_aegis_melt", 0.8}, {"fire_blown", 0.6}, {"fire_body_burst", -0.5}, {"fire_burn", 0.8}, {"fire_charge_body", 0.5},
    {"fire_conduct_owner", 0.85}, {"fire_counter_blast", 0.9}, {"fire_dampen", 0.45}, {"fire_disrupt_zone", 0.8},
    {"fire_evaporate", 0.8}, {"fire_fanned", -1.0}, {"fire_fill_void", 0.8}, {"fire_fulgurite", 0.8}, {"fire_glassify", 0.8},
    {"fire_guard_absorb", 0.8}, {"fire_heat", 0.5}, {"fire_melt", 0.8}, {"fire_reactive", 0.6}, {"fire_smother", -1.0},
    {"fire_snuffed", -1.0}, {"fire_static", 0.8}, {"fire_static_full", 0.6}, {"fire_suppressed", -1.0}, {"fire_tornado", -1.0},
    {"water_brittle", 0.8}, {"water_burn", -1.0}, {"water_carry", 0.9}, {"water_condense_in", 0.8}, {"water_dampen", 0.45},
    {"water_drown", 0.8}, {"water_feed", -1.0}, {"water_freeze", 0.8}, {"water_hot_block", 0.6}, {"water_melt", 0.8},
    {"water_quench", 0.8}, {"water_ridge", 0.8}, {"water_skin", 0.9}, {"water_sling", 1.0},
};
const NamedValue kKitKeywords[] = {{"catch", 1.0}, {"sling", 1.0}, {"return", 0.9}, {"burn", -1.0}, {"fanned", -1.0}, {"feed", -1.0},
                                   {"smother", -1.0}, {"snuff", -1.0}, {"weak", 0.3}, {"slow", 0.3}, {"bend", 0.3}};
const NamedValue kRoleWeight[] = {{"poke", 0.5}, {"zone", 0.42}, {"finisher", 0.55}, {"setup", 0.35}, {"counter", 0.2}};
const char* const kChannelOf[][2] = {{"flame", "H"}, {"blue_fire", "H"}, {"steam", "H"}, {"lightning", "E"}, {"frost", "C"},
                                     {"blast", "P"}, {"gust", "P"}, {"sound", "P"}, {"water", "K"}, {"sand", "K"},
                                     {"vacuum", "P"}, {"stone", "K"}, {"metal", "K"}, {"ice", "K"}, {"vine", "P"},
                                     {"lava_wave", "H"}, {"fire_field", "H"}, {"tornado", "P"}, {"mist", "P"}, {"ember", "H"}};
const char* const kLegacyVolume[][2] = {{"fire_attack", "flame"}, {"air_attack", "gust"}, {"water_attack", "water"}, {"lightning", "lightning"}};

bool ap_lookup(const NamedValue* table, size_t n, const std::string& k, double* out) {
	for (size_t i = 0; i < n; ++i)
		if (k == table[i].k) {
			*out = table[i].v;
			return true;
		}
	return false;
}
double ap_role_weight(const std::string& role, double def) {
	double v = def;
	ap_lookup(kRoleWeight, sizeof(kRoleWeight) / sizeof(kRoleWeight[0]), role, &v);
	return v;
}
std::string ap_channel_of(const std::string& cls) {
	for (const auto& p : kChannelOf)
		if (cls == p[0]) return p[1];
	return "P";
}
const char* ap_legacy_volume(const std::string& id) {
	for (const auto& p : kLegacyVolume)
		if (id == p[0]) return p[1];
	return nullptr;
}
bool ap_is_volume_verb(const std::string& v) { return v == "cone" || v == "beam" || v == "burst"; }
Dict ap_def(const std::string& id) { return Moves::defs().get(id).as_dict(); }
int ap_gesture_of(const std::string& slot) {
	if (slot == "thrust") return static_cast<int>(Gesture::Up);
	if (slot == "ground") return static_cast<int>(Gesture::Down);
	if (slot == "sweep") return static_cast<int>(Gesture::Side);
	return 0;
}
bool ap_arr_has(const Array& a, const std::string& s) { return arr_has_str(a, s); }
}  // namespace

// ================================================================ outcome values

double outcome_value(const std::string& outcome, const std::string& band, const Dict& rule) {
	double v = 0.0;
	if (ap_lookup(kOutcomeValue, sizeof(kOutcomeValue) / sizeof(kOutcomeValue[0]), outcome, &v)) {
		if (outcome == "block" && dnum(rule, "chip", 0.0) > 0.0) v = 0.35;   // a plain guard: blocked with chip damage
	} else if (ap_lookup(kKitValue, sizeof(kKitValue) / sizeof(kKitValue[0]), outcome, &v)) {
		// kit value
	} else {
		bool found = false;
		for (const NamedValue& kw : kKitKeywords) {
			if (outcome.find(kw.k) != std::string::npos) {
				v = kw.v;
				found = true;
				break;
			}
		}
		if (!found) {
			if (band == "partial") v = 0.3;
			else if (band == "fail") v = -1.0;
			else v = 0.75;
		}
	}
	// A rule's partial band is the threat continuing weakened, whatever its name.
	if (band == "partial" && !rule.has("bands") && v > 0.45) v = 0.45;
	return v;
}

double perfect_chance(double err) {
	if (err <= 1e-4) return 0.97;
	return clampf(0.07 / err, 0.05, 0.97);
}

// ================================================================ threats

AiThreat body_threat(CombatWorld& w, ActorState& me, MatBody& b) {
	AiThreat res;
	if (!b.alive || b.controller >= 0 || &b == w.pool) return res;
	const int hostile_owner = b.attack_id != 0 ? b.attack_owner : b.owner;
	if (hostile_owner == me.id || hostile_owner < 0) return res;
	ActorState* foe = w.get_actor(hostile_owner);
	if (foe != nullptr && foe->team == me.team) return res;
	res.key = "b" + itos(b.id) + ":" + itos(b.attack_id);
	res.kind = "body";
	res.body = &b;
	res.dist = 0.0;
	res.tti = 9.0;
	res.closing = 0.0;
	if (b.form == Form::Wave) {
		Vec3 to = me.pos - b.pos;
		to.y = 0.0f;
		const double d = to.length();
		const Vec3 wdir = b.wave_dir.length() > 0.1f ? b.wave_dir : Vec3(b.vel.x, 0, b.vel.z).normalized();
		if (d > 16.0 || wdir.length() < 0.1f || to.normalized().dot(wdir) < 0.5f) return AiThreat();
		double speed = Vec3(b.vel.x, 0, b.vel.z).length();
		if (speed < 0.5) {
			speed = dnum(b.props, "speed", dnum(ap_def("pour"), "wave_speed"));
			if (b.mat == Mat::Stone) speed *= Thermal::flow_factor(b);
		}
		speed = maxf(speed, 0.5);
		res.dist = d;
		res.closing = speed;
		res.tti = maxf(0.0, d - b.wave_width * 0.5 - Sim::ACTOR_RADIUS) / speed;
	} else if (b.form == Form::Zone || b.form == Form::Cloud) {
		Vec3 to2 = me.pos - b.pos;
		to2.y = 0.0f;
		const double gap = to2.length() - maxf(b.zone_radius, b.radius) - Sim::ACTOR_RADIUS;
		const Vec3 v(b.vel.x, 0, b.vel.z);
		const double closing = to2.length() > 0.01f ? v.dot(to2.normalized()) : 0.0;
		if (gap > 0.8 && closing < 0.5) return AiThreat();
		if (gap > 12.0) return AiThreat();
		res.dist = maxf(gap, 0.0);
		res.closing = maxf(closing, 0.0);
		res.tti = gap <= 0.0 ? 0.0 : (closing > 0.5 ? gap / closing : 0.6);
	} else if (b.is_projectile()) {
		const Vec3 rel = me.chest() - b.pos;
		const double closing2 = b.vel.dot(rel.normalized());
		if (closing2 < 2.0 || rel.length() > 18.0f) return AiThreat();
		const double tti = rel.length() / closing2;
		// Ballistic bodies fall along their arc: include gravity in the aim check.
		Vec3 miss = b.pos + b.vel * tti + V3(0.0, -0.5 * Sim::GRAVITY * b.gravity_scale * tti * tti, 0.0) - me.chest();
		miss.y *= 0.5f;
		if (miss.length() > 1.6 + b.radius) return AiThreat();
		res.dist = rel.length();
		res.closing = closing2;
		res.tti = tti;
	} else {
		return AiThreat();
	}
	res.agent = Agent::of_body(w, b, &me);
	res.cls = res.agent->cls;
	res.owner = hostile_owner;
	res.valid = true;
	return res;
}

AiThreat action_threat(CombatWorld& w, ActorState& me, ActorState* foe) {
	AiThreat res;
	if (foe == nullptr || foe->action == nullptr || foe->stun > 0.0) return res;
	const ActionInst& act = *foe->action;
	if (act.phase != ActionPhase::Startup && act.phase != ActionPhase::Charge && act.phase != ActionPhase::Channel) return res;
	const Dict& def = act.def;
	std::string cls = dstr(ddict(def, "threat"), "cls", "");
	const std::string verb = dstr(def, "verb", "");
	const char* lv = ap_legacy_volume(act.id);
	const bool legacy = lv != nullptr;
	if (!legacy && !ap_is_volume_verb(verb)) return res;
	if (legacy) {
		cls = lv;
		if (act.id == "fire_attack" && foe->has("lightning") && act.phase == ActionPhase::Charge) cls = "lightning";
	}
	if (cls.empty()) return res;
	int tier = act.tier();
	const bool charging = act.phase == ActionPhase::Charge || act.phase == ActionPhase::Channel;
	const int seen_tier = tier;
	if (charging) tier = mini(tier + 1, maxi(Charge::max_tier(def), tier));   // a growing charge: answer the next tier
	double reach = reach_of(def, maxi(tier, (charging && legacy) ? 1 : tier), "volume");
	if (charging)
		for (int t = tier; t <= Charge::max_tier(def); ++t) reach = maxf(reach, reach_of(def, t, "volume"));
	if (cls == "lightning" && legacy) reach = dnum(ap_def("lightning"), "range");
	Vec3 to = me.pos - foe->pos;
	to.y = 0.0f;
	const double d = to.length();
	if (d > reach + 0.5) return res;
	double power = 0.0;
	const Value& tp = ddict(def, "threat").get("power");
	if (tp.is_array() && !tp.as_array().empty()) power = vnum(tp.as_array().get(static_cast<size_t>(clampi(tier, 0, static_cast<int>(tp.as_array().size()) - 1))));
	else if (!tp.is_nil()) power = vnum(tp);
	else power = maxf(Charge::counter_power(def, tier), 0.0);
	if (cls == "lightning" && legacy) power = dnum(ap_def("lightning"), "damage");
	if (power <= 0.0) power = vnum(Charge::pget(def, tier, "damage", Value(8.0)), 8.0);
	const std::string chn = ap_channel_of(cls);
	AgentRef ag = Agent::of_volume(&w, foe, nullptr, cls, foe->chest(), d > 0.01 ? to.normalized() : foe->forward(), D({{chn, power}}));
	ag->tier = tier;
	ag->hostile = true;
	double tti = 0.12;
	if (act.phase == ActionPhase::Startup) tti = maxf(0.0, dnum(act.data, "startup", dnum(def, "startup", 0.0)) - act.total) + 0.02;
	res.valid = true;
	res.key = "v" + itos(act.attack_id);
	res.kind = "volume";
	res.agent = ag;
	res.cls = cls;
	res.dist = d;
	res.tti = tti;
	res.closing = 0.0;
	res.charging = charging;
	res.attack_id = act.attack_id;
	res.owner = foe->id;
	res.move = act.id;
	res.tier = seen_tier;
	return res;
}

AgentRef perceived(const Agent& agent, double err) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = agent.kind;
	g->cls = agent.cls;
	g->ccls = agent.ccls;
	g->body = agent.body;
	g->actor = agent.actor;
	g->inst = agent.inst;
	g->def = agent.def;
	g->pos = agent.pos;
	g->dir = agent.dir;
	g->mass = agent.mass * (1.0 + err);
	g->speed = agent.speed;
	g->heat = agent.heat;
	g->tier = agent.tier;
	g->power = agent.power;
	g->mat = agent.mat;
	g->hostile = agent.hostile;
	g->data = agent.data;
	g->ch.K = agent.ch.K * (1.0 + err);
	g->ch.H = agent.ch.H * (1.0 + err);
	g->ch.C = agent.ch.C * (1.0 + err);
	g->ch.E = agent.ch.E * (1.0 + err);
	g->ch.P = agent.ch.P * (1.0 + err);
	return g;
}

// ================================================================ kit

std::vector<KitMove> kit_moves(const AiKit& kit, const std::vector<std::string>& slots) {
	std::vector<KitMove> out;
	for (const auto& kv : kit) {
		const int e = kv.first;
		const std::vector<int>& subs = kv.second;
		const bool has0 = std::find(subs.begin(), subs.end(), 0) != subs.end();
		std::set<std::string> seen;
		for (int s : subs) {
			for (const std::string& slot : slots) {
				const std::string id = Moves::resolve(e, s, slot);
				if (id.empty() || !Moves::defs().has(id)) continue;
				if (s != 0 && has0 && id == Moves::resolve(e, 0, slot)) continue;
				const std::string k = id + "/" + slot;
				if (seen.count(k)) continue;
				seen.insert(k);
				out.push_back({id, e, s, slot});
			}
		}
	}
	return out;
}

// ================================================================ costs / timing

double busy_time(const ActorState& me, const std::string& press) {
	if (me.stun > 0.0) return me.stun;
	const ActionInst* a = me.action.get();
	if (a == nullptr) return 0.0;
	if (a->id == "guard") return press != "guard" ? 0.0 : Sim::DT;
	if (a->phase == ActionPhase::Charge || a->phase == ActionPhase::Channel) return (press == "guard" || press == "evade") ? 0.0 : 0.1;
	const Dict& d = a->def;
	const double rec = dnum(d, "recovery", 0.0) * Status::recovery_mult(me);
	double cancel_at = rec;
	if ((press == "guard" || press == "evade") && d.has("cancel")) cancel_at = rec * dnum(d, "cancel");
	if (a->phase == ActionPhase::Recovery) return maxf(0.0, cancel_at - a->t);
	const double left = cancel_at + dnum(a->data, "active", dnum(d, "active", 0.0));
	if (a->phase == ActionPhase::Active) return maxf(0.0, left - a->t);
	return left + maxf(0.0, dnum(a->data, "startup", dnum(d, "startup", 0.0)) - a->total);
}

double hold_for(const Dict& def, int tier) {
	if (tier <= 0) return 0.0;
	const auto times = Charge::tier_times(def);
	return times[static_cast<size_t>(clampi(tier - 1, 0, 2))] + 0.02;
}

Cost move_cost(const Dict& def, int tier, double hold) {
	Cost c;
	c.focus = Charge::pgetf(def, 0, "cost", 0.0);
	c.heat = vnum(Charge::pget(def, 0, "heat", def.get("cost_hu", Value(0.0))));
	if (tier >= 1) {
		c.focus += vnum(Charge::pget(def, tier, "cost_add", Value(maxf(0.0, dnum(def, "heavy_cost", dnum(def, "cost", 0.0)) - dnum(def, "cost", 0.0)))));
		c.heat += vnum(Charge::pget(def, tier, "heat_add", Value(maxf(0.0, dnum(def, "heavy_cost_hu", 0.0) - dnum(def, "cost_hu", 0.0)))));
		if (Charge::max_tier(def) >= 2) {
			const double t1 = Charge::tier_times(def)[0];
			c.focus += maxf(0.0, hold - t1) * Charge::pgetf(def, tier, "charge_drain", Charge::DEFAULT_DRAIN);
		}
	}
	c.water = Charge::pgetf(def, tier, "water", 0.0);
	c.metal = Charge::pgetf(def, tier, "metal", 0.0);
	// Projectiles drawn from a carried source spend it (waterskin / metal satchel).
	const std::string src = vstr(Charge::pget(def, tier, "source", Value("")));
	if (src == "waterskin" || src == "metal") {
		const double kg = Charge::pgetf(def, tier, "mass", 0.0) * maxf(1.0, Charge::pgetf(def, tier, "count", 1.0));
		if (src == "waterskin") c.water = maxf(c.water, kg);
		else c.metal = maxf(c.metal, kg);
	}
	return c;
}

bool can_afford(CombatWorld& w, const ActorState& me, const Cost& cost) {
	if (me.focus + 1e-6 < cost.focus) return false;
	const double hu = cost.heat;
	if (hu > 0.0 && !w.can_pay_heat(me, hu + cost.focus * Sim::HU_PER_FOCUS)) return false;
	if (cost.water > 0.0 && me.water_carried + 1e-6 < cost.water && !me.in_water) return false;
	if (cost.metal > 0.0 && me.metal_carried + 1e-6 < cost.metal) return false;
	return true;
}

double reach_of(const Dict& def, int tier, const std::string& kind) {
	(void)kind;
	const std::string verb = dstr(def, "verb", "");
	const Array ai_rng = ddict(def, "ai").get("range", A({0.0, 6.0})).as_array();
	const double ai_max = ai_rng.size() > 1 ? vnum(ai_rng[1]) : 6.0;
	if (def.has("heavy_range") && tier >= 1) return dnum(def, "heavy_range");
	if (verb == "cone" || verb == "beam") return Charge::pgetf(def, tier, "range", ai_max);
	if (verb == "burst")
		return vnum(Charge::pget(def, tier, "range", Charge::pget(def, tier, "distance", Value(0.0)))) + Charge::pgetf(def, tier, "radius", 1.5);
	if (verb == "grip") return Charge::pgetf(def, tier, "reach", ai_max);
	if (verb == "zone" || verb == "summon")
		return vnum(Charge::pget(def, tier, "range", Charge::pget(def, tier, "distance", Value(ai_max)))) + Charge::pgetf(def, tier, "radius", 1.0);
	if (verb == "projectile" || verb == "ground_line") return ai_max;
	if (def.has("range")) return Charge::pgetf(def, tier, "range", ai_max);
	if (def.has("reach")) return dnum(def, "reach");
	return ai_max;
}

std::string meet_kind(const Dict& def, const std::string& slot) {
	if (slot == "guard" || slot == "evade_hold") return "contact";
	const std::string verb = dstr(def, "verb", "");
	if (verb == "barrier" || verb == "stance" || verb == "mode") return "contact";
	if (verb == "projectile" || verb == "ground_line" || verb == "pour") return "travel";
	return "ranged";
}

// ================================================================ counters

AgentRef counter_agent(CombatWorld& w, ActorState& me, const KitMove& c, int tier, bool perfect, const AiThreat& threat) {
	if (c.slot == "guard" && c.id == "guard") {
		AgentRef g = std::make_shared<Agent>();
		g->kind = "move";
		g->actor = &me;
		g->tier = 0;
		g->perfect = perfect;
		g->pos = me.chest();
		g->dir = me.forward();
		switch (c.element) {
			case Sim::EARTH:
				g->ccls = "wall_stone";
				g->power = Sim::WALL_MASS * 0.25;
				break;
			case Sim::WATER:
				if (me.water_carried >= 1.0) {
					g->ccls = "shield_water";
					g->power = me.water_carried * 1.0;
				} else {
					g->ccls = "guard";
					g->power = Interactions::PLAIN_GUARD_CP;
				}
				break;
			case Sim::FIRE:
				g->ccls = "aura_flame";
				g->power = Interactions::PLAIN_GUARD_CP;
				break;
			default:
				g->ccls = "guard_wind";
				g->power = Interactions::WIND_GUARD_CP;
				break;
		}
		g->cls = g->ccls;
		return g;
	}
	AgentRef ag = Agent::of_move(&w, &me, c.id, tier, perfect);
	if (c.slot == "tech") {
		if (tech_mode(w, me, c, threat) == "DRAW") {
			ag->ccls = "draw_heat";
			ag->cls = ag->ccls;
		}
	}
	return ag;
}

std::string tech_mode(CombatWorld& w, const ActorState& me, const KitMove& c, const AiThreat& threat) {
	(void)w;
	const MatBody* b = threat.body;
	if (c.element == Sim::FIRE && c.sub == 0 && c.id == "fire_tech") {
		if (b == nullptr) return "";
		if (b->is_stone() && (b->liquid > 0.0 || b->is_hot())) return me.has("heat_draw") ? "DRAW" : "";
		if (b->is_stone() || b->is_water()) return (me.has("magma") || b->is_water()) ? "HEAT" : "";
		return "HEAT";
	}
	return "GRIP";
}

bool tech_legal(CombatWorld& w, ActorState& me, const KitMove& c, const AiThreat& threat, const Agent& ag) {
	const MatBody* b = threat.body;
	if (b == nullptr) return false;
	if (c.id == "fire_tech" && c.sub == 0) return !tech_mode(w, me, c, threat).empty();
	if (b->form == Form::Zone) return false;
	return Interactions::allows(*b, ag.ccls);
}

std::vector<AiOption> counters(CombatWorld& w, ActorState& me, const AiThreat& threat, const AiKit& kit, const AiParams& prm, double err) {
	std::vector<AiOption> out;
	AgentRef ag = perceived(*threat.agent, err);
	const double tti = threat.tti;
	const bool is_volume = threat.kind == "volume";
	const MatBody* b = threat.body;
	for (const KitMove& c : kit_moves(kit, counter_slots())) {
		if (!prm.only.empty() && std::find(prm.only.begin(), prm.only.end(), c.id) == prm.only.end()) continue;
		const Dict def = ap_def(c.id);
		const std::string& slot = c.slot;
		if (slot != "guard" && !def.has("counter")) continue;
		if (slot == "guard" && c.id != "guard" && !def.has("counter") && dstr(def, "verb", "") != "barrier") continue;
		const std::string mk = meet_kind(def, slot);
		if (is_volume && mk != "contact" && slot != "sink" && slot != "push") continue;   // instant volumes are answered where they land
		if (slot == "tech" && (b == nullptr || (c.id == "air_tech" && b->mass > 30.0))) continue;
		const int maxt = c.id != "guard" ? Charge::max_tier(def) : 0;
		if (slot == "evade_hold" && dstr(def, "verb", "") != "stance") continue;
		bool tried_perfect = false;
		for (int tier = 0; tier <= maxt; ++tier) {
			AiOption opt = _evaluate(w, me, threat, *ag, c, def, tier, false, prm);
			if (opt.valid) out.push_back(opt);
			if (slot == "guard" && !tried_perfect && !is_volume) {
				tried_perfect = true;
				AiOption po = _evaluate(w, me, threat, *ag, c, def, tier, true, prm);
				if (po.valid) out.push_back(po);
			}
		}
	}
	// Evade with the current element's evade (no switch).
	if (!Status::rooted(me)) {
		double p = 0.85;
		if (threat.kind == "volume") p = !threat.charging ? 0.7 : 0.8;
		else if (threat.kind == "body" && b != nullptr && (b->form == Form::Wave || b->form == Form::Zone || b->form == Form::Cloud)) p = 0.55;
		const double busy = busy_time(me, "evade");
		if (busy > tti - 0.05) p *= 0.3;
		AiOption o;
		o.valid = true;
		o.id = Moves::resolve(me.element, me.sub(), "evade");
		o.element = me.element;
		o.sub = me.sub();
		o.slot = "evade";
		o.press = "evade";
		o.p = p;
		o.outcome = "evade";
		o.band = "evade";
		o.value = EVADE_VALUE;
		o.utility = EVADE_VALUE * p - 0.01;
		o.kind = "evade";
		o.press_in = maxf(0.0, minf(tti - 0.3, busy));
		o.label = "evade";
		out.push_back(o);
	}
	std::stable_sort(out.begin(), out.end(), [](const AiOption& x, const AiOption& y) {
		if (absf(x.utility - y.utility) > 1e-6) return x.utility > y.utility;
		if (absf(x.value - y.value) > 1e-6) return x.value > y.value;
		return x.id + x.slot + itos(x.tier) < y.id + y.slot + itos(y.tier);
	});
	return out;
}

AiOption _evaluate(CombatWorld& w, ActorState& me, const AiThreat& threat, const Agent& ag, const KitMove& c, const Dict& def, int tier,
                   bool perfect, const AiParams& prm) {
	AiOption none;
	const std::string& slot = c.slot;
	const MatBody* b = threat.body;
	const double tti = threat.tti;
	AgentRef counter = counter_agent(w, me, c, tier, perfect, threat);
	if (slot == "tech" && !tech_legal(w, me, c, threat, *counter)) return none;
	const std::string tmode = slot == "tech" ? tech_mode(w, me, c, threat) : std::string();
	if (slot == "tech" && b != nullptr) {
		if (b->mass > vnum(Charge::pget(def, tier, "max_mass", Value(me.max_control_mass)))) return none;
		if (tmode == "HEAT" && b->is_stone()) {
			// Magma grip: melting it costs about 19.8 HU/kg (reserve first, then Focus).
			const double need = b->mass * (Sim::STONE_C * (Sim::STONE_MELT_C - b->temp) + Sim::STONE_LATENT);
			if (me.heat_reserve + me.focus * Sim::HU_PER_FOCUS < need * 0.9 + 60.0) return none;
		}
	}
	// Legacy Earth wall: needs ground in front and the threat far enough to meet the risen wall.
	if (slot == "guard" && c.id == "guard" && c.element == Sim::EARTH) {
		if (!me.grounded || me.focus < 8.0) return none;
	}
	const IxResult pred = Interactions::predict(&w, ag, *counter);
	double value = outcome_value(pred.outcome, pred.band, pred.rule);
	if (dnum(pred.rule, "chip", 0.0) > 0.0 && (b == nullptr || !b->is_projectile())) value = minf(value, 0.35);
	if (value <= -0.99) return none;
	// ---- timing
	std::string press = "attack";
	int gesture = 0;
	if (slot == "guard" || slot == "push" || slot == "sink") press = "guard";
	else if (slot == "tech") press = "tech";
	else if (slot == "evade_hold") press = "evade";
	else gesture = ap_gesture_of(slot);
	const double sw = (c.element == me.element && c.sub == me.sub_of(c.element)) ? 0.0 : Sim::DT;
	const double busy = busy_time(me, press);
	const double startup = dnum(def, "startup", 0.0);
	const double hold = c.id != "guard" ? hold_for(def, tier) : 0.0;
	double t_fire = 0.0;
	if (slot == "guard") t_fire = hold;
	else if (slot == "push" || slot == "sink") t_fire = hold + 2.0 * Sim::DT + startup;
	else if (slot == "tech") {
		t_fire = startup + hold;
		if (tmode == "DRAW") t_fire = dnum(ap_def("fire_tech"), "draw_startup");
	} else if (slot == "evade_hold") t_fire = 0.2;
	else t_fire = maxf(startup, hold);
	const double t_ready = sw + busy + t_fire;
	const std::string kind = meet_kind(def, slot);
	double release_in = t_ready;
	double press_in = sw + busy;
	const double dist = threat.dist;
	const double closing = threat.closing;
	if (kind == "contact") {
		if (t_ready > tti - 0.02) return none;
	} else if (kind == "ranged") {
		double reach = reach_of(def, tier);
		if (slot == "tech" && tmode == "DRAW") reach = dnum(ap_def("fire_tech"), "draw_range") - 0.5;
		if (reach < 1.0) return none;
		const double in_reach = dist <= reach ? 0.0 : (dist - reach) / maxf(closing, 0.5);
		release_in = maxf(t_ready, in_reach);
		if (release_in + ACTIVE_PAD > tti) return none;
		// Press late enough that the release lands with the threat in reach (charge holds included).
		press_in = maxf(press_in, release_in - t_fire);
		if (b != nullptr && slot != "sink" && !w.los(me.chest(), b->pos + V3(0, 0.3, 0))) return none;
	} else {
		if (t_ready + 0.15 > tti) return none;
	}
	if (slot == "tech" && tmode == "DRAW") {
		if (b == nullptr || (prm.draw_ok && !prm.draw_ok(*const_cast<MatBody*>(b)))) return none;
	}
	// ---- resources
	Cost cost = move_cost(def, tier, hold);
	if (slot == "guard" && c.id == "guard" && c.element == Sim::EARTH) cost.focus += 8.0;
	if (!can_afford(w, me, cost)) return none;
	// ---- success probability
	double p = 1.0;
	double plain_value = value;
	if (perfect) {
		p = perfect_chance(prm.timing_err);
		AgentRef plain_ag = counter_agent(w, me, c, tier, false, threat);
		const IxResult plain = Interactions::predict(&w, ag, *plain_ag);
		plain_value = outcome_value(plain.outcome, plain.band, plain.rule);
		if (dnum(plain.rule, "chip", 0.0) > 0.0 && (b == nullptr || !b->is_projectile())) plain_value = minf(plain_value, 0.35);
		if (value <= plain_value + 1e-6) return none;   // perfect adds nothing here
		if (tti - t_ready < PERFECT_LEAD + 0.05) return none;
	}
	const double ev = p * value + (1.0 - p) * plain_value;
	const double cost_term = cost.focus / 100.0 * 0.5 + cost.heat / 1000.0 + (sw > 0.0 ? 0.02 : 0.0) + hold * 0.04;
	double guard_for = 0.0;
	if (press == "guard" && slot == "guard") {
		guard_for = tti + 0.3;
		if (b != nullptr && (b->form == Form::Wave || b->form == Form::Zone)) guard_for = tti + 0.8;
	}
	AiOption o;
	o.valid = true;
	o.id = c.id;
	o.element = c.element;
	o.sub = c.sub;
	o.slot = slot;
	o.press = press;
	o.tier = tier;
	o.hold = hold;
	o.perfect = perfect;
	o.p = p;
	o.outcome = pred.outcome;
	o.band = pred.band;
	o.ratio = pred.ratio;
	o.value = value;
	o.utility = ev - cost_term;
	o.kind = kind;
	o.press_in = press_in;
	o.release_in = release_in;
	o.guard_for = guard_for;
	o.gesture = gesture;
	o.label = std::string(SubName(c.element, c.sub)) + " " + c.id + (perfect ? "*" : "") + " T" + itos(tier);
	o.mode = tmode;
	return o;
}

AiOption choose(const std::vector<AiOption>& options, const AiParams& prm, Rng& rng) {
	if (options.empty()) return AiOption();
	if (rng.randf() < prm.counter) return options[0];
	std::vector<const AiOption*> pool;
	for (const AiOption& o : options)
		if (o.value > -0.5) pool.push_back(&o);
	if (pool.empty()) return options[0];
	return *pool[static_cast<size_t>(rng.randi_range(0, static_cast<int>(pool.size()) - 1))];
}

// ================================================================ offense

AiObserve observe(CombatWorld& w, const ActorState& me, const ActorState& foe) {
	AiObserve st;
	Vec3 to = foe.pos - me.pos;
	to.y = 0.0f;
	const double d = to.length();
	const Vec3 dir = d > 0.01 ? to / static_cast<float>(d) : me.forward();
	st.dist = d;
	st.dir = dir;
	st.wet = foe.wetness > Status::WET_AT || Status::has(foe, "wet");
	st.in_water = foe.in_water;
	st.on_plate = foe.surface == "metal";
	st.airborne = !foe.grounded;
	st.cover = !w.arena.has_los(me.chest(), foe.chest());
	st.hidden = Status::hidden(foe) && d > 2.0;
	st.puddle = foe.surface == "puddle";
	if (foe.action != nullptr) {
		const ActionPhase ph = foe.action->phase;
		st.charging = ph == ActionPhase::Charge || (ph == ActionPhase::Channel && foe.action->id != "guard");
		st.recovering = ph == ActionPhase::Recovery;
		st.charge_tier = foe.action->tier();
	}
	const double t = w.wall_hit(me.chest(), foe.chest());
	if (t >= 0.0) {
		for (size_t i = 0; i < w.bodies.size(); ++i) {
			const MatBody& b = *w.bodies[i];
			if (b.alive && b.form == Form::Wall && b.wall_rise > 0.5 && w.wall_segment_t(me.chest(), foe.chest(), b) >= 0.0) {
				st.behind_barrier = true;
				st.wall_body = b.id;
				break;
			}
		}
	}
	const Vec3 probe = foe.chest() + dir * 2.2f;
	st.near_wall = !w.arena.has_los(foe.chest(), probe);
	return st;
}

std::vector<AiOption> offense(CombatWorld& w, ActorState& me, ActorState& foe, const AiKit& kit, const AiParams& prm, Rng& rng) {
	const AiObserve st = observe(w, me, foe);
	std::vector<AiOption> out;
	const double d = st.dist;
	bool lava_near = false;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (b.alive && b.is_stone() && b.liquid > 0.5 && b.controller < 0 && b.pos.distance_to(me.pos) < 8.0f) lava_near = true;
	}
	for (const KitMove& c : kit_moves(kit, offense_slots())) {
		const Dict def = ap_def(c.id);
		const Dict ai = ddict(def, "ai");
		const std::string role = dstr(ai, "role", "");
		const Array tags = darr(ai, "tags");
		if (role.empty() || role == "mobility") continue;
		if (role == "counter" && !(ap_arr_has(tags, "melt_wall") || ap_arr_has(tags, "scorch_wall"))) continue;
		if (c.slot == "tech" && (c.id == "air_tech" || c.id == "water_tech" || c.id == "fire_tech")) continue;
		bool skip = false;
		for (const Value& tg : tags) {
			const std::string ts = vstr(tg);
			if (ts == "needs_lava" && !lava_near) skip = true;
			else if ((begins_with(ts, "needs_") && ts != "needs_lava") || begins_with(ts, "after_")) skip = true;
		}
		if (skip) continue;
		const Array rng_arr = ai.get("range", A({0.0, 8.0})).as_array();
		const double lo = vnum(rng_arr.get(0));
		const double hi = rng_arr.size() > 1 ? vnum(rng_arr[1]) : 8.0;
		const bool melts = ap_arr_has(tags, "melt_wall") || ap_arr_has(tags, "scorch_wall");
		int aim_body = -1;
		Vec3 aim;
		double dd = d;
		if (melts) {
			if (!st.behind_barrier) continue;
			MatBody* wb = w.get_body(st.wall_body);
			if (wb == nullptr) continue;
			dd = Vec3(wb->pos.x - me.pos.x, 0, wb->pos.z - me.pos.z).length();   // heat the wall face, not the rival
			aim_body = wb->id;
		}
		if ((st.cover || st.behind_barrier) && ap_arr_has(tags, "bank_shot")) {
			aim = bank_aim(w, me, foe, hi);
			if (aim == Vec3()) continue;
		}
		const int max_t = mini(Charge::max_tier(def), prm.tiers);
		int tier = 0;
		if (max_t > 0 && rng.randf() < 0.25 + 0.4 * prm.aggression) tier = rng.randi_range(1, max_t);
		double score = ap_role_weight(role, 0.3);
		std::vector<std::string> reasons;
		const double reach_hi = hi + (tier >= 1 ? 1.5 : 0.0);
		if (dd < lo - 0.5 || dd > reach_hi) continue;
		if (ap_is_volume_verb(dstr(def, "verb", "")) || ap_legacy_volume(c.id) != nullptr) {
			// Cones, beams and bursts reach by tier: hold to the first tier that reaches (if allowed).
			int need = -1;
			for (int t = 0; t <= Charge::max_tier(def); ++t)
				if (reach_of(def, t) + 0.3 >= dd) {
					need = t;
					break;
				}
			if (need < 0 || need > maxi(max_t, 0)) continue;
			tier = maxi(tier, need);
		}
		const bool env = prm.env;
		const std::string cls = dstr(ddict(def, "threat"), "cls", "");
		const bool electric = cls == "lightning" || ap_arr_has(tags, "conducts") || ap_arr_has(tags, "punish_wet");
		if (env && electric && (st.wet || st.in_water || st.on_plate || st.puddle)) {
			score += 0.7;
			reasons.push_back("conduct");
		}
		if (env && st.near_wall &&
		    (ap_arr_has(tags, "knockback") || ap_arr_has(tags, "knockdown") || ap_arr_has(tags, "push") || ap_arr_has(tags, "shove") || c.id == "air_attack")) {
			score += 0.4;
			reasons.push_back("splat");
		}
		if (st.airborne && dnum(def, "startup", 0.2) <= 0.18) {
			score += 0.35;
			reasons.push_back("juggle");
		}
		if (prm.punish && st.charging) {
			if (ap_arr_has(tags, "disrupt") || ap_arr_has(tags, "interrupt_channel") || ap_arr_has(tags, "vs_charge")) {
				score += 0.9;
				reasons.push_back("disrupt");
			} else if (dnum(def, "startup", 0.2) <= 0.14) {
				score += 0.4;
				reasons.push_back("interrupt");
			}
		}
		if (prm.punish && st.recovering && dnum(def, "startup", 0.2) <= 0.17) {
			score += 0.3;
			reasons.push_back("punish");
		}
		if (st.behind_barrier || st.cover) {
			bool answers = false;
			if (ap_arr_has(tags, "blast_through_t2") && max_t >= 2) {
				tier = maxi(tier, 2);
				answers = true;
			}
			if (ap_arr_has(tags, "from_above_t3") && Charge::max_tier(def) >= 3 && prm.tiers >= 3) {
				tier = 3;
				answers = true;
			}
			if (melts || ((ap_arr_has(tags, "melt_wall_t3") || ap_arr_has(tags, "softens_walls")) && st.behind_barrier)) {
				answers = st.behind_barrier;
				if (melts && _kit_has_tag(kit, "needs_lava")) {
					score += 0.5;   // Melt & Return: the slumped face becomes our lava
					reasons.push_back("melt_return");
				}
				if (ap_arr_has(tags, "melt_wall_t3") && max_t >= 3) tier = 3;
			}
			if (aim != Vec3() || ap_arr_has(tags, "around_cover") || ap_arr_has(tags, "relay")) answers = true;
			if (c.id == "magma_surge" && lava_near) answers = true;
			if (answers) {
				score += 0.8;
				reasons.push_back("barrier");
			} else if (!cls.empty() || ap_arr_has(tags, "projectile")) {
				score -= 0.6;   // straight into the wall
			}
		}
		double hold = hold_for(def, tier);
		Cost cost = move_cost(def, tier, hold);
		if (!can_afford(w, me, cost)) {
			if (tier > 0) {
				tier = 0;
				hold = 0.0;
				cost = move_cost(def, 0, 0.0);
			}
			if (!can_afford(w, me, cost)) continue;
		}
		if (me.focus - cost.focus < 12.0) score -= 0.25;   // keep a guard's worth of Focus
		if (c.element != me.element || c.sub != me.sub_of(c.element)) score -= 0.03;
		score -= 0.12 * dnum(prm.recent, c.id, 0.0);   // vary the offense
		score += rng.randf_range(0.0f, 0.3f);
		int gesture = 0;
		std::string press = "attack";
		if (c.slot == "tech") {
			press = "tech";
			if (dstr(def, "verb", "") == "ranged_heat") hold = maxf(hold, 1.5);   // Smelter / Scorch: about a second to slump a wall face
			else if (hold <= 0.0) hold = maxf(0.35, dnum(def, "startup", 0.2) + 0.1);
		} else {
			gesture = ap_gesture_of(c.slot);
		}
		AiOption o;
		o.valid = true;
		o.id = c.id;
		o.element = c.element;
		o.sub = c.sub;
		o.slot = c.slot;
		o.press = press;
		o.gesture = gesture;
		o.tier = tier;
		o.hold = hold;
		o.score = score;
		o.reasons = reasons;
		o.aim_body = aim_body;
		o.aim = aim;
		o.label = std::string(SubName(c.element, c.sub)) + " " + c.id + " T" + itos(tier);
		out.push_back(o);
	}
	std::stable_sort(out.begin(), out.end(), [](const AiOption& x, const AiOption& y) {
		if (absf(x.score - y.score) > 1e-6) return x.score > y.score;
		return x.id < y.id;
	});
	return out;
}

AiOption chain_follow(CombatWorld& w, ActorState& me, ActorState& foe, const AiKit& kit, const AiParams& prm, Rng& rng) {
	const Array used = darr(me.chain, "slots");
	const double d = me.pos.distance_to(foe.pos);
	const bool weave_ok = prm.weave && !dbool(me.chain, "weaved", false) && me.focus > 20.0;
	AiOption best;
	double best_s = -kInf;
	for (const KitMove& c : kit_moves(kit, attack_slots())) {
		if (arr_has_str(used, c.slot)) continue;
		const bool same = c.element == me.action->element && c.sub == me.action->sub;
		if (!same && !weave_ok) continue;
		const Dict def = ap_def(c.id);
		const Dict ai = ddict(def, "ai");
		const std::string role = dstr(ai, "role", "");
		if (role.empty() || role == "mobility" || role == "counter") continue;
		const Array rr = ai.get("range", A({0.0, 8.0})).as_array();
		if (d < vnum(rr.get(0)) - 0.5 || d > vnum(rr.get(1))) continue;
		const Cost cost = move_cost(def, 0, 0.0);
		if (!can_afford(w, me, cost) || me.focus - cost.focus < 8.0) continue;
		double s = ap_role_weight(role, 0.3) + rng.randf_range(0.0f, 0.3f) + (same ? 0.1 : 0.0);
		if (dnum(def, "startup", 0.2) <= 0.17) s += 0.15;
		if (s > best_s) {
			best_s = s;
			best = AiOption();
			best.valid = true;
			best.id = c.id;
			best.element = c.element;
			best.sub = c.sub;
			best.slot = c.slot;
			best.press = "attack";
			best.gesture = ap_gesture_of(c.slot);
			best.tier = 0;
			best.hold = 0.0;
			best.weave = !same;
			best.label = "chain " + c.id;
		}
	}
	return best;
}

Vec3 bank_aim(CombatWorld& w, const ActorState& me, const ActorState& foe, double max_range) {
	const double hs = w.arena.half_size;
	Vec3 best;
	double best_len = kInf;
	const Vec3 p0 = me.chest();
	const Vec3 f = foe.chest();
	for (int k = 0; k < 4; ++k) {
		Vec3 img = f;
		if (k == 0) img.x = f32(2.0 * hs - f.x);
		else if (k == 1) img.x = f32(-2.0 * hs - f.x);
		else if (k == 2) img.z = f32(2.0 * hs - f.z);
		else img.z = f32(-2.0 * hs - f.z);
		const Vec3 dir = img - p0;
		double tl = 0.0;
		if (k < 2) {
			const double wx = k == 0 ? hs : -hs;
			if (absf(dir.x) < 1e-3) continue;
			tl = (wx - p0.x) / dir.x;
		} else {
			const double wz = k == 2 ? hs : -hs;
			if (absf(dir.z) < 1e-3) continue;
			tl = (wz - p0.z) / dir.z;
		}
		if (tl <= 0.0 || tl >= 1.0) continue;
		const Vec3 hit = p0 + dir * tl;
		const Vec3 inside = hit - Vec3(dir.x, 0, dir.z).normalized() * 0.4f;
		if (dir.length() > max_range || !w.los(p0, inside) || !w.los(inside, f)) continue;
		if (dir.length() < best_len) {
			best_len = dir.length();
			best = Vec3(dir.x, 0, dir.z).normalized();
		}
	}
	return best;
}

bool _kit_has_tag(const AiKit& kit, const std::string& tag) {
	for (const KitMove& c : kit_moves(kit, offense_slots()))
		if (arr_has_str(darr(ddict(ap_def(c.id), "ai"), "tags"), tag)) return true;
	return false;
}

}  // namespace AiPlanner
}  // namespace ff
