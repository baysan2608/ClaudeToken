// Facade tests: ff::Session as the Unreal modules drive it (Public/ff only, through the real input path).
#include "ff_test.h"

#include "ff/FourfoldCore.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace ff;
using fft::S;

namespace {
constexpr float kYaw = 3.14159265f;   // camera behind the player (spawned at z = 8 facing -z): looks along -z

struct FacadeFx : fft::TestCase {
	std::vector<Event> log;
	InputFrame in;

	void step(Session& s, int n = 1) {
		for (int i = 0; i < n; ++i) {
			s.Step(in, kYaw);
			in.ClearEdges();
		}
		s.TakeEvents(log);
	}
	bool has(const std::string& type, const std::function<bool(const Value&)>& pred = nullptr) const {
		for (const Event& e : log)
			if (e.type == type && (!pred || pred(e.data))) return true;
		return false;
	}
	int count(const std::string& type, const std::function<bool(const Value&)>& pred = nullptr) const {
		int n = 0;
		for (const Event& e : log)
			if (e.type == type && (!pred || pred(e.data))) ++n;
		return n;
	}
	const ActorView* player(const Session& s) const { return s.GetSnapshot().FindActor(s.GetSnapshot().player_id); }
	void release_all() {
		in = InputFrame();
	}
};
}  // namespace

FF_TEST_F(test_facade, FacadeFx, test_lab_loads_with_fighters_dummies_arena_and_hud) {
	std::string err;
	check(Session::DataOk(&err), "data ok " + err);
	Session s;
	check(s.LoadScenario("lab"), "lab loads");
	check(!s.LoadScenario("no_such_scenario"), "unknown scenario refused");
	check(s.ScenarioId() == "lab", "scenario id");
	s.TakeEvents(log);
	check(has("app_scenario_loaded", [](const Value& d) { return d.get("id") == "lab"; }), "app_scenario_loaded");
	const Snapshot& sn = s.GetSnapshot();
	check(sn.actors.size() == 5, S("player + rival + 3 dummies (", sn.actors.size(), ")"));
	const ActorView* p = player(s);
	check(p != nullptr && p->is_player && p->team == 0 && p->name == "You", "player view");
	const ActorView* r = sn.FindActor(sn.rival_id);
	check(r != nullptr && r->is_rival && !r->ai_controlled, "rival view (Lab AI off by default)");
	int dummies = 0;
	for (const ActorView& a : sn.actors) dummies += a.is_dummy ? 1 : 0;
	check(dummies == 3, "3 dummies");
	check(!s.Arena().solids.empty() && s.Arena().half_size > 10.0f, "arena view");
	const HudModel h = s.BuildHud();
	check(h.valid && h.element_name == "Earth" && h.sub_name == "Stone", "HUD element names");
	check(h.tech_label == "LIFT" && h.tech_available, "Earth/Stone technique label");
	check(!h.petal_up.empty() && !h.petal_down.empty() && !h.petal_side.empty(), S("gesture petals ", h.petal_up, "/", h.petal_down, "/", h.petal_side));
	check(!h.guard_petal_up.empty() && !h.guard_petal_down.empty(), "guard petals");
	check(h.attack_charge < 0.0f && !h.charge.active, "no attack ring at rest");
	step(s, 1);
	check(s.BuildHud().has_marker && s.BuildHud().target_id >= 0, "locked target marker after the first tick");
	check(!s.DebugLines().empty(), "debug lines");
	check(s.PracticeItems().size() == 13, S("practice items (", s.PracticeItems().size(), ")"));
	check(s.ResolveMove(0, 0, Slot::Strike) == "earth_attack" && s.GetMove("earth_attack").name == "Stone Shot", "move registry queries");
	check(s.ListMoves(2, 2).size() == 10, S("Fire / Lightning lists 10 slots (", s.ListMoves(2, 2).size(), ")"));
}

