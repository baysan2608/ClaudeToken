// Fourfold core - port of game/core/charge.gd.
#include "Sim/Charge.h"

#include "Sim/ActorState.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

namespace ff {
namespace Charge {

Dict pdef(const ActionInst& inst) {
	const Value& sd = inst.data.get("spec_def");
	if (sd.is_dict()) return sd.as_dict();
	return inst.def;
}

int max_tier(const Dict& def) {
	const Value& tiers = def.get("tiers");
	if (tiers.has("t3")) return 3;
	if (tiers.has("t2")) return 2;
	if (tiers.has("t1") || def.has("heavy_min")) return 1;
	return 0;
}

std::array<double, 3> tier_times(const Dict& def) {
	std::array<double, 3> t{{TIER_TIMES[0], TIER_TIMES[1], TIER_TIMES[2]}};
	const Value& tt = def.get("tier_times");
	if (const Array* arr = tt.array_ptr()) {
		for (size_t i = 0; i < 3; ++i) t[i] = i < arr->size() ? (*arr)[i].as_float() : TIER_TIMES[i];
	} else {
		t[0] = dnum(def, "heavy_min", TIER_TIMES[0]);
	}
	if (def.has("heavy_min")) t[0] = maxf(t[0], dnum(def, "heavy_min"));
	return t;
}

int tier_for(const Dict& def, double held_s) {
	const int mt = max_tier(def);
	const auto times = tier_times(def);
	int tier = 0;
	for (int k = 0; k < mt; ++k)
		if (held_s >= times[static_cast<size_t>(k)] - 1e-6) tier = k + 1;
	return tier;
}

Value pget(const Dict& def, int tier, std::string_view key, const Value& dflt) {
	const Value& tiers = def.get("tiers");
	if (tiers.is_dict()) {
		static const char* const kTn[4] = {"t0", "t1", "t2", "t3"};
		for (int t = clampi(tier, 0, 3); t >= 0; --t) {
			const Value& td = tiers.get(kTn[t]);
			if (td.has(key)) return td.get(key);
		}
	}
	return def.get(key, dflt);
}

Value param(const ActionInst& inst, std::string_view key, const Value& def) { return pget(pdef(inst), inst.tier(), key, def); }

double counter_power(const Dict& def, int tier) {
	const Value& c = def.get("counter");
	if (!c.has("power")) return -1.0;
	const Value& p = c.get("power");
	if (const Array* arr = p.array_ptr()) {
		if (arr->empty()) return -1.0;
		return (*arr)[static_cast<size_t>(clampi(tier, 0, static_cast<int>(arr->size()) - 1))].as_float();
	}
	return p.as_float();
}

bool held(const ActionInst& inst, const ActorIntent& it) {
	const std::string& s = inst.slot;
	if (s == "strike" || s == "thrust" || s == "ground" || s == "sweep") return it.attack_held;
	if (s == "guard" || s == "push" || s == "sink") return it.guard_held;
	if (s == "tech") return it.tech_held;
	if (s == "evade" || s == "evade_hold") return it.evade_held;
	if (inst.phase == ActionPhase::Charge) return it.attack_held;
	return it.tech_held;
}

void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const Dict d = pdef(inst);
	const int mt = max_tier(d);
	if (mt <= 0 || dtruthy(inst.data, "released") || dtruthy(inst.data, "charge_frozen")) return;
	if (!held(inst, it)) return;
	const int tier = dint(inst.data, "tier", 0);
	double ct = dnum(inst.data, "charge_t", inst.total - Sim::DT);
	if (tier >= 1 && mt >= 2) {
		const double drain = vnum(pget(d, tier, "charge_drain", DEFAULT_DRAIN)) * Sim::DT;
		if (drain > 0.0 && !w.spend_focus(a, drain)) {
			if (!dtruthy(inst.data, "stalled")) {
				inst.data.set("stalled", true);
				w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "charge"}, {"tier", tier}}));
			}
			return;
		}
	}
	ct += Sim::DT;
	inst.data.set("charge_t", ct);
	const int nt = tier_for(d, ct);
	if (nt > tier) {
		inst.data.set("tier", nt);
		FxEvents::charge(w, a, inst, nt, nt >= mt);
	}
}

Vec2 progress(const ActionInst& inst) {
	const Dict d = pdef(inst);
	const int mt = max_tier(d);
	const int tier = dint(inst.data, "tier", 0);
	if (mt <= 0 || tier >= mt) return V2(tier, 1.0);
	const auto times = tier_times(d);
	const double ct = dnum(inst.data, "charge_t", inst.total);
	const double t0 = tier == 0 ? 0.0 : times[static_cast<size_t>(tier - 1)];
	const double t1 = times[static_cast<size_t>(tier)];
	return V2(tier, clampf((ct - t0) / maxf(t1 - t0, 1e-3), 0.0, 1.0));
}

double paramf(const ActionInst& inst, std::string_view key, double def) { return vnum(param(inst, key, Value(def)), def); }
int parami(const ActionInst& inst, std::string_view key, int def) { return vint(param(inst, key, Value(def)), def); }
bool paramb(const ActionInst& inst, std::string_view key, bool def) { return vbool(param(inst, key, Value(def)), def); }
std::string params(const ActionInst& inst, std::string_view key, std::string_view def) {
	return vstr(param(inst, key, Value(std::string(def))), def);
}
double pgetf(const Dict& def, int tier, std::string_view key, double dflt) { return vnum(pget(def, tier, key, Value(dflt)), dflt); }

}  // namespace Charge
}  // namespace ff
