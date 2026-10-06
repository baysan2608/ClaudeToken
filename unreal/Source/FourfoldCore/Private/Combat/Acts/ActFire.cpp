// Fourfold core - port of game/combat/act_fire.gd: Fire flare jab (tap) / blaze (hold) / lightning (long hold, if
// learned), thermal technique (HEAT a stone into lava, DRAW heat out of lava / hot rock, SCORCH walls / heavy bodies,
// VENT the reserve), pour (send held lava along the ground). The thermal mode is chosen ONCE at technique press.
// Charged strike: T2 Fire Column, T3 Inferno (both leave a fire field).
#include "Combat/Acts.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Agent.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/FxEvents.h"
#include "Sim/Interactions.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>
#include <set>

namespace ff {
namespace ActFire {

namespace {
Dict af_fire_tech_def() { return Moves::defs().get("fire_tech").as_dict(); }
Dict af_preview(const char* mode, int body, bool ok, const char* reason) {
	return D({{"mode", mode}, {"body", body}, {"ok", ok}, {"reason", reason}});
}
}  // namespace

Dict preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	// What the fire technique would do right now: {mode, body, ok, reason}.
	const Dict d = af_fire_tech_def();
	const ActorState* ap = &a;
	// Legality through the engine: draw_heat / heat_grip cells (legacy: hot stone / any stone / water).
	MatBody* hot = w.find_body(a, dir, dnum(d, "draw_range"), 50.0, [ap](MatBody& b) {
		return b.controller != ap->id && b.form != Form::Wall && Interactions::allows(b, "draw_heat");
	});
	if (hot != nullptr) {
		if (!a.has("heat_draw")) return af_preview("DRAW", hot->id, false, "technique");
		if (!w.los(a.chest(), hot->pos + V3(0, 0.3, 0))) return af_preview("DRAW", hot->id, false, "sight");
		return af_preview("DRAW", hot->id, true, "");
	}
	MatBody* stone = w.find_body(a, dir, INCOMING_RANGE, 40.0, [ap](MatBody& b) {
		return !b.is_water() && b.is_projectile() && b.attack_owner != ap->id && b.vel.dot(ap->chest() - b.pos) > 0.0f &&
		       Interactions::allows(b, "heat_grip");
	});
	if (stone == nullptr) {
		stone = w.find_body(a, dir, dnum(d, "draw_range"), 55.0, [ap](MatBody& b) {
			return !b.is_water() && b.form != Form::Wall && b.controller != ap->id && b.phase != Phase::Molten &&
			       Interactions::allows(b, "heat_grip");
		});
	}
	if (stone != nullptr) {
		if (!a.has("magma")) return af_preview("HEAT", stone->id, false, "technique");
		// Too heavy is still attempted (the grip strains and fails); the HUD warns first.
		return af_preview("HEAT", stone->id, true, stone->mass > a.max_control_mass ? "mass" : "");
	}
	MatBody* wet = w.find_body(a, dir, dnum(d, "reach"), 45.0, [ap](MatBody& b) {
		return b.is_water() && b.form != Form::Pool && b.controller != ap->id && (Interactions::allows(b, "heat_grip") || b.form == Form::Cloud);
	});
	if (wet != nullptr) return af_preview("HEAT", wet->id, true, "");
	MatBody* sc = scorch_target(w, a, dir);
	if (sc != nullptr) return af_preview("SCORCH", sc->id, true, "");
	if (a.heat_reserve >= VENT_MIN) return af_preview("VENT", -1, true, "");
	return af_preview("", -1, false, "target");
}

// SCORCH target: a raised wall within 6 m in the aim (nearest), else a loose body the magma grip can't take.
MatBody* scorch_target(CombatWorld& w, ActorState& a, Vec3 dir) {
	const Dict sd = ddict(af_fire_tech_def(), "scorch");
	if (sd.empty()) return nullptr;
	const double rng_m = dnum(sd, "range", 6.0);
	const double cone = std::cos(deg_to_rad(dnum(sd, "cone", 40.0)));
	MatBody* best = nullptr;
	double bd = kInf;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.form != Form::Wall || b.wall_rise < 0.3) continue;
		Vec3 to = b.pos - a.pos;
		to.y = 0.0f;
		const double dd = to.length();
		if (dd > rng_m + b.wall_half.x || (dd > 0.5 && to.normalized().dot(dir) < cone)) continue;
		if (dd < bd || (is_equal_approx(dd, bd) && best != nullptr && b.id < best->id)) {
			best = &b;
			bd = dd;
		}
	}
	if (best != nullptr) return best;
	const ActorState* ap = &a;
	return w.find_body(a, dir, rng_m, dnum(sd, "cone", 40.0), [ap](MatBody& b) {
		return b.controller != ap->id && b.form != Form::Pool && b.form != Form::Zone && b.form != Form::Puddle && b.form != Form::Wave &&
		       b.mass >= 0.5 && !b.static_body && b.mat != Mat::Fire && b.mat != Mat::Air && b.mat != Mat::Steam &&
		       Interactions::allows(b, "heat_ranged") && (!Interactions::allows(b, "heat_grip") || b.mass > ap->max_control_mass);
	});
}

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	if (inst.id == "fire_attack") {
		if (!w.can_pay_heat(a, dnum(inst.def, "cost_hu"))) {
			inst.data.set("fizzle", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		}
	} else if (inst.id == "fire_tech") {
		MatBody* held_b = w.held(a);
		if (held_b != nullptr && held_b->is_stone()) {
			inst.data.set("mode", "HEAT");
			inst.data.set("target", held_b->id);
		} else {
			const Dict pv = preview(w, a, w.aim_dir(a, it));
			inst.data.set("mode", pv.get("mode"));
			inst.data.set("target", pv.get("body"));
			if (!dbool(pv, "ok")) {
				inst.data.set("fizzle", true);
				w.emit("insufficient", D({{"actor", a.id}, {"what", pv.get("reason")}, {"move", "thermal"}, {"mode", pv.get("mode")}}));
			}
		}
		const std::string mode = dstr(inst.data, "mode");
		if (mode == "SCORCH") {
			inst.data.set("spec_def", inst.def.get("scorch", Dict()));
			inst.data.set("aim", w.aim_dir(a, it));
		}
		if (mode == "DRAW") {
			inst.data.set("startup", dnum(inst.def, "draw_startup"));
			w.emit("telegraph", D({{"actor", a.id}, {"move", "heat_draw"}, {"body", inst.data.get("target")}, {"time", inst.def.get("draw_startup")}}));
		}
		w.emit("thermal", D({{"actor", a.id}, {"mode", inst.data.get("mode")}, {"body", inst.data.get("target")}}));
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "fire_attack") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		return w.attack_after_startup(a, inst, it);
	}
	if (inst.id == "fire_tech") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		const std::string mode = dstr(inst.data, "mode");
		if (mode == "VENT") return ActionPhase::Active;
		if (mode == "SCORCH") {
			VerbHeat::start(w, a, inst, it);
			if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
			Verbs::fx(w, a, inst, "beam", D({{"length", 6.0}, {"body", dint(inst.data, "target", -1)}, {"dur", 1.5}}));
		}
		return ActionPhase::Channel;
	}
	return ActionPhase::Active;
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "fire_attack") {
		if (p == ActionPhase::Charge && a.has("lightning"))
			w.emit("telegraph", D({{"actor", a.id}, {"move", "lightning"}, {"time", inst.def.get("lightning_min")}}));
		if (p == ActionPhase::Active) {
			if (inst.tier() >= 2 && inst.heavy) _column(w, a, inst);
			else _flare(w, a, inst);
		}
	} else if (inst.id == "fire_tech") {
		if (p == ActionPhase::Active && dstr(inst.data, "mode") == "VENT") _vent(w, a);
		if (p == ActionPhase::Recovery && dstr(inst.data, "mode", "") == "SCORCH") VerbHeat::end(w, a, inst);
	} else if (inst.id == "pour") {
		if (p == ActionPhase::Active) _pour(w, a, inst);
	} else if (inst.id == "lightning") {
		if (p == ActionPhase::Active) {
			if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
				w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "lightning"}}));
				return;
			}
			const Vec3 aim = dvec(inst.data, "aim_point", a.chest() + a.forward() * 10.0);
			Conduction::discharge(w, a, aim, inst.def, inst.attack_id, true);
		}
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "fire_attack") {
		if (!it.attack_held) inst.data.set("released", true);
		inst.data.set("face", w.aim_dir(a, it));
		if (inst.phase == ActionPhase::Charge) {
			const bool can_bolt = a.has("lightning") && a.focus >= dnum(Moves::defs().get("lightning").as_dict(), "cost");
			if (can_bolt && inst.total >= dnum(inst.def, "lightning_min") && !dbool(inst.data, "bolt_ready", false)) {
				inst.data.set("bolt_ready", true);
				w.emit("charge_ready", D({{"actor", a.id}, {"move", "lightning"}}));
			}
			if (dbool(inst.data, "released", false) && inst.total >= dnum(inst.def, "heavy_min")) {
				if (dbool(inst.data, "bolt_ready", false)) {
					const Vec3 aimp = w.aim_point(a, it);
					const Value face = inst.data.get("face");
					a.action.reset();
					w.start_action(a, "lightning", it, D({{"aim_point", aimp}, {"face", face}}));
				} else {
					w.set_phase(a, inst, ActionPhase::Active);
				}
			}
		}
	} else if (inst.id == "fire_tech") {
		if (it.tech_cancel && (inst.phase == ActionPhase::Startup || inst.phase == ActionPhase::Channel)) {
			// Honoured from the first frame: a cancel during startup never grips, draws or vents.
			ActEarth::_drop(w, a);
			w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		if (inst.phase == ActionPhase::Startup) {
			inst.data.set("face", _face_target(w, a, inst));
			return;
		}
		if (inst.phase != ActionPhase::Channel) return;
		inst.data.set("aim", w.aim_dir(a, it));
		inst.data.set("aim_active", it.aim_active);
		const std::string mode = dstr(inst.data, "mode");
		if (mode == "HEAT") _heat_tick(w, a, inst, it);
		else if (mode == "DRAW") _draw_tick(w, a, inst, it);
		else if (mode == "SCORCH") VerbHeat::tick(w, a, inst, it);
	} else if (inst.id == "pour") {
		MatBody* b = w.held(a);
		if (b != nullptr) {
			const double k = clampf(inst.t / dnum(inst.def, "startup"), 0.0, 1.0);
			const Vec3 dir = dvec(inst.data, "aim", a.forward());
			bool blocked = false;
			const Vec3 g = _pour_start(w, a, dir, &blocked) + V3(0, 0.2, 0);
			b->hold_point = (a.pos + V3(0, 1.05, 0) + dir * (0.3 + b->radius)).lerp(g, f32(ease(k, 2.0)));
		}
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	(void)reason;
	if (inst.id == "fire_tech" && dstr(inst.data, "mode", "") == "SCORCH") {
		VerbHeat::end(w, a, inst);
		return;
	}
	if (inst.id == "fire_tech" || inst.id == "pour") {
		MatBody* b = w.held(a);
		if (b != nullptr) w.emit("conversion_interrupted", D({{"actor", a.id}, {"body", b->id}, {"liquid", b->liquid}}));
		ActEarth::_drop(w, a);
	}
}

// ---------------------------------------------------------------------------

Vec3 _face_target(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.get_body(dint(inst.data, "target", -1));
	if (b != nullptr && b->alive) {
		Vec3 d = b->pos - a.pos;
		d.y = 0.0f;
		if (d.length() > 0.2f) return d.normalized();
	}
	return a.forward();
}

void _heat_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const Dict& d = inst.def;
	MatBody* b = w.get_body(dint(inst.data, "target", -1));
	if (b == nullptr || !b->alive) {
		ActEarth::_drop(w, a);
		w.emit("whiff", D({{"actor", a.id}, {"move", "thermal"}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("face", _face_target(w, a, inst));
	if (b->is_water()) {
		_heat_water(w, a, inst, *b, it);
		return;
	}
	const bool holding = b->controller == a.id;
	if (!holding) {
		// Magma grip: reach for the stone during the grip window only (early press = whiff).
		const double dist = a.chest().distance_to(b->pos);
		if (dist <= dnum(d, "reach")) {
			if (b->mass > a.max_control_mass) {
				w.request_grip(a, *b, 0.0, "magma_grip");   // emits control_fail (mass)
				w.set_phase(a, inst, ActionPhase::Recovery);
				return;
			}
			w.request_grip(a, *b, w.grip_strength(a, *b, dnum(d, "grip"), dnum(d, "reach")), "magma_grip");
		}
		MatBody* tb = w.get_body(dint(inst.data, "target", -1));
		if (inst.t > GRIP_WINDOW && (tb == nullptr || tb->controller != a.id)) {
			w.emit("whiff", D({{"actor", a.id}, {"move", "magma_grip"}, {"body", b->id}, {"dist", dist}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		if (!it.tech_held && inst.t > 0.05) w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	// Holding: between the cupped hands (magma_hold pose) and pour heat in at a bounded rate.
	b->hold_point = a.pos + V3(0, 1.05, 0) + a.forward() * (0.3 + b->radius);
	if (b->liquid < 1.0) {
		double want = dnum(d, "heat_rate") * Sim::DT;
		// Never spend the Focus needed to keep holding molten mass (about 2 s of upkeep).
		const double keep = Sim::HOLD_UPKEEP_FOCUS * 2.0;
		const double affordable = a.heat_reserve + maxf(0.0, a.focus - keep) * Sim::HU_PER_FOCUS;
		want = minf(want, affordable);
		const double paid = want > 0.0 ? w.pay_heat(a, want) : 0.0;
		if (paid < dnum(d, "heat_rate") * Sim::DT * 0.5 && !dbool(inst.data, "starved", false)) {
			inst.data.set("starved", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "heat"}}));
		}
		const double used = w.heat_body(*b, paid);
		w.ledger.spent += paid - used;
		b->touch(a.id, "heat", w.tick);
		if (w.tick % 6 == 0) w.emit("heating", D({{"actor", a.id}, {"body", b->id}, {"liquid", b->liquid}, {"temp", b->temp}}));
	}
	if (!it.tech_held) {
		if (b->phase == Phase::Molten) {
			const Vec3 aim = dvec(inst.data, "aim");
			const int bid = b->id;
			a.action.reset();
			w.start_action(a, "pour", it, D({{"aim", aim}, {"face", aim}, {"body", bid}}));
		} else {
			// Not molten yet: throw the (hot) stone instead.
			w.set_phase(a, inst, ActionPhase::Active);
			const Vec3 tgt = ActEarth::_throw_target(w, a, inst);
			w.release_body(a, ActEarth::launch_vel(b->pos, tgt, dnum(d, "speed")), true, dnum(d, "damage"), dnum(d, "balance"));
			w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"kind", "hot_stone"}}));
		}
	}
}

void _heat_water(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b, const ActorIntent& it) {
	if (a.chest().distance_to(b.pos) > dnum(inst.def, "reach") || !it.tech_held) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	const double paid = w.pay_heat(a, dnum(inst.def, "heat_rate") * 0.5 * Sim::DT);
	if (b.phase == Phase::Frozen || b.liquid < 1.0) {
		const double used = w.heat_body(b, paid);
		w.ledger.spent += paid - used;
	} else {
		w.boil_water(b, paid, b.pos);
		if (b.mass <= 0.05) {
			w.decay_body(b, "boiled");
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
	}
}

void _draw_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const Dict& d = inst.def;
	MatBody* b = w.get_body(dint(inst.data, "target", -1));
	if (!it.tech_held || b == nullptr || !b->alive) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("face", _face_target(w, a, inst));
	const double dist = a.chest().distance_to(b->pos);
	if (dist > dnum(d, "draw_range") + 1.0) {
		w.emit("draw_break", D({{"actor", a.id}, {"body", b->id}, {"reason", "range"}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	if (!w.los(a.chest(), b->pos + V3(0, 0.3, 0))) {
		if (w.tick % 10 == 0) w.emit("draw_break", D({{"actor", a.id}, {"body", b->id}, {"reason", "sight"}}));
		return;
	}
	const double room = Sim::RESERVE_MAX - a.heat_reserve;
	if (room <= 0.5) {
		if (!dbool(inst.data, "full", false)) {
			inst.data.set("full", true);
			w.emit("reserve_full", D({{"actor", a.id}}));
		}
		return;
	}
	const double avail = maxf(0.0, b->thermal_energy());
	// Drawing is strongest up close: full rate within 3 m, half at max range.
	const double falloff = 1.0 - 0.5 * clampf((dist - 3.0) / (dnum(d, "draw_range") - 3.0), 0.0, 1.0);
	const double e = minf(dnum(d, "draw_rate") * falloff * Sim::DT, minf(room, avail));
	const double fcost = e / Sim::DRAW_HU_PER_FOCUS;
	if (a.focus < fcost) {
		if (!dbool(inst.data, "starved", false)) {
			inst.data.set("starved", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "draw"}}));
		}
		return;
	}
	w.spend_focus(a, fcost);
	const double taken = -Thermal::heat(*b, -e);
	a.heat_reserve += taken;
	b->touch(a.id, "draw", w.tick);
	if (w.tick % 6 == 0)
		w.emit("drawing", D({{"actor", a.id}, {"body", b->id}, {"liquid", b->liquid}, {"temp", b->temp}, {"reserve", a.heat_reserve}}));
}

void _pour(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b != nullptr && !b->is_stone() && Materials::is_fusible(b->mat)) {
		// Molten metal / fused sand: thrown as a molten blob (it sets as metal / glass where it lands).
		const Vec3 tgt = ActEarth::_throw_target(w, a, inst);
		w.release_body(a, ActEarth::launch_vel(b->pos, tgt, dnum(inst.def, "wave_speed") * 2.0), true, dnum(inst.def, "damage") * 0.7,
		               dnum(inst.def, "balance") * 0.6);
		w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"kind", std::string("molten_") + Sim::mat_name(b->mat)}}));
		return;
	}
	if (b == nullptr || !b->is_stone() || b->liquid <= 0.0) return;
	const Dict& d = inst.def;
	Vec3 dir = dvec(inst.data, "aim", a.forward());
	dir.y = 0.0f;
	dir = dir.normalized();
	bool blocked = false;
	const Vec3 start = _pour_start(w, a, dir, &blocked);
	w.release_body(a, Vec3(), true, dnum(d, "damage"), dnum(d, "balance"));
	const Form old_form = b->form;
	b->form = Form::Wave;
	b->pos = start;
	b->wave_dir = dir;
	b->wave_budget = dnum(d, "base_budget") + dnum(d, "budget_per_kg") * b->mass;
	b->wave_width = 1.1 + b->mass * 0.025;
	b->wave_path.assign(1, start);
	b->max_life = -1.0;
	b->age = 0.0;
	b->touch(a.id, "pour", w.tick);
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", Sim::form_name(old_form)}, {"to", "wave"}, {"why", "poured"}}));
	if (blocked) {
		// Poured point-blank into cover: the lava stays on this side and pools there.
		w.emit("wave_blocked", D({{"body", b->id}, {"at", start}}));
		w._settle_wave(*b, "blocked");
	}
}

