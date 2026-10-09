// Fourfold core - ff::Session: the facade the Unreal modules use. Port of the engine-free parts of game/game.gd
// (scenario load, the 60 Hz tick with PlayerController / rival AI / Lab scripts, KO and round reset, practice dummies,
// the boulder launcher, lava vents, mastery challenges, Lab actions) plus game/actors/autoplay.gd's duel / soak modes.
#include "ff/Session.h"

#include "AI/AiBrain.h"
#include "App/HudBuilder.h"
#include "App/PlayerController.h"
#include "App/Progression.h"
#include "App/Scenarios.h"
#include "App/SessionCore.h"
#include "App/SnapshotBuilder.h"
#include "Combat/Acts.h"
#include "Combat/Moves.h"
#include "Data/GameData.h"
#include "Lab/LabCombos.h"
#include "Lab/LabScript.h"
#include "Lab/LabSession.h"
#include "Lab/LabTuning.h"
#include "Lab/MatrixQuery.h"
#include "Lab/MoveListData.h"
#include "Lab/SpawnCatalog.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <cmath>
#include <map>

namespace ff {

namespace {
Dict ss_params(const LabSpawnParams& p) {
	Dict d;
	if (p.mass >= 0.0f) d.set("mass", static_cast<double>(p.mass));
	if (p.speed >= 0.0f) d.set("speed", static_cast<double>(p.speed));
	if (p.temp >= 0.0f) d.set("temp", static_cast<double>(p.temp));
	if (p.tier >= 0) d.set("tier", p.tier);
	return d;
}

LabParamSpec ss_spec(const Dict& specs, const char* k) {
	LabParamSpec s;
	if (!specs.has(k)) return s;
	const Array a = specs.get(k).as_array();
	s.present = true;
	s.def = static_cast<float>(vnum(a.get(0)));
	s.min = static_cast<float>(vnum(a.get(1)));
	s.max = static_cast<float>(vnum(a.get(2)));
	return s;
}
}  // namespace

// The state and tick logic live in SessionCore (App/SessionCore.h) so CoreTests can drive them directly.
struct Session::Impl : SessionCore {};

// ============================================================================ Session

Session::Session(const SessionConfig& cfg) : impl_(std::make_unique<Impl>()) {
	impl_->cfg = cfg;
	impl_->seed = cfg.seed;
	Moves::ensure_ready();
	impl_->arena = SnapshotBuilder::arena(CombatWorld(cfg.seed));
}

Session::~Session() = default;

bool Session::DataOk(std::string* error) {
	const bool ok = GameData::ok();
	if (error) *error = GameData::error();
	if (ok) Moves::ensure_ready();
	return ok;
}

std::vector<PracticeItem> Session::PracticeItems() const { return Scenarios::practice_items(impl_->progress); }

bool Session::LoadScenario(const std::string& id, const ScenarioOptions& opts) {
	Impl& m = *impl_;
	if (id == "__lab_mode") {
		SetLabMode(!m.progress.lab_mode);
		m.toast(std::string("Lab mode ") + (m.progress.lab_mode ? "on" : "off"));
		return true;
	}
	if (!Scenarios::has(id)) return false;
	m.opts = opts;
	if (Scenarios::is_spar_difficulty(opts.spar_difficulty)) m.progress.spar_difficulty = opts.spar_difficulty;
	if (!opts.spar_kit.empty() && (Scenarios::is_spar_kit(opts.spar_kit) || opts.spar_kit.find('/') != std::string::npos))
		m.progress.spar_kit = opts.spar_kit;
	m.autoplay = opts.autoplay;
	m.auto_progress = Progression();
	m.auto_progress.spar_difficulty = m.progress.spar_difficulty;
	m.auto_progress.spar_kit = m.progress.spar_kit;
	m.auto_t = 0.0;
	m.soak_next = 0.0;
	m.auto_f = InputFrame();
	m.load_scenario(id);
	return true;
}

void Session::ResetScenario() {
	if (impl_->scenario_id.empty()) return;
	if (impl_->roam) {
		impl_->load_roam(impl_->roam, impl_->roam_opts);
		return;
	}
	impl_->load_scenario(impl_->scenario_id);
}

const std::string& Session::ScenarioId() const { return impl_->scenario_id; }

void Session::Step(const InputFrame& player_input, float camera_yaw) { impl_->step(player_input, camera_yaw); }

int64_t Session::Tick() const { return impl_->world ? impl_->world->tick : 0; }
float Session::TimeScale() const { return static_cast<float>(impl_->lab.time_scale); }
bool Session::Frozen() const { return impl_->lab.frozen; }

int Session::TakeStepRequests() {
	const int n = impl_->lab._steps;
	impl_->lab._steps = 0;
	return n;
}

const Snapshot& Session::GetSnapshot() const { return impl_->snap; }

void Session::TakeEvents(std::vector<Event>& out) {
	for (Event& e : impl_->events) out.push_back(std::move(e));
	impl_->events.clear();
}

const ArenaView& Session::Arena() const { return impl_->arena; }

HudModel Session::BuildHud() const {
	Impl& m = *impl_;
	HudBuilder::Context c;
	c.world = m.world.get();
	c.player = m.player;
	c.player_intent = &m.player_intent;
	c.scenario_title = dstr(m.scen_def, "title");
	c.objective = dstr(m.scen_def, "objective");
	c.challenge_text = m.challenge_text;
	return HudBuilder::build(c);
}

std::vector<std::string> Session::DebugLines() const {
	if (!impl_->world) return {};
	return HudBuilder::debug_lines(*impl_->world, impl_->scenario_id, impl_->ai ? impl_->ai->debug_state : std::string());
}

std::vector<MoveInfo> Session::ListMoves(int element, int sub) const {
	std::vector<MoveInfo> out;
	for (int s = 0; s < kNumSlots; ++s) {
		const std::string id = Moves::resolve(element, sub, std::string(SlotName(static_cast<Slot>(s))));
		if (id.empty() || !Moves::defs().has(id)) continue;
		MoveInfo mi = MoveListData::move_info(id);
		mi.slot = static_cast<Slot>(s);
		out.push_back(mi);
	}
	return out;
}

MoveInfo Session::GetMove(const std::string& id) const { return MoveListData::move_info(id); }

std::string Session::ResolveMove(int element, int sub, Slot slot) const { return Moves::resolve(element, sub, std::string(SlotName(slot))); }

void Session::SetRivalAi(const std::string& preset, const std::string& kit, int sub) {
	Impl& m = *impl_;
	m.lab.ai_preset = preset;
	m.lab.ai_kit = kit;
	m.lab.ai_sub = sub;
	m.lab_apply_ai();
}

std::string Session::AiDebug() const { return impl_->ai ? impl_->ai->debug_state : std::string(); }

const LabState& Session::Lab() const {
	impl_->lab_view = impl_->lab.to_state();
	return impl_->lab_view;
}

void Session::SetLab(const LabState& s) {
	Impl& m = *impl_;
	const bool ai_changed = s.ai_preset != m.lab.ai_preset || s.ai_kit != m.lab.ai_kit || s.ai_sub != m.lab.ai_sub || s.ai_drill != m.lab.ai_drill;
	m.lab.from_state(s);
	if (ai_changed) m.lab_apply_ai();
}

void Session::LabRequestStep(int n) { impl_->lab.request_step(n); }

std::vector<LabSpawnEntry> Session::LabSpawnEntries() const {
	std::vector<LabSpawnEntry> out;
	for (const Dict& e : SpawnCatalog::entries()) {
		LabSpawnEntry v;
		v.id = dstr(e, "id");
		v.label = dstr(e, "label");
		v.group = dstr(e, "group");
		v.build = dstr(e, "build");
		v.inert_only = dbool(e, "inert_only");
		v.launch_only = dbool(e, "launch_only");
		const Dict specs = SpawnCatalog::param_specs(e);
		v.mass = ss_spec(specs, "mass");
		v.speed = ss_spec(specs, "speed");
		v.temp = ss_spec(specs, "temp");
		v.tier = ss_spec(specs, "tier");
		v.summary = SpawnCatalog::describe(e, Dict());
		out.push_back(v);
	}
	return out;
}

bool Session::LabSpawn(const std::string& entry_id, const LabSpawnParams& params, bool launch, std::string* message) {
	if (!impl_->world) return false;
	const bool ok = impl_->lab_spawn(entry_id, ss_params(params), launch, message);
	impl_->rebuild_snapshot();
	return ok;
}

void Session::LabTry(int element, int sub, Slot slot, int tier) {
	impl_->player_script = std::make_unique<LabScript>(LabScript::for_move(element, sub, std::string(SlotName(slot)), tier));
}

void Session::LabClear() {
	if (!impl_->world) return;
	impl_->lab_clear();
	impl_->rebuild_snapshot();
}

void Session::LabHeal() {
	if (!impl_->world) return;
	impl_->lab_heal();
	impl_->rebuild_snapshot();
}

std::vector<LabCombo> Session::LabCombos() const {
	std::vector<LabCombo> out;
	for (const Dict& c : LabCombos::all()) out.push_back(LabCombos::to_view(c));
	return out;
}

void Session::LabStartCombo(const std::string& id, bool demo) {
	if (!impl_->world) return;
	impl_->combo_demo = demo;
	if (demo) impl_->lab_demo_combo(id);
	else impl_->lab_start_combo(id);
}

void Session::LabStopCombo() {
	impl_->tracker.stop();
	impl_->combo_demo = false;
}

ComboTrackerView Session::LabComboState() const {
	const ComboTracker& t = impl_->tracker;
	ComboTrackerView v;
	v.active = t.state != ComboTracker::State::Idle;
	v.demo = impl_->combo_demo;
	v.success = t.state == ComboTracker::State::Success;
	v.failed = t.state == ComboTracker::State::Failed || (!t.fail_reason.empty() && t.state == ComboTracker::State::Running && t.step == 0);
	v.combo_id = dstr(t.combo, "id");
	v.message = t.status_text();
	if (!t.fail_reason.empty() && t.state == ComboTracker::State::Running) v.message = t.fail_reason + " | " + v.message;
	v.step = static_cast<int>(t.step);
	v.steps = static_cast<int>(t.steps().size());
	v.time_left = static_cast<float>(t.time_left());
	return v;
}

std::vector<MatrixCounter> Session::LabMatrixCounters() const {
	std::vector<MatrixCounter> out;
	for (const Dict& c : MatrixQuery::counters()) out.push_back(MatrixQuery::to_view(c));
	return out;
}

MatrixResult Session::LabMatrixPredict(const std::string& threat_entry, const LabSpawnParams& params, const std::string& counter_id,
                                       int counter_tier, bool perfect) const {
	return MatrixQuery::result_view(MatrixQuery::predict(threat_entry, ss_params(params), counter_id, counter_tier, perfect));
}

void Session::LabMatrixStage(const std::string& threat_entry, const LabSpawnParams& params, const std::string& counter_id) {
	Impl& m = *impl_;
	if (!m.world) return;
	const Dict c = MatrixQuery::find_counter(counter_id);
	const int el = dint(c, "element", -1);
	if (el >= 0) {
		m.player_script = std::make_unique<LabScript>();
		m.player_script->frames.push_back(D({{"element_select", el}, {"sub_select", dint(c, "sub", 0)}}));
	}
	m.lab_spawn(threat_entry, ss_params(params), true);
	m.rebuild_snapshot();
}

std::vector<TuningField> Session::LabTuningFields(const std::string& move_id) const {
	std::vector<TuningField> out;
	for (const LabTuning::Field& f : LabTuning::numeric_fields(move_id)) {
		TuningField t;
		t.move_id = move_id;
		t.key = f.path;
		t.value = f.value;
		t.original = vnum(LabTuning::original_value(move_id, f.path), f.value);
		t.overridden = LabTuning::is_changed(move_id, f.path);
		out.push_back(t);
	}
	return out;
}

void Session::LabSetTuning(const std::string& move_id, const std::string& key, double value) { LabTuning::set_value(move_id, key, value); }
void Session::LabClearTuning() { LabTuning::reset_all(); }
std::string Session::LabSaveTuning() const { return LabTuning::save_json(); }
bool Session::LabLoadTuning(const std::string& json) { return LabTuning::load_json(json) >= 0; }

std::string Session::SaveProgress() const { return impl_->progress.to_json(); }

bool Session::LoadProgress(const std::string& json) { return impl_->progress.from_json(json); }

void Session::ResetProgress() {
	impl_->progress.reset();
	if (!impl_->scenario_id.empty()) impl_->load_scenario(impl_->scenario_id);
}

void Session::SetLabMode(bool on) {
	Impl& m = *impl_;
	m.progress.lab_mode = on;
	if (!m.scenario_id.empty()) m.load_scenario(m.scenario_id);
}

bool Session::LabMode() const { return impl_->progress.lab_mode; }

std::vector<RuleCellInfo> Session::LabRuleCells() const {
	std::vector<RuleCellInfo> out;
	for (const LabTuning::RuleCell& c : LabTuning::rule_cells()) {
		RuleCellInfo r;
		r.key = c.key;
		r.idx = c.idx;
		r.id = c.id;
		r.label = c.label;
		out.push_back(r);
	}
	return out;
}

std::vector<TuningField> Session::LabRuleFields(const std::string& key, int idx) const {
	std::vector<TuningField> out;
	for (const LabTuning::Field& f : LabTuning::rule_fields(key, idx)) {
		TuningField t;
		t.move_id = key + "#" + itos(idx);
		t.key = f.path;
		t.value = f.value;
		t.original = vnum(LabTuning::rule_original(key, idx, f.path), f.value);
		t.overridden = LabTuning::rule_changed(key, idx, f.path);
		out.push_back(t);
	}
	return out;
}

bool Session::LabSetRuleValue(const std::string& key, int idx, const std::string& field, double value) {
	return LabTuning::set_rule_value(key, idx, field, value);
}

std::string Session::MoveInputText(Slot slot, const std::string& device) const { return MoveListData::input_text(std::string(SlotName(slot)), device); }
std::string Session::MoveCostText(const std::string& id) const { return MoveListData::cost_text(Moves::defs().get(id).as_dict()); }
std::string Session::MoveFramesText(const std::string& id) const { return MoveListData::frames_text(Moves::defs().get(id).as_dict()); }

void Session::SetSparOptions(const std::string& difficulty, const std::string& kit) {
	Impl& m = *impl_;
	if (Scenarios::is_spar_difficulty(difficulty)) m.progress.spar_difficulty = difficulty;
	if (Scenarios::is_spar_kit(kit) || kit.find('/') != std::string::npos) m.progress.spar_kit = kit;
}

bool Session::LoadRoam(std::shared_ptr<const WorldDef> world, const RoamOptions& opts) {
	if (!world || world->nx < 2 || world->nz < 2) return false;
	Impl& m = *impl_;
	m.opts = ScenarioOptions();
	m.opts.autoplay = opts.autoplay;
	m.autoplay = opts.autoplay;
	m.auto_progress = Progression();
	m.auto_t = 0.0;
	m.soak_next = 0.0;
	m.auto_f = InputFrame();
	m.load_roam(std::move(world), opts);
	return true;
}

RoamView Session::Roam() const { return impl_->roam_view(); }

}  // namespace ff
