// Fourfold core - port of game/core/interactions.gd (rule table from Data/rules.json).
#include "Sim/Interactions.h"

#include "Data/GameData.h"
#include "Sim/ActorState.h"
#include "Sim/CombatWorld.h"
#include "Sim/Hooks.h"
#include "Sim/Materials.h"
#include "Sim/MatBody.h"
#include "Sim/Outcomes.h"
#include "Util/GdUtil.h"

#include <algorithm>

namespace ff {
namespace Interactions {
namespace {

struct IxRegistry {
	bool ready = false;
	std::unordered_map<std::string, std::vector<Dict>> rules;
	std::vector<std::string> order;
	std::map<std::string, OutcomeFn> handlers;
	std::unordered_map<std::string, std::string> tag_threat, tag_counter;
	std::map<std::string, ChannelFn> channels;
	int n = 0;
	Dict threat_family, counter_family, kind_class, class_channel, zone_counter;
	Dict default_rule, clash_rule, pass_rule;
	Array partial_once;
};

IxRegistry& reg() {
	static IxRegistry r;
	return r;
}

std::string ix_rk(std::string_view t, std::string_view c) {
	std::string k;
	k.reserve(t.size() + c.size() + 1);
	k.append(t);
	k.push_back('|');
	k.append(c);
	return k;
}

bool ix_same_tiers(const Dict& a, const Dict& b) { return a.get("tiers", Value(Array())) == b.get("tiers", Value(Array())); }

bool ix_tiers_overlap(const Dict& a, const Dict& b) {
	if (!a.has("tiers") || !b.has("tiers")) return true;
	const Array at = a.get("tiers").as_array();
	const Array bt = b.get("tiers").as_array();
	for (const Value& t : at)
		if (bt.has(t)) return true;
	return false;
}

bool ix_tier_ok(const Dict& r, int tier) {
	if (!r.has("tiers") || tier < 0) return true;
	return r.get("tiers").as_array().has(Value(tier));
}

}  // namespace

void ensure() {
	IxRegistry& r = reg();
	if (r.ready) return;
	r.ready = true;
	const Dict& data = GameData::rules();
	r.threat_family = data.get("threat_family").as_dict();
	r.counter_family = data.get("counter_family").as_dict();
	r.kind_class = data.get("kind_class").as_dict();
	r.class_channel = data.get("class_channel").as_dict();
	r.zone_counter = data.get("zone_counter").as_dict();
	r.default_rule = data.get("default_rule").as_dict();
	r.clash_rule = data.get("clash_rule").as_dict();
	r.pass_rule = data.get("pass_rule").as_dict();
	r.partial_once = data.get("constants").get("PARTIAL_ONCE").as_array();
	for (const auto& it : data.get("tag_threat").as_dict()) r.tag_threat[it.first] = vstr(it.second);
	for (const auto& it : data.get("tag_counter").as_dict()) r.tag_counter[it.first] = vstr(it.second);
	for (const auto& it : data.get("rules").as_dict()) {
		std::vector<Dict> list;
		for (const Value& v : it.second.as_array()) {
			Dict d = v.as_dict().duplicate(true);
			r.n = maxi(r.n, dint(d, "seq", 0));
			list.push_back(d);
		}
		r.rules[it.first] = std::move(list);
		r.order.push_back(it.first);
	}
	const HookTable& ht = Hooks::table();
	for (const auto& it : data.get("outcome_handlers").as_dict()) {
		const std::string fn = GameData::fn_name(it.second);
		auto f = ht.outcome.find(fn);
		if (f != ht.outcome.end()) r.handlers[it.first] = f->second;
	}
	for (const auto& it : data.get("channel_hooks").as_dict()) {
		const std::string fn = GameData::fn_name(it.second);
		auto f = ht.channel.find(fn);
		if (f != ht.channel.end()) r.channels[it.first] = f->second;
	}
}

const Dict& DEFAULT_RULE() {
	ensure();
	return reg().default_rule;
}
const Dict& CLASH_RULE() {
	ensure();
	return reg().clash_rule;
}
const Dict& PASS_RULE() {
	ensure();
	return reg().pass_rule;
}

std::string kind_class(std::string_view kind, std::string_view def) {
	ensure();
	const Value& v = reg().kind_class.get(kind);
	return v.is_string() ? v.as_string() : std::string(def);
}

std::string class_channel(std::string_view cls, std::string_view def) {
	ensure();
	const Value& v = reg().class_channel.get(cls);
	return v.is_string() ? v.as_string() : std::string(def);
}

// ================================================================ classes

std::string classify(const MatBody& b) {
	ensure();
	if (b.props.has("cls")) return dstr(b.props, "cls");
	if (!b.tag.empty()) {
		auto it = reg().tag_threat.find(b.tag);
		if (it != reg().tag_threat.end()) return it->second;
	}
	switch (b.form) {
		case Form::Puddle: return "puddle";
		case Form::Pool: return "pool";
		case Form::Wall: return wall_class(b);
		default: break;
	}
	switch (b.mat) {
		case Mat::Stone:
			if (b.form == Form::Wave && b.liquid > 0.0) return "lava_wave";
			if (b.phase == Phase::Molten) return "magma";
			if (b.is_hot()) return "hot_rock";
			if (b.mass > 80.0) return "boulder";
			if (b.mass > 30.0) return "stone_heavy";
			return "stone";
		case Mat::Metal: return b.liquid > 0.5 ? "molten_metal" : "metal";
		case Mat::Sand:
			if (b.form == Form::Cloud || b.form == Form::Zone) return "sand_cloud";
			if (b.form == Form::Wave) return "sand_surge";
			return "sand";
		case Mat::Glass: return "glass";
		case Mat::Water:
			if (b.phase == Phase::Frozen) return "ice";
			if (b.form == Form::Wave) return "water_wave";
			if (b.form == Form::Cloud || b.form == Form::Zone) return "mist";
			return "water";
		case Mat::Steam: return (b.tag == "mist" || b.tag == "fog") ? "mist" : "steam";
		case Mat::Plant: return "vine";
		case Mat::Fire:
			if (b.tag == "fire_field" || b.tag == "fire_line") return "fire_field";
			if (b.tag == "comet" || b.tag == "corona") return "blue_fire";
			if (b.tag == "ember") return "ember";
			return dtruthy(b.props, "blue") ? "blue_fire" : "flame";
		case Mat::Air:
			if (in_list(b.tag, {"tornado", "twister", "eddy", "funnel", "vortex_wall"})) return "tornado";
			if (in_list(b.tag, {"vacuum_well", "null_bubble", "mine"})) return "vacuum";
			if (in_list(b.tag, {"tremor", "sound_barrier"})) return "sound";
			return "gust";
	}
	return "stone";
}

std::string wall_class(const MatBody& b) {
	const std::string& t = b.tag;
	if (t == "obsidian") return "wall_obsidian";
	if (t == "glass") return "wall_glass";
	if (t == "sand") return "wall_sand";
	if (t == "mud") return "wall_mud";
	if (t == "ice" || t == "ridge") return "wall_ice";
	if (t == "vine") return "wall_vine";
	if (t == "plate") return "plate_metal";
	if (t == "spikes") return "spikes";
	switch (b.mat) {
		case Mat::Sand: return "wall_sand";
		case Mat::Glass: return "wall_glass";
		case Mat::Water: return "wall_ice";
		case Mat::Plant: return "wall_vine";
		case Mat::Metal: return "plate_metal";
		default: return "wall_stone";
	}
}

std::string counter_class(const MatBody& b, CombatWorld* w) {
	ensure();
	if (b.props.has("ccls")) return dstr(b.props, "ccls");
	if (!b.tag.empty()) {
		auto it = reg().tag_counter.find(b.tag);
		if (it != reg().tag_counter.end()) return it->second;
	}
	switch (b.form) {
		case Form::Wall: return wall_class(b);
		case Form::Puddle: return "puddle";
		case Form::Pool: return "pool";
		case Form::Zone:
		case Form::Cloud: {
			const Value& zc = reg().zone_counter.get(b.tag);
			if (zc.is_string()) return zc.as_string();
			if (b.form == Form::Zone && !b.tag.empty() && b.mat == Mat::Air) return b.tag;
			break;
		}
		case Form::Wave:
			if (b.mat == Mat::Water) return "wave_water";
			if (b.mat == Mat::Sand) return "wave_sand";
			if (b.mat == Mat::Stone) return "wave_lava";
			break;
		default: break;
	}
	if (w != nullptr && b.controller >= 0 && b.mat == Mat::Water && b.phase == Phase::Liquid) {
		ActorState* h = w->get_actor(b.controller);
		if (h != nullptr && h->guarding && h->action != nullptr && dtruthy(h->action->data, "shield")) return "shield_water";
	}
	if (b.mat == Mat::Metal && b.tag == "plate") return "plate_metal";
	return classify(b);
}

bool is_barrier(CombatWorld& w, const MatBody& b) {
	if (b.form == Form::Wall || b.form == Form::Zone || b.form == Form::Puddle || b.form == Form::Pool) return true;
	return counter_class(b, &w) == "shield_water" || dtruthy(b.props, "barrier");
}

std::string family(std::string_view cls) {
	ensure();
	return vstr(reg().threat_family.get(cls), "");
}

std::string counter_family(std::string_view ccls) {
	ensure();
	const Value& v = reg().counter_family.get(ccls);
	if (v.is_string()) return v.as_string();
	return vstr(reg().threat_family.get(ccls), "");
}

void register_tag_class(const std::string& tag, const std::string& threat_cls, const std::string& counter_cls) {
	ensure();
	if (!threat_cls.empty()) reg().tag_threat[tag] = threat_cls;
	if (!counter_cls.empty()) reg().tag_counter[tag] = counter_cls;
}

void register_channels(const std::string& tag, ChannelFn cb) {
	ensure();
	reg().channels[tag] = std::move(cb);
}

const ChannelFn* channel_hook(const std::string& tag) {
	ensure();
	auto it = reg().channels.find(tag);
	return it == reg().channels.end() ? nullptr : &it->second;
}

void register_outcome(const std::string& nm, OutcomeFn cb) {
	ensure();
	reg().handlers[nm] = std::move(cb);
}

const OutcomeFn* handler(const std::string& nm) {
	ensure();
	auto it = reg().handlers.find(nm);
	return it == reg().handlers.end() ? nullptr : &it->second;
}

// ================================================================ rules

bool can_add(const std::string& threat_cls, const std::string& counter_cls, const Dict& r) {
	ensure();
	if (dbool(r, "legacy", false)) return true;
	auto it = reg().rules.find(ix_rk(threat_cls, counter_cls));
	if (it == reg().rules.end()) return true;
	for (const Dict& o : it->second)
		if (dbool(o, "legacy", false) && ix_tiers_overlap(o, r)) return false;
	return true;
}

bool add_rule(const std::string& threat_cls, const std::string& counter_cls, const Dict& rule_in) {
	ensure();
	const std::string k = ix_rk(threat_cls, counter_cls);
	if (!can_add(threat_cls, counter_cls, rule_in)) return false;   // Godot: push_error + assert in debug
	Dict r = rule_in.duplicate(true);
	IxRegistry& g = reg();
	g.n += 1;
	if (!r.has("id")) r.set("id", k);
	r.set("key", k);
	r.set("seq", g.n);
	auto found = g.rules.find(k);
	std::vector<Dict> keep;
	if (found != g.rules.end()) {
		for (const Dict& o : found->second)
			if (dbool(o, "legacy", false) || !ix_same_tiers(o, r)) keep.push_back(o);
	} else {
		g.order.push_back(k);
	}
	keep.push_back(r);
	std::stable_sort(keep.begin(), keep.end(), [](const Dict& x, const Dict& y) {
		const bool xr = x.has("tiers");
		const bool yr = y.has("tiers");
		if (xr != yr) return xr;
		return dint(x, "seq") < dint(y, "seq");
	});
	g.rules[k] = std::move(keep);
	return true;
}

void remove_rule(const std::string& threat_cls, const std::string& counter_cls, bool include_legacy) {
	ensure();
	const std::string k = ix_rk(threat_cls, counter_cls);
	auto it = reg().rules.find(k);
	if (it == reg().rules.end()) return;
	std::vector<Dict> keep;
	for (const Dict& o : it->second)
		if (dbool(o, "legacy", false) && !include_legacy) keep.push_back(o);
	if (keep.empty()) {
		reg().rules.erase(it);
		auto& ord = reg().order;
		ord.erase(std::remove(ord.begin(), ord.end(), k), ord.end());
	} else {
		it->second = std::move(keep);
	}
}

Dict rule(std::string_view threat_cls, std::string_view counter_cls, int tier, const Dict* fallback_rule) {
	ensure();
	const std::string tf = family(threat_cls);
	const std::string cf = counter_family(counter_cls);
	const std::string_view t = threat_cls;
	const std::string_view c = counter_cls;
	const std::string_view keys[8][2] = {{t, c}, {t, cf}, {tf, c}, {tf, cf}, {"*", c}, {"*", cf}, {t, "*"}, {tf, "*"}};
	const auto& rules = reg().rules;
	for (const auto& kc : keys) {
		if (kc[0].empty() || kc[1].empty()) continue;   // GDScript: k.begins_with("|") or k.ends_with("|")
		auto it = rules.find(ix_rk(kc[0], kc[1]));
		if (it == rules.end()) continue;
		for (const Dict& r : it->second)
			if (ix_tier_ok(r, tier)) return r;
	}
	return fallback_rule ? *fallback_rule : reg().default_rule;
}

bool has_rule(std::string_view threat_cls, std::string_view counter_cls, int tier) {
	const Dict empty;
	return rule(threat_cls, counter_cls, tier, &empty).size() > 0;
}

const std::unordered_map<std::string, std::vector<Dict>>& all_rules() {
	ensure();
	return reg().rules;
}

std::vector<std::string> rule_keys_in_order() {
	ensure();
	return reg().order;
}

State save_state() {
	ensure();
	State s;
	for (const auto& it : reg().rules) {
		std::vector<Dict> l;
		for (const Dict& d : it.second) l.push_back(d.duplicate(true));
		s.rules[it.first] = std::move(l);
	}
	s.order = reg().order;
	s.handlers = reg().handlers;
	s.tag_threat = reg().tag_threat;
	s.tag_counter = reg().tag_counter;
	s.channels = reg().channels;
	s.n = reg().n;
	return s;
}

void load_state(const State& st) {
	ensure();
	reg().rules.clear();
	for (const auto& it : st.rules) {
		std::vector<Dict> l;
		for (const Dict& d : it.second) l.push_back(d.duplicate(true));
		reg().rules[it.first] = std::move(l);
	}
	reg().order = st.order;
	reg().handlers = st.handlers;
	reg().tag_threat = st.tag_threat;
	reg().tag_counter = st.tag_counter;
	reg().channels = st.channels;
	reg().n = st.n;
}

bool allows(const MatBody& b, std::string_view counter_cls) {
	const Dict r = rule(classify(b), counter_cls, -1, &PASS_RULE());
	std::string o = dstr(r, "outcome", "pass");
	const Array bands = r.get("bands").as_array();
	if (r.has("bands") && !bands.empty()) o = vstr(bands[bands.size() - 1].as_array().get(1));
	return o != "pass" && o != "fail" && !o.empty();
}

double cohesion(int tier) { return 0.6 + 0.1 * static_cast<double>(tier); }
double disrupt_threshold(int tier) { return 6.0 + 4.0 * static_cast<double>(tier); }

// ================================================================ power

double threat_power(const Agent& threat, const Dict& r, std::string_view counter_fam) {
	const Value& w = r.get("w");
	const double wc = vnum(w.get("C"), counter_fam == "heat" ? 1.0 : 0.0);
	return threat.ch.K * vnum(w.get("K"), 1.0) + threat.ch.H * vnum(w.get("H"), 1.0) + threat.ch.C * wc +
	       threat.ch.E * vnum(w.get("E"), 1.0) + threat.ch.P * vnum(w.get("P"), 1.0);
}

double counter_power(const Agent& counter) {
	if (counter.power >= 0.0) return counter.power;
	if (counter.body != nullptr)
		return counter.body->mass * Materials::hardness(*counter.body) + dnum(counter.body->props, "cp_bonus", 0.0);
	return PLAIN_GUARD_CP;
}

bool _cond(const Dict& cond, const Agent& threat) {
	const double m = threat.mass;
	if (cond.has("mass_lt") && !(m < dnum(cond, "mass_lt"))) return false;
	if (cond.has("mass_max") && !(m <= dnum(cond, "mass_max"))) return false;
	if (cond.has("mass_min") && !(m >= dnum(cond, "mass_min"))) return false;
	if (cond.has("hostile") && dbool(cond, "hostile") != threat.hostile) return false;
	if (threat.body != nullptr) {
		if (cond.has("mats") && !arr_has_str(darr(cond, "mats"), Sim::mat_name(threat.body->mat))) return false;
		if (cond.has("tags") && !arr_has_str(darr(cond, "tags"), threat.body->tag)) return false;
		if (cond.has("forms") && !arr_has_str(darr(cond, "forms"), Sim::form_name(threat.body->form))) return false;
	}
	return true;
}

IxResult predict(CombatWorld* /*w*/, const Agent& threat, const Agent& counter, const Dict* fallback_rule) {
	const Dict r = rule(threat.cls, counter.ccls, counter.tier, fallback_rule ? fallback_rule : &DEFAULT_RULE());
	const std::string cfam = counter_family(counter.ccls);
	const double tp = threat_power(threat, r, cfam);
	const double cp = counter_power(counter);
	double eff = dnum(r, "eff", 1.0);
	if (r.has("eff_insulator") && counter.body != nullptr && Materials::insulates(*counter.body)) eff *= dnum(r, "eff_insulator");
	const double mult = counter.perfect ? dnum(r, "perfect_mult", 1.5) : 1.0;
	const double cpe = cp * mult * eff;
	const double ratio = tp > 1e-6 ? cpe / tp : 1.0e6;
	const double full_at = dnum(r, "full_at", 1.0);
	const double partial_at = dnum(r, "partial_at", 0.5);
	std::string band = ratio >= full_at ? "full" : (ratio >= partial_at ? "partial" : "fail");
	std::string outcome;
	const Value& by_form = r.get("by_form");
	if (threat.body != nullptr && by_form.has(Sim::form_name(threat.body->form))) {
		outcome = vstr(by_form.get(Sim::form_name(threat.body->form)));
		band = "form";
	} else if (threat.kind == "body" && !threat.hostile && r.has("inert")) {
		outcome = _cond(ddict(r, "when"), threat) ? dstr(r, "inert") : dstr(r, "inert_else", "pass");
		band = "inert";
	} else if (r.has("when") && !_cond(ddict(r, "when"), threat)) {
		outcome = dstr(r, "else", "pass");
		band = "cond";
	} else if (r.has("bands") && !darr(r, "bands").empty()) {
		const Array bands = darr(r, "bands");
		for (const Value& bd : bands) {
			const Array b = bd.as_array();
			if (ratio >= b.get(0).as_float() - 1e-9) outcome = vstr(b.get(1));
		}
		if (outcome.empty()) outcome = vstr(bands[0].as_array().get(1));
	} else {
		if (band == "full") outcome = dstr(r, "outcome", "block");
		else if (band == "partial") outcome = dstr(r, "partial", "weaken");
		else outcome = dstr(r, "fail", "overwhelm");
	}
	if (counter.perfect && r.has("perfect") && ratio >= full_at && band != "cond" && band != "inert" && band != "form")
		outcome = dstr(r, "perfect");
	IxResult res;
	res.outcome = outcome;
	res.band = band;
	res.ratio = ratio;
	res.tp = tp;
	res.cp = cp;
	res.cp_eff = cpe;
	res.eff = eff;
	res.perfect = counter.perfect;
	res.rule = r;
	res.rule_id = dstr(r, "id", "");
	res.to = dstr(r, "to", "");
	return res;
}

IxResult resolve(CombatWorld& w, Agent& threat, Agent& counter, IxCtx ctx, const Dict* fallback_rule) {
	IxResult res = predict(&w, threat, counter, fallback_rule);
	res.result = "";
	res.stopped = false;
	res.pass_scale = 1.0;
	res.counter_broken = false;
	res.knock_scale = 1.0;
	res.heat_used = 0.0;
	res.absorbed = 0.0;
	const Dict r = res.rule;
	if (threat.body != nullptr && counter.body != nullptr && (ctx.continuous || ctx.site == "wall" || ctx.site == "wave_wall")) {
		const std::string key = itos(threat.body->id) + ">" + itos(counter.body->id);
		auto it = w._partial_pairs.find(key);
		if (it != w._partial_pairs.end() && w.tick - it->second <= CONTACT_TICKS) {
			// A partial already applied during this contact: what is left passes on.
			it->second = w.tick;
			res.outcome = "pass";
			res.band = "contact";
			return res;
		}
		if (arr_has_str(reg().partial_once, res.outcome)) w._partial_pairs[key] = w.tick;
	}
	const bool ok = Outcomes::apply(w, res.outcome, threat, counter, res, r, ctx);
	if (!ok && r.has("fallback")) {
		res.outcome = dstr(r, "fallback");
		Outcomes::apply(w, res.outcome, threat, counter, res, r, ctx);
	}
	_emit(w, threat, counter, res, ctx);
	return res;
}

void _emit(CombatWorld& w, const Agent& threat, const Agent& counter, const IxResult& res, const IxCtx& ctx) {
	if (!w._record_events) return;
	if (ctx.continuous) {
		const std::string key = itos(threat.body != nullptr ? threat.body->id : -1) + "|" +
		                        itos(counter.body != nullptr ? counter.body->id : -1) + "|" + counter.ccls;
		auto it = w._ix_last.find(key);
		const int64_t last = it == w._ix_last.end() ? -1000000 : it->second;
		if (w.tick - last < IX_EVENT_TICKS) return;
		w._ix_last[key] = w.tick;
	}
	const Vec3 pos = threat.body != nullptr ? threat.body->pos : (threat.pos == Vec3() ? counter.pos : threat.pos);
	w.emit("interaction", D({{"threat", threat.cls},
	                         {"counter", counter.ccls},
	                         {"outcome", res.outcome},
	                         {"band", res.band},
	                         {"ratio", res.ratio},
	                         {"tp", res.tp},
	                         {"cp", res.cp_eff},
	                         {"perfect", res.perfect},
	                         {"pos", pos},
	                         {"dir", threat.dir},
	                         {"threat_actor", threat.actor != nullptr ? threat.actor->id : -1},
	                         {"counter_actor", counter.actor != nullptr ? counter.actor->id : -1},
	                         {"threat_body", threat.body != nullptr ? threat.body->id : -1},
	                         {"counter_body", counter.body != nullptr ? counter.body->id : -1},
	                         {"to", res.to},
	                         {"rule", res.rule_id},
	                         {"tier", maxi(threat.tier, counter.tier)}}));
}

}  // namespace Interactions
}  // namespace ff