// Where a pour along dir starts: on the ground 1.3 m ahead, or pulled back short of any wall in between.
Vec3 _pour_start(CombatWorld& w, const ActorState& a, Vec3 dir, bool* blocked) {
	Vec3 start = a.pos + dir * 1.3;
	const Vec3 from = a.pos + V3(0, 0.2, 0);
	const Vec3 to(start.x, from.y, start.z);
	double hit_t = w.arena.segment_hit(from, to, 0.1);
	const double wall_t = w.wall_hit(from, to);
	if (wall_t >= 0.0 && (hit_t < 0.0 || wall_t < hit_t)) hit_t = wall_t;
	if (hit_t >= 0.0) start = a.pos + dir * maxf(0.0, 1.3 * hit_t - 0.25);
	start.y = f32(w.arena.ground_height(start.x, start.z, a.pos.y));
	if (blocked != nullptr) *blocked = hit_t >= 0.0;
	return start;
}

void _flare(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Dict& d = inst.def;
	const bool heavy = inst.heavy;
	const double rng_m = heavy ? dnum(d, "heavy_range") : dnum(d, "range");
	const double cone = heavy ? dnum(d, "heavy_cone") : dnum(d, "cone");
	const double cost = heavy ? dnum(d, "heavy_cost_hu") : dnum(d, "cost_hu");
	const double paid = w.pay_heat(a, cost);
	const Vec3 dir = dvec(inst.data, "face");
	w.emit("flare", D({{"actor", a.id}, {"dir", dir}, {"range", rng_m}, {"heavy", heavy}, {"from_reserve", a.heat_reserve > 0.0}}));
	// The flame is a heat volume; flame.heat is the budget still to spend (HU).
	AgentRef flame = Agent::of_volume(&w, &a, &inst, "flame", a.chest(), dir, D({{"H", paid / Interactions::HU_PER_PU}, {"heat_hu", paid}}));
	FxEvents::fx_for(w, a, inst, "cone", "flame", D({{"length", rng_m}, {"angle", cone}, {"power", paid / Interactions::HU_PER_PU}}));
	// Water shields in the way take the heat first (steam), protecting their holder.
	std::set<int> shielded;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || !b.is_water() || b.form == Form::Pool) continue;
		const Vec3 to = b.pos - a.chest();
		if (to.length() > rng_m || (to.length() > 0.5f && Vec3(to.x, 0, to.z).normalized().dot(dir) < std::cos(deg_to_rad(cone + 10.0))))
			continue;
		IxCtx ctx;
		ctx.site = "flare";
		if (Interactions::is_barrier(w, b)) {
			AgentRef c = Agent::of_body(w, b);
			Interactions::resolve(w, *flame, *c, ctx);
		} else {
			AgentRef th = Agent::of_body(w, b, &a);
			Interactions::resolve(w, *th, *flame, ctx);
		}
		if (b.controller >= 0) shielded.insert(b.controller);
	}
	for (ActorState* t : w.actors_in_cone(a, dir, rng_m, cone)) {
		if (shielded.count(t->id)) {
			w.emit("block", D({{"actor", t->id}, {"attacker", a.id}, {"kind", "fire_water"}}));
			continue;
		}
		const Dict info = D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", heavy ? d.get("heavy_damage") : d.get("damage")},
		                     {"balance", heavy ? d.get("heavy_balance") : d.get("balance")}, {"knock", dir * (heavy ? 3.0 : 1.2)},
		                     {"kind", "fire"}, {"from", a.chest()}});
		if (t->guarding) {
			// Aura guards (wind wraps the fighter) answer regardless of facing.
			AgentRef g = Agent::of_guard(w, *t);
			if (dbool(Interactions::rule(flame->cls, g->ccls, g->tier), "aura", false)) {
				IxCtx ctx;
				ctx.info = info;
				ctx.info_agent = flame;
				ctx.target = t;
				Interactions::resolve(w, *flame, *g, ctx);
				continue;
			}
		}
		// A perfect fire guard keeps half the remaining heat (rule absorb_reserve, inside hit_actor).
		w.hit_actor(*t, info, flame);
	}
	// Stones in the cone warm up.
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || !b.is_stone() || b.form == Form::Wall || flame->heat <= 0.0) continue;
		const Vec3 to = b.pos - a.chest();
		if (to.length() < rng_m && (to.length() < 0.5f || Vec3(to.x, 0, to.z).normalized().dot(dir) > std::cos(deg_to_rad(cone)))) {
			AgentRef th = Agent::of_body(w, b, &a);
			IxCtx ctx;
			ctx.site = "flare";
			Interactions::resolve(w, *th, *flame, ctx);
		}
	}
	w.ledger.spent += maxf(0.0, flame->heat);
}

