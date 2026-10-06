// Fourfold core - port of game/core/interactions.gd: the counter rule (MOVESET §5, §15.3; COMBAT_SPEC E4).
// A threat Agent meets a counter Agent: threat power TP (rule-weighted channels) vs counter power CP (barrier mass x
// hardness or the move's tier power, x1.5 perfect, x eff). ratio = CP_eff / TP picks the band (full / partial / fail)
// or a custom band; the band picks the outcome; the outcome handler (Outcomes) applies it through the ledgers.
// Rules come from Data/rules.json (every core + kit cell in registration order); handlers and channel hooks are
// resolved by name through Hooks.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Sim/Agent.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;

// Interactions.predict / resolve result (GDScript dictionary with these keys; kit-specific keys live in `extra`).
struct IxResult {
	std::string outcome, band, rule_id, to;
	double ratio = 0.0, tp = 0.0, cp = 0.0, cp_eff = 0.0, eff = 1.0;
	bool perfect = false;
	Dict rule;
	// resolve()
	std::string result;              // actor-guard result ("block", "guard_break", "perfect", "deflect", "redirected", "")
	bool stopped = false;
	double pass_scale = 1.0;
	bool counter_broken = false;
	double knock_scale = 1.0;
	double heat_used = 0.0;
	double absorbed = 0.0;
	bool empty = false;              // GDScript `{}` (no resolution happened)
	Dict extra;                      // conduct, conduct_to, shielded, reflected, amp, ...
};

// resolve() context (GDScript ctx dictionary).
struct IxCtx {
	Dict info;                       // hit info (guard sites)
	AgentRef info_agent;             // info.agent
	ActorState* target = nullptr;
	bool continuous = false;
	std::string site;
	bool allow_redirect = false;
	std::function<void(double)> redirect_cb;
	Dict extra;
};

using OutcomeFn = std::function<bool(CombatWorld&, Agent&, Agent&, IxResult&, const Dict&, IxCtx&)>;
using ChannelFn = std::function<void(CombatWorld&, MatBody&, Agent&)>;

namespace Interactions {

inline constexpr double PLAIN_GUARD_CP = 10.0;
inline constexpr double WIND_GUARD_CP = 12.0;
inline constexpr double ENV_CP = 1.0e6;
inline constexpr double HU_PER_PU = 20.0;
inline constexpr int IX_EVENT_TICKS = 30;
inline constexpr int CONTACT_TICKS = 30;

void ensure_ready();
const Dict& DEFAULT_RULE();
const Dict& CLASH_RULE();
const Dict& PASS_RULE();
// hit_actor info.kind -> volume class; main channel per class.
std::string kind_class(std::string_view kind, std::string_view def = "blast");
std::string class_channel(std::string_view cls, std::string_view def = "P");

std::string classify(const MatBody& b);
std::string wall_class(const MatBody& b);
std::string counter_class(const MatBody& b, CombatWorld* w = nullptr);
bool is_barrier(CombatWorld& w, const MatBody& b);
std::string family(std::string_view cls);
std::string counter_family(std::string_view ccls);

void register_tag_class(const std::string& tag, const std::string& threat_cls, const std::string& counter_cls = "");
void register_channels(const std::string& tag, ChannelFn cb);
const ChannelFn* channel_hook(const std::string& tag);
void register_outcome(const std::string& nm, OutcomeFn cb);
const OutcomeFn* handler(const std::string& nm);

bool can_add(const std::string& threat_cls, const std::string& counter_cls, const Dict& rule);
bool add_rule(const std::string& threat_cls, const std::string& counter_cls, const Dict& rule);
void remove_rule(const std::string& threat_cls, const std::string& counter_cls, bool include_legacy = false);
Dict rule(std::string_view threat_cls, std::string_view counter_cls, int tier = -1, const Dict* fallback_rule = nullptr);
bool has_rule(std::string_view threat_cls, std::string_view counter_cls, int tier = -1);
const std::unordered_map<std::string, std::vector<Dict>>& all_rules();
std::vector<std::string> rule_keys_in_order();

// Snapshot / restore of every registry (tests).
struct State {
	std::unordered_map<std::string, std::vector<Dict>> rules;
	std::vector<std::string> order;
	std::map<std::string, OutcomeFn> handlers;
	std::unordered_map<std::string, std::string> tag_threat, tag_counter;
	std::map<std::string, ChannelFn> channels;
	int n = 0;
};
State save_state();
void load_state(const State& st);

bool allows(const MatBody& b, std::string_view counter_cls);
double cohesion(int tier);
double disrupt_threshold(int tier);
double threat_power(const Agent& threat, const Dict& r, std::string_view counter_fam = "");
double counter_power(const Agent& counter);
bool _cond(const Dict& cond, const Agent& threat);
IxResult predict(CombatWorld* w, const Agent& threat, const Agent& counter, const Dict* fallback_rule = nullptr);
IxResult resolve(CombatWorld& w, Agent& threat, Agent& counter, IxCtx ctx = IxCtx(), const Dict* fallback_rule = nullptr);
void _emit(CombatWorld& w, const Agent& threat, const Agent& counter, const IxResult& res, const IxCtx& ctx);

}  // namespace Interactions
}  // namespace ff
