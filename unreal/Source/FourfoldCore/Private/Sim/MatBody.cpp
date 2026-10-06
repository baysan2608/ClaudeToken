// Fourfold core - port of game/core/mat_body.gd.
#include "Sim/MatBody.h"

#include "Sim/Materials.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

bool MatBody::is_molten_any() const { return Materials::is_fusible(mat) && phase == Phase::Molten; }

double MatBody::thermal_energy() const {
	// Heat above ambient in HU, including latent heat and any carried heat payload (energy ledger).
	if (mat == Mat::Stone)
		return mass * Sim::STONE_C * (temp - Sim::AMBIENT_C) + mass * Sim::STONE_LATENT * liquid + heat_payload;
	if (mat == Mat::Water)
		return mass * Sim::WATER_C * (temp - Sim::AMBIENT_C) - mass * Sim::WATER_LATENT_FUSION * (1.0 - liquid) + heat_payload;
	if (Materials::is_fusible(mat))
		return mass * Materials::c(mat) * (temp - Sim::AMBIENT_C) + mass * Materials::latent(mat) * liquid + heat_payload;
	if (mat == Mat::Plant) return mass * Materials::c(mat) * (temp - Sim::AMBIENT_C) + heat_payload;
	return heat_payload;
}

void MatBody::update_radius() {
	switch (mat) {
		case Mat::Stone:
		case Mat::Sand:
		case Mat::Glass: radius = Sim::stone_radius(mass); break;
		case Mat::Metal: radius = Sim::stone_radius(mass * 0.35); break;
		case Mat::Water: radius = Sim::water_radius(mass); break;
		case Mat::Plant: radius = Sim::stone_radius(mass * 0.6); break;
		default: radius = zone_radius <= 0.0 ? 0.5 : zone_radius; break;
	}
}

void MatBody::update_radius_puddle() { radius = clampf(std::sqrt(mass / (kPi * 10.0)), 0.3, 2.2); }

std::string MatBody::describe() const {
	std::string s = "#" + itos(id) + " " + Sim::mat_name(mat);
	if (!tag.empty()) s += "[" + tag + "]";
	s += std::string(" ") + Sim::form_name(form) + "/" + Sim::phase_name(phase) + " " + ftos(mass, 1) + "kg " + ftos(temp, 0) +
	     "C liq=" + ftos(liquid, 2) + " ctl=" + itos(controller);
	return s;
}

}  // namespace ff
