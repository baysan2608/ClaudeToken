// Fourfold core - port of game/actors/ai_brain.gd (think + the legacy rules; planner mode in AiBrainPlanner.cpp).
#include "AI/AiBrain.h"

#include "Combat/Moves.h"
#include "Sim/CombatWorld.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

AiBrain::AiBrain(CombatWorld& world, ActorState& actor, const Dict& config, uint64_t seed_value) : w(&world), me(&actor) {
	cfg = D({{"aggression", 0.55}, {"counter", 0.75}, {"reaction", 0.28}, {"elements", A({0, 2})}, {"drill", ""}, {"interval", 2.6}});
	cfg.merge(config.duplicate(true), true);
	rng.set_seed(seed_value);
	next_attack_ = 1.5 + rng.randf();
}

bool AiBrain::cfg_has_element(int e) const { return arr_has_int(cfg.get("elements").as_array(), e); }

const ActorIntent& AiBrain::think(double dt) {
	t_ += dt;
	intent.clear();
	ActorState* foe = w->get_actor(me->lock_target);
	if (me->health <= 0.0) return intent;
	if (me->stun > 0.0) {
		hold_.clear();
		pending_press_.clear();
		if (planner) {
			plan_ = AiPlan();
			guard_at_ = -1.0;
		}
		debug_state = "stunned";
		return intent;
	}
	_continue_holds(foe);
	if (planner && aim_hold_ != Vec3()) {
		if (hold_ == "attack" || (me->action != nullptr && me->action->phase != ActionPhase::Recovery)) {
			intent.aim_dir = aim_hold_;
			intent.aim_active = true;
		} else {
			aim_hold_ = Vec3();
		}
	}
	if (planner && plan_.active) _exec_plan(foe);
	if (!pending_press_.empty()) {
		_press(pending_press_);
		if (pending_gesture_ != 0) {
			intent.attack_gesture = pending_gesture_;
			pending_gesture_ = 0;
		}
		pending_press_.clear();
		return intent;
	}
	if (guard_at_ >= 0.0 || !hold_.empty() || await_draw_ >= 0 || plan_.active) {
		if (_react(foe, true)) return intent;   // keep perceiving while busy; decide once free
	}
	if (guard_at_ >= 0.0) {
		if (t_ >= guard_at_) {
			_press("guard");
			hold_ = "guard";
			hold_until_ = t_ + 0.45;
			guard_at_ = -1.0;
		}
		_move_tactical(foe, 0.0);
		return intent;
	}
	if (!hold_.empty()) {
		// No walking while drawing: approaching the foe (and so their wave) can make the draw arrive too late.
		_move_tactical(foe, (hold_ == "tech" && hold_draw_) ? 0.0 : 0.3);
		return intent;
	}
	if (plan_.active) {
		_move_tactical(foe, plan_.stage == "tech" ? 0.0 : 0.3);
		return intent;
	}
	if (await_draw_ >= 0) {
		MatBody* wb = w->get_body(await_draw_);
		if (wb == nullptr || !wb->alive || wb->form != Form::Wave) {
			await_draw_ = -1;
		} else if (me->pos.distance_to(wb->pos) <= dnum(Moves::defs().get("fire_tech").as_dict(), "draw_range") - 0.5) {
			await_draw_ = -1;
			_start_draw(*wb);
			return intent;
		} else {
			if (me->element != Sim::FIRE) intent.element_select = Sim::FIRE;
			if (planner && me->sub_of(Sim::FIRE) != 0) intent.sub_select = 0;
			return intent;
		}
	}
	if (_react(foe)) return intent;
	if (foe == nullptr) return intent;
	if (cfg_drill() == "passive") {
		debug_state = "passive";
		return intent;
	}
	if (_opportunities(*foe)) return intent;
	if (planner) {
		if (_chain(foe) || _interrupt(foe)) return intent;
		_offense_planner(*foe);
	} else {
		_offense(*foe);
	}
	_move_tactical(foe, 1.0);
	return intent;
}

