// Fourfold core - port of game/core/sim.gd: shared enums (ff/Types.h) and tuning constants of the authoritative sim.
// Units: m, s, kg (gameplay scale), degrees C, HU (heat units), Focus / Balance 0..100.
#pragma once

#include "ff/Types.h"

#include <cmath>
#include <string>
#include <string_view>

namespace ff {
namespace Sim {

inline constexpr int HZ = 60;
inline constexpr double DT = 1.0 / 60.0;
inline constexpr double GRAVITY = 18.0;

inline constexpr int EARTH = 0, WATER = 1, FIRE = 2, AIR = 3;

inline constexpr const char* SLOTS[] = {"strike", "thrust", "ground", "sweep", "guard", "push", "sink", "tech", "evade", "evade_hold"};
inline constexpr const char* ATTACK_SLOTS[] = {"strike", "thrust", "ground", "sweep"};

// --- Thermal
inline constexpr double AMBIENT_C = 20.0;
inline constexpr double STONE_C = 0.01;
inline constexpr double STONE_MELT_C = 1000.0;
inline constexpr double STONE_LATENT = 10.0;
inline constexpr double WATER_C = 0.05;
inline constexpr double WATER_FREEZE_C = 0.0;
inline constexpr double WATER_BOIL_C = 100.0;
inline constexpr double WATER_LATENT_FUSION = 3.3;
inline constexpr double WATER_LATENT_VAPOR = 22.0;
inline constexpr double SOFTEN_UP = 0.15;
inline constexpr double MOLTEN_UP = 0.80;
inline constexpr double MOLTEN_DOWN = 0.55;
inline constexpr double SOLID_DOWN = 0.05;
inline constexpr double ICE_DOWN = 0.10;
inline constexpr double ICE_UP = 0.60;
inline constexpr double AMBIENT_LOSS = 0.6;
inline constexpr double HOLD_UPKEEP_FOCUS = 3.0;
inline constexpr double QUENCH_RATE = 1400.0;
inline constexpr double HOT_ROCK_C = 300.0;

// --- Actor resources
inline constexpr double FOCUS_MAX = 100.0;
inline constexpr double FOCUS_REGEN = 14.0;
inline constexpr double FOCUS_REGEN_DELAY = 0.7;
inline constexpr double HU_PER_FOCUS = 10.0;
inline constexpr double DRAW_HU_PER_FOCUS = 25.0;
inline constexpr double RESERVE_MAX = 500.0;
inline constexpr double RESERVE_DISSIPATE = 12.0;
inline constexpr double BALANCE_MAX = 100.0;
inline constexpr double BALANCE_REGEN = 22.0;
inline constexpr double BALANCE_REGEN_DELAY = 1.0;
inline constexpr double HEALTH_MAX = 100.0;

// --- Bodies
inline constexpr int MAX_BODIES = 32;
inline constexpr int MAX_PUDDLES = 8;
inline constexpr int MAX_REMNANTS = 6;
inline constexpr double REMNANT_LIFETIME = 45.0;
inline constexpr double STONE_SHOT_MASS = 20.0;
inline constexpr double STONE_HEAVY_MASS = 45.0;
inline constexpr double WALL_MASS = 120.0;

// --- Actor body
inline constexpr double ACTOR_RADIUS = 0.35;
inline constexpr double ACTOR_HEIGHT = 1.75;
inline constexpr double ACTOR_MASS = 70.0;
inline constexpr double STEP_HEIGHT = 0.4;
inline constexpr double WAVE_STEP = 0.3;

inline double stone_radius(double mass) { return 0.18 * std::pow((mass > 0.5 ? mass : 0.5) / 10.0, 1.0 / 3.0); }
inline double water_radius(double mass) { return 0.12 * std::pow((mass > 0.2 ? mass : 0.2) / 2.0, 1.0 / 3.0); }

inline bool is_attack_slot(std::string_view s) { return s == "strike" || s == "thrust" || s == "ground" || s == "sweep"; }
inline bool is_slot(std::string_view s) {
	for (const char* x : SLOTS)
		if (s == x) return true;
	return false;
}

inline const char* mat_name(Mat m) { return kMatNames[static_cast<int>(m)].data(); }
inline const char* form_name(Form f) { return kFormNames[static_cast<int>(f)].data(); }
inline const char* phase_name(Phase p) { return kPhaseNames[static_cast<int>(p)].data(); }
// Sim.MAT_NAMES.find(name): -1 when unknown.
inline int mat_index(std::string_view n) {
	for (int i = 0; i < 9; ++i)
		if (kMatNames[i] == n) return i;
	return -1;
}
inline int form_index(std::string_view n) {
	for (int i = 0; i < 10; ++i)
		if (kFormNames[i] == n) return i;
	return -1;
}

}  // namespace Sim
}  // namespace ff
