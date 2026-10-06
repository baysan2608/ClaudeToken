// Fourfold core - port of game/core/status.gd.
#include "Sim/Status.h"

#include "Data/GameData.h"
#include "Sim/ActorState.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <vector>

namespace ff {
namespace Status {
namespace {
Dict& status_specs_store() {
	static Dict s;
	return s;
}
}  // namespace

Dict& specs() { return status_specs_store(); }

void ensure() {
	Dict& s = status_specs_store();
	if (!s.empty()) return;
	// hooks.json status_specs = Status.CORE + every kit registration, in registration order.
	const Dict src = GameData::hooks().get("status_specs").as_dict();
	for (const auto& it : src) s.set(it.first, it.second.duplicate(true));
}

void register_spec(const std::string& nm, const Dict& spec) {
	ensure();
	specs().set(nm, spec.duplicate(true));
}

Dict spec(std::string_view nm) { return specs().get(nm).as_dict(); }

void apply(CombatWorld& w, ActorState& a, const std::string& nm, double dur, double mag, int src) {
	if (nm == "wet") a.wetness = maxf(a.wetness, clampf(mag, WET_AT + 0.01, 1.0));
	if (immune(a, "status:" + nm)) return;
	Dict cur = a.status.get(nm).as_dict();
	if (cur.empty()) {
		a.status.set(nm, D({{"t", dur}, {"mag", mag}, {"src", src}}));
		FxEvents::status(w, a, nm, true, dur, mag);
		return;
	}
	if (dur < 0.0 || (dnum(cur, "t") >= 0.0 && dur > dnum(cur, "t"))) cur.set("t", dur);
	cur.set("mag", maxf(dnum(cur, "mag"), mag));
	cur.set("src", src);
}

void remove(CombatWorld& w, ActorState& a, const std::string& nm) {
	if (a.status.has(nm)) {
		a.status.erase(nm);
		FxEvents::status(w, a, nm, false, 0.0, 0.0);
	}
	if (nm == "wet") a.wetness = minf(a.wetness, WET_AT * 0.5);
}

bool has(const ActorState& a, std::string_view nm) {
	if (nm == "wet") return a.wetness > WET_AT;
	return a.status.has(nm);
}

void tick(CombatWorld& w, ActorState& a, double dt) {
	const bool wet_now = a.wetness > WET_AT;
	if (wet_now != a.status.has("wet")) {
		if (wet_now) a.status.set("wet", D({{"t", -1.0}, {"mag", a.wetness}, {"src", -1}}));
		else a.status.erase("wet");
		FxEvents::status(w, a, "wet", wet_now, -1.0, a.wetness);
	}
	if (a.status.empty()) return;
	std::vector<std::string> ended;
	const Dict& sp_all = specs();
	for (const auto& it : a.status) {
		const std::string& nm = it.first;
		if (nm == "wet") continue;
		Dict s = it.second.as_dict();
		const Dict sp = sp_all.get(nm).as_dict();
		if (sp.has("dps") && !immune(a, nm == "burning" ? "burn" : "dot"))
			a.health = maxf(0.0, a.health - dnum(sp, "dps") * dnum(s, "mag") * dt);
		if (dnum(s, "t") >= 0.0) {
			s.set("t", dnum(s, "t") - dt);
			if (dnum(s, "t") <= 0.0) ended.push_back(nm);
		}
	}
	for (const std::string& nm : ended) {
		a.status.erase(nm);
		FxEvents::status(w, a, nm, false, 0.0, 0.0);
	}
}

namespace {
double status_mult(const ActorState& a, std::string_view key) {
	double m = 1.0;
	const Dict& sp_all = specs();
	for (const auto& it : a.status) {
		const Value& sp = sp_all.get(it.first);
		if (sp.has(key)) m *= sp.get(key).as_float(1.0);
	}
	return m;
}
bool status_flag(const ActorState& a, std::string_view key) {
	const Dict& sp_all = specs();
	for (const auto& it : a.status)
		if (sp_all.get(it.first).get(key, Value(false)).truthy()) return true;
	return false;
}
}  // namespace

double speed_mult(const ActorState& a) { return a.status.empty() ? 1.0 : status_mult(a, "speed"); }
double recovery_mult(const ActorState& a) { return a.status.empty() ? 1.0 : status_mult(a, "recovery"); }
double friction_mult(const ActorState& a) { return a.status.empty() ? 1.0 : status_mult(a, "friction"); }
double tech_cost_mult(const ActorState& a) { return a.status.empty() ? 1.0 : status_mult(a, "tech_cost"); }
bool rooted(const ActorState& a) { return status_flag(a, "rooted"); }

double armor(const ActorState& a) {
	double r = a.armor;
	const Dict& sp_all = specs();
	for (const auto& it : a.status) {
		const Value& sp = sp_all.get(it.first);
		if (sp.has("armor")) r = maxf(r, sp.get("armor").as_float());
	}
	return clampf(r, 0.0, 0.95);
}

bool immune(const ActorState& a, std::string_view what) {
	if (a.anchored && (what == "knockback" || what == "pull" || what == "lift")) return true;
	if (a.flying && what == "ground") return true;
	const Dict& sp_all = specs();
	for (const auto& it : a.status) {
		const Value& sp = sp_all.get(it.first);
		const Value& im = sp.get("immune");
		if (const Array* arr = im.array_ptr())
			if (arr_has_str(*arr, what)) return true;
	}
	return false;
}

bool lock_blocked(const ActorState& a) { return status_flag(a, "no_lock"); }
bool hidden(const ActorState& a) { return status_flag(a, "hidden"); }

}  // namespace Status
}  // namespace ff