bool AiBrain::_react(ActorState* foe, bool stamp_only) {
	if (planner) return _react_planner(foe, stamp_only);
	return _react_to_threats(foe, stamp_only);
}

// ------------------------------------------------------------------ holds

void AiBrain::_continue_holds(ActorState* foe) {
	if (hold_ == "guard") {
		intent.guard_held = true;
		// The strike we guard against is still visibly winding up: keep the guard up until it is released.
		if (guard_attack_ != 0 && foe != nullptr && foe->action != nullptr && foe->action->attack_id == guard_attack_ &&
		    (foe->action->phase == ActionPhase::Startup || foe->action->phase == ActionPhase::Charge))
			hold_until_ = maxf(hold_until_, t_ + 0.15);
		if (t_ >= hold_until_) {
			hold_.clear();
			guard_attack_ = 0;
			intent.guard_held = false;
		}
	} else if (hold_ == "tech") {
		intent.tech_held = true;
		MatBody* b = w->get_body(hold_body_);
		ActionInst* act = me->action.get();
		if (!hold_started_ && act != nullptr && act != hold_from_.get() && (act->id == "fire_tech" || act->id == "earth_tech")) hold_started_ = true;
		bool done = t_ >= hold_until_;
		if (!hold_started_) {
			// Pressed while busy: keep pressing until the technique starts (give up after 1 s or once the target is gone).
			done = done || t_ - hold_start_ > 1.0 || b == nullptr || !b->alive;
			intent.tech_pressed = !done;
		} else if (act == nullptr) {
			done = true;
		} else if (act->id == "fire_tech" && dstr(act->data, "mode", "") == "DRAW") {
			// Keep drawing until the lava has set (or we can't anymore).
			if (b == nullptr || !b->alive || (b->phase == Phase::Solid && b->temp < 700.0) || me->heat_reserve >= Sim::RESERVE_MAX - 5.0 || me->focus < 2.0)
				done = true;
		} else if (act->id == "earth_tech") {
			done = done || (w->held(*me) != nullptr && t_ >= hold_until_ - 0.0);
		}
		if (done) {
			hold_.clear();
			intent.tech_held = false;
			intent.tech_released = true;
		}
	} else if (hold_ == "attack") {
		intent.attack_held = true;
		if (t_ >= hold_until_) {
			hold_.clear();
			intent.attack_held = false;
			intent.attack_released = true;
		}
	}
}

void AiBrain::_press(const std::string& what) {
	hold_start_ = t_;
	if (what == "attack") {
		intent.attack_pressed = true;
		intent.attack_held = true;
		intent.attack_released = false;
	} else if (what == "guard") {
		intent.guard_pressed = true;
		intent.guard_held = true;
	} else if (what == "tech") {
		intent.tech_pressed = true;
		intent.tech_held = true;
	} else if (what == "evade") {
		intent.evade_pressed = true;
	}
}

bool AiBrain::_switch_then(int element, const std::string& what) {
	// Switch element (if allowed) and press on the next tick. Returns false if unavailable.
	if (!cfg_has_element(element) || element < 0 || element > 3 || !me->elements[static_cast<size_t>(element)]) return false;
	if (me->element != element) {
		intent.element_select = element;
		pending_press_ = what;
	} else {
		_press(what);
	}
	return true;
}

// ------------------------------------------------------------------ threats

