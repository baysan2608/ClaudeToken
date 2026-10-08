// Fourfold core - ports of the HUD context builders of game/game.gd.
#include "App/HudBuilder.h"

#include "App/CounterHints.h"

#include "Combat/Acts.h"
#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

#include <cctype>
#include <cmath>

namespace ff {
namespace HudBuilder {

namespace {
const char* const kAttackMovesHb[4] = {"earth_attack", "water_attack", "fire_attack", "air_attack"};

std::string hb_upper(std::string s) {
	for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}
// GDScript String.capitalize(): "snake_case words" -> "Snake Case Words".
std::string hb_capitalize(const std::string& s) {
	std::string out;
	bool start = true;
	for (char c : s) {
		if (c == '_' || c == ' ') {
			if (!out.empty() && out.back() != ' ') out += ' ';
			start = true;
			continue;
		}
		out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		start = false;
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}
std::string hb_marker_label(const std::string& mode, const MatBody& b, const std::string& reason) {
	return mode + " " + itos(static_cast<int64_t>(b.mass)) + " kg" + (reason == "mass" ? "  too heavy" : "");
}
}  // namespace

std::string _short_name(const std::string& id) {
	const Dict def = Moves::defs().get(id).as_dict();
	const std::string nm = dstr(def, "name", id);
	const size_t sep = nm.find(" / ");
	return sep == std::string::npos ? nm : nm.substr(0, sep);
}

Dict gesture_petals(const ActorState& a) {
	Dict out;
	const char* pairs[3][2] = {{"up", "thrust"}, {"down", "ground"}, {"side", "sweep"}};
	for (const auto& p : pairs) {
		const std::string id = Moves::resolve(a.element, a.sub(), p[1]);
		if (!id.empty()) out.set(p[0], _short_name(id));
	}
	return out;
}

Dict guard_petals(const ActorState& a) {
	Dict out;
	const char* pairs[2][2] = {{"up", "push"}, {"down", "sink"}};
	for (const auto& p : pairs) {
		const std::string id = Moves::resolve(a.element, a.sub(), p[1]);
		if (!id.empty()) out.set(p[0], _short_name(id));
	}
	return out;
}

std::string shape_label(const ActorState& a) {
	const ActionInst* inst = a.action.get();
	if (inst == nullptr || inst->slot != "tech" ||
	    !(inst->phase == ActionPhase::Channel || inst->phase == ActionPhase::Charge || inst->phase == ActionPhase::Startup))
		return "";
	const std::string sh = Charge::params(*inst, "shape", "");
	return !sh.empty() ? hb_capitalize(sh) : "Shape";
}

Dict charge_ring_context(const ActorState& a) {
	const ActionInst* inst = a.action.get();
	if (inst == nullptr || !(inst->phase == ActionPhase::Charge || inst->phase == ActionPhase::Channel)) return Dict();
	const Dict d = Charge::pdef(*inst);
	const int mx = Charge::max_tier(d);
	if (mx <= 0) return Dict();
	std::string slot = "attack";
	if (inst->slot == "guard" || inst->slot == "push" || inst->slot == "sink") slot = "guard";
	else if (inst->slot == "tech") slot = "tech";
	else if (inst->slot == "evade" || inst->slot == "evade_hold") slot = "evade";
	const Vec2 pr = Charge::progress(*inst);
	return D({{"slot", slot}, {"tier", static_cast<int>(pr.x)}, {"frac", snappedf(pr.y, 0.01)}, {"max", mx}});
}

double attack_charge_sec(int element) {
	const Dict def = Moves::defs().get(kAttackMovesHb[clampi(element, 0, 3)]).as_dict();
	const double t = maxf(dnum(def, "startup", 0.0), Moves::HOLD_THRESHOLD);
	return std::ceil(t / Sim::DT - 0.001) * Sim::DT;
}

Dict attack_ring_context(const ActorState& a) {
	if (a.buffered == "attack") return D({{"attack_charge", 0.0}, {"attack_element", -1}});
	const ActionInst* inst = a.action.get();
	if (inst != nullptr && (inst->phase == ActionPhase::Startup || inst->phase == ActionPhase::Charge)) {
		for (const char* m : kAttackMovesHb)
			if (inst->id == m) return D({{"attack_charge", inst->total}, {"attack_element", inst->element}});
		if (Sim::is_attack_slot(inst->slot)) {
			const double t = maxf(dnum(inst->def, "startup", 0.0), Moves::HOLD_THRESHOLD);
			return D({{"attack_charge", inst->total}, {"attack_element", inst->element}, {"attack_decide", std::ceil(t / Sim::DT - 0.001) * Sim::DT}});
		}
	}
	return D({{"attack_charge", -1.0}, {"attack_element", -1}, {"attack_decide", 0.0}});
}

ChargeView charge_view(const ActorState& a) {
	ChargeView v;
	const Dict c = charge_ring_context(a);
	if (c.empty()) return v;
	v.active = true;
	v.button = dstr(c, "slot");
	v.tier = dint(c, "tier");
	v.frac = static_cast<float>(dnum(c, "frac"));
	v.max_tier = dint(c, "max");
	return v;
}

bool water_in_reach(CombatWorld& w, ActorState& player, const ActorIntent& it) {
	const ArenaMap& ar = w.arena;
	const Dict wt = Moves::defs().get("water_tech").as_dict();
	const double reach = dnum(wt, "reach");
	const Vec2 pn = V2(clampf(player.pos.x, ar.pool_min.x, ar.pool_max.x), clampf(player.pos.z, ar.pool_min.y, ar.pool_max.y));
	if (pn.distance_to(Vec2(player.pos.x, player.pos.z)) < reach && w.pool != nullptr && w.pool->mass > dnum(wt, "draw_rate") * Sim::DT) return true;
	const Vec3 aim = w.aim_dir(player, it);
	return w.find_body(player, aim, reach, 70.0, [](MatBody& b) { return ActWater::_water_filter(b); }) != nullptr;
}

Dict tech_context(CombatWorld& w, ActorState& player, const ActorIntent& it, MatBody* held) {
	Dict out = D({{"label", ""}, {"ok", true}});
	const Vec3 dir = w.aim_dir(player, it);
	const Dict pv = w.tech_preview(player, dir);
	const std::string id = Moves::resolve(player.element, player.sub(), "tech");
	const Dict def = Moves::defs().get(id).as_dict();
	const std::string mode = dstr(pv, "mode", "");
	if (!mode.empty()) {
		out.set("label", hb_upper(mode));
	} else if (def.has("mode_label")) {
		out.set("label", hb_upper(dstr(def, "mode_label")));
	} else if (!id.empty()) {
		const std::string sn = _short_name(id);
		const size_t sp = sn.rfind(' ');
		out.set("label", hb_upper(sp == std::string::npos ? sn : sn.substr(sp + 1)));
	} else {
		out.set("label", "-");
	}
	if (held != nullptr && mode.empty()) out.set("label", "RELEASE");
	if (!pv.empty()) {
		out.set("ok", dbool(pv, "ok", true));
		MatBody* pb = w.get_body(dint(pv, "body", -1));
		if (pb != nullptr) {
			out.set("marker_body", pb->id);
			out.set("marker_label", hb_marker_label(mode, *pb, dstr(pv, "reason", "")));
		}
	}
	return out;
}

std::vector<StatusView> statuses_of(const ActorState& a) {
	std::vector<StatusView> out;
	for (const auto& kv : a.status) {
		StatusView s;
		s.name = kv.first;
		s.t = static_cast<float>(dnum(kv.second, "t"));
		s.mag = static_cast<float>(dnum(kv.second, "mag", 1.0));
		out.push_back(s);
	}
	return out;
}

HudModel build(const Context& c) {
	HudModel h;
	if (c.world == nullptr || c.player == nullptr) return h;
	CombatWorld& w = *c.world;
	ActorState& p = *c.player;
	static const ActorIntent kNoIntent;
	const ActorIntent& it = c.player_intent != nullptr ? *c.player_intent : kNoIntent;
	h.valid = true;
	h.player_id = p.id;
	h.health = static_cast<float>(p.health);
	h.balance = static_cast<float>(p.balance);
	h.focus = static_cast<float>(p.focus);
	h.heat_reserve = static_cast<float>(p.heat_reserve);
	h.water_carried = static_cast<float>(p.water_carried);
	h.metal_carried = static_cast<float>(p.metal_carried);
	h.static_charge = static_cast<float>(p.static_charge);
	// Resources shown only when they matter (game/ui/hud.gd).
	h.show_heat = p.heat_reserve > 1.0;
	h.show_water = p.element == Sim::WATER || p.water_carried < 5.95;
	h.show_metal = (p.element == Sim::EARTH && p.sub() == 1) || p.metal_carried < 11.95;
	h.show_static = p.static_charge > 0.5 || (p.element == Sim::FIRE && p.sub() == 2);
	h.element = p.element;
	h.sub = p.sub();
	h.element_name = std::string(ElementName(p.element));
	h.sub_name = std::string(SubName(p.element, p.sub()));
	for (size_t i = 0; i < 4; ++i) {
		h.elements_unlocked[i] = p.elements[i];
		h.subs_unlocked[i] = p.subs_unlocked[static_cast<size_t>(clampi(p.element, 0, 3))][i];
		h.sub_names[i] = std::string(SubName(p.element, static_cast<int>(i)));
	}
	h.statuses = statuses_of(p);

	// ---- technique label (Game._hud_context)
	std::string label;
	bool ok = true;
	int marker_body = -1;
	std::string marker_label;
	MatBody* held = w.held(p);
	const bool sub0 = p.sub() == 0;
	switch (sub0 ? p.element : -1) {
		case Sim::EARTH: label = held != nullptr ? "THROW" : "LIFT"; break;
		case Sim::WATER:
			label = held != nullptr ? "STREAM" : "DRAW";
			ok = held != nullptr || p.water_carried >= 1.0 || water_in_reach(w, p, it);
			break;
		case Sim::FIRE:
			if (held != nullptr && held->is_stone()) {
				label = held->phase == Phase::Molten ? "POUR" : "HEAT";
			} else {
				const Dict pv = ActFire::preview(w, p, w.aim_dir(p, it));
				label = !dstr(pv, "mode").empty() ? dstr(pv, "mode") : "\xE2\x80\x94";
				ok = dbool(pv, "ok");
				MatBody* pb = w.get_body(dint(pv, "body", -1));
				if (pb != nullptr) {
					marker_body = pb->id;
					marker_label = hb_marker_label(dstr(pv, "mode"), *pb, dstr(pv, "reason"));
				}
			}
			break;
		case Sim::AIR: label = (!p.grounded && p.has("glide")) ? "GLIDE" : "LIFT"; break;
		default: break;
	}
	if (p.sub() != 0 || label.empty()) {
		const Dict tl = tech_context(w, p, it, held);
		label = dstr(tl, "label");
		ok = dbool(tl, "ok", true);
		if (tl.has("marker_body")) {
			marker_body = dint(tl, "marker_body");
			marker_label = dstr(tl, "marker_label");
		}
	}
	h.tech_label = label;
	h.tech_available = ok;
	h.holding = held != nullptr;

	const Dict gp = gesture_petals(p);
	h.petal_up = dstr(gp, "up");
	h.petal_down = dstr(gp, "down");
	h.petal_side = dstr(gp, "side");
	const Dict gd = guard_petals(p);
	h.guard_petal_up = dstr(gd, "up");
	h.guard_petal_down = dstr(gd, "down");
	h.shape_label = shape_label(p);
	{
		const CounterHints::Result ch = CounterHints::query(w, p);
		h.has_threat = ch.valid;
		h.threat_cls = ch.threat_cls;
		h.threat_tti = static_cast<float>(ch.tti);
		h.perfect_window = static_cast<float>(Moves::PERFECT_WINDOW);
		h.threat_world = ch.pos;
		h.counters = ch.hints;
	}

	const Dict ar = attack_ring_context(p);
	h.attack_charge = static_cast<float>(dnum(ar, "attack_charge", -1.0));
	h.attack_element = dint(ar, "attack_element", -1);
	h.attack_decide = static_cast<float>(dnum(ar, "attack_decide", 0.0));
	if (h.attack_decide <= 0.0f && h.attack_charge >= 0.0f)
		h.attack_decide = static_cast<float>(attack_charge_sec(h.attack_element >= 0 ? h.attack_element : p.element));
	h.charge = charge_view(p);
	if (h.charge.active && p.action) h.charge_move_name = _short_name(p.action->id);

	ActorState* tgt = w.get_actor(p.lock_target);
	MatBody* mb = w.get_body(marker_body);
	if (mb != nullptr) {
		h.has_marker = true;
		h.marker_world = mb->pos;
		h.marker_label = marker_label;
	} else if (tgt != nullptr) {
		h.has_marker = true;
		h.marker_world = tgt->chest();
		h.marker_label = tgt->name;
	}
	h.target_id = tgt != nullptr ? tgt->id : -1;
	if (tgt != nullptr) {
		h.has_rival = true;
		h.rival_name = tgt->name;
		h.rival_health = static_cast<float>(tgt->health);
		h.rival_balance = static_cast<float>(tgt->balance);
		h.rival_focus = static_cast<float>(tgt->focus);
		h.rival_statuses = statuses_of(*tgt);
	}
	h.scenario_title = c.scenario_title;
	h.objective = c.objective;
	h.challenge_text = c.challenge_text;
	return h;
}

std::vector<std::string> debug_lines(CombatWorld& w, const std::string& scenario_id, const std::string& ai_debug) {
	std::vector<std::string> out;
	out.push_back(scenario_id + "  tick " + itos(w.tick) + "  bodies " + itos(w.alive_count()) + "/" + itos(Sim::MAX_BODIES));
	for (auto& ap : w.actors) {
		const ActorState& a = *ap;
		const std::string act = a.action == nullptr ? "-" : a.action->id + "/" + a.action->phase_name() + " " + ftos(a.action->total, 2);
		out.push_back(a.name + " hp" + ftos(a.health, 0) + " bal" + ftos(a.balance, 0) + " foc" + ftos(a.focus, 0) + " heat" + ftos(a.heat_reserve, 0) +
		              " " + std::string(ElementName(a.element)) + " " + (a.stun <= 0.0 ? act : "stun:" + a.stun_kind));
	}
	if (!ai_debug.empty()) out.push_back("AI: " + ai_debug);
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (b.alive && b.form != Form::Pool && (b.controller >= 0 || b.attack_id != 0 || b.is_hot())) {
			std::string lin;
			if (!b.lineage.empty()) {
				lin = " lin[";
				for (size_t k = 0; k < b.lineage.size(); ++k) lin += (k ? ", " : "") + itos(b.lineage[k]);
				lin += "]";
			}
			out.push_back(b.describe() + " " + b.origin + lin);
		}
	}
	out.push_back("energy \xCE\x94 " + ftos(w.system_energy(), 1) + " ledger " + ftos(w.ledger_balance(), 1));
	return out;
}

}  // namespace HudBuilder
}  // namespace ff
