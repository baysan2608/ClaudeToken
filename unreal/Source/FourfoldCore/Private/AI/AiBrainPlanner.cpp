// Fourfold core - port of game/actors/ai_brain.gd, planner mode (docs/AI.md): configure, perception + counter
// decisions, plan execution, planner offense, chains / interrupts, drills.
#include "AI/AiBrain.h"

#include "AI/AiPresets.h"
#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {

namespace {
const char* const kLegacyDrillsAb[] = {"stone_rain", "seize", "lightning"};
bool ab_is_legacy_drill(const std::string& d) {
	for (const char* x : kLegacyDrillsAb)
		if (d == x) return true;
	return false;
}
std::vector<int> ab_int_list(const Value& v) {
	std::vector<int> out;
	for (const Value& x : v.as_array()) out.push_back(vint(x));
	return out;
}
}  // namespace

std::vector<std::string> AiBrain::preset_names() { return AiPresets::names(); }

void AiBrain::configure(const Dict& opts) {
	planner = true;
	const std::string pname = dstr(opts, "preset", dstr(cfg, "preset", "adept"));
	prm = AiPresets::get_preset(pname);
	cfg.set("preset", pname);
	for (const char* k : {"reaction", "counter", "aggression", "misjudge", "timing_err"}) cfg.set(k, prm.get(k));
	for (const auto& kv : opts) {
		const std::string& k = kv.first;
		if (k == "preset" || k == "elements" || k == "subs" || k == "kit" || k == "unlock") continue;
		cfg.set(k, kv.second);
		if (prm.has(k)) prm.set(k, kv.second);
	}
	const bool unlock = dbool(opts, "unlock", true);
	// Kit breadth: explicit elements, else the preset's count starting from the fighter's element.
	std::vector<int> els;
	if (opts.has("elements")) {
		for (const Value& e : opts.get("elements").as_array()) {
			const int ei = vint(e);
			if (ei >= 0 && ei < 4 && std::find(els.begin(), els.end(), ei) == els.end()) els.push_back(ei);
		}
	} else {
		std::vector<int> order = {me->element};
		for (int e = 0; e < 4; ++e)
			if (std::find(order.begin(), order.end(), e) == order.end()) order.push_back(e);
		for (int e : order) {
			if (static_cast<int>(els.size()) >= dint(prm, "elements")) break;
			if (unlock || me->elements[static_cast<size_t>(e)]) els.push_back(e);
		}
	}
	const Value& subs_opt = opts.get("subs");
	kit.clear();
	for (int e : els) {
		std::vector<int> subs;
		if (subs_opt.is_dict() && subs_opt.has(itos(e))) {
			subs = ab_int_list(subs_opt.get(itos(e)));
		} else if (subs_opt.is_array() && subs_opt.as_array().size() > static_cast<size_t>(e) && subs_opt.as_array()[static_cast<size_t>(e)].is_array()) {
			subs = ab_int_list(subs_opt.as_array()[static_cast<size_t>(e)]);
		} else {
			for (int s = 0; s < dint(prm, "subs"); ++s) subs.push_back(s);
		}
		std::vector<int> clean;
		for (int s : subs)
			if (s >= 0 && s < 4 && std::find(clean.begin(), clean.end(), s) == clean.end()) clean.push_back(s);
		kit.push_back({e, clean});
	}
	// Drill kits: the drilled element/sub (or every element for the matrix) is part of the kit.
	const std::string drill = dstr(cfg, "drill", "");
	const auto es = AiPresets::parse_element_drill(drill);
	if (es.first >= 0) {
		if (!ai_kit_has(kit, es.first)) kit.push_back({es.first, {}});
		std::vector<int>* sv = ai_kit_subs(kit, es.first);
		if (std::find(sv->begin(), sv->end(), es.second) == sv->end()) sv->push_back(es.second);
	}
	if (drill == "matrix") _build_matrix();
	if (unlock) {
		std::array<bool, 4> granted{{false, false, false, false}};
		for (const auto& kv : kit) {
			granted[static_cast<size_t>(kv.first)] = true;
			for (int s : kv.second) me->subs_unlocked[static_cast<size_t>(kv.first)][static_cast<size_t>(s)] = true;
		}
		if (drill == "matrix") {
			granted = {true, true, true, true};
			for (auto& row : me->subs_unlocked)
				for (bool& u : row) u = true;
		}
		me->elements = granted;
		for (const auto& kv : ddict(prm, "kit")) me->kit.set(kv.first, kv.second);
		for (const auto& kv : ddict(opts, "kit")) me->kit.set(kv.first, kv.second);
	} else {
		AiKit kept;
		for (auto& kv : kit) {
			if (!me->elements[static_cast<size_t>(kv.first)]) continue;
			std::vector<int> keep;
			for (int s : kv.second)
				if (me->subs_unlocked[static_cast<size_t>(kv.first)][static_cast<size_t>(s)]) keep.push_back(s);
			kept.push_back({kv.first, keep});
		}
		kit = kept;
	}
	std::vector<int> el_list;
	for (const auto& kv : kit) el_list.push_back(kv.first);
	std::sort(el_list.begin(), el_list.end());
	Array ea;
	for (int e : el_list) ea.append(e);
	cfg.set("elements", ea);
	if (!kit.empty() && !ai_kit_has(kit, me->element)) intent.element_select = el_list[0];
	_set_ranges();
	plan_ = AiPlan();
	drill_i_ = 0;
}