bool AiBrain::_react_to_threats(ActorState* foe, bool stamp_only) {
	// The foe's pour wind-up is the wave's telegraph (the wave is the same body).
	if (foe != nullptr && foe->action != nullptr && foe->action->id == "pour" && foe->held_body >= 0) {
		const std::string pk = "pour" + itos(foe->action->attack_id);
		_perceived(pk);
		pour_body_ = foe->held_body;
		pour_seen_ = seen_[pk];
	}
	// Incoming material attacks.
	for (size_t i = 0; i < w->bodies.size(); ++i) {
		MatBody& b = *w->bodies[i];
		if (!b.alive || b.attack_id == 0 || b.attack_owner == me->id || b.controller >= 0) continue;
		const std::string key = "b" + itos(b.id) + ":" + itos(b.attack_id);
		if (b.form == Form::Wave) {
			if (b.id == pour_body_) {
				pour_body_ = -1;
				if (!seen_.count(key)) seen_[key] = pour_seen_;   // seen coming since the pour began
			}
			Vec3 to = me->pos - b.pos;
			to.y = 0.0f;
			if (to.length() > 16.0f || to.normalized().dot(b.wave_dir) < 0.5f) continue;
			if (!_perceived(key) || stamp_only) continue;
			if (decided_.count(key)) continue;
			decided_[key] = _choose_wave_response(b, to.length());
			return _act_on(decided_[key], b);
		} else if (b.is_projectile()) {
			const Vec3 rel = me->chest() - b.pos;
			const double closing = b.vel.dot(rel.normalized());
			if (closing < 2.0 || rel.length() > 18.0f) continue;
			// Will it pass close to me?
			const double tti = rel.length() / closing;
			Vec3 miss = b.pos + b.vel * tti - me->chest();
			miss.y *= 0.5f;
			if (miss.length() > 1.6f) continue;
			if (!_perceived(key) || stamp_only) continue;
			if (decided_.count(key)) continue;
			decided_[key] = _choose_projectile_response(b, tti);
			return _act_on(decided_[key], b, tti);
		}
	}
	// Visible lightning charge from the foe.
	if (foe != nullptr && foe->action != nullptr && foe->action->id == "fire_attack" && foe->action->phase == ActionPhase::Charge && foe->has("lightning")) {
		const std::string key = "bolt" + itos(foe->action->attack_id);
		if (_perceived(key) && !stamp_only && !decided_.count(key)) {
			decided_[key] = "bolt";
			if (me->surface != "stone") {
				intent.move = me->pos.x > 0.0f ? Vec3(-1, 0, 0) : Vec3(1, 0, 0);
				_press("evade");
			} else if (me->element == Sim::EARTH || _switch_then(Sim::EARTH, "guard")) {
				if (me->element == Sim::EARTH) _press("guard");
				hold_ = "guard";
				hold_until_ = t_ + 0.9;
				guard_attack_ = foe->action->attack_id;
			}
			return true;
		}
	}
	// Close-range strikes still winding up, within their reach.
	if (foe != nullptr && foe->action != nullptr) {
		const std::string mid = foe->action->id;
		const bool charging = foe->action->phase == ActionPhase::Charge;
		const bool winding = foe->action->phase == ActionPhase::Startup || (charging && !(mid == "fire_attack" && foe->has("lightning")));
		const double reach = dnum(foe->action->def, charging ? "heavy_range" : "range", 4.5) + 0.5;
		if (winding && (mid == "fire_attack" || mid == "air_attack" || mid == "water_attack") && foe->pos.distance_to(me->pos) < reach) {
			const std::string key = "m" + itos(foe->action->attack_id);
			if (_perceived(key) && !stamp_only && !decided_.count(key)) {
				decided_[key] = "melee";
				if (rng.randf() < cfg_num("counter")) {
					_press("guard");
					hold_ = "guard";
					hold_until_ = t_ + 0.4;
					guard_attack_ = foe->action->attack_id;
				} else {
					intent.move = (me->pos - foe->pos).normalized();
					_press("evade");
				}
				return true;
			}
		}
	}
	// Forget threats perceived long ago.
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
	return false;
}

std::string AiBrain::_choose_wave_response(MatBody& b, double dist) {
	const bool counter = rng.randf() < cfg_num("counter");
	if (counter && me->has("heat_draw") && cfg_has_element(Sim::FIRE) && me->focus >= 10.0 && me->heat_reserve < Sim::RESERVE_MAX - 60.0) {
		if (w->los(me->chest(), b.pos + V3(0, 0.3, 0)) && _draw_sets_in_time(b)) return "draw";
	}
	if (counter && cfg_has_element(Sim::EARTH) && me->focus >= 10.0 && dist > 3.0) return "wall";
	return "dodge";
}

