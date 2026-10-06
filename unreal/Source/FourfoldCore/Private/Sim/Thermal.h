// Fourfold core - port of game/core/thermal.gd: bounded thermal transitions on MatBody. heat() returns the HU
// actually absorbed (or removed, negative) so callers charge exact costs and the ledger stays balanced.
#pragma once

namespace ff {

class MatBody;

namespace Thermal {

inline constexpr double STONE_MAX_C = 1350.0;
inline constexpr double ICE_MIN_C = -20.0;
inline constexpr double ICE_AMBIENT_MULT = 6.0;

// Vapour (kg) produced by the last heat() call and HU used by the last boil() call (64-bit, like Godot).
double last_vapor();
double last_used();

double heat(MatBody& b, double energy);
double boil(MatBody& b, double energy);
double vapor_energy(double kg);
double ambient_step(MatBody& b, double dt);
bool update_phase(MatBody& b);
double flow_factor(const MatBody& b);
double heat01(const MatBody& b);

// Internal pieces (kits call the solid model directly in a few places).
double stone(MatBody& b, double e);
double solid(MatBody& b, double e, double sc, double melt_c, double lat, double max_c);
double sensible(MatBody& b, double e, double sc, double max_c);
double payload(MatBody& b, double e);
double water(MatBody& b, double e);

}  // namespace Thermal
}  // namespace ff