Dict AiBrain::describe() const {
	Dict subs;
	for (const auto& kv : kit) {
		Array a;
		for (int s : kv.second) a.append(s);
		subs.set(itos(kv.first), a);
	}
	return D({{"planner", planner}, {"preset", dstr(cfg, "preset", "")}, {"elements", cfg.get("elements", Array())}, {"subs", subs},
	          {"drill", dstr(cfg, "drill", "")}, {"reaction", cfg_num("reaction")}, {"counter", cfg_num("counter")},
	          {"aggression", cfg_num("aggression")}, {"misjudge", cfg_num("misjudge", 0.0)}, {"timing_err", cfg_num("timing_err", 0.0)}});
}

void AiBrain::_set_ranges() {
	std::vector<double> mids;
	for (const AiPlanner::KitMove& c : AiPlanner::kit_moves(kit, AiPlanner::offense_slots())) {
		const Dict ai = ddict(Moves::defs().get(c.id).as_dict(), "ai");
		const std::string role = dstr(ai, "role", "");
		if (role == "poke" || role == "finisher" || role == "zone") {
			const Array r = ai.get("range", A({2.0, 8.0})).as_array();
			mids.push_back(clampf((vnum(r.get(0)) + vnum(r.get(1))) * 0.5, 3.0, 10.0));
		}
	}
	double pref = 7.5;
	if (!mids.empty()) {
		std::sort(mids.begin(), mids.end());
		pref = mids[mids.size() / 2];
	}
	const double ag = cfg_num("aggression");
	cfg.set("range_lo", clampf(pref - 1.5 - 1.5 * ag, 2.5, 9.0));
	cfg.set("range_hi", clampf(pref + 3.0 - 1.5 * ag, 5.0, 13.0));
}

AiParams AiBrain::_planner_params() {
	AiParams p;
	p.merge(prm);
	for (const char* k : {"reaction", "counter", "aggression", "misjudge", "timing_err"}) p.merge(D({{k, cfg.get(k, prm.get(k, Value(0.0)))}}));
	p.draw_ok = [this](MatBody& b) { return _draw_sets_in_time(b); };
	p.recent = recent_;
	return p;
}

// ------------------------------------------------------------------ perception + counter decisions