bool AiBrain::_draw_sets_in_time(MatBody& b) {
	// Can a heat draw decided now stop this wave short of us? Tick-by-tick estimate with the sim's own rules.
	const Dict fd = Moves::defs().get("fire_tech").as_dict();
	MatBody c;
	c.mat = b.mat;
	c.mass = b.mass;
	c.temp = b.temp;
	c.liquid = b.liquid;
	const double reach = b.wave_width * 0.5 + Sim::ACTOR_RADIUS + 0.1;
	// Measured from where I come to rest (momentum carries me v^2 / 2a further).
	const Vec3 hv(me->vel.x, 0, me->vel.z);
	const Vec3 rest = me->pos + hv * (hv.length() / (2.0 * CombatWorld::DECEL));
	double fl = Vec2(rest.x - b.pos.x, rest.z - b.pos.z).length();
	const double dy = me->chest().y - b.pos.y;
	double budget = b.wave_budget;
	double cap = minf(Sim::RESERVE_MAX - me->heat_reserve, me->focus * Sim::DRAW_HU_PER_FOCUS);
	const int free_t = static_cast<int>(std::round(_busy_time() / Sim::DT)) + (me->element == Sim::FIRE ? 0 : 1);
	int first = -1;   // tick of the first draw
	const double wave_speed = dnum(Moves::defs().get("pour").as_dict(), "wave_speed");
	for (int i = 0; i < 4 * Sim::HZ; ++i) {
		if (first < 0 && fl <= dnum(fd, "draw_range") - 0.5) first = maxi(i, free_t) + static_cast<int>(std::round(dnum(fd, "draw_startup") / Sim::DT));
		if (first >= 0 && i >= first && cap > 0.0) {
			const double d = std::sqrt(fl * fl + dy * dy);
			const double rate = dnum(fd, "draw_rate") * (1.0 - 0.5 * clampf((d - 3.0) / (dnum(fd, "draw_range") - 3.0), 0.0, 1.0));
			cap += Thermal::heat(c, -minf(rate * Sim::DT, cap));
		}
		Thermal::ambient_step(c, Sim::DT);
		const double speed = wave_speed * Thermal::flow_factor(c);
		if (speed < 0.35 || budget <= 0.0) return true;   // set (or spent) short of us
		fl -= speed * Sim::DT;
		budget -= speed * Sim::DT;
		if (fl < reach) return false;
	}
	return true;
}

double AiBrain::_busy_time() const {
	const ActionInst* a = me->action.get();
	if (a == nullptr || a->id == "guard" || a->phase == ActionPhase::Charge || a->phase == ActionPhase::Channel) return 0.0;
	double left = dnum(a->def, "recovery", 0.0);
	if (a->phase == ActionPhase::Recovery) return maxf(0.0, left - a->t);
	left += dnum(a->data, "active", dnum(a->def, "active", 0.0));
	if (a->phase == ActionPhase::Active) return maxf(0.0, left - a->t);
	return left + maxf(0.0, dnum(a->data, "startup", dnum(a->def, "startup", 0.0)) - a->total);
}

std::string AiBrain::_choose_projectile_response(MatBody& b, double tti) {
	const double r = rng.randf();
	const double counter = cfg_num("counter");
	if (b.is_stone() && b.mass <= me->max_control_mass && cfg_has_element(Sim::EARTH) && r < counter * 0.55 && tti > 0.18) return "redirect";
	if (r < counter * 0.85) return "block";
	return "dodge";
}