FF_TEST_F(test_facade, FacadeFx, test_strike_tap_charge_flick_guard_and_technique_through_the_input_path) {
	Session s;
	s.LoadScenario("lab");
	step(s, 30);
	// ---- strike tap
	in.attack_pressed = true;
	in.attack_held = true;
	s.Step(in, kYaw);
	in.ClearEdges();
	const ActorView* p = player(s);
	check(p->action.active && p->action.id == "earth_attack" && p->action.phase == ActionPhase::Startup, "attack starts on the press tick");
	check(std::fabs(p->action.startup - 0.24f) < 1e-5f && p->action.recovery > 0.0f, S("effective startup 0.24 (", p->action.startup, ")"));
	check(s.BuildHud().attack_charge >= 0.0f && s.BuildHud().attack_decide > 0.2f, "attack ring follows the sim");
	in.attack_held = false;
	in.attack_released = true;
	step(s, 70);
	check(has("action", [](const Value& d) { return d.get("move") == "earth_attack" && d.get("phase") == "startup"; }), "action startup event");
	check(has("launch", [](const Value& d) { return d.get("tier") == 0 && d.get("heavy") == false; }), "tap: a light stone shot");
	check(has("hit", [](const Value& d) { return d.get("attacker").as_int() == 1; }), "the shot hits a target");
	// ---- T3 charge
	log.clear();
	in.attack_pressed = true;
	in.attack_held = true;
	bool ring = false;
	for (int i = 0; i < 115; ++i) {
		s.Step(in, kYaw);
		in.ClearEdges();
		const HudModel h = s.BuildHud();
		if (h.charge.active && h.charge.button == "attack" && h.charge.max_tier == 3 && h.charge_move_name == "Stone Shot") ring = true;
	}
	s.TakeEvents(log);
	check(ring, "charge ring on the attack button");
	for (int t = 1; t <= 3; ++t)
		check(has("charge", [t](const Value& d) { return d.get("tier") == t && d.get("move") == "earth_attack"; }), S("charge tier ", t));
	in.attack_held = false;
	in.attack_released = true;
	step(s, 60);
	check(has("launch", [](const Value& d) { return d.get("tier") == 3 && d.get("heavy") == true; }), "T3 release: Crag Breaker");
	// ---- flick morph (ATTACK flick up = thrust)
	log.clear();
	in.attack_pressed = true;
	in.attack_held = true;
	in.attack_gesture = Gesture::Up;
	s.Step(in, kYaw);
	in.ClearEdges();
	in.attack_held = false;
	in.attack_released = true;
	step(s, 60);
	const std::string thrust = s.ResolveMove(0, 0, Slot::Thrust);
	check(has("action", [&](const Value& d) { return d.get("move") == thrust && d.get("slot") == "thrust"; }), "flick up plays the thrust move " + thrust);
	// ---- guard + perfect (the rival throws a stone at the player)
	log.clear();
	std::string msg;
	check(s.LabSpawn("stone_20", LabSpawnParams(), true, &msg), "Lab spawn: " + msg);
	int bid = -1;
	for (const BodyView& b : s.GetSnapshot().bodies)
		if (b.attack_id != 0) bid = b.id;
	check(bid >= 0, "a live shot");
	bool pressed = false;
	for (int i = 0; i < 120; ++i) {
		const BodyView* b = s.GetSnapshot().FindBody(bid);
		const ActorView* pp = player(s);
		if (!pressed && b != nullptr && (b->pos - (pp->pos + Vec3(0, 1.25f, 0))).length() < 2.0f) {
			in.guard_pressed = true;
			in.guard_held = true;
			pressed = true;
		}
		s.Step(in, kYaw);
		in.ClearEdges();
		if (i > 100) in.guard_held = false;
	}
	s.TakeEvents(log);
	check(pressed && has("guard"), "guard raised");
	check(has("perfect_deflect", [](const Value& d) { return d.get("verb") == "redirect"; }), "timed Earth guard redirects the stone");
	check(has("interaction", [](const Value& d) { return d.get("perfect") == true && d.get("outcome") == "redirect"; }), "interaction event (perfect)");
	// ---- technique hold / release
	release_all();
	step(s, 40);
	log.clear();
	in.tech_pressed = true;
	in.tech_held = true;
	bool held = false;
	for (int i = 0; i < 40; ++i) {
		s.Step(in, kYaw);
		in.ClearEdges();
		if (player(s)->held_body >= 0) held = true;
	}
	in.tech_held = false;
	in.tech_released = true;
	step(s, 60);
	check(has("action", [](const Value& d) { return d.get("move") == "earth_tech"; }), "technique action");
	check(held, "the technique seizes a stone");
	check(has("launch"), "release throws it");
}

