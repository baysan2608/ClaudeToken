// Fourfold core - THE facade the Unreal modules use (engine-free; implemented by stream `core`).
// FROZEN CONTRACT (architect): signatures below never change; the core stream may ADD methods.
//
// A Session owns one CombatWorld (the authoritative 60 Hz sim), the scenario logic of game/game.gd
// (scenario load, KO / round reset, practice dummies, launcher, mastery challenges), the rival AI (AiBrain +
// AiPlanner + presets), the Lab session (time scale, spawner, Try scripts, combos, matrix query, live tuning),
// PlayerController (InputFrame + camera yaw -> ActorIntent) and the view-model builders.
//
// Usage from UE (stream `game`, UFourfoldSimSubsystem):
//   ff::Session s;                                   // parses the embedded data once (Session::DataOk)
//   s.LoadScenario("lab");                           // or "spar" with ScenarioOptions
//   each frame: acc += dt * s.TimeScale(); while (acc >= kSimDt) { s.Step(input, camYawSim); acc -= kSimDt; }
//               (when s.Frozen(): step only s.TakeStepRequests() times)
//   s.TakeEvents(events); const ff::Snapshot& snap = s.GetSnapshot(); ff::HudModel hud = s.BuildHud();
// Interpolate visuals between the previous and current snapshot with alpha = acc / kSimDt.
//
// Session-level events (Event.type, data fields):
//   app_scenario_loaded {id, title}        app_round_reset {reason}      app_ko {actor}
//   app_toast {text, kind: info|mastery|warning}                          app_challenge {id, progress, count, done}
//   app_combo {id, state: started|step|success|fail, step}                app_lab {action, ok, message}
#pragma once

#include "ff/Config.h"
#include "ff/Events.h"
#include "ff/Input.h"
#include "ff/Snapshot.h"
#include "ff/Types.h"
#include "ff/ViewModels.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ff {

struct SessionConfig {
	uint64_t seed = 1;
	bool record_events = true;
};

struct ScenarioOptions {
	std::string spar_difficulty;          // "" = saved progress value; novice | adept | master
	std::string spar_kit;                 // "" = saved; mixed | earth | water | fire | air | all | "<element>/<sub>"
	std::string autoplay;                 // "" | "duel" (AI drives the player too: attract mode / soak) | "soak"
	int player_element = -1;              // -1 = scenario default
	int player_sub = -1;
};

class FOURFOLDCORE_API Session {
public:
	explicit Session(const SessionConfig& cfg = SessionConfig());
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;

	// True when the embedded data (moves / rules / scenarios / lab tables) parsed; error text otherwise.
	static bool DataOk(std::string* error = nullptr);

	// ---------------------------------------------------------------- scenarios / menus
	std::vector<PracticeItem> PracticeItems() const;          // Scenarios.practice_items (+ Spar options)
	bool LoadScenario(const std::string& id, const ScenarioOptions& opts = ScenarioOptions());
	void ResetScenario();                                     // reload the current scenario, same options
	const std::string& ScenarioId() const;

	// ---------------------------------------------------------------- stepping
	// Exactly one 60 Hz tick. camera_yaw: the camera looks along (sin yaw, 0, cos yaw) in sim space.
	void Step(const InputFrame& player_input, float camera_yaw);
	int64_t Tick() const;
	float TimeScale() const;                                  // Lab time scale (scale your accumulator)
	bool Frozen() const;                                      // Lab freeze
	int TakeStepRequests();                                   // Lab frame-step presses since the last call

	// ---------------------------------------------------------------- outputs
	const Snapshot& GetSnapshot() const;
	void TakeEvents(std::vector<Event>& out);                 // appends, then clears the internal queue
	const ArenaView& Arena() const;
	HudModel BuildHud() const;
	std::vector<std::string> DebugLines() const;

	// ---------------------------------------------------------------- move registry (move list, petals, Lab)
	std::vector<MoveInfo> ListMoves(int element, int sub) const;     // slot order, sub-0 fallbacks included
	MoveInfo GetMove(const std::string& id) const;                    // id empty when unknown
	std::string ResolveMove(int element, int sub, Slot slot) const;  // Moves.resolve

	// ---------------------------------------------------------------- rival AI
	void SetRivalAi(const std::string& preset, const std::string& kit, int sub = -1);
	std::string AiDebug() const;

	// ---------------------------------------------------------------- Lab (dev panel)
	const LabState& Lab() const;
	void SetLab(const LabState& s);
	void LabRequestStep(int n = 1);
	std::vector<LabSpawnEntry> LabSpawnEntries() const;
	bool LabSpawn(const std::string& entry_id, const LabSpawnParams& params, bool launch, std::string* message = nullptr);
	void LabTry(int element, int sub, Slot slot, int tier);           // plays it through the real input path
	void LabClear();                                                  // clears bodies / zones
	void LabHeal();                                                   // refills everyone
	std::vector<LabCombo> LabCombos() const;
	void LabStartCombo(const std::string& id, bool demo);
	void LabStopCombo();
	ComboTrackerView LabComboState() const;
	std::vector<MatrixCounter> LabMatrixCounters() const;
	MatrixResult LabMatrixPredict(const std::string& threat_entry, const LabSpawnParams& params,
	                              const std::string& counter_id, int counter_tier, bool perfect) const;
	void LabMatrixStage(const std::string& threat_entry, const LabSpawnParams& params, const std::string& counter_id);
	std::vector<TuningField> LabTuningFields(const std::string& move_id) const;
	void LabSetTuning(const std::string& move_id, const std::string& key, double value);
	void LabClearTuning();
	std::string LabSaveTuning() const;                                // JSON text
	bool LabLoadTuning(const std::string& json);

	// ---------------------------------------------------------------- progression (host persists the JSON text)
	std::string SaveProgress() const;
	bool LoadProgress(const std::string& json);
	void ResetProgress();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace ff
