// Fourfold core - facade link test: uses ONLY Public/ff (as the Unreal modules do) and links the hidden-visibility
// shared library ff_core_shared. Every facade symbol must be exported (FOURFOLDCORE_API) or this fails to link -
// exactly what the Mac editor's modular build would hit.
#include "ff/FourfoldCore.h"

#include <cstdio>
#include <string>
#include <vector>

int main() {
	std::string err;
	if (!ff::Session::DataOk(&err)) {
		std::printf("data error: %s\n", err.c_str());
		return 1;
	}
	ff::Value v;
	if (!ff::ParseJson("{\"a\":[1,2.5,{\"$v3\":[1,2,3]}]}", v)) return 1;
	const std::string back = ff::ToJson(v);

	ff::Session s;
	const std::vector<ff::PracticeItem> items = s.PracticeItems();
	if (!s.LoadScenario("lab")) return 1;
	ff::InputFrame in;
	for (int i = 0; i < 120; ++i) {
		in.move = ff::Vec2(0.0f, i < 60 ? 1.0f : 0.0f);
		in.attack_pressed = (i == 70);
		in.attack_held = (i == 70);
		in.attack_released = (i == 71);
		s.Step(in, 0.0f);
		in.ClearEdges();
	}
	std::vector<ff::Event> events;
	s.TakeEvents(events);
	const ff::Snapshot& snap = s.GetSnapshot();
	const ff::HudModel hud = s.BuildHud();
	const ff::ArenaView& arena = s.Arena();
	const std::vector<std::string> dbg = s.DebugLines();
	const std::vector<ff::MoveInfo> moves = s.ListMoves(0, 0);
	const ff::MoveInfo mi = s.GetMove("earth_attack");
	const std::string rid = s.ResolveMove(0, 0, ff::Slot::Strike);
	s.SetRivalAi("adept", "mixed", -1);
	const std::string aid = s.AiDebug();
	ff::LabState lab = s.Lab();
	lab.time_scale = 0.5f;
	s.SetLab(lab);
	s.LabRequestStep(2);
	const int steps = s.TakeStepRequests();
	const std::vector<ff::LabSpawnEntry> spawn = s.LabSpawnEntries();
	std::string msg;
	if (!spawn.empty()) s.LabSpawn(spawn[0].id, ff::LabSpawnParams(), false, &msg);
	s.LabTry(0, 0, ff::Slot::Strike, 0);
	s.LabClear();
	s.LabHeal();
	const std::vector<ff::LabCombo> combos = s.LabCombos();
	if (!combos.empty()) s.LabStartCombo(combos[0].id, true);
	const ff::ComboTrackerView cv = s.LabComboState();
	s.LabStopCombo();
	const std::vector<ff::MatrixCounter> counters = s.LabMatrixCounters();
	ff::MatrixResult mr;
	if (!spawn.empty() && !counters.empty()) {
		mr = s.LabMatrixPredict(spawn[0].id, ff::LabSpawnParams(), counters[0].id, 1, false);
		s.LabMatrixStage(spawn[0].id, ff::LabSpawnParams(), counters[0].id);
	}
	const std::vector<ff::TuningField> tf = s.LabTuningFields("earth_attack");
	s.LabSetTuning("earth_attack", "damage", 9.0);
	const std::string tj = s.LabSaveTuning();
	s.LabClearTuning();
	s.LabLoadTuning(tj);
	s.LabClearTuning();
	const std::string pj = s.SaveProgress();
	s.LoadProgress(pj);
	s.ResetProgress();
	s.ResetScenario();
	std::printf("facade link ok: tick %lld, %zu events, %zu actors, %zu bodies, hud %d, arena %zu solids, %zu dbg, %zu moves (%s, %s), "
	            "ai '%s', %d step req, %zu spawn, %zu combos (%d), %zu counters (%s), %zu tuning, scale %.2f, %zu items, %s, %zu json\n",
	            static_cast<long long>(s.Tick()), events.size(), snap.actors.size(), snap.bodies.size(), hud.valid ? 1 : 0,
	            arena.solids.size(), dbg.size(), moves.size(), mi.name.c_str(), rid.c_str(), aid.substr(0, 40).c_str(), steps,
	            spawn.size(), combos.size(), cv.steps, counters.size(), mr.outcome.c_str(), tf.size(), static_cast<double>(s.TimeScale()),
	            items.size(), s.ScenarioId().c_str(), back.size() + pj.size());
	return 0;
}
