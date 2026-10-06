// Fourfold core - port of game/ui/lab/lab_script.gd.
#include "Lab/LabScript.h"

#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

Dict LabScript::next() {
	if (tick >= frames.size()) return Dict();
	return frames[tick++];
}

Dict& LabScript::_at(size_t t) {
	while (frames.size() <= t) frames.push_back(Dict());
	return frames[t];
}

void LabScript::_hold(size_t from_t, size_t to_t, const std::string& key) {
	for (size_t t = from_t; t <= to_t; ++t) _at(t).set(key, true);
}

void LabScript::apply_dict(InputFrame& o, const Dict& d) {
	for (const auto& kv : d) {
		const std::string& k = kv.first;
		const Value& v = kv.second;
		if (k == "attack_pressed") o.attack_pressed = v.truthy();
		else if (k == "attack_held") o.attack_held = v.truthy();
		else if (k == "attack_released") o.attack_released = v.truthy();
		else if (k == "attack_gesture") o.attack_gesture = static_cast<Gesture>(vint(v));
		else if (k == "guard_pressed") o.guard_pressed = v.truthy();
		else if (k == "guard_held") o.guard_held = v.truthy();
		else if (k == "guard_released") o.guard_released = v.truthy();
		else if (k == "guard_gesture") o.guard_gesture = static_cast<Gesture>(vint(v));
		else if (k == "tech_pressed") o.tech_pressed = v.truthy();
		else if (k == "tech_held") o.tech_held = v.truthy();
		else if (k == "tech_released") o.tech_released = v.truthy();
		else if (k == "tech_cancel") o.tech_cancel = v.truthy();
		else if (k == "evade_pressed") o.evade_pressed = v.truthy();
		else if (k == "evade_held") o.evade_held = v.truthy();
		else if (k == "element_select") o.element_select = vint(v);
		else if (k == "sub_select") o.sub_select = vint(v);
		else if (k == "target_cycle") o.target_cycle = v.truthy();
	}
}

void LabScript::apply_dict(ActorIntent& o, const Dict& d) {
	for (const auto& kv : d) {
		const std::string& k = kv.first;
		const Value& v = kv.second;
		if (k == "attack_pressed") o.attack_pressed = v.truthy();
		else if (k == "attack_held") o.attack_held = v.truthy();
		else if (k == "attack_released") o.attack_released = v.truthy();
		else if (k == "attack_gesture") o.attack_gesture = vint(v);
		else if (k == "guard_pressed") o.guard_pressed = v.truthy();
		else if (k == "guard_held") o.guard_held = v.truthy();
		else if (k == "guard_gesture") o.guard_gesture = vint(v);
		else if (k == "tech_pressed") o.tech_pressed = v.truthy();
		else if (k == "tech_held") o.tech_held = v.truthy();
		else if (k == "tech_released") o.tech_released = v.truthy();
		else if (k == "tech_cancel") o.tech_cancel = v.truthy();
		else if (k == "evade_pressed") o.evade_pressed = v.truthy();
		else if (k == "evade_held") o.evade_held = v.truthy();
		else if (k == "element_select") o.element_select = vint(v);
		else if (k == "sub_select") o.sub_select = vint(v);
		else if (k == "target_cycle") o.target_cycle = v.truthy();
		else if (k == "move") o.move = vvec(v);
		else if (k == "aim_dir") o.aim_dir = vvec(v);
		else if (k == "aim_active") o.aim_active = v.truthy();
	}
}

double LabScript::hold_seconds(const Dict& def, int tier) {
	if (tier <= 0) return 0.0;
	const auto times = Charge::tier_times(def);
	return times[static_cast<size_t>(clampi(tier - 1, 0, 2))] + 0.10;
}

LabScript LabScript::for_move(int element, int sub, const std::string& slot, int tier) {
	LabScript s;
	const std::string id = Moves::resolve(element, sub, slot);
	const Dict def = Moves::defs().get(id).as_dict();
	std::string nm = dstr(def, "name", id);
	const size_t sep = nm.find(" / ");
	if (sep != std::string::npos) nm = nm.substr(0, sep);
	s.label = nm + " " + (tier > 0 ? "T" + itos(tier) : std::string());
	Dict& first = s._at(0);
	first.set("element_select", element);
	first.set("sub_select", sub);
	const size_t t0 = LEAD;
	auto ticks_of = [](double secs) { return static_cast<size_t>(std::ceil(secs / Sim::DT)); };
	if (slot == "strike" || slot == "thrust" || slot == "ground" || slot == "sweep") {
		const size_t hold_ticks = tier > 0 ? ticks_of(hold_seconds(def, tier)) : 4;
		Dict& f = s._at(t0);
		f.set("attack_pressed", true);
		f.set("attack_gesture", slot == "strike" ? 0 : (slot == "thrust" ? 1 : (slot == "ground" ? 2 : 3)));
		s._hold(t0, t0 + hold_ticks, "attack_held");
		s._at(t0 + hold_ticks + 1).set("attack_released", true);
	} else if (slot == "guard") {
		const size_t gh = std::max<size_t>(48, ticks_of(hold_seconds(def, tier)) + 10);
		s._at(t0).set("guard_pressed", true);
		s._hold(t0, t0 + gh, "guard_held");
	} else if (slot == "push" || slot == "sink") {
		const size_t gh2 = tier > 0 ? std::max<size_t>(10, ticks_of(hold_seconds(def, tier))) : 10;
		s._at(t0).set("guard_pressed", true);
		s._hold(t0, t0 + gh2 + 32, "guard_held");
		s._at(t0 + gh2).set("guard_gesture", slot == "push" ? static_cast<int>(Gesture::Up) : static_cast<int>(Gesture::Down));
	} else if (slot == "tech") {
		const size_t th = std::max<size_t>(40, ticks_of(hold_seconds(def, tier)) + 4);
		s._at(t0).set("tech_pressed", true);
		s._hold(t0, t0 + th, "tech_held");
		s._at(t0 + th + 1).set("tech_released", true);
	} else if (slot == "evade") {
		s._at(t0).set("evade_pressed", true);
	} else if (slot == "evade_hold") {
		s._at(t0).set("evade_pressed", true);
		s._hold(t0, t0 + 40, "evade_held");
	}
	return s;
}

size_t LabScript::ticks_for(int element, int sub, const std::string& slot, int tier) { return for_move(element, sub, slot, tier).length(); }

}  // namespace ff
