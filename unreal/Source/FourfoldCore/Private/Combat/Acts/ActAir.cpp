// Fourfold core - port of game/combat/act_air.gd: Air sub 0 (Gust) palm gust (tap) / cyclone push (hold) / Gale /
// Hurricane Palm (longer holds), updraft + glide technique (or Wind Grip when a light body is in the aim cone).
// Air bends trajectories and pushes light things; it is not a universal cancel.
#include "Combat/Acts.h"

#include "Combat/Kits/Air/AirGust.h"
#include "Combat/Moves.h"
#include "Sim/Agent.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace ActAir {

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	if (inst.id == "air_attack") {
		if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
			inst.data.set("fizzle", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		}
	} else if (inst.id == "air_tech") {
		inst.data.erase("face");
		if (a.grounded && _wind_grip_context(w, a, inst, it)) return;
		if (a.grounded) {
			if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
				inst.data.set("fizzle", true);
				w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "updraft"}}));
			}
		} else {
			inst.data.set("airborne_start", true);
			inst.data.set("startup", 0.0);
		}
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "air_attack") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		return w.attack_after_startup(a, inst, it);
	}
	if (inst.id == "air_tech") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		if (!dbool(inst.data, "airborne_start", false)) {
			a.vel.y = f32(dnum(inst.def, "lift_speed"));
			a.grounded = false;
			w.emit("updraft", D({{"actor", a.id}}));
		}
		return ActionPhase::Channel;
	}
	return ActionPhase::Active;
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "air_attack" && p == ActionPhase::Active) _push(w, a, inst);
	if (inst.id == "air_tech" && p != ActionPhase::Channel) {
		if (a.gliding) w.emit("glide_end", D({{"actor", a.id}}));
		a.gliding = false;
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "air_attack") {
		if (!it.attack_held) inst.data.set("released", true);
		inst.data.set("face", w.aim_dir(a, it));
		if (inst.phase == ActionPhase::Charge && dbool(inst.data, "released", false) && inst.total >= dnum(inst.def, "heavy_min")) {
			int tier = maxi(1, inst.tier());
			if (w.spend_focus(a, dnum(inst.def, "heavy_cost") - dnum(inst.def, "cost"))) {
				// Longer holds (Gale T2 / Hurricane Palm T3) pay their own surcharge; short of Focus they fire a tier lower.
				while (tier >= 2 && !w.spend_focus(a, Charge::pgetf(inst.def, tier, "cost_add", 0.0))) tier -= 1;
				inst.data.set("tier", tier);
				w.set_phase(a, inst, ActionPhase::Active);
			} else {
				inst.heavy = false;
				inst.data.set("tier", 0);
				w.set_phase(a, inst, ActionPhase::Active);
			}
		}
	} else if (inst.id == "air_tech") {
		if (it.tech_cancel && inst.phase == ActionPhase::Startup) {
			// A cancel during the crouch never launches the updraft.
			w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
			w.finish_action(a, inst);
			return;
		}
		if (inst.phase != ActionPhase::Channel) return;
		if (a.grounded && inst.t > 0.1) {
			w.finish_action(a, inst);
			return;
		}
		if (!it.tech_held || it.tech_cancel) {
			if (a.gliding) w.emit("glide_end", D({{"actor", a.id}}));
			a.gliding = false;
			w.finish_action(a, inst);
			return;
		}
		if (a.vel.y < 0.0f && a.has("glide") && !a.gliding) {
			a.gliding = true;
			w.emit("glide", D({{"actor", a.id}}));
		}
		if (a.gliding && !w.spend_focus(a, dnum(inst.def, "glide_cost") * Sim::DT)) {
			a.gliding = false;
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "glide"}}));
			w.finish_action(a, inst);
		}
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	(void)w;
	(void)inst;
	(void)reason;
	a.gliding = false;
}

// Context technique: a light body in the aim cone within 9 m -> Wind Grip (the action morphs into gust_grip).
bool _wind_grip_context(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)inst;
	const Dict& defs = Moves::defs();
	if (!defs.has("gust_grip") || a.focus < dnum(defs.get("gust_grip").as_dict(), "cost", 6.0)) return false;
	if (AirGust::grip_target(w, a, w.aim_dir(a, it)) == nullptr) return false;
	return w.morph_action(a, "gust_grip", "tech", it) != nullptr;
}

void _push(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Dict& d = inst.def;
	const bool heavy = inst.heavy;
	const int tier = inst.tier();
	const bool strong = tier >= 2;   // Gale / Hurricane Palm: tier data from AirGust
	const double rng_m = strong ? Charge::paramf(inst, "range", 8.0) : (heavy ? dnum(d, "heavy_range") : dnum(d, "range"));
	const double cone = strong ? Charge::paramf(inst, "cone", 50.0) : (heavy ? dnum(d, "heavy_cone") : dnum(d, "cone"));
	const double knock = strong ? Charge::paramf(inst, "knock", 14.0) : (heavy ? dnum(d, "heavy_knock") : dnum(d, "knock"));
	const double dmg = strong ? Charge::paramf(inst, "damage", 9.0) : (heavy ? dnum(d, "heavy_damage") : dnum(d, "damage"));
	const double bal = strong ? Charge::paramf(inst, "balance", 44.0) : (heavy ? dnum(d, "heavy_balance") : dnum(d, "balance"));
	const Vec3 dir = dvec(inst.data, "face");
	w.emit("gust", D({{"actor", a.id}, {"dir", dir}, {"range", rng_m}, {"heavy", heavy}, {"tier", tier}}));
	// The gust is a pressure volume: a threat to fighters, a counter (class "gust") to the bodies it meets.
	AgentRef gust = Agent::of_volume(&w, &a, &inst, "gust", a.chest(), dir, D({{"P", Charge::paramf(inst, "power", heavy ? HEAVY_POWER : POWER)}}));
	gust->data.set("knock", knock);
	FxEvents::fx_for(w, a, inst, "cone", "wind", D({{"length", rng_m}, {"angle", cone}, {"power", gust->power}}));
	for (ActorState* t : w.actors_in_cone(a, dir, rng_m, cone)) {
		// Fighters held up by wind (flight) are the easiest to blow around: x1.5 balance.
		const double b_dmg = bal * (Status::has(*t, "flight") ? 1.5 : 1.0);
		w.hit_actor(*t,
		            D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", dmg}, {"balance", b_dmg},
		               {"knock", dir * knock + V3(0, 1.5, 0)}, {"kind", "air"}, {"from", a.chest()}}),
		            gust);
	}
	const double cos_lim = std::cos(deg_to_rad(cone));
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (!b.alive || b.controller >= 0 || b.static_body) continue;
		const Vec3 to = b.pos - a.chest();
		const double dist = to.length();
		if (dist > rng_m || (dist > 0.5 && Vec3(to.x, 0, to.z).normalized().dot(dir) < cos_lim)) continue;
		// Legacy cell (*, gust) T0-T1; T2-T3 cells are the Air kit's (AirRules).
		AgentRef th = Agent::of_body(w, b, &a);
		IxCtx ctx;
		ctx.site = "gust";
		Interactions::resolve(w, *th, *gust, ctx, strong ? &Interactions::PASS_RULE() : &Interactions::DEFAULT_RULE());
	}
}

}  // namespace ActAir
}  // namespace ff
