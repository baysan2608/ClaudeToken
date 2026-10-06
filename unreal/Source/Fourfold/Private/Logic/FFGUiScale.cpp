// Fourfold game logic island - HUD -> input context bridge (see FFGUiScale.h).
#include "FFGUiScale.h"

namespace ffg {

TouchContext TouchContextFromHud(const ff::HudModel& h) {
	TouchContext c;
	if (!h.valid) return c;
	c.element = Clampi(h.element, 0, 3);
	c.unlocked_elements = h.elements_unlocked;
	c.tech_label = h.tech_label;
	c.tech_available = h.tech_available;
	c.holding = h.holding;
	c.has_charge = true;
	c.attack_charge = h.attack_charge;
	c.attack_element = h.attack_element;
	c.attack_decide = h.attack_decide;
	c.sub = Clampi(h.sub, 0, 3);
	c.subs_unlocked = h.subs_unlocked;
	c.sub_names = h.sub_names;
	for (size_t i = 0; i < 4; ++i)
		if (c.sub_names[i].empty()) c.sub_names[i] = std::string(ff::SubName(c.element, static_cast<int>(i)));
	c.petal_up = h.petal_up;
	c.petal_down = h.petal_down;
	c.petal_side = h.petal_side;
	c.guard_petal_up = h.guard_petal_up;
	c.guard_petal_down = h.guard_petal_down;
	c.shape_label = h.shape_label;
	if (h.charge.active && h.charge.max_tier > 0) {
		c.charge_slot = h.charge.button;
		c.charge_tier = h.charge.tier;
		c.charge_frac = h.charge.frac;
		c.charge_max = h.charge.max_tier;
	}
	return c;
}

void DesktopContextFromHud(const ff::HudModel& h, DesktopInput& d) {
	if (!h.valid) return;
	d.SetUnlocked(h.elements_unlocked);
	d.SetSubContext(h.element, h.sub, h.subs_unlocked);
}

}  // namespace ffg