bool AiBrain::_react_planner(ActorState* foe, bool stamp_only) {
	// The foe's pour wind-up is the wave's telegraph (the wave is the same body).
	if (foe != nullptr && foe->action != nullptr && foe->action->id == "pour" && foe->held_body >= 0) {
		const std::string pk = "pour" + itos(foe->action->attack_id);
		_perceived(pk);
		pour_body_ = foe->held_body;
		pour_seen_ = seen_[pk];
	}
	AiThreat best;
	for (size_t i = 0; i < w->bodies.size(); ++i) {
		MatBody& b = *w->bodies[i];
		if (!b.alive || b.controller >= 0) continue;
		if (b.attack_id == 0 && b.form != Form::Zone && b.form != Form::Cloud) continue;
		AiThreat th = AiPlanner::body_threat(*w, *me, b);
		if (!th.valid) continue;
		const std::string key = th.key;
		if (b.form == Form::Wave && b.id == pour_body_) {
			pour_body_ = -1;
			if (!seen_.count(key)) seen_[key] = pour_seen_;
		}
		if (!_perceived(key) || decided_.count(key)) continue;
		if (!best.valid || th.tti < best.tti) best = th;
	}
	if (foe != nullptr && (!Status::hidden(*foe) || foe->pos.distance_to(me->pos) < 2.0f)) {
		AiThreat vt = AiPlanner::action_threat(*w, *me, foe);
		if (vt.valid && _perceived(vt.key) && !decided_.count(vt.key)) {
			if (!best.valid || vt.tti < best.tti) best = vt;
		}
	}
	if (seen_.size() > 64) {
		for (auto it = seen_.begin(); it != seen_.end();) {
			if (t_ - it->second > 8.0) {
				decided_.erase(it->first);
				it = seen_.erase(it);
			} else {
				++it;
			}
		}
	}
	if (!best.valid) return false;
	if (stamp_only) {
		// Busy. An open-ended charge / channel of ours is dropped for an imminent threat; otherwise only a waiting
		// (not yet pressed) plan gives way to a more urgent threat.
		const ActionInst* act = me->action.get();
		if ((hold_ == "attack" || (hold_ == "tech" && !hold_draw_)) && guard_at_ < 0.0 && best.tti < 0.7 && act != nullptr &&
		    (act->phase == ActionPhase::Charge || act->phase == ActionPhase::Channel) && w->held(*me) == nullptr) {
			hold_.clear();
			plan_ = AiPlan();
			return _decide(best);
		}
		if (!plan_.active || plan_.stage != "wait" || !hold_.empty() || guard_at_ >= 0.0) return false;
		if (best.tti >= plan_.press_at - t_) return false;
		plan_ = AiPlan();
	}
	return _decide(best);
}

bool AiBrain::_perceived(const std::string& key) {
	if (!seen_.count(key)) {
		seen_[key] = t_ + rng.randf_range(-0.05f, 0.08f) + ((planner && Status::has(*me, "blinded")) ? 0.2 : 0.0);
		return false;
	}
	return t_ - seen_[key] >= cfg_num("reaction");
}

bool AiBrain::_decide(const AiThreat& th) {
	const double m = cfg_num("misjudge", 0.0);
	const double err = rng.randf_range(static_cast<float>(-m), static_cast<float>(m));
	AiParams p = _planner_params();
	const std::vector<AiOption> opts = AiPlanner::counters(*w, *me, th, kit, p, err);
	const AiOption pick = AiPlanner::choose(opts, p, rng);
	const std::string key = th.key;
	if (!pick.valid) {
		decided_[key] = "none";
		return false;
	}
	decided_[key] = pick.label;
	last_plan = pick.to_dict();
	last_plan.set("threat", th.cls);
	last_plan.set("key", key);
	last_plan.set("tti", th.tti);
	last_plan.set("err", err);
	last_plan.set("t", t_);
	last_plan.set("options", static_cast<int64_t>(opts.size()));
	debug_state = "counter " + pick.label;
	_adopt(pick, &th);
	return true;
}

