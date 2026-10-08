// Fourfold core - view models built by the Session for the UE HUD / touch overlay / menus / Lab panel.
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive only).
// Ports of Game._hud_context / attack_ring_context / gesture_petals / guard_petals / shape_label /
// charge_ring_context / _tech_context (game/game.gd), Scenarios.practice_items, LabSession, SpawnCatalog,
// LabCombos / ComboTracker, MatrixQuery, LabTuning, MoveListData (game/ui/lab/*).
#pragma once

#include "ff/Config.h"
#include "ff/Math.h"
#include "ff/Snapshot.h"
#include "ff/Types.h"

#include <array>
#include <string>
#include <vector>

namespace ff {

// ------------------------------------------------------------------ HUD / touch overlay
// One answer to the incoming threat (context counters on the GUARD button, docs/game/CONTROLS_HUD_PLAN.md part A).
struct CounterHintView {
	std::string slot;        // "guard" | "push" | "sink" | "tech" | one attack slot: "thrust" | "ground" | "sweep" | "strike"
	std::string label;       // the answer as a verb: "Send back", "Sink", "Melt", "Ground", ... ("No effect" for a pass)
	std::string move;        // short name of the move that gives it
	std::string outcome;     // the counter rule's outcome (block, reflect, transform, ...)
	std::string band;        // "full" | "partial" | "fail" | "none" (pass / no effect)
	int tier = 0;            // lowest charge tier that reaches this band
	bool perfect = false;    // the band needs a perfect guard
};

// The counter rule's outcome as a short verb for HUD text ("reflect" -> "Send back", "transform" + to "lava" -> "To lava",
// kit outcomes "water_freeze" -> "Freeze"); "" for a pass (nothing happened).
FOURFOLDCORE_API std::string CounterOutcomeLabel(const std::string& outcome, const std::string& to);
// A threat class as HUD text: "stone_heavy" -> "Heavy stone", "wall_stone" -> "Stone wall", "water_jet" -> "Water jet".
FOURFOLDCORE_API std::string ThreatLabel(const std::string& cls);

struct HudModel {
	bool valid = false;               // false when there is no player actor
	int player_id = -1;
	float health = 0.0f, balance = 0.0f, focus = 0.0f;
	float heat_reserve = 0.0f, water_carried = 0.0f, metal_carried = 0.0f, static_charge = 0.0f;
	bool show_heat = false, show_water = false, show_metal = false, show_static = false;   // only what matters now
	int element = 0, sub = 0;
	std::string element_name, sub_name;
	std::array<bool, 4> elements_unlocked{{true, true, true, true}};
	std::array<bool, 4> subs_unlocked{{true, true, true, true}};    // of the current element
	std::array<std::string, 4> sub_names;                            // of the current element (ring petals)
	std::vector<StatusView> statuses;

	// TECHNIQUE button: label (HEAT DRAW VENT GRIP WELL MAGNET ... / RELEASE) and legality; a held body
	std::string tech_label;
	bool tech_available = true;
	bool holding = false;

	// Gesture petals: names of the current sub-element's thrust (up) / ground (down) / sweep (side) and push / sink
	std::string petal_up, petal_down, petal_side;
	std::string guard_petal_up, guard_petal_down;
	std::string shape_label;          // what T+A does now ("" = no technique running)

	// ATTACK ring (follows the sim): seconds since the attack action started while its tap/hold decision or charge
	// runs (0 while the press waits in the buffer, -1 none), its element (-1 = the selected one) and decision time.
	float attack_charge = -1.0f;
	int attack_element = -1;
	float attack_decide = 0.0f;
	// Charge tiers of the running action (ring on the matching button) + the move's name for the centre bar.
	ChargeView charge;
	std::string charge_move_name;

	// Target marker (world position; the UI projects it) - a technique target body or the locked rival.
	bool has_marker = false;
	Vec3 marker_world;
	std::string marker_label;
	int target_id = -1;
	// Locked rival panel.
	bool has_rival = false;
	std::string rival_name;
	float rival_health = 0.0f, rival_balance = 0.0f, rival_focus = 0.0f;
	std::vector<StatusView> rival_statuses;

	// Scenario text.
	std::string scenario_title, objective, challenge_text;