bool AiBrain::_act_on(const std::string& decision, MatBody& b, double tti) {
	debug_state = decision;
	if (decision == "draw") {
		if (me->pos.distance_to(b.pos) > dnum(Moves::defs().get("fire_tech").as_dict(), "draw_range") - 0.5) {
			await_draw_ = b.id;   // wait for range (switching to Fire meanwhile)
			if (me->element != Sim::FIRE) intent.element_select = Sim::FIRE;
			return true;
		}
		_start_draw(b);
		return true;
	}
	if (decision == "wall") {
		_switch_then(Sim::EARTH, "guard");
		hold_ = "guard";
		hold_until_ = t_ + 1.6;
		return true;
	}
	if (decision == "redirect") {
		if (me->element != Sim::EARTH) intent.element_select = Sim::EARTH;
		// Time the guard press so the stone meets the rising wall inside the perfect window.
		const double wall_lead = 1.25 / maxf(b.vel.length(), 1.0);
		if (planner) {
			const double e = dnum(prm, "timing_err", 0.05);
			guard_at_ = t_ + maxf(0.0, tti - wall_lead - 0.09 + rng.randf_range(static_cast<float>(-e), static_cast<float>(e)));
			if (me->sub_of(Sim::EARTH) != 0) intent.sub_select = 0;
		} else {
			guard_at_ = t_ + maxf(0.0, tti - wall_lead - 0.09 - rng.randf_range(0.0f, 0.06f));
		}
		return true;
	}
	if (decision == "block") {
		_press("guard");
		hold_ = "guard";
		hold_until_ = t_ + tti + 0.25;
		return true;
	}
	if (decision == "dodge") {
		Vec3 side = me->forward().cross(Vec3::Up());
		if (rng.randf() < 0.5f) side = -side;
		intent.move = side;
		_press("evade");
		return true;
	}
	return false;
}

void AiBrain::_start_draw(MatBody& b) {
	debug_state = "draw";
	_hold_tech(b.id, 4.0);
	hold_draw_ = true;
	if (planner && me->sub_of(Sim::FIRE) != 0) {
		intent.element_select = Sim::FIRE;
		intent.sub_select = 0;
		pending_press_ = "tech";
	} else if (me->element != Sim::FIRE) {
		intent.element_select = Sim::FIRE;
		pending_press_ = "tech";
	} else {
		_press("tech");
	}
}

void AiBrain::_hold_tech(int body_id, double secs) {
	// Hold the technique button; _continue_holds re-presses until the action really starts.
	hold_ = "tech";
	hold_body_ = body_id;
	hold_start_ = t_;
	hold_until_ = t_ + secs;
	hold_started_ = false;
	hold_from_ = me->action;
	hold_draw_ = false;
}

// ------------------------------------------------------------------ opportunities / offense

bool AiBrain::_opportunities(ActorState& foe) {
	// Foe holding molten material near us: maybe draw its heat. Rolled once per hold.
	MatBody* held = w->get_body(foe.held_body);
	const ActionInst* act = foe.action.get();
	if (held != nullptr && act != nullptr && act->id != "pour" && held->is_stone() && held->liquid > 0.3 && me->has("heat_draw") && cfg_has_element(Sim::FIRE)) {
		const std::string key = "molten" + itos(held->id) + ":" + itos(act->attack_id);
		if (_perceived(key) && !decided_.count(key) && me->pos.distance_to(held->pos) < 8.5f) {
			decided_[key] = rng.randf() < cfg_num("counter") * 0.35 ? "draw" : "skip";
			if (decided_[key] == "draw") return _act_on("draw", *held);
		}
	}
	return false;
}