void AiBrain::_adopt(const AiOption& pick, const AiThreat* th) {
	AiPlan p;
	p.active = true;
	p.o = pick;
	p.t0 = t_;
	p.stage = "wait";
	MatBody* b = th != nullptr ? th->body : nullptr;
	p.body = b != nullptr ? b->id : -1;
	p.attack_id = th != nullptr ? th->attack_id : 0;
	p.tti_abs = t_ + (th != nullptr ? th->tti : 0.0);
	p.legacy = "";
	if (b == nullptr && pick.aim_body >= 0) p.body = pick.aim_body;   // offense on a body (Smelter on a wall face)
	if (pick.slot == "tech" && pick.mode == "DRAW" && b != nullptr) p.legacy = "draw";
	else if (pick.slot == "guard" && pick.id == "guard" && pick.element == Sim::EARTH && pick.perfect && b != nullptr && b->is_projectile())
		p.legacy = "redirect";
	else if (pick.slot == "evade" && b != nullptr) p.legacy = "dodge";
	double press_in = pick.press_in;
	if (pick.perfect && p.legacy.empty()) {
		const double e = cfg_num("timing_err", 0.05);
		press_in = maxf(press_in, (th != nullptr ? th->tti : 0.0) - AiPlanner::PERFECT_LEAD + rng.randf_range(static_cast<float>(-e), static_cast<float>(e)));
	}
	if (p.legacy == "redirect" || p.legacy == "draw") press_in = 0.0;
	p.press_at = t_ + press_in;
	if (pick.guard_for != 0.0 || (pick.press == "guard" && pick.slot == "guard")) {
		p.has_guard_until = true;
		p.guard_until = t_ + pick.guard_for;
	}
	plan_ = p;
	_exec_plan(w->get_actor(me->lock_target));
}

void AiBrain::_end_plan() {
	if (plan_.active && plan_.stage == "tech") {
		intent.tech_held = false;
		intent.tech_released = true;
	}
	plan_ = AiPlan();
}

void AiBrain::_exec_plan(ActorState* foe) {
	AiPlan& p = plan_;
	if (!p.active) return;
	if (t_ > p.t0 + 5.0) {
		_end_plan();
		return;
	}
	if (p.stage == "wait") {
		if (p.o.element != me->element || me->sub_of(p.o.element) != p.o.sub) {
			intent.element_select = p.o.element;
			intent.sub_select = p.o.sub;
			p.press_at = maxf(p.press_at, t_ + Sim::DT);
			return;
		}
		if (t_ + 1e-6 < p.press_at) return;
		if (!p.legacy.empty()) {
			MatBody* b = w->get_body(p.body);
			const std::string legacy = p.legacy;
			const double tti_abs = p.tti_abs;
			plan_ = AiPlan();
			if (b == nullptr || !b->alive) return;
			_act_on(legacy, *b, maxf(0.0, tti_abs - t_));
			return;
		}
		_press_plan(p, foe);
	} else if (p.stage == "guard_gesture") {
		intent.guard_held = true;
		const ActionInst* a = me->action.get();
		if (t_ >= p.gesture_at && a != nullptr && a->id == "guard" && a->phase == ActionPhase::Channel) {
			intent.guard_gesture = static_cast<int>(p.o.slot == "push" ? Gesture::Up : Gesture::Down);
			p.stage = "after_gesture";
			p.gest_t = t_;
		} else if (t_ > p.gesture_at + 0.5 || (a == nullptr && t_ > p.press_t + 0.2)) {
			plan_ = AiPlan();
		}
	} else if (p.stage == "after_gesture") {
		// Keep the guard button down one more tick, then let the push / sink run.
		if (t_ - p.gest_t < 0.05) intent.guard_held = true;
		else plan_ = AiPlan();
	} else if (p.stage == "tech") {
		_tech_stage(p, foe);
	} else if (p.stage == "evade_hold") {
		intent.evade_held = true;
		intent.move = p.move;
		if (t_ >= p.until) plan_ = AiPlan();
	}
}