// Fire Column (T2) and Inferno (T3): a heat cone like the blaze that leaves a fire field holding field_share of the
// paid heat (MOVESET §7.9).
void _column(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int tier = inst.tier();
	const double rng_m = Charge::paramf(inst, "col_range", 7.0);
	const double cone = Charge::paramf(inst, "col_cone", 10.0);
	const double paid = w.pay_heat(a, Charge::paramf(inst, "col_hu", 300.0));
	const Vec3 dir = dvec(inst.data, "face");
	const double field_hu = paid * Charge::paramf(inst, "field_share", 0.35);
	const double vol_hu = paid - field_hu;
	w.emit("flare", D({{"actor", a.id}, {"dir", dir}, {"range", rng_m}, {"heavy", true}, {"tier", tier}, {"from_reserve", a.heat_reserve > 0.0}}));
	AgentRef flame = Agent::of_volume(&w, &a, &inst, "flame", a.chest(), dir, D({{"H", vol_hu / Interactions::HU_PER_PU}, {"heat_hu", vol_hu}}));
	FxEvents::fx_for(w, a, inst, "cone", "flame",
	                 D({{"length", rng_m}, {"angle", cone}, {"power", flame->power}, {"shape", tier == 2 ? "spear" : "open"}}));
	std::set<int> shielded;
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || b.form == Form::Pool || b.controller == a.id || (b.static_body && b.form != Form::Wall)) continue;
		const Vec3 to = b.pos - a.chest();
		const Vec3 fl(to.x, 0, to.z);
		if (fl.length() > rng_m + b.radius || (fl.length() > 0.5f && fl.normalized().dot(dir) < std::cos(deg_to_rad(cone + 6.0)))) continue;
		VerbVolume::meet_body(w, &a, *flame, b);
		if (b.alive && b.is_water() && b.controller >= 0) shielded.insert(b.controller);
	}
	for (ActorState* t : w.actors_in_cone(a, dir, rng_m, cone)) {
		if (shielded.count(t->id)) {
			w.emit("block", D({{"actor", t->id}, {"attacker", a.id}, {"kind", "fire_water"}}));
			continue;
		}
		const Dict info = D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "col_damage", 20.0)},
		                     {"balance", Charge::paramf(inst, "col_balance", 40.0)},
		                     {"knock", dir * Charge::paramf(inst, "knock", 4.0) + V3(0, 1.0, 0)}, {"kind", "fire"}, {"from", a.chest()},
		                     {"power", flame->power}, {"tier", tier}, {"mat", "flame"}});
		if (t->guarding) {
			AgentRef g = Agent::of_guard(w, *t);
			if (dbool(Interactions::rule(flame->cls, g->ccls, g->tier), "aura", false)) {
				IxCtx ctx;
				ctx.info = info;
				ctx.info_agent = flame;
				ctx.target = t;
				Interactions::resolve(w, *flame, *g, ctx);
				continue;
			}
		}
		const std::string res = w.hit_actor(*t, info, flame);
		if (res == "hit" || res == "knockdown") Status::apply(w, *t, "burning", tier == 2 ? 1.5 : 2.5, 1.0, a.id);
	}
	w.ledger.spent += maxf(0.0, flame->heat);
	Vec3 fp = a.pos + dir * (rng_m * (tier == 2 ? 0.8 : 0.55));
	if (w.arena.segment_hit(a.chest(), fp + V3(0, 0.5, 0)) >= 0.0 || w.wall_hit(a.chest(), fp + V3(0, 0.5, 0)) >= 0.0)
		fp = a.pos + dir * minf(2.0, rng_m * 0.3);
	MatBody* z = FireUtil::spawn_field(w, a.id, fp, Charge::paramf(inst, "field_r", 1.5), Charge::paramf(inst, "field_life", 2.0), field_hu,
	                                   false, tier);
	z->props.set("dps", tier == 2 ? 3.0 : 4.0);
	w.emit("fire_column", D({{"actor", a.id}, {"tier", tier}, {"field", z->id}, {"hu", paid}}));
}

void _vent(CombatWorld& w, ActorState& a) {
	const double e = a.heat_reserve;
	a.heat_reserve = 0.0;
	w.ledger.vented += e;
	w.emit("vent", D({{"actor", a.id}, {"amount", e}}));
}

}  // namespace ActFire
}  // namespace ff