void AiBrain::_offense(ActorState& foe) {
	const std::string drill = cfg_drill();
	const double dist = me->pos.distance_to(foe.pos);
	if (t_ < next_attack_ || me->action != nullptr) return;
	if (!w->arena.has_los(me->chest(), foe.chest())) {
		// Behind cover: don't throw into it (_move_tactical steps out).
		next_attack_ = t_ + 0.3;
		return;
	}
	const double interval = !drill.empty() ? cfg_num("interval") : lerpf(3.2, 1.1, cfg_num("aggression"));
	next_attack_ = t_ + interval * rng.randf_range(0.8f, 1.25f);
	if (drill == "stone_rain") {
		_switch_then(Sim::EARTH, "attack");
		hold_ = "attack";
		hold_until_ = t_ + (rng.randf() < 0.25f ? 0.6 : 0.05);
		return;
	}
	if (drill == "seize") {
		MatBody* loose = _nearest_loose(9.0);
		if (loose != nullptr) {
			debug_state = "seize";
			if (_switch_then(Sim::EARTH, "tech")) _hold_tech(loose->id, 0.9);
		}
		return;
	}
	if (drill == "lightning" && me->has("lightning")) {
		_switch_then(Sim::FIRE, "attack");
		hold_ = "attack";
		hold_until_ = t_ + 0.85;
		return;
	}
	// Free sparring: pick by range and resources.
	MatBody* hot = _nearby_rock();
	if (hot != nullptr && cfg_has_element(Sim::EARTH) && rng.randf() < 0.6f) {
		debug_state = "seize rock";
		if (_switch_then(Sim::EARTH, "tech")) _hold_tech(hot->id, 0.75);
		return;
	}
	if (dist < 4.2 && cfg_has_element(Sim::FIRE) && me->focus > 8.0) {
		debug_state = "flare";
		_switch_then(Sim::FIRE, "attack");
		hold_ = "attack";
		hold_until_ = t_ + (rng.randf() < 0.3f ? 0.5 : 0.05);
		return;
	}
	if (cfg_has_element(Sim::EARTH) && me->focus > 10.0) {
		debug_state = "stone";
		_switch_then(Sim::EARTH, "attack");
		hold_ = "attack";
		hold_until_ = t_ + (rng.randf() < 0.25 * cfg_num("aggression") + 0.1 ? 0.65 : 0.05);
		return;
	}
	if (cfg_has_element(Sim::WATER) && me->water_carried > 2.0) {
		_switch_then(Sim::WATER, "attack");
		hold_ = "attack";
		hold_until_ = t_ + 0.05;
	} else if (cfg_has_element(Sim::AIR)) {
		_switch_then(Sim::AIR, "attack");
		hold_ = "attack";
		hold_until_ = t_ + 0.05;
	}
}

MatBody* AiBrain::_nearest_loose(double r) {
	MatBody* best = nullptr;
	for (size_t i = 0; i < w->bodies.size(); ++i) {
		MatBody& b = *w->bodies[i];
		if (b.alive && b.is_stone() && b.controller != me->id && b.attack_id == 0 && b.form == Form::Chunk && b.mass <= me->max_control_mass) {
			const double d = me->pos.distance_to(b.pos);
			if (d < r && (best == nullptr || d < me->pos.distance_to(best->pos))) best = &b;
		}
	}
	return best;
}

MatBody* AiBrain::_nearby_rock() {
	for (size_t i = 0; i < w->bodies.size(); ++i) {
		MatBody& b = *w->bodies[i];
		if (b.alive && b.is_stone() && b.controller < 0 && b.attack_id == 0 && b.on_ground && b.phase == Phase::Solid && b.form == Form::Chunk &&
		    b.mass <= me->max_control_mass && me->pos.distance_to(b.pos) < 6.5f) {
			Vec3 to = b.pos - me->pos;
			to.y = 0.0f;
			if (to.normalized().dot(me->forward()) > 0.3f) return &b;
		}
	}
	return nullptr;
}

