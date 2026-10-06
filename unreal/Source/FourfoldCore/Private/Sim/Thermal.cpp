// Fourfold core - port of game/core/thermal.gd.
#include "Sim/Thermal.h"

#include "Sim/MatBody.h"
#include "Sim/Materials.h"
#include "Util/GodotMath.h"

#include <cmath>

namespace ff {
namespace Thermal {
namespace {
double g_last_vapor = 0.0;
double g_last_used = 0.0;
}  // namespace

double last_vapor() { return g_last_vapor; }
double last_used() { return g_last_used; }

double heat(MatBody& b, double energy) {
	g_last_vapor = 0.0;
	if (b.mass <= 0.0 || energy == 0.0) return 0.0;
	if (b.mat == Mat::Stone) return stone(b, energy);
	if (b.mat == Mat::Water) return water(b, energy);
	if (Materials::is_fusible(b.mat))
		return solid(b, energy, Materials::c(b.mat), Materials::melt(b.mat), Materials::latent(b.mat), Materials::max_temp(b.mat));
	if (b.mat == Mat::Plant) return sensible(b, energy, Materials::c(b.mat), Materials::max_temp(b.mat));
	if (b.mat == Mat::Fire) return payload(b, energy);
	return 0.0;
}

double stone(MatBody& b, double e) { return solid(b, e, Sim::STONE_C, Sim::STONE_MELT_C, Sim::STONE_LATENT, STONE_MAX_C); }

double solid(MatBody& b, double e, double sc, double melt_c, double lat, double max_c) {
	const double m = b.mass;
	double applied = 0.0;
	if (e > 0.0) {
		double left = e;
		if (b.temp < melt_c && b.liquid <= 0.0) {
			const double need = (melt_c - b.temp) * m * sc;
			const double use = minf(left, need);
			b.temp += use / (m * sc);
			left -= use;
			applied += use;
		}
		if (left > 0.0 && b.liquid < 1.0) {
			b.temp = maxf(b.temp, melt_c);
			const double need_l = (1.0 - b.liquid) * m * lat;
			const double use_l = minf(left, need_l);
			b.liquid = minf(1.0, b.liquid + use_l / (m * lat));
			left -= use_l;
			applied += use_l;
		}
		if (left > 0.0 && b.liquid >= 1.0) {
			const double need_s = (max_c - b.temp) * m * sc;
			const double use_s = clampf(left, 0.0, maxf(need_s, 0.0));
			b.temp += use_s / (m * sc);
			applied += use_s;
		}
		return applied;
	}
	// Cooling: superheat, then latent (solidification), then sensible heat to ambient.
	double take = -e;
	if (b.temp > melt_c) {
		const double avail = (b.temp - melt_c) * m * sc;
		const double use = minf(take, avail);
		b.temp -= use / (m * sc);
		take -= use;
		applied -= use;
	}
	if (take > 0.0 && b.liquid > 0.0) {
		const double avail_l = b.liquid * m * lat;
		const double use_l = minf(take, avail_l);
		b.liquid = maxf(0.0, b.liquid - use_l / (m * lat));
		if (b.liquid < 1e-6) b.liquid = 0.0;
		take -= use_l;
		applied -= use_l;
	}
	if (take > 0.0 && b.liquid <= 0.0 && b.temp > Sim::AMBIENT_C) {
		const double avail_s = (b.temp - Sim::AMBIENT_C) * m * sc;
		const double use_s = minf(take, avail_s);
		b.temp -= use_s / (m * sc);
		applied -= use_s;
	}
	return applied;
}

double sensible(MatBody& b, double e, double sc, double max_c) {
	const double m = b.mass;
	if (sc <= 0.0 || m <= 0.0) return 0.0;
	if (e > 0.0) {
		const double use = minf(e, maxf(0.0, (max_c - b.temp) * m * sc));
		b.temp += use / (m * sc);
		return use;
	}
	const double use_c = minf(-e, maxf(0.0, (b.temp - Sim::AMBIENT_C) * m * sc));
	b.temp -= use_c / (m * sc);
	return -use_c;
}

double payload(MatBody& b, double e) {
	if (e > 0.0) {
		b.heat_payload += e;
		return e;
	}
	const double use = minf(-e, b.heat_payload);
	b.heat_payload -= use;
	return -use;
}

double water(MatBody& b, double e) {
	const double m = b.mass;
	double applied = 0.0;
	double vapor = 0.0;
	if (e > 0.0) {
		double left = e;
		if (b.temp < Sim::WATER_FREEZE_C) {
			const double need = (Sim::WATER_FREEZE_C - b.temp) * m * Sim::WATER_C;
			const double use = minf(left, need);
			b.temp += use / (m * Sim::WATER_C);
			left -= use;
			applied += use;
		}
		if (left > 0.0 && b.liquid < 1.0) {
			const double need_l = (1.0 - b.liquid) * m * Sim::WATER_LATENT_FUSION;
			const double use_l = minf(left, need_l);
			b.liquid = minf(1.0, b.liquid + use_l / (m * Sim::WATER_LATENT_FUSION));
			left -= use_l;
			applied += use_l;
		}
		if (left > 0.0 && b.liquid >= 1.0 && b.temp < Sim::WATER_BOIL_C) {
			const double need_s = (Sim::WATER_BOIL_C - b.temp) * m * Sim::WATER_C;
			const double use_s = minf(left, need_s);
			b.temp += use_s / (m * Sim::WATER_C);
			left -= use_s;
			applied += use_s;
		}
		if (left > 0.0 && b.liquid >= 1.0) {
			vapor = minf(b.mass, left / Sim::WATER_LATENT_VAPOR);
			applied += vapor * Sim::WATER_LATENT_VAPOR;
			b.mass -= vapor;
		}
		g_last_vapor = vapor;
		return applied;
	}
	double take = -e;
	if (b.temp > Sim::WATER_FREEZE_C) {
		const double avail = (b.temp - Sim::WATER_FREEZE_C) * m * Sim::WATER_C;
		const double use = minf(take, avail);
		b.temp -= use / (m * Sim::WATER_C);
		take -= use;
		applied -= use;
	}
	if (take > 0.0 && b.liquid > 0.0) {
		const double avail_l = b.liquid * m * Sim::WATER_LATENT_FUSION;
		const double use_l = minf(take, avail_l);
		b.liquid = maxf(0.0, b.liquid - use_l / (m * Sim::WATER_LATENT_FUSION));
		if (b.liquid < 1e-6) b.liquid = 0.0;
		take -= use_l;
		applied -= use_l;
	}
	if (take > 0.0 && b.liquid <= 0.0 && b.temp > ICE_MIN_C) {
		const double avail_s = (b.temp - ICE_MIN_C) * m * Sim::WATER_C;
		const double use_s = minf(take, avail_s);
		b.temp -= use_s / (m * Sim::WATER_C);
		applied -= use_s;
	}
	return applied;
}

double boil(MatBody& b, double energy) {
	g_last_used = 0.0;
	if (b.mat != Mat::Water || b.mass <= 0.0 || energy <= 0.0) return 0.0;
	double per_kg = Sim::WATER_LATENT_VAPOR + Sim::WATER_C * maxf(0.0, Sim::WATER_BOIL_C - b.temp);
	if (b.liquid < 1.0) per_kg += Sim::WATER_LATENT_FUSION * (1.0 - b.liquid);
	const double kg = minf(b.mass, energy / per_kg);
	b.mass -= kg;
	g_last_used = kg * per_kg;
	return kg;
}

double vapor_energy(double kg) { return kg * (Sim::WATER_LATENT_VAPOR + Sim::WATER_C * (Sim::WATER_BOIL_C - Sim::AMBIENT_C)); }

double ambient_step(MatBody& b, double dt) {
	const double k = Sim::AMBIENT_LOSS * std::pow(maxf(b.mass, 0.01), 2.0 / 3.0);
	if (b.mat == Mat::Stone) {
		const double over = b.temp - Sim::AMBIENT_C;
		if (over <= 0.5 && b.liquid <= 0.0) return 0.0;
		return stone(b, -k * over / 100.0 * dt);
	}
	if (b.mat == Mat::Water) {
		if (b.liquid < 1.0 || b.temp < Sim::AMBIENT_C - 0.5) {
			const double gain = k * ICE_AMBIENT_MULT * (Sim::AMBIENT_C - minf(b.temp, 0.0)) / 100.0 * dt;
			return water(b, gain);
		}
		if (b.temp > Sim::AMBIENT_C + 0.5) return water(b, -k * (b.temp - Sim::AMBIENT_C) / 100.0 * dt);
		return 0.0;
	}
	if (Materials::is_fusible(b.mat)) {
		const double over_s = b.temp - Sim::AMBIENT_C;
		if (over_s <= 0.5 && b.liquid <= 0.0) return 0.0;
		return solid(b, -k * over_s / 100.0 * dt, Materials::c(b.mat), Materials::melt(b.mat), Materials::latent(b.mat),
		             Materials::max_temp(b.mat));
	}
	if (b.mat == Mat::Plant) {
		const double over_p = b.temp - Sim::AMBIENT_C;
		if (over_p <= 0.5) return 0.0;
		return sensible(b, -k * over_p / 100.0 * dt, Materials::c(b.mat), Materials::max_temp(b.mat));
	}
	if (b.mat == Mat::Fire && b.heat_payload > 0.0)
		return payload(b, -b.heat_payload * Materials::props(Mat::Fire).fire_decay * dt);
	return 0.0;
}

bool update_phase(MatBody& b) {
	const Phase old = b.phase;
	if (Materials::is_fusible(b.mat)) {
		switch (b.phase) {
			case Phase::Solid:
				if (b.liquid >= Sim::MOLTEN_UP) b.phase = Phase::Molten;
				else if (b.liquid >= Sim::SOFTEN_UP) b.phase = Phase::Softened;
				break;
			case Phase::Softened:
				if (b.liquid >= Sim::MOLTEN_UP) b.phase = Phase::Molten;
				else if (b.liquid <= Sim::SOLID_DOWN) b.phase = Phase::Solid;
				break;
			case Phase::Molten:
				if (b.liquid <= Sim::SOLID_DOWN) b.phase = Phase::Solid;
				else if (b.liquid <= Sim::MOLTEN_DOWN) b.phase = Phase::Softened;
				break;
			default: b.phase = Phase::Solid; break;
		}
	} else if (b.mat == Mat::Water) {
		switch (b.phase) {
			case Phase::Liquid:
				if (b.liquid <= Sim::ICE_DOWN) b.phase = Phase::Frozen;
				break;
			case Phase::Frozen:
				if (b.liquid >= Sim::ICE_UP) b.phase = Phase::Liquid;
				break;
			default: b.phase = Phase::Liquid; break;
		}
	}
	return old != b.phase;
}

double flow_factor(const MatBody& b) { return smoothstep(0.05, 0.85, b.liquid); }

double heat01(const MatBody& b) {
	if (b.mat == Mat::Stone) return clampf((b.temp - 250.0) / (Sim::STONE_MELT_C - 250.0), 0.0, 1.0);
	if (Materials::is_fusible(b.mat)) return clampf((b.temp - 250.0) / (Materials::melt(b.mat) - 250.0), 0.0, 1.0);
	return 0.0;
}

}  // namespace Thermal
}  // namespace ff