FF_TEST_F(test_facade, FacadeFx, test_lab_try_combo_matrix_tuning_and_freeze) {
	Session s;
	s.LoadScenario("lab");
	s.LabTry(2, 0, Slot::Strike, 2);
	step(s, 160);
	check(has("action", [](const Value& d) { return d.get("move") == "fire_attack"; }), "Try plays Fire / Flame strike through the input path");
	check(has("charge", [](const Value& d) { return d.get("tier") == 2; }), "Try reaches T2");
	const auto combos = s.LabCombos();
	check(combos.size() == 28 && !combos[0].steps.empty(), "28 combos");
	s.LabStartCombo(combos[0].id, true);
	check(s.LabComboState().active && s.LabComboState().steps == static_cast<int>(combos[0].steps.size()), "combo tracker running");
	step(s, 30);
	check(has("app_combo", [](const Value& d) { return d.get("state") == "started"; }), "app_combo started");
	s.LabStopCombo();
	check(!s.LabComboState().active, "combo stopped");
	const MatrixResult mr = s.LabMatrixPredict("stone_20", LabSpawnParams(), "wall_stone", 0, false);
	check(mr.ok && mr.band == "full" && mr.outcome == "block" && mr.rule_id == "legacy_wall", "matrix predict (stone vs Bulwark)");
	const auto tf = s.LabTuningFields("earth_attack");
	check(!tf.empty(), "tuning fields");
	s.LabSetTuning("earth_attack", "damage", 99.0);
	check(s.GetMove("earth_attack").name == "Stone Shot", "tuning keeps the def");
	const std::string tj = s.LabSaveTuning();
	s.LabClearTuning();
	bool restored = false;
	for (const TuningField& f : s.LabTuningFields("earth_attack"))
		if (f.key == "damage") restored = std::fabs(f.value - 12.0) < 1e-9 && !f.overridden;
	check(restored, "clear restores 12");
	check(s.LabLoadTuning(tj), "load tuning json");
	bool loaded = false;
	for (const TuningField& f : s.LabTuningFields("earth_attack"))
		if (f.key == "damage") loaded = std::fabs(f.value - 99.0) < 1e-9 && f.overridden && std::fabs(f.original - 12.0) < 1e-9;
	check(loaded, "loaded override 99 (original 12)");
	s.LabClearTuning();
	const auto cells = s.LabRuleCells();
	check(cells.size() >= 1264, S("rule cells ", cells.size()));
	LabState ls = s.Lab();
	ls.frozen = true;
	ls.time_scale = 0.25f;
	s.SetLab(ls);
	check(s.Frozen() && std::fabs(s.TimeScale() - 0.25f) < 1e-6f, "freeze + time scale");
	s.LabRequestStep(3);
	check(s.TakeStepRequests() == 3 && s.TakeStepRequests() == 0, "frame-step requests");
}

FF_TEST_F(test_facade, FacadeFx, test_ko_reset_progression_and_lab_mode) {
	Session s;
	s.LoadScenario("spar");
	const int rival = s.GetSnapshot().rival_id;
	step(s, 5);
	log.clear();
	// KO: the rival drops to 0 HP -> knockdown, app_ko, round reset after 2.5 s.
	s.LabClear();
	LabState ls = s.Lab();
	ls.ai_enabled = false;
	s.SetLab(ls);
	// Damage through the Lab: a heavy shot is not guaranteed to KO, so drive health directly via repeated heals of the player only.
	for (int i = 0; i < 400 && !has("app_ko"); ++i) {
		s.LabSpawn("stone_200", LabSpawnParams(), true);
		step(s, 30);
	}
	(void)rival;
	check(has("app_ko"), "a KO happens under 200 kg stones");
	step(s, 200);
	check(has("app_round_reset", [](const Value& d) { return d.get("reason") == "ko"; }), "round reset after the knockdown");
	check(s.GetSnapshot().tick < 200, "fresh world after the reset");
	// Progression JSON.
	s.SetSparOptions("master", "fire/blue");
	const std::string pj = s.SaveProgress();
	Session s2;
	check(s2.LoadProgress(pj), "progress loads");
	check(s2.SaveProgress() == pj, "progress round-trips");
	check(s2.LoadScenario("spar"), "spar with saved options");
	bool kit_ok = false;
	for (const PracticeItem& it : s2.PracticeItems())
		if (it.id == "spar" && it.options.size() == 2) kit_ok = it.options[0].value == "master" && it.options[1].value == "fire/blue";
	check(kit_ok, "spar options persisted");
	check(!s2.LabMode(), "lab mode off");
	check(s2.LoadScenario("__lab_mode") && s2.LabMode(), "__lab_mode toggles");
	s2.ResetProgress();
	check(!s2.LabMode(), "reset progress");
}

FF_TEST_F(test_facade, FacadeFx, test_spar_duel_and_soak_run_long_without_faults) {
	for (const char* mode : {"", "duel", "soak"}) {
		Session s;
		ScenarioOptions o;
		o.autoplay = mode;
		check(s.LoadScenario("spar", o), S("spar ", mode));
		int64_t ticks = 0;
		for (int i = 0; i < 3600; ++i) {
			s.Step(in, kYaw);
			in.ClearEdges();
			std::vector<Event> ev;
			s.TakeEvents(ev);
			++ticks;
			const Snapshot& sn = s.GetSnapshot();
			for (const ActorView& a : sn.actors)
				if (!(a.pos.y > -5.0f && a.pos.y < 30.0f && std::fabs(a.pos.x) < 40.0f && std::fabs(a.pos.z) < 40.0f)) {
					check(false, S(mode, ": actor out of bounds at ", i));
					i = 3600;
				}
		}
		check(ticks == 3600, "3600 ticks");
		const HudModel h = s.BuildHud();
		check(h.valid, S(mode, ": HUD valid"));
		if (std::string(mode) == "duel") check(s.GetSnapshot().actors[0].ai_controlled, "duel: AI drives the player");
	}
}