void AiBrain::_press_plan(AiPlan& p, ActorState* foe) {
	p.press_t = t_;
	const std::string press = p.o.press;
	if (press == "attack") {
		_press("attack");
		intent.attack_gesture = p.o.gesture;
		hold_ = "attack";
		hold_until_ = t_ + maxf(p.o.hold, 0.05);
		aim_hold_ = p.o.aim;
		plan_ = AiPlan();
	} else if (press == "guard") {
		_press("guard");
		if (p.o.slot == "guard") {
			hold_ = "guard";
			hold_until_ = p.has_guard_until ? p.guard_until : t_ + 0.6;
			guard_attack_ = p.attack_id;
			plan_ = AiPlan();
		} else {
			p.stage = "guard_gesture";
			p.gesture_at = t_ + maxf(p.o.hold, 2.0 * Sim::DT);
		}
	} else if (press == "tech") {
		_press("tech");
		_aim_plan(p, foe);
		p.stage = "tech";
		p.started = false;
		p.release_at = t_ + maxf(p.o.hold, dnum(Moves::defs().get(p.o.id).as_dict(), "startup", 0.1) + 0.05);
	} else if (press == "evade") {
		Vec3 side = me->forward().cross(Vec3::Up());
		if (rng.randf() < 0.5f) side = -side;
		intent.move = p.o.slot != "evade_hold" ? side : Vec3();   // stances are taken standing
		_press("evade");
		if (p.o.slot == "evade_hold") {
			intent.evade_held = true;
			p.stage = "evade_hold";
			p.move = Vec3();
			p.until = t_ + 0.25 + maxf(p.o.hold, 0.6);
		} else {
			plan_ = AiPlan();
		}
	} else {
		plan_ = AiPlan();
	}
}

void AiBrain::_aim_plan(AiPlan& p, ActorState* foe) {
	MatBody* b = w->get_body(p.body);
	Vec3 target;
	if (b != nullptr && b->alive && w->held(*me) == nullptr) target = b->pos;
	else if (foe != nullptr) target = foe->pos;
	else return;
	Vec3 d = target - me->pos;
	d.y = 0.0f;
	if (d.length() > 0.1f) {
		intent.aim_dir = d.normalized();
		intent.aim_active = true;
	}
}

void AiBrain::_tech_stage(AiPlan& p, ActorState* foe) {
	intent.tech_held = true;
	_aim_plan(p, foe);
	const ActionInst* act = me->action.get();
	if (!p.started) {
		if (act != nullptr && act->slot == "tech" && act->id == p.o.id) {
			p.started = true;
		} else if (t_ - p.press_t < 0.6) {
			intent.tech_pressed = true;   // the world buffers a press only briefly: keep pressing
			return;
		} else {
			_end_plan();
			return;
		}
	}
	if (act == nullptr || act->slot != "tech") {
		plan_ = AiPlan();
		return;
	}
	MatBody* held = w->held(*me);
	const bool grip = dstr(act->def, "verb", "") == "grip" || act->id == "earth_tech";
	if (held != nullptr && !p.has_caught) {
		p.has_caught = true;
		p.caught_at = t_;
	}
	bool release = false;
	if (grip) {
		if (p.has_caught) release = t_ >= p.caught_at + maxf(0.2, p.o.hold * 0.5);
		else release = t_ > p.press_t + 1.6;
	} else {
		release = t_ >= p.release_at;
	}
	if (release || t_ > p.press_t + 3.0) {
		if (foe != nullptr) {
			Vec3 d = foe->pos - me->pos;
			d.y = 0.0f;
			if (d.length() > 0.1f) {
				intent.aim_dir = d.normalized();
				intent.aim_active = true;
			}
		}
		intent.tech_held = false;
		intent.tech_released = true;
		plan_ = AiPlan();
	}
}

// ------------------------------------------------------------------ offense (planner)

void AiBrain::_offense_planner(ActorState& foe) {
	const std::string drill = cfg_drill();
	if (t_ < next_attack_ || me->action != nullptr) return;
	if (ab_is_legacy_drill(drill)) {
		_offense(foe);
		return;
	}
	const double interval = !drill.empty() ? cfg_num("interval") : lerpf(3.2, 1.1, cfg_num("aggression"));
	if (drill == "matrix") {
		next_attack_ = t_ + interval * rng.randf_range(0.9f, 1.1f);
		_drill_matrix(foe);
		return;
	}
	const auto es = AiPresets::parse_element_drill(drill);
	if (es.first >= 0) {
		next_attack_ = t_ + interval * rng.randf_range(0.8f, 1.25f);
		_drill_element(foe, es.first, es.second);
		return;
	}
	if (Status::hidden(foe) && me->pos.distance_to(foe.pos) > 2.0f) {
		next_attack_ = t_ + 0.3;   // can't see it: no aimed attacks into the fog
		return;
	}
	const std::vector<AiOption> opts = AiPlanner::offense(*w, *me, foe, kit, _planner_params(), rng);
	if (opts.empty()) {
		next_attack_ = t_ + 0.4;
		return;
	}
	const AiOption& pick = opts[0];
	const AiObserve st = AiPlanner::observe(*w, *me, foe);
	if (st.cover && !pick.has_reason("barrier")) {
		// Behind cover: don't throw into it (_move_tactical steps out).
		next_attack_ = t_ + 0.3;
		return;
	}
	next_attack_ = t_ + interval * rng.randf_range(0.8f, 1.25f);
	debug_state = "offense " + pick.label;
	Dict decayed;
	for (const auto& kv : recent_) {
		const double v = vnum(kv.second) * 0.7;
		if (v >= 0.05) decayed.set(kv.first, v);
	}
	recent_ = decayed;
	recent_.set(pick.id, dnum(recent_, pick.id, 0.0) + 1.0);
	_adopt(pick);
}