	// Context counters: the most urgent threat coming at the player and the answers the current element / sub-element
	// can give it, predicted by the sim's own counter rule (no state change). has_threat = false when nothing is coming.
	bool has_threat = false;
	std::string threat_cls;
	float threat_tti = 0.0f;          // seconds to impact
	float perfect_window = 0.18f;     // a guard pressed this close before impact is perfect (Moves::PERFECT_WINDOW)
	Vec3 threat_world;
	std::vector<CounterHintView> counters;   // guard, push, sink, tech (those the sub-element has), then the best attack answer
};

// ------------------------------------------------------------------ menus
struct PracticeOption {
	std::string key, label;               // e.g. "spar_difficulty", "Rival"
	std::vector<std::string> values, labels;
	std::string value;
};
struct PracticeItem {
	std::string id, title, subtitle;
	bool locked = false;
	std::vector<PracticeOption> options;
};

struct MoveInfo {
	std::string id, name, desc;
	int element = 0, sub = 0;
	Slot slot = Slot::None;
	std::string verb;
	float startup = 0.0f, active = 0.0f, recovery = 0.0f;
	float cost_focus = 0.0f, cost_heat = 0.0f, cost_water = 0.0f, cost_metal = 0.0f;
	int max_tier = 0;
	std::vector<float> tier_times;        // seconds to reach T1..Tmax
	bool legacy = false;                  // a sub-0 legacy move (tests pin it)
	std::string counter_cls, threat_cls;
};

// ------------------------------------------------------------------ Lab (dev panel)
struct LabState {
	float time_scale = 1.0f;              // 0.1 / 0.25 / 0.5 / 1.0
	bool frozen = false;
	bool infinite = false;                // infinite Focus / heat / water / metal
	bool god = false;
	bool overlay = false;                 // hitbox / zone / power debug overlay
	bool ai_enabled = true;
	std::string ai_preset = "adept";      // novice | adept | master
	std::string ai_kit = "mixed";         // mixed | earth | water | fire | air | all
	int ai_sub = -1;                      // >= 0: only that sub-element
	std::string ai_drill;                 // "" | passive | matrix
};

struct LabParamSpec {
	bool present = false;
	float def = 0.0f, min = 0.0f, max = 0.0f;
};
struct LabSpawnEntry {
	std::string id, label, group, build;  // build: body | verb | zone | perform
	bool inert_only = false, launch_only = false;
	LabParamSpec mass, speed, temp, tier;
	std::string summary;                  // SpawnCatalog.describe at defaults
};
struct LabSpawnParams {                   // negative = entry default
	float mass = -1.0f, speed = -1.0f, temp = -1.0f;
	int tier = -1;
};

struct LabComboStep {
	std::string text;                     // human readable ("E/St G* Bulwark (perfect)")
	int element = 0, sub = 0;
	Slot slot = Slot::Strike;
	int tier = 0;
	std::string kind;                     // move | shape
	float hold = 0.0f, within = 4.0f;
	std::string hint;
};
struct LabCombo {
	std::string id, name, description, result_text;
	std::vector<LabComboStep> steps;
};
struct ComboTrackerView {
	bool active = false, demo = false, success = false, failed = false;
	std::string combo_id, message;
	int step = 0, steps = 0;
	float time_left = 0.0f;
};

struct MatrixCounter {
	std::string id, label, kind;          // kind: move | env | legacy
	int max_tier = 0, element = -1, sub = 0;
	Slot slot = Slot::None;
};
struct MatrixResult {
	bool ok = false;
	std::string msg;
	float tp = 0.0f, cp = 0.0f, cp_eff = 0.0f, ratio = 0.0f, needs = 0.0f;
	bool perfect = false;
	std::string band, outcome, rule_id, to, threat_cls, counter_cls, summary;
};

struct TuningField {
	std::string move_id, key;             // any numeric field of a def (top level or "tiers.t2.mass")
	double value = 0.0, original = 0.0;
	bool overridden = false;
};

// ---- additions (core stream, additive)
// A counter-rule cell of the Lab tuning page (Session::LabRuleCells); key = "<threat class>|<counter class>".
struct RuleCellInfo {
	std::string key, id, label;
	int idx = 0;
};

}  // namespace ff
