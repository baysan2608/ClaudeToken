// Fourfold core - shared enums and constants (values identical to the Godot sim, game/core/sim.gd).
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (append-only: never renumber).
#pragma once

#include "ff/Config.h"

#include <cstdint>
#include <string_view>

namespace ff {

inline constexpr int kSimHz = 60;
inline constexpr double kSimDt = 1.0 / 60.0;

enum class Element : int8_t { Earth = 0, Water = 1, Fire = 2, Air = 3 };
// Sim.Gesture: ATTACK flick (thrust / ground / sweep) and GUARD flick (push / sink).
enum class Gesture : int8_t { None = 0, Up = 1, Down = 2, Side = 3 };
// Sim.Mat (lava = Stone in Phase::Molten; ice = Water in Phase::Frozen).
enum class Mat : int8_t { Stone = 0, Water = 1, Steam = 2, Metal = 3, Sand = 4, Glass = 5, Plant = 6, Fire = 7, Air = 8 };
enum class Phase : int8_t { Solid = 0, Softened = 1, Molten = 2, Liquid = 3, Frozen = 4, Gas = 5 };
enum class Form : int8_t { Chunk = 0, Blob = 1, Wave = 2, Wall = 3, Stream = 4, Shard = 5, Puddle = 6, Pool = 7, Cloud = 8, Zone = 9 };
// ActionInst.P
enum class ActionPhase : int8_t { Startup = 0, Charge = 1, Channel = 2, Active = 3, Recovery = 4, Done = 5 };
// Sim.SLOTS order.
enum class Slot : int8_t { Strike = 0, Thrust, Ground, Sweep, Guard, Push, Sink, Tech, Evade, EvadeHold, None };

inline constexpr int kNumElements = 4;
inline constexpr int kNumSubs = 4;
inline constexpr int kNumSlots = 10;

inline constexpr std::string_view kElementNames[kNumElements] = {"Earth", "Water", "Fire", "Air"};
inline constexpr std::string_view kSubNames[kNumElements][kNumSubs] = {
	{"Stone", "Metal", "Sand", "Magma"},
	{"Water", "Ice", "Mist", "Plant"},
	{"Flame", "Blue", "Lightning", "Combustion"},
	{"Gust", "Vortex", "Vacuum", "Sound"},
};
inline constexpr std::string_view kSlotNames[kNumSlots] = {"strike", "thrust", "ground", "sweep", "guard", "push", "sink",
                                                           "tech", "evade", "evade_hold"};
inline constexpr std::string_view kMatNames[] = {"stone", "water", "steam", "metal", "sand", "glass", "plant", "fire", "air"};
inline constexpr std::string_view kPhaseNames[] = {"solid", "softened", "molten", "liquid", "frozen", "gas"};
inline constexpr std::string_view kFormNames[] = {"chunk", "blob", "wave", "wall", "stream", "shard", "puddle", "pool", "cloud", "zone"};
inline constexpr std::string_view kActionPhaseNames[] = {"startup", "charge", "channel", "active", "recovery", "done"};

inline std::string_view SlotName(Slot s) {
	const int i = static_cast<int>(s);
	return (i >= 0 && i < kNumSlots) ? kSlotNames[i] : std::string_view("");
}
inline Slot SlotFromName(std::string_view n) {
	for (int i = 0; i < kNumSlots; ++i)
		if (kSlotNames[i] == n) return static_cast<Slot>(i);
	return Slot::None;
}
inline std::string_view ElementName(int e) { return (e >= 0 && e < kNumElements) ? kElementNames[e] : std::string_view(""); }
inline std::string_view SubName(int e, int s) {
	return (e >= 0 && e < kNumElements && s >= 0 && s < kNumSubs) ? kSubNames[e][s] : std::string_view("");
}

}  // namespace ff