bool AiBrain::_chain(ActorState* foe) {
	const ActionInst* a = me->action.get();
	if (foe == nullptr || a == nullptr || a->phase != ActionPhase::Recovery || !dbool(a->data, "contact", false)) return false;
	if (!Sim::is_attack_slot(a->slot) || !a->def.has("chain")) return false;
	if (dint(me->chain, "n", 0) >= mini(dint(prm, "chain", 1), CombatWorld::CHAIN_MAX)) return false;
	const double rec = dnum(a->def, "recovery") * Status::recovery_mult(*me);
	if (rec > 0.0 && a->t / rec < dnum(a->def, "chain")) return false;
	const std::string key = "ch" + itos(a->attack_id);
	if (decided_.count(key)) return false;
	const AiOption pick = AiPlanner::chain_follow(*w, *me, *foe, kit, _planner_params(), rng);
	decided_[key] = pick.valid ? pick.label : std::string("none");
	if (!pick.valid) return false;
	debug_state = pick.label;
	if (pick.element != me->element || me->sub_of(pick.element) != pick.sub) {
		intent.element_select = pick.element;
		intent.sub_select = pick.sub;
		pending_press_ = "attack";
		pending_gesture_ = pick.gesture;
		hold_ = "attack";
		hold_until_ = t_ + Sim::DT + 0.05;
		return true;
	}
	_press("attack");
	intent.attack_gesture = pick.gesture;
	hold_ = "attack";
	hold_until_ = t_ + 0.05;
	return true;
}