void AiBrain::_move_tactical(ActorState* foe, double amount) {
	if (foe == nullptr || amount <= 0.0) return;
	Vec3 to = foe->pos - me->pos;
	to.y = 0.0f;
	const double dist = to.length();
	if (dist < 0.01) return;
	const Vec3 dir = to / static_cast<float>(dist);
	Vec3 side = dir.cross(Vec3::Up()) * strafe_;
	// Cover between us (arena geometry, not guard walls): side-step toward whichever side regains sight.
	const bool blind = !w->arena.has_los(me->chest(), foe->chest());
	if (blind) {
		const Vec3 probe = dir.cross(Vec3::Up()) * 2.0f;
		const bool pos_ok = w->arena.has_los(me->chest() + probe, foe->chest());
		if (pos_ok != w->arena.has_los(me->chest() - probe, foe->chest())) strafe_ = pos_ok ? 1.0 : -1.0;
		side = dir.cross(Vec3::Up()) * strafe_;
	}
	strafe_t_ -= Sim::DT;
	if (strafe_t_ <= 0.0 && !blind) {
		strafe_t_ = rng.randf_range(1.2f, 2.8f);
		if (rng.randf() < 0.5f) strafe_ = -strafe_;
	}
	Vec3 want;
	double lo = 6.5 - 2.0 * cfg_num("aggression");
	double hi = 11.0 - 2.0 * cfg_num("aggression");
	if (cfg.has("range_lo")) {
		lo = cfg_num("range_lo");
		hi = cfg_num("range_hi");
	}
	if (dist > hi) {
		want = dir;
	} else if (dist < lo) {
		want = -dir;
	} else if (!blind && _wet(side, 2.0) && !_wet(-side, 2.0)) {
		// Holding range but strafing into the pool: strafe the other way round.
		strafe_ = -strafe_;
		strafe_t_ = maxf(strafe_t_, 1.2);
		side = -side;
	}
	want = blind ? side : want + side * 0.45f;
	if (planner && _needs_water() && dist > 4.0) {
		// A water kit with an empty waterskin walks to the pool to refill.
		const ArenaMap& a = w->arena;
		const Vec3 tgt = V3(clampf(me->pos.x, a.pool_min.x + 0.6, a.pool_max.x - 0.6), me->pos.y, clampf(me->pos.z, a.pool_min.y + 0.6, a.pool_max.y - 0.6));
		Vec3 to_pool = tgt - me->pos;
		to_pool.y = 0.0f;
		if (to_pool.length() > 0.2f) {
			debug_state = "refill water";
			intent.move = to_pool.normalized() * (amount * 0.9);
			return;
		}
	}
	// Don't wander into the pool unless chasing: steer round it.
	want = _around_pool(want);
	intent.move = want.limit_length(1.0f) * (amount * (dist < hi ? 0.55 : 0.9));
}

bool AiBrain::_needs_water() const {
	if (!ai_kit_has(kit, Sim::WATER) || me->water_carried >= 1.5 || me->in_water) return false;
	for (const auto& kv : kit) {
		if (kv.first != Sim::WATER) {
			// Another element to fight with: only a short detour, while fighting as Water and the pool is close.
			return me->element == Sim::WATER && _pool_distance() < 5.0;
		}
	}
	return true;
}

double AiBrain::_pool_distance() const {
	const ArenaMap& a = w->arena;
	const double dx = maxf(maxf(a.pool_min.x - me->pos.x, me->pos.x - a.pool_max.x), 0.0);
	const double dz = maxf(maxf(a.pool_min.y - me->pos.z, me->pos.z - a.pool_max.y), 0.0);
	return V2(dx, dz).length();
}

Vec3 AiBrain::_around_pool(Vec3 want) {
	// Turns `want` away from the pool by the smallest angle that keeps the path ahead dry.
	const double reach = detour_t_ > 0.0 ? 2.0 : 1.5;
	detour_t_ -= Sim::DT;
	if (want.length() < 0.01f || !_wet(want, reach)) return want;
	detour_t_ = 1.0;
	int k = _turn_steps(want, detour_, reach);
	const int k_other = _turn_steps(want, -detour_, reach);
	if (k_other + 2 <= k) {
		detour_ = -detour_;
		k = k_other;
	}
	if (k > 12) return want;   // deep in the pool already: just keep going
	return rotated(want, Vec3::Up(), detour_ * k * kPi / 12.0);
}

bool AiBrain::_wet(Vec3 v, double reach) const {
	const ArenaMap& a = w->arena;
	const Vec3 n = v.normalized();
	const int steps = static_cast<int>(reach / 0.25);
	for (int k = 1; k <= steps; ++k) {
		const Vec3 p = me->pos + n * (0.25 * k);
		if (p.x > a.pool_min.x - 0.4f && p.x < a.pool_max.x + 0.4f && p.z > a.pool_min.y - 0.4f && p.z < a.pool_max.y + 0.4f) return true;
	}
	return false;
}

int AiBrain::_turn_steps(Vec3 v, double sense, double reach) const {
	for (int k = 1; k < 13; ++k)
		if (!_wet(rotated(v, Vec3::Up(), sense * k * kPi / 12.0), reach)) return k;
	return 13;
}

}  // namespace ff
