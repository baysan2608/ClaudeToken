// Fourfold core - code hooks referenced by name in the exported data ("fn:Class.method", 212 names):
//   move defs: hook_execute, hook_tick, hook_impact, counter_scale
//   rules.json: outcome_handlers, channel_hooks
//   hooks.json: body_ticks, zone_effects, tech_previews
// Every ported C++ function registers itself by its Godot name in the HookTable (one Register* function per kit
// file). The process-wide runtime registries (CombatWorld._body_ticks / _zone_effects / _tech_previews in Godot)
// are std::function maps so tests can add lambdas. Unresolved names fall back to the verb's default behaviour and
// are listed by Hooks::unresolved() (test + docs/core/PORT_STATUS.md).
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;
class Agent;
struct ActionInst;
struct ActorIntent;
struct IxResult;
struct IxCtx;

using ExecHook = bool (*)(CombatWorld&, ActorState&, ActionInst&);
using TickHook = void (*)(CombatWorld&, ActorState&, ActionInst&, const ActorIntent&);
using ImpactHook = bool (*)(CombatWorld&, MatBody&, const std::string&);
using CounterScaleHook = double (*)(CombatWorld&, ActorState&, int);
using OutcomeHook = bool (*)(CombatWorld&, Agent&, Agent&, IxResult&, const Dict&, IxCtx&);
using ChannelHook = void (*)(CombatWorld&, MatBody&, Agent&);
using BodyTickHook = bool (*)(CombatWorld&, MatBody&, double);
using ZoneHook = void (*)(CombatWorld&, MatBody&, double);
using PreviewHook = Dict (*)(CombatWorld&, ActorState&, Vec3);

struct HookTable {
	std::unordered_map<std::string, ExecHook> exec;
	std::unordered_map<std::string, TickHook> tick;
	std::unordered_map<std::string, ImpactHook> impact;
	std::unordered_map<std::string, CounterScaleHook> counter_scale;
	std::unordered_map<std::string, OutcomeHook> outcome;
	std::unordered_map<std::string, ChannelHook> channel;
	std::unordered_map<std::string, BodyTickHook> body_tick;
	std::unordered_map<std::string, ZoneHook> zone;
	std::unordered_map<std::string, PreviewHook> preview;
};

using BodyTickFn = std::function<bool(CombatWorld&, MatBody&, double)>;
using ZoneEffectFn = std::function<void(CombatWorld&, MatBody&, double)>;
using TechPreviewFn = std::function<Dict(CombatWorld&, ActorState&, Vec3)>;

namespace Hooks {

const HookTable& table();
void ensure();     // builds the runtime registries from hooks.json (idempotent; Moves::ensure calls it)

// Runtime registries (process-wide, like Godot's static vars).
std::map<std::string, BodyTickFn>& body_ticks();
std::map<std::string, ZoneEffectFn>& zone_effects();
std::map<std::string, TechPreviewFn>& tech_previews();
void register_body_tick(const std::string& tag, BodyTickFn cb);
void register_zone_effect(const std::string& tag, ZoneEffectFn cb);
void register_tech_preview(int element, int sub, TechPreviewFn cb);
void unregister_hooks(const std::vector<std::string>& tags);

// Def-level hooks: v is the def value ("fn:Class.method"); nullptr when absent or not ported.
ExecHook exec_of(const Value& v);
TickHook tick_of(const Value& v);
ImpactHook impact_of(const Value& v);
CounterScaleHook counter_scale_of(const Value& v);

// Every "Class.method" name referenced by the data that has no C++ function, with its kind ("hook_execute" ...).
std::vector<std::pair<std::string, std::string>> unresolved();
// Every referenced name with its kind (for docs / tests).
std::vector<std::pair<std::string, std::string>> referenced();

}  // namespace Hooks

// Registration entry points (one per kit file group; each adds its functions by Godot name).
void RegisterCoreHooks(HookTable& t);
void RegisterEarthHooks(HookTable& t);
void RegisterWaterHooks(HookTable& t);
void RegisterFireHooks(HookTable& t);
void RegisterAirHooks(HookTable& t);

}  // namespace ff