bool AiBrain::_interrupt(ActorState* foe) {
	if (!dbool(prm, "punish", false) || foe == nullptr || foe->action == nullptr || me->action != nullptr) return false;
	const ActionInst& fa = *foe->action;
	if (fa.phase != ActionPhase::Charge && !(fa.phase == ActionPhase::Channel && fa.id != "guard")) return false;
	const std::string key = "int" + itos(fa.attack_id);
	if (!_perceived(key) || decided_.count(key)) return false;
	decided_[key] = "skip";
	if (rng.randf() >= cfg_num("counter")) return false;
	std::vector<AiOption> offs = AiPlanner::offense(*w, *me, *foe, kit, _planner_params(), rng);
	for (AiOption& o : offs) {
		if (o.has_reason("disrupt") || o.has_reason("interrupt")) {
			// Disrupt needs P >= 6 + 4 x the charge's tier: the lowest tier that clears it, else a quick T0 hit.
			const Dict def = Moves::defs().get(o.id).as_dict();
			const double dist = me->pos.distance_to(foe->pos);
			if (o.has_reason("disrupt")) {
				for (int t = o.tier; t <= Charge::max_tier(def); ++t) {
					const int grow = AiPlanner::hold_for(def, t) > 0.3 ? 1 : 0;
					if (Charge::counter_power(def, t) >= Interactions::disrupt_threshold(fa.tier() + grow) && AiPlanner::reach_of(def, t) + 0.3 >= dist) {
						o.tier = t;
						break;
					}
				}
			}
			if (o.press == "attack") o.hold = AiPlanner::hold_for(def, o.tier);
			decided_[key] = o.label;
			debug_state = "interrupt " + o.label;
			_adopt(o);
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------ drills

std::vector<AiPlanner::KitMove> AiBrain::_drill_moves(int e, int s) {
	std::vector<AiPlanner::KitMove> out;
	AiKit k = {{e, {s}}};
	for (const AiPlanner::KitMove& c : AiPlanner::kit_moves(k, AiPlanner::offense_slots())) {
		const Dict def = Moves::defs().get(c.id).as_dict();
		const std::string role = dstr(ddict(def, "ai"), "role", "");
		if (c.slot == "tech" &&
		    (c.id == "air_tech" || c.id == "water_tech" || c.id == "fire_tech" || c.id == "earth_tech" || role == "mobility" || role == "counter"))
			continue;
		if (def.has("threat") || role == "poke" || role == "zone" || role == "finisher" || role == "setup") out.push_back(c);
	}
	return out;
}

void AiBrain::_drill_element(ActorState& foe, int e, int s) {
	const auto moves = _drill_moves(e, s);
	if (moves.empty()) return;
	const AiPlanner::KitMove& c = moves[static_cast<size_t>(rng.randi_range(0, static_cast<int>(moves.size()) - 1))];
	_fire_drill_move(c, foe);
}

void AiBrain::_fire_drill_move(const AiPlanner::KitMove& c, ActorState& foe) {
	(void)foe;
	const Dict def = Moves::defs().get(c.id).as_dict();
	const int tier = rng.randf() < 0.5f ? rng.randi_range(0, mini(2, Charge::max_tier(def))) : 0;
	int g = 0;
	if (c.slot == "thrust") g = static_cast<int>(Gesture::Up);
	else if (c.slot == "ground") g = static_cast<int>(Gesture::Down);
	else if (c.slot == "sweep") g = static_cast<int>(Gesture::Side);
	double hold = AiPlanner::hold_for(def, tier);
	if (c.slot == "tech" && hold <= 0.0) hold = maxf(0.35, dnum(def, "startup", 0.2) + 0.1);
	AiOption o;
	o.valid = true;
	o.id = c.id;
	o.element = c.element;
	o.sub = c.sub;
	o.slot = c.slot;
	o.press = c.slot == "tech" ? "tech" : "attack";
	o.gesture = g;
	o.tier = tier;
	o.hold = hold;
	o.label = "drill " + c.id + " T" + itos(tier);
	debug_state = o.label;
	_adopt(o);
}

void AiBrain::_build_matrix() {
	std::vector<std::pair<std::string, std::vector<AiPlanner::KitMove>>> by;
	for (int e = 0; e < 4; ++e) {
		for (int s = 0; s < 4; ++s) {
			for (const AiPlanner::KitMove& c : _drill_moves(e, s)) {
				const std::string cls = dstr(ddict(Moves::defs().get(c.id).as_dict(), "threat"), "cls", "");
				if (cls.empty()) continue;
				auto it = std::find_if(by.begin(), by.end(), [&](const auto& p) { return p.first == cls; });
				if (it == by.end()) {
					by.push_back({cls, {}});
					it = by.end() - 1;
				}
				bool dup = false;
				for (const auto& x : it->second)
					if (x.id == c.id) dup = true;
				if (!dup) it->second.push_back(c);
			}
		}
	}
	std::sort(by.begin(), by.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	matrix_ = by;
}

std::string AiBrain::matrix_next() const {
	if (matrix_.empty()) return "";
	return matrix_[static_cast<size_t>(drill_i_) % matrix_.size()].first;
}

void AiBrain::_drill_matrix(ActorState& foe) {
	if (matrix_.empty()) _build_matrix();
	if (matrix_.empty()) return;
	const auto& row = matrix_[static_cast<size_t>(drill_i_) % matrix_.size()];
	drill_i_ += 1;
	const auto& moves = row.second;
	const AiPlanner::KitMove c = moves[static_cast<size_t>(rng.randi_range(0, static_cast<int>(moves.size()) - 1))];
	_fire_drill_move(c, foe);
	debug_state = "matrix " + row.first + ": " + c.id;
}

}  // namespace ff
