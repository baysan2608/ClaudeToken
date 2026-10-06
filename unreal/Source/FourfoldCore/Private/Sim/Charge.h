// Fourfold core - port of game/core/charge.gd: unified charge tiers (MOVESET §4). T0 tap, T1 at heavy_min (0.40 s),
// T2 1.00 s, T3 1.80 s (per-move tier_times). Focus drains at charge_drain/s from T1 on for moves with t2/t3 data; an
// empty pool stalls the tier (one `insufficient` event), never drops it. Tier data: def.tiers {t1, t2, t3}; param()
// reads the reached tier, then lower tiers, then the def, then the default.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"

#include <array>
#include <string>
#include <string_view>

namespace ff {

class CombatWorld;
class ActorState;
struct ActionInst;
struct ActorIntent;

namespace Charge {

inline constexpr double TIER_TIMES[3] = {0.40, 1.00, 1.80};
inline constexpr double DEFAULT_DRAIN = 8.0;

Dict pdef(const ActionInst& inst);
int max_tier(const Dict& def);
std::array<double, 3> tier_times(const Dict& def);
int tier_for(const Dict& def, double held_s);
Value param(const ActionInst& inst, std::string_view key, const Value& def = Value());
Value pget(const Dict& def, int tier, std::string_view key, const Value& dflt = Value());
double counter_power(const Dict& def, int tier);
bool held(const ActionInst& inst, const ActorIntent& it);
void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
Vec2 progress(const ActionInst& inst);   // (tier, fraction to the next tier)

// Typed shortcuts: float(Charge.param(inst, k, d)) etc.
double paramf(const ActionInst& inst, std::string_view key, double def);
int parami(const ActionInst& inst, std::string_view key, int def);
bool paramb(const ActionInst& inst, std::string_view key, bool def);
std::string params(const ActionInst& inst, std::string_view key, std::string_view def);
double pgetf(const Dict& def, int tier, std::string_view key, double dflt);

}  // namespace Charge
}  // namespace ff
