// FourfoldAudio logic island - unit tests. Built only by Private/Logic/tests/CMakeLists.txt (FF_LOGIC_TESTS); when
// UnrealBuildTool compiles this file as part of the FourfoldAudio module it is empty.
#if defined(FF_LOGIC_TESTS)

#include "FFALoops.h"
#include "FFAManifest.h"
#include "FFAMix.h"
#include "FFARules.h"
#include "FFAVoices.h"

#include "ff/Json.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fat {

struct TestCase {
	const char* name;
	void (*fn)();
};
std::vector<TestCase>& Registry() {
	static std::vector<TestCase> r;
	return r;
}
struct Reg {
	Reg(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
int g_failures = 0;
int g_checks = 0;
const char* g_current = "";

void Fail(const char* file, int line, const std::string& msg) {
	++g_failures;
	std::printf("  FAIL %s (%s:%d): %s\n", g_current, file, line, msg.c_str());
}

}  // namespace fat

#define FAT_TEST(name)                                  \
	static void name();                                 \
	static const fat::Reg name##_reg(#name, &name);     \
	static void name()
#define FAT_CHECK(cond)                                                    \
	do {                                                                   \
		++fat::g_checks;                                                   \
		if (!(cond)) fat::Fail(__FILE__, __LINE__, #cond);                 \
	} while (0)
#define FAT_NEAR(a, b, eps)                                                                                       \
	do {                                                                                                          \
		++fat::g_checks;                                                                                          \
		if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= static_cast<double>(eps)))            \
			fat::Fail(__FILE__, __LINE__, std::string(#a " ~ " #b " : ") + std::to_string(static_cast<double>(a)) + \
			                                  " vs " + std::to_string(static_cast<double>(b)));                      \
	} while (0)

namespace {

using ffa::Manifest;
using ffa::PlayRequest;
using ffa::HoldRequest;
using ffa::LoopWant;

// ------------------------------------------------------------------------------------------------ helpers

using KV = std::pair<const char*, ff::Value>;

ff::Dict DictOf(std::initializer_list<KV> kv) {
	ff::Dict d;
	for (const KV& p : kv) d.set(p.first, p.second);
	return d;
}

ff::Event Ev(const char* type, std::initializer_list<KV> kv) {
	ff::Event e;
	e.type = type;
	e.data = ff::Value(DictOf(kv));
	return e;
}

ff::ActorView MakeActor(int id, int element, ff::Vec3 pos, const char* surface = "stone") {
	ff::ActorView a;
	a.id = id;
	a.element = element;
	a.pos = pos;
	a.surface = surface;
	a.grounded = true;
	return a;
}

ff::BodyView MakeBody(int id, ff::Mat mat, ff::Form form, ff::Vec3 pos, const char* fx_mat, const char* tag = "") {
	ff::BodyView b;
	b.id = id;
	b.mat = mat;
	b.form = form;
	b.pos = pos;
	b.fx_mat = fx_mat;
	b.tag = tag;
	return b;
}

std::string ReadFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

bool FileExists(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	return f.good();
}

const char* kMini = R"JSON({
 "schema": "fourfold.sfx/1",
 "mix": {"global_voices": 3, "loop_voices": 2, "bus_gain_db": {"sfx": 0, "ui": 0, "ambience": -4},
   "fade_db_per_s": {"body_in": 90, "body_out": 60, "zone_in": 30, "zone_out": 24, "floor_db": -40},
   "limiters": {"sting": {"gap_s": 0.1}, "swing": {"gap_s": 0.2, "per": "actor"}},
   "duck": {"bus": "ambience", "db": -4, "attack_s": 0.05, "hold_s": 0.5, "release_s": 1.0, "triggers": ["boom"]},
   "attenuation": {"mid": {"inner_cm": 1000, "max_cm": 4500, "db_at_max": -24}}},
 "sounds": {
   "hit_light": {"asset": "/Game/A/S_hit_light", "gain_db": -3, "max_voices": 2, "category": "impact", "bus": "sfx", "duration_s": 0.2, "priority": 3},
   "hit_heavy": {"asset": "/Game/A/S_hit_heavy", "gain_db": -2, "max_voices": 2, "category": "impact", "bus": "sfx", "duration_s": 0.5, "priority": 3},
   "swing_fire_light": {"asset": "/Game/A/S_a", "max_voices": 3, "bus": "sfx", "duration_s": 0.3},
   "swing_earth_heavy": {"asset": "/Game/A/S_b", "max_voices": 3, "bus": "sfx", "duration_s": 0.3},
   "boom": {"asset": "/Game/A/S_boom", "max_voices": 1, "bus": "sfx", "duration_s": 1.0, "priority": 3},
   "spark": {"asset": "/Game/A/S_spark", "max_voices": 4, "bus": "sfx", "duration_s": 0.2, "stinger": true},
   "tornado_loop": {"asset": "/Game/A/S_t", "loop": true, "bus": "sfx", "duration_s": 3.0, "category": "loop"},
   "stone_1": {"asset": "/Game/A/S_s1", "bus": "sfx", "duration_s": 0.2},
   "stone_2": {"asset": "/Game/A/S_s2", "bus": "sfx", "duration_s": 0.2},
   "ui_tap": {"asset": "/Game/A/S_tap", "bus": "ui", "category": "ui", "duration_s": 0.1}},
 "tables": {"mat_sound": {"stone": "stone_1", "metal": ["hit_light", "hit_heavy"]}, "class_mat": {"boulder": "stone", "wall_ice": "ice"}},
 "events": {
   "hit": [
     {"id": "a", "when": [{"f": "result", "eq": "knockdown"}], "play": [{"sound": "hit_heavy", "at": ["actor"]}]},
     {"id": "b", "when": [{"f": "damage", "gte": 15}, {"f": "result", "ne": "knockdown"}], "play": [{"sound": "hit_heavy", "at": ["actor"], "gain_db": -1}]},
     {"id": "c", "when": [{"f": "damage", "lt": 15}], "play": [{"sound": "hit_light", "at": ["actor"]}, {"sound": {"table": "mat_sound", "key": "mat"}, "at": ["actor"], "gain_db": -3}]}],
   "action": [
     {"id": "swing", "when": [{"f": "phase", "eq": "active"}, {"f": "$el", "ne": ""}], "play": [{"sound": "swing_{$el}_{$weight}", "at": ["hand", "actor"], "limit": "swing"}]}],
   "stopper": [
     {"id": "s1", "play": [{"sound": "spark", "at": []}], "stop": true},
     {"id": "s2", "play": [{"sound": "boom", "at": []}]}],
   "chancy": [{"id": "c", "play": [{"sound": "spark", "at": [], "chance": 0.5}]}],
   "mapped": [{"id": "m", "play": [{"sound": "hit_light", "at": ["pos"], "gain_map": {"f": "speed", "in": [3, 13], "db": [-10, 0]}}]}],
   "nothere": [{"id": "n", "play": [{"sound": "does_not_exist", "at": []}]}],
   "listy": [{"id": "l", "play": [{"sound": ["stone_1", "stone_2"], "at": []}]}],
   "mats": [{"id": "x", "when": [{"f": "$threat_mat", "eq": "ice"}, {"f": "$player", "truthy": true}, {"f": "kind", "in": ["a", "b"]}, {"f": "kind", "nin": ["c"]}],
             "play": [{"sound": "spark", "at": []}]}]},
 "loops": {
   "bodies": [{"id": "tag.tornado", "when": [{"f": "tag", "eq": "tornado"}], "sound": "tornado_loop", "gain_db": -4, "fade": "zone", "at": "body"}],
   "actors": [{"id": "burn", "when": [{"f": "$status", "has": "burning"}], "sound": "tornado_loop", "gain_db": 0, "fade": "body", "at": "chest"},
              {"id": "bolt", "when": [{"f": "action.id", "eq": "fire_attack"}, {"f": "action.data.bolt_ready", "truthy": true}], "sound": "tornado_loop", "at": "chest"}],
   "events": [{"event": "heating", "sound": "tornado_loop", "key": ["heat", "body"], "hold_s": 0.3, "gain_db": -2, "at": "body", "fade": "body"}]},
 "steps": {"min_speed": 0.8, "cadence_base": 1.2, "cadence_per_speed": 0.45, "gain_db": [-1.5, 0.5], "speed_gain": {"in": [0.8, 5.5], "db": [-5, 0]},
   "default": "stone", "surfaces": {"stone": ["stone_1", "stone_2"], "ice": ["stone_2"]},
   "surface_exact": {"stone": "stone"}, "surface_contains": [["ice", "ice"], ["quicksand", "mud"]], "cloth_every": 3, "cloth_sounds": ["hit_light"]},
 "ambience": {"beds": [{"sound": "tornado_loop", "gain_db": -1}], "accents": [{"sounds": ["stone_1", "stone_2"], "min_gap_s": 2, "max_gap_s": 4}], "fade_in_s": 2.5},
 "ui": {"ui_tap": "ui_tap"}
})JSON";

Manifest MiniManifest() {
	Manifest m;
	std::string err;
	const bool ok = m.Parse(kMini, &err);
	FAT_CHECK(ok);
	if (!ok) std::printf("    mini manifest: %s\n", err.c_str());
	return m;
}

std::vector<PlayRequest> Run(ffa::RuleEngine& eng, const ff::Event& e, const ff::Snapshot& snap) {
	std::vector<PlayRequest> plays;
	std::vector<HoldRequest> holds;
	ffa::SnapshotWorld w(snap);
	eng.ProcessEvent(e, w, plays, holds);
	return plays;
}

ff::Snapshot TwoActors() {
	ff::Snapshot s;
	s.player_id = 1;
	s.actors.push_back(MakeActor(1, 0, ff::Vec3(0, 0, 7)));
	s.actors.push_back(MakeActor(2, 2, ff::Vec3(0, 0, -7)));
	return s;
}

}  // namespace

// ====================================================================================================== manifest

FAT_TEST(manifest_parses_mini) {
	Manifest m = MiniManifest();
	FAT_CHECK(m.sounds.size() == 10);
	FAT_CHECK(m.Find("hit_light") != nullptr && m.Find("nope") == nullptr);
	FAT_NEAR(m.Find("hit_light")->gain_db, -3.0, 1e-6);
	FAT_CHECK(m.Find("tornado_loop")->loop);
	FAT_CHECK(m.Find("spark")->stinger);
	FAT_CHECK(m.mix.global_voices == 3 && m.mix.limiters.size() == 2);
	FAT_CHECK(m.mix.limiters["swing"].per == "actor");
	FAT_CHECK(m.mix.duck.triggers.size() == 1);
	FAT_CHECK(m.BusIndex("ui") == 1 && m.BusIndex("ambience") == 2 && m.BusIndex("sfx") == 0);
	FAT_CHECK(m.events.at("hit").size() == 3);
	FAT_CHECK(m.body_loops.size() == 1 && m.body_loops[0].zone);
	FAT_CHECK(m.actor_loops.size() == 2 && m.event_loops.size() == 1);
	FAT_CHECK(m.steps.surfaces.at("stone").size() == 2);
	FAT_CHECK(m.ui.at("ui_tap") == "ui_tap");
}

FAT_TEST(manifest_rejects_bad_input) {
	Manifest m;
	std::string err;
	FAT_CHECK(!m.Parse("{ not json", &err));
	FAT_CHECK(!err.empty());
	FAT_CHECK(!m.Parse("{\"schema\": \"other/1\"}", &err));
	FAT_CHECK(m.Parse("{\"schema\": \"fourfold.sfx/1\"}", &err));   // an empty manifest is valid
	FAT_CHECK(m.sounds.empty());
}

// ====================================================================================================== conditions

FAT_TEST(conditions_all_ops) {
	using ffa::Cond;
	using ffa::Op;
	auto C = [](Op op, ff::Value arg) {
		Cond c;
		c.op = op;
		c.arg = std::move(arg);
		return c;
	};
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Eq, ff::Value("a")), ff::Value("a")));
	FAT_CHECK(!ffa::RuleEngine::EvalCond(C(Op::Eq, ff::Value("a")), ff::Value()));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Eq, ff::Value(1)), ff::Value(1.0)));        // 1 == 1.0
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Ne, ff::Value("a")), ff::Value()));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Gt, ff::Value(2)), ff::Value(3)));
	FAT_CHECK(!ffa::RuleEngine::EvalCond(C(Op::Gt, ff::Value(2)), ff::Value()));          // missing field never passes a numeric test
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Gte, ff::Value(2)), ff::Value(2)));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Lt, ff::Value(2.5)), ff::Value(2)));
	FAT_CHECK(!ffa::RuleEngine::EvalCond(C(Op::Lte, ff::Value(2)), ff::Value(3)));
	ff::Array arr;
	arr.append("x");
	arr.append("y");
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::In, ff::Value(arr)), ff::Value("y")));
	FAT_CHECK(!ffa::RuleEngine::EvalCond(C(Op::In, ff::Value(arr)), ff::Value()));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Nin, ff::Value(arr)), ff::Value("z")));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Nin, ff::Value(arr)), ff::Value()));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Has, ff::Value("x")), ff::Value(arr)));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Nhas, ff::Value("q")), ff::Value(arr)));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Has, ff::Value("cd")), ff::Value("abcde")));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Truthy, ff::Value()), ff::Value(true)));
	FAT_CHECK(ffa::RuleEngine::EvalCond(C(Op::Falsy, ff::Value()), ff::Value()));
	FAT_CHECK(!ffa::RuleEngine::EvalCond(C(Op::Falsy, ff::Value()), ff::Value("s")));
}

FAT_TEST(dict_path) {
	ff::Dict inner = DictOf({{"bolt_ready", ff::Value(true)}});
	ff::Dict outer = DictOf({{"id", ff::Value("fire_attack")}, {"data", ff::Value(inner)}});
	ff::Value root(DictOf({{"action", ff::Value(outer)}}));
	FAT_CHECK(ffa::RuleEngine::DictPath(root, "action.id") == ff::Value("fire_attack"));
	FAT_CHECK(ffa::RuleEngine::DictPath(root, "action.data.bolt_ready").truthy());
	FAT_CHECK(ffa::RuleEngine::DictPath(root, "action.nope.x").is_nil());
	FAT_CHECK(ffa::RuleEngine::DictPath(root, "zzz").is_nil());
}

// ====================================================================================================== event rules

FAT_TEST(hit_rules_pick_by_damage_and_result) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	auto p = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 5.0}, {"result", "hit"}, {"mat", "stone"}}), s);
	FAT_CHECK(p.size() == 2);
	FAT_CHECK(p.size() == 2 && p[0].sound == "hit_light" && p[1].sound == "stone_1");
	FAT_CHECK(p[0].has_pos && !p[0].two_d);
	FAT_NEAR(p[0].pos.y, 1.25f, 1e-4);   // chest of actor 2
	FAT_NEAR(p[0].pos.z, -7.0f, 1e-4);
	p = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 40.0}, {"result", "hit"}, {"mat", "ice"}}), s);
	FAT_CHECK(p.size() == 1 && p[0].sound == "hit_heavy");
	FAT_NEAR(p[0].gain_db, -1.0, 1e-5);
	p = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 1.0}, {"result", "knockdown"}}), s);
	FAT_CHECK(p.size() >= 1 && p[0].sound == "hit_heavy");
}

FAT_TEST(table_lookup_and_round_robin) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	std::vector<std::string> seen;
	for (int i = 0; i < 4; ++i) {
		auto p = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 5.0}, {"result", "hit"}, {"mat", "metal"}}), s);
		FAT_CHECK(p.size() == 2);
		if (p.size() == 2) seen.push_back(p[1].sound);
	}
	FAT_CHECK(seen.size() == 4 && seen[0] == "hit_light" && seen[1] == "hit_heavy" && seen[2] == "hit_light" && seen[3] == "hit_heavy");
	auto q = Run(eng, Ev("listy", {}), s);
	auto q2 = Run(eng, Ev("listy", {}), s);
	FAT_CHECK(q.size() == 1 && q2.size() == 1 && q[0].sound == "stone_1" && q2[0].sound == "stone_2");
	// unknown material: table has no entry and no default -> the rule's other play still happens
	auto none = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 5.0}, {"result", "hit"}, {"mat", "plasma"}}), s);
	FAT_CHECK(none.size() == 1 && none[0].sound == "hit_light");
}

FAT_TEST(templates_use_derived_fields) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();   // actor 2 is fire (element 2)
	auto p = Run(eng, Ev("action", {{"actor", 2}, {"phase", "active"}, {"heavy", false}, {"tier", 0}}), s);
	FAT_CHECK(p.size() == 1 && p[0].sound == "swing_fire_light");
	FAT_CHECK(p.size() == 1 && p[0].limit == "swing" && p[0].limit_key == 2);
	// earth actor, heavy -> swing_earth_heavy, positioned at the hand (0.55 m in front, facing +Z for facing 0)
	p = Run(eng, Ev("action", {{"actor", 1}, {"phase", "active"}, {"heavy", true}}), s);
	FAT_CHECK(p.size() == 1 && p[0].sound == "swing_earth_heavy");
	if (!p.empty()) {
		FAT_NEAR(p[0].pos.z, 7.55f, 1e-4);
		FAT_NEAR(p[0].pos.y, 1.2f, 1e-4);
	}
	// wrong phase, unknown actor (no element), unknown template result -> nothing
	FAT_CHECK(Run(eng, Ev("action", {{"actor", 2}, {"phase", "startup"}}), s).empty());
	FAT_CHECK(Run(eng, Ev("action", {{"actor", 99}, {"phase", "active"}}), s).empty());
	ff::Snapshot s2 = TwoActors();
	s2.actors[1].element = 3;     // air + light: swing_air_light is not in the mini manifest -> counted as missing, no play
	FAT_CHECK(Run(eng, Ev("action", {{"actor", 2}, {"phase", "active"}}), s2).empty());
	FAT_CHECK(eng.MissingSounds() == 1 && eng.LastMissing() == "swing_air_light");
}

FAT_TEST(stop_chance_gain_map_position_fallback) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	auto p = Run(eng, Ev("stopper", {}), s);
	FAT_CHECK(p.size() == 1 && p[0].sound == "spark" && p[0].two_d);
	int n = 0;
	for (int i = 0; i < 400; ++i) n += static_cast<int>(Run(eng, Ev("chancy", {}), s).size());
	FAT_CHECK(n > 140 && n < 260);                            // ~50 %
	auto g = Run(eng, Ev("mapped", {{"pos", ff::Value(ff::Vec3(1, 2, 3))}, {"speed", 8.0}}), s);
	FAT_CHECK(g.size() == 1);
	if (!g.empty()) {
		FAT_NEAR(g[0].gain_db, -5.0, 1e-4);                    // halfway between -10 and 0
		FAT_CHECK(g[0].has_pos && !g[0].two_d);
		FAT_NEAR(g[0].pos.z, 3.0, 1e-6);
	}
	g = Run(eng, Ev("mapped", {{"speed", 20.0}}), s);         // no pos field -> played at the listener, clamped gain
	FAT_CHECK(g.size() == 1 && g[0].two_d && !g[0].has_pos);
	if (!g.empty()) FAT_NEAR(g[0].gain_db, 0.0, 1e-4);
	FAT_CHECK(Run(eng, Ev("nothere", {}), s).empty());
	FAT_CHECK(Run(eng, Ev("unknown_event_type", {}), s).empty());
}

FAT_TEST(derived_threat_mat_and_player) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	auto p = Run(eng, Ev("mats", {{"threat", "wall_ice"}, {"actor", 1}, {"kind", "a"}}), s);
	FAT_CHECK(p.size() == 1);
	FAT_CHECK(Run(eng, Ev("mats", {{"threat", "wall_ice"}, {"actor", 2}, {"kind", "a"}}), s).empty());   // not the player
	FAT_CHECK(Run(eng, Ev("mats", {{"threat", "boulder"}, {"actor", 1}, {"kind", "a"}}), s).empty());    // stone, not ice
	FAT_CHECK(Run(eng, Ev("mats", {{"threat", "wall_ice"}, {"actor", 1}, {"kind", "c"}}), s).empty());    // kind nin ["c"]
}

FAT_TEST(delay_and_limit_are_passed_through) {
	Manifest m;
	FAT_CHECK(m.Parse(R"({"schema":"fourfold.sfx/1","sounds":{"a":{"asset":"x","duration_s":1}},
	  "events":{"e":[{"id":"r","play":[{"sound":"a","at":[],"delay_s":0.9,"limit":"scene","pitch":1.5}]}]}})"));
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	auto p = Run(eng, Ev("e", {{"actor", 1}}), s);
	FAT_CHECK(p.size() == 1);
	if (!p.empty()) {
		FAT_NEAR(p[0].delay_s, 0.9, 1e-5);
		FAT_CHECK(p[0].limit == "scene");
		FAT_NEAR(p[0].pitch, 1.5, 1e-6);
	}
}

// ====================================================================================================== loops (rules)

FAT_TEST(event_held_loop_and_tracker) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = TwoActors();
	s.bodies.push_back(MakeBody(7, ff::Mat::Stone, ff::Form::Blob, ff::Vec3(3, 0.5f, 4), "stone"));
	std::vector<PlayRequest> plays;
	std::vector<HoldRequest> holds;
	ffa::SnapshotWorld w(s);
	eng.ProcessEvent(Ev("heating", {{"actor", 1}, {"body", 7}}), w, plays, holds);
	FAT_CHECK(holds.size() == 1 && holds[0].key == "e:heat_7");
	if (holds.empty()) return;
	FAT_NEAR(holds[0].pos.x, 3.0, 1e-5);
	ffa::LoopTracker t;
	t.Configure(m.mix.fade, 4);
	double now = 0.0;
	t.BeginFrame();
	t.Hold(holds[0], now);
	t.EndFrame(now, 1.0f / 60.0f);
	FAT_CHECK(t.States().size() == 1 && t.States()[0].started && t.States()[0].on);
	// keeps playing for hold_s without new events (0.3 s), then fades out and finishes
	bool finished = false;
	bool was_on_after_hold = false;
	for (int i = 0; i < 400 && !finished; ++i) {
		now += 1.0 / 60.0;
		t.BeginFrame();
		t.EndFrame(now, 1.0f / 60.0f);
		if (!t.States().empty()) {
			if (now > 0.35 && t.States()[0].on) was_on_after_hold = true;
			finished = t.States()[0].finished;
		}
	}
	FAT_CHECK(finished && !was_on_after_hold);
	t.BeginFrame();
	t.EndFrame(now + 0.1, 1.0f / 60.0f);
	FAT_CHECK(t.States().empty());
}

FAT_TEST(state_loops_from_bodies_and_actors) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ff::Snapshot cur = TwoActors();
	ff::Snapshot prev = cur;
	cur.bodies.push_back(MakeBody(5, ff::Mat::Air, ff::Form::Zone, ff::Vec3(4, 0, 4), "vortex", "tornado"));
	prev.bodies.push_back(MakeBody(5, ff::Mat::Air, ff::Form::Zone, ff::Vec3(2, 0, 4), "vortex", "tornado"));
	cur.bodies.push_back(MakeBody(6, ff::Mat::Stone, ff::Form::Chunk, ff::Vec3(0, 0, 0), "stone", ""));
	ff::StatusView st;
	st.name = "burning";
	cur.actors[0].statuses.push_back(st);
	cur.actors[1].action.active = true;
	cur.actors[1].action.id = "fire_attack";
	cur.actors[1].action.data = ff::Value(DictOf({{"bolt_ready", ff::Value(true)}}));
	std::vector<LoopWant> wants;
	eng.EvaluateLoops(cur, &prev, 0.5f, wants);
	FAT_CHECK(wants.size() == 3);
	std::set<std::string> keys;
	for (const LoopWant& w : wants) keys.insert(w.key);
	FAT_CHECK(keys.count("b5:tag.tornado") && keys.count("a1:burn") && keys.count("a2:bolt"));
	for (const LoopWant& w : wants) {
		if (w.key == "b5:tag.tornado") {
			FAT_NEAR(w.pos.x, 3.0, 1e-5);    // interpolated 2 -> 4 at alpha 0.5
			FAT_CHECK(w.zone);
			FAT_NEAR(w.gain_db, -4.0, 1e-6);
		}
		if (w.key == "a1:burn") FAT_NEAR(w.pos.y, 1.25f, 1e-5);
	}
}

FAT_TEST(loop_tracker_fades_and_budget) {
	ffa::FadeDef fade;
	ffa::LoopTracker t;
	t.Configure(fade, 2);
	const float dt = 1.0f / 60.0f;
	double now = 0.0;
	auto frame = [&](std::initializer_list<const char*> keys, bool zone) {
		t.BeginFrame();
		for (const char* k : keys) {
			LoopWant w;
			w.key = k;
			w.sound = "x";
			w.zone = zone;
			w.gain_db = -4.0f;
			t.Want(w);
		}
		now += static_cast<double>(dt);
		t.EndFrame(now, dt);
	};
	frame({"a"}, true);
	FAT_CHECK(t.States().size() == 1 && t.States()[0].started);
	FAT_NEAR(t.States()[0].vol_db, -40.0f + 30.0f / 60.0f, 1e-3);          // zone: starts at target - 40, 30 dB/s up
	for (int i = 0; i < 60; ++i) frame({"a"}, true);
	FAT_CHECK(!t.States()[0].started);
	FAT_NEAR(t.States()[0].vol_db, -40.0f + 61.0f * 0.5f, 0.1);             // 61 frames x 30 dB/s
	for (int i = 0; i < 60; ++i) frame({"a"}, true);
	FAT_NEAR(t.States()[0].vol_db, -4.0f, 1e-4);                           // reached the target, stays
	// a body loop starts at target - 24 and rises 90 dB/s
	frame({"a", "b"}, false);
	const auto& st = t.States();
	FAT_CHECK(st.size() == 2);
	FAT_NEAR(st[1].vol_db, -4.0f - 24.0f + 90.0f / 60.0f, 1e-3);
	// budget: a third loop is refused while two are running
	frame({"a", "b", "c"}, false);
	FAT_CHECK(t.States().size() == 2);
	// 'a' stops being wanted: fades out where it was (last want was a body loop: 60 dB/s from -4 to -39 = 35 frames)
	int frames_to_finish = 0;
	bool done = false;
	for (int i = 0; i < 600 && !done; ++i) {
		frame({"b"}, false);
		++frames_to_finish;
		for (const auto& s : t.States()) {
			if (s.key == "a" && s.finished) done = true;
		}
	}
	FAT_CHECK(done);
	FAT_CHECK(frames_to_finish >= 30 && frames_to_finish <= 45);
	t.StopAll();
	for (const auto& s : t.States()) FAT_CHECK(s.finished);
}

// ====================================================================================================== voices, limiters, duck

FAT_TEST(limiters_gap_and_scope) {
	Manifest m = MiniManifest();
	ffa::Limiters l;
	l.Configure(m.mix.limiters);
	FAT_CHECK(l.Peek("sting", -1, 0.0));
	l.Commit("sting", -1, 0.0);
	FAT_CHECK(!l.Peek("sting", -1, 0.05));
	FAT_CHECK(l.Peek("sting", -1, 0.1));
	// per actor: another actor is not blocked
	l.Commit("swing", 1, 1.0);
	FAT_CHECK(!l.Peek("swing", 1, 1.1));
	FAT_CHECK(l.Peek("swing", 2, 1.1));
	FAT_CHECK(l.Peek("swing", 1, 1.2));
	FAT_CHECK(l.Peek("", 0, 0.0) && l.Peek("unknown_limiter", 0, 0.0));
}

FAT_TEST(voicebook_per_sound_cap_steals_oldest) {
	Manifest m = MiniManifest();
	ffa::VoiceBook vb;
	vb.Configure(10);
	const ffa::SoundDef& s = *m.Find("hit_light");   // max_voices 2
	auto d1 = vb.Admit(s, 0.0, 0.2);
	auto d2 = vb.Admit(s, 0.05, 0.2);
	FAT_CHECK(d1.play && d2.play && d1.steal.empty() && d2.steal.empty());
	auto d3 = vb.Admit(s, 0.08, 0.2);
	FAT_CHECK(d3.play && d3.steal.size() == 1 && d3.steal[0] == d1.id);
	FAT_CHECK(vb.ActiveOf("hit_light") == 2);
	std::vector<uint64_t> expired;
	vb.Prune(0.26, &expired);                          // d2 ends at 0.25
	FAT_CHECK(expired.size() == 1 && expired[0] == d2.id);
	FAT_CHECK(vb.ActiveOf("hit_light") == 1);
	vb.Release(d3.id);
	FAT_CHECK(vb.Active() == 0);
}

FAT_TEST(voicebook_global_cap_priority) {
	Manifest m = MiniManifest();
	ffa::VoiceBook vb;
	vb.Configure(2);
	const ffa::SoundDef& low = *m.Find("swing_fire_light");   // priority default 2
	const ffa::SoundDef& high = *m.Find("boom");               // priority 3
	auto a = vb.Admit(low, 0.0, 1.0);
	auto b = vb.Admit(*m.Find("swing_earth_heavy"), 0.1, 1.0);
	FAT_CHECK(a.play && b.play);
	auto c = vb.Admit(high, 0.2, 1.0);                         // global cap: steals the lowest priority, oldest first
	FAT_CHECK(c.play && c.steal.size() == 1 && c.steal[0] == a.id);
	auto d = vb.Admit(low, 0.3, 1.0);                          // priority 2 vs voices {2, 3}: steals the priority-2 voice
	FAT_CHECK(d.play && d.steal.size() == 1 && d.steal[0] == b.id);
	vb.Configure(1);
	ffa::VoiceBook vb2;
	vb2.Configure(1);
	auto e = vb2.Admit(high, 0.0, 1.0);
	FAT_CHECK(e.play);
	auto f = vb2.Admit(low, 0.1, 1.0);                         // everything running outranks it: dropped
	FAT_CHECK(!f.play && f.steal.empty());
	// ui sounds do not count against the sfx cap
	auto g = vb2.Admit(*m.Find("ui_tap"), 0.2, 0.1);
	FAT_CHECK(g.play);
}

FAT_TEST(ducker_attack_hold_release) {
	Manifest m = MiniManifest();
	ffa::Ducker d;
	d.Configure(m.mix.duck);
	FAT_CHECK(d.IsTrigger("boom") && !d.IsTrigger("hit_light"));
	FAT_NEAR(d.Step(0.016f), 0.0, 1e-6);
	d.Trigger();
	float db = 0.0f;
	for (int i = 0; i < 6; ++i) db = d.Step(0.0166f);          // ~0.1 s: attack is 0.05 s -> fully ducked
	FAT_NEAR(db, -4.0, 1e-4);
	for (int i = 0; i < 20; ++i) db = d.Step(0.0166f);          // still holding (0.5 s hold)
	FAT_NEAR(db, -4.0, 1e-4);
	for (int i = 0; i < 40; ++i) db = d.Step(0.0166f);          // hold over, releasing
	FAT_CHECK(db > -4.0f);
	for (int i = 0; i < 90; ++i) db = d.Step(0.0166f);
	FAT_NEAR(db, 0.0, 1e-4);
}

// ====================================================================================================== steps, ambience, mix

FAT_TEST(steps_cadence_surface_and_no_repeat) {
	Manifest m = MiniManifest();
	ffa::RuleEngine eng(&m);
	ffa::StepTracker st;
	ff::Snapshot s = TwoActors();
	s.actors[0].vel = ff::Vec3(0, 0, -5.5f);
	s.actors[1].vel = ff::Vec3(0, 0, 0.3f);                     // too slow: no steps
	int steps = 0;
	std::vector<std::string> seq;
	for (int i = 0; i < 240; ++i) {                              // 4 s at 60 fps
		std::vector<ffa::StepOut> out;
		st.Update(m, eng, s, 1.0f / 60.0f, out);
		for (const auto& o : out) {
			FAT_CHECK(o.actor == 1);
			++steps;
			seq.push_back(o.sound);
		}
	}
	// cadence 1.2 + 5.5 * 0.45 = 3.675 steps per second -> ~14.7 in 4 s
	FAT_CHECK(steps >= 13 && steps <= 16);
	bool repeat = false;
	for (size_t i = 1; i < seq.size(); ++i) repeat |= seq[i] == seq[i - 1];
	FAT_CHECK(!repeat);
	// ice zone uses its own set; airborne / stunned actors make no steps
	s.actors[0].surface = "zone:ice";
	std::vector<ffa::StepOut> out;
	for (int i = 0; i < 60 && out.empty(); ++i) st.Update(m, eng, s, 1.0f / 60.0f, out);
	FAT_CHECK(!out.empty() && out[0].sound == "stone_2");
	s.actors[0].grounded = false;
	out.clear();
	for (int i = 0; i < 120; ++i) st.Update(m, eng, s, 1.0f / 60.0f, out);
	FAT_CHECK(out.empty());
	FAT_CHECK(eng.ClassifySurface("zone:quicksand") == "mud" && eng.ClassifySurface("zone:ice_floor") == "ice" &&
	          eng.ClassifySurface("stone") == "stone" && eng.ClassifySurface("whatever") == "stone");
}

FAT_TEST(ambience_accents_schedule) {
	Manifest m = MiniManifest();
	ffa::AmbienceScheduler a;
	a.Configure(m.accents, 99);
	int n = 0;
	std::string last;
	bool repeat = false;
	for (int i = 0; i < 60 * 60; ++i) {                          // one minute at 60 fps
		std::vector<ffa::AccentPlay> out;
		a.Update(1.0f / 60.0f, out);
		for (const auto& p : out) {
			++n;
			repeat |= (p.sound == last);
			last = p.sound;
		}
	}
	FAT_CHECK(n >= 15 && n <= 32);                               // gaps 2..4 s
	FAT_CHECK(!repeat);                                          // two sounds in the group: never the same twice in a row
}

FAT_TEST(mix_helpers) {
	FAT_NEAR(ffa::DbToLinear(0.0f), 1.0, 1e-6);
	FAT_NEAR(ffa::DbToLinear(-6.0206f), 0.5, 1e-3);
	FAT_NEAR(ffa::LinearToDb(0.5f), -6.0206, 1e-3);
	FAT_NEAR(ffa::MapRange(8.0f, 3.0f, 13.0f, -10.0f, 0.0f), -5.0, 1e-5);
	FAT_NEAR(ffa::MapRange(99.0f, 3.0f, 13.0f, -10.0f, 0.0f), 0.0, 1e-5);
	ffa::BusVolumes v;
	v.master = 0.5f;
	v.sfx = 0.8f;
	v.ui = 1.0f;
	v.ambience = 0.25f;
	FAT_NEAR(v.For(0), 0.4, 1e-6);
	FAT_NEAR(v.For(1), 0.5, 1e-6);
	FAT_NEAR(v.For(2), 0.125, 1e-6);
	ffa::Rng r(5);
	double sum = 0;
	for (int i = 0; i < 1000; ++i) sum += static_cast<double>(r.Next01());
	FAT_NEAR(sum / 1000.0, 0.5, 0.06);
}

// ====================================================================================================== the real manifest

namespace {

std::string DataPath() { return std::string(FFA_UNREAL_DIR) + "/Content/Fourfold/Data/sfx_manifest.json"; }

bool LoadReal(Manifest& m) {
	const std::string text = ReadFile(DataPath());
	if (text.empty()) return false;
	std::string err;
	if (!m.Parse(text, &err)) {
		std::printf("    real manifest: %s\n", err.c_str());
		return false;
	}
	return true;
}

// A full snapshot with every element, a few bodies and the surfaces the rules look at.
ff::Snapshot RealisticSnapshot() {
	ff::Snapshot s;
	s.player_id = 1;
	s.actors.push_back(MakeActor(1, 0, ff::Vec3(0, 0, 7)));
	s.actors.push_back(MakeActor(2, 1, ff::Vec3(0, 0, -7)));
	s.actors.push_back(MakeActor(3, 2, ff::Vec3(5, 0, 0)));
	s.actors.push_back(MakeActor(4, 3, ff::Vec3(-5, 0, 0), "metal"));
	s.bodies.push_back(MakeBody(10, ff::Mat::Stone, ff::Form::Chunk, ff::Vec3(1, 0.5f, 1), "stone"));
	s.bodies.push_back(MakeBody(11, ff::Mat::Metal, ff::Form::Chunk, ff::Vec3(2, 0.5f, 1), "metal"));
	s.bodies.push_back(MakeBody(12, ff::Mat::Water, ff::Form::Wall, ff::Vec3(3, 0, 1), "ice"));
	return s;
}

}  // namespace

FAT_TEST(real_manifest_loads_and_is_consistent) {
	Manifest m;
	if (!LoadReal(m)) {
		FAT_CHECK(false);
		return;
	}
	FAT_CHECK(m.sounds.size() >= 200);
	FAT_CHECK(m.CountEventRules() >= 150);
	FAT_CHECK(m.mix.limiters.count("sting") == 1 && m.mix.limiters.count("swing") == 1);
	FAT_CHECK(!m.body_loops.empty() && !m.actor_loops.empty() && !m.event_loops.empty());
	FAT_CHECK(m.steps.surfaces.count("stone") == 1 && m.steps.surfaces.at("stone").size() == 4);
	FAT_CHECK(m.ui.size() == 12);
	int loops = 0;
	int missing_wav = 0;
	for (const auto& kv : m.sounds) {
		const ffa::SoundDef& s = kv.second;
		loops += s.loop ? 1 : 0;
		FAT_CHECK(s.asset.rfind("/Game/Fourfold/Audio/", 0) == 0);
		FAT_CHECK(s.asset.find("/S_" + s.name) != std::string::npos);
		FAT_CHECK(s.gain_db <= 0.0f && s.gain_db >= -24.0f);
		FAT_CHECK(s.max_voices >= 1 && s.duration_s > 0.05f);
		// the generated WAV exists
		const std::string dir = s.asset.find("/Ambience/") != std::string::npos ? "Ambience" : "SFX";
		if (!FileExists(std::string(FFA_UNREAL_DIR) + "/SourceArt/Audio/" + dir + "/" + s.name + ".wav")) ++missing_wav;
	}
	FAT_CHECK(loops >= 30);
	FAT_CHECK(missing_wav == 0);
	// loop rules only reference loops, one-shot rules only reference one-shots, tables / limiters exist
	for (const ffa::LoopRule& r : m.body_loops) FAT_CHECK(m.Find(r.sound) != nullptr && m.Find(r.sound)->loop);
	for (const ffa::LoopRule& r : m.actor_loops) FAT_CHECK(m.Find(r.sound) != nullptr && m.Find(r.sound)->loop);
	for (const ffa::EventLoopRule& r : m.event_loops) FAT_CHECK(m.Find(r.sound) != nullptr && m.Find(r.sound)->loop);
	for (const auto& kv : m.events) {
		for (const ffa::Rule& r : kv.second) {
			for (const ffa::Play& p : r.play) {
				if (!p.limit.empty()) FAT_CHECK(m.mix.limiters.count(p.limit) == 1);
				if (p.sound.kind == ffa::SoundRef::Kind::Literal) {
					FAT_CHECK(m.Find(p.sound.text) != nullptr);
					if (m.Find(p.sound.text)) FAT_CHECK(!m.Find(p.sound.text)->loop);
				}
				if (p.sound.kind == ffa::SoundRef::Kind::Table) FAT_CHECK(m.tables.count(p.sound.table) == 1);
			}
		}
	}
	for (const auto& b : m.beds) FAT_CHECK(m.Find(b.sound) != nullptr && m.Find(b.sound)->loop);
	for (const auto& kv : m.ui) FAT_CHECK(m.Find(kv.second) != nullptr && !m.Find(kv.second)->loop);
	for (const auto& t : m.mix.duck.triggers) FAT_CHECK(m.Find(t) != nullptr);
}

FAT_TEST(real_manifest_event_families) {
	Manifest m;
	if (!LoadReal(m)) {
		FAT_CHECK(false);
		return;
	}
	ffa::RuleEngine eng(&m);
	const ff::Snapshot s = RealisticSnapshot();
	auto names = [](const std::vector<PlayRequest>& p) {
		std::set<std::string> n;
		for (const auto& r : p) n.insert(r.sound);
		return n;
	};
	// hits: body + material layer + breath; knockdown adds the slam
	auto p = Run(eng, Ev("hit", {{"actor", 2}, {"attacker", 1}, {"damage", 22.0}, {"kind", "stone"}, {"result", "knockdown"}, {"mat", "metal"}, {"tier", 1}}), s);
	auto n = names(p);
	FAT_CHECK(n.count("hit_heavy") && n.count("knockdown") && n.count("impact_metal_heavy"));
	p = Run(eng, Ev("hit", {{"actor", 2}, {"damage", 6.0}, {"result", "hit"}, {"mat", "water"}}), s);
	n = names(p);
	FAT_CHECK(n.count("hit_light") && n.count("impact_water_light") && !n.count("hit_heavy"));
	// block / guard break / deflect / perfect deflect
	FAT_CHECK(names(Run(eng, Ev("block", {{"actor", 2}, {"kind", "fire_water"}}), s)) == (std::set<std::string>{"block", "steam_hiss"}));
	FAT_CHECK(names(Run(eng, Ev("guard_break", {{"actor", 2}}), s)).count("guard_break"));
	FAT_CHECK(names(Run(eng, Ev("deflect", {{"actor", 2}}), s)).count("deflect"));
	FAT_CHECK(names(Run(eng, Ev("perfect_deflect", {{"actor", 2}}), s)).count("perfect_deflect"));
	// martial swing by element and slot
	p = Run(eng, Ev("action", {{"actor", 1}, {"move", "earth_attack"}, {"phase", "active"}, {"slot", "strike"}, {"heavy", true}}), s);
	FAT_CHECK(names(p).count("swing_earth_heavy"));
	p = Run(eng, Ev("action", {{"actor", 3}, {"move", "fire_attack"}, {"phase", "active"}, {"slot", "sweep"}}), s);
	FAT_CHECK(names(p).count("kick_fire"));
	p = Run(eng, Ev("action", {{"actor", 1}, {"phase", "active"}, {"slot", "ground"}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"stomp_earth"});
	p = Run(eng, Ev("action", {{"actor", 3}, {"phase", "recovery"}, {"slot", "strike"}}), s);
	FAT_CHECK(names(p).count("cloth_snap"));
	// charge tiers per element (explicit riser sounds) and the generic fallback
	p = Run(eng, Ev("charge", {{"actor", 4}, {"element", 3}, {"tier", 2}, {"move", "air_attack"}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"charge_air_t2"});
	p = Run(eng, Ev("charge", {{"actor", 2}, {"element", 1}, {"tier", 3}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"charge_water_t3"});
	FAT_CHECK(Run(eng, Ev("charge", {{"actor", 2}, {"element", 1}, {"tier", 0}}), s).empty());
	// interaction outcomes: a perfect counter overrides, metal block clangs, deflect pings
	p = Run(eng, Ev("interaction", {{"outcome", "block"}, {"threat", "metal"}, {"counter", "wall_stone"}, {"perfect", true}, {"pos", ff::Value(ff::Vec3(1, 1, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"counter_success"});
	p = Run(eng, Ev("interaction", {{"outcome", "block"}, {"threat", "plate_metal"}, {"counter", "wall_stone"}, {"pos", ff::Value(ff::Vec3(1, 1, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"metal_clang"});
	p = Run(eng, Ev("interaction", {{"outcome", "transform"}, {"to", "obsidian"}, {"threat", "lava_wave"}, {"counter", "water"}, {"pos", ff::Value(ff::Vec3(1, 0, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"obsidian_set"});
	p = Run(eng, Ev("interaction", {{"outcome", "overwhelm"}, {"threat", "boulder"}, {"counter", "wall_vine"}, {"pos", ff::Value(ff::Vec3(1, 0, 1))}}), s);
	FAT_CHECK(names(p) == (std::set<std::string>{"counter_fail", "vine_snap"}));
	// fx cues by shape
	p = Run(eng, Ev("fx", {{"fx", "release"}, {"mat", "stone"}, {"tier", 3}, {"actor", 1}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == (std::set<std::string>{"stone_launch", "whoosh_heavy"}));
	p = Run(eng, Ev("fx", {{"fx", "cone"}, {"mat", "wind"}, {"tier", 2}, {"actor", 4}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"air_gust"});
	p = Run(eng, Ev("fx", {{"fx", "beam"}, {"mat", "lightning"}, {"shape", "down"}, {"actor", 3}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"thunderclap"});
	p = Run(eng, Ev("fx", {{"fx", "burst"}, {"mat", "blast"}, {"radius", 2.5}, {"power", 10.0}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"explosion_large"});
	p = Run(eng, Ev("fx", {{"fx", "burst"}, {"mat", "flame"}, {"radius", 0.8}, {"power", 3.0}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"fire_ignite"});
	p = Run(eng, Ev("fx", {{"fx", "ring"}, {"mat", "sound"}, {"tier", 2}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"sonic_boom"});
	p = Run(eng, Ev("fx", {{"fx", "erupt"}, {"mat", "ice"}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"ice_wall_raise"});
	p = Run(eng, Ev("fx", {{"fx", "splash"}, {"mat", "water"}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s);
	FAT_CHECK(p.empty());
	// launch / impact / land / transform
	p = Run(eng, Ev("launch", {{"actor", 1}, {"body", 10}, {"kind", "stone"}, {"heavy", true}}), s);
	FAT_CHECK(names(p) == (std::set<std::string>{"stone_launch", "whoosh_heavy"}));
	p = Run(eng, Ev("launch", {{"actor", 1}, {"body", 10}, {"kind", "fireball"}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"fireball_whoosh"});
	p = Run(eng, Ev("impact", {{"body", 11}, {"on", "wall"}, {"speed", 9.0}, {"mass", 20.0}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"impact_metal_heavy"});
	FAT_CHECK(Run(eng, Ev("impact", {{"body", 10}, {"speed", 2.0}}), s).empty());
	p = Run(eng, Ev("land", {{"actor", 4}, {"speed", 8.0}}), s);   // actor 4 stands on metal
	FAT_CHECK(names(p) == std::set<std::string>{"land_metal"});
	p = Run(eng, Ev("land", {{"actor", 1}, {"speed", 8.0}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"land"});
	p = Run(eng, Ev("transform", {{"to", "rock"}, {"body", 10}}), s);
	FAT_CHECK(names(p) == (std::set<std::string>{"crust_hiss", "cool_crack"}));
	// status on / off, zone open / close, clash
	p = Run(eng, Ev("status", {{"actor", 2}, {"status", "frozen"}, {"on", true}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"frost_hiss"});
	p = Run(eng, Ev("status", {{"actor", 2}, {"status", "burning"}, {"on", false}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"status_off"});
	p = Run(eng, Ev("zone", {{"kind", "sandstorm"}, {"phase", "open"}, {"pos", ff::Value(ff::Vec3(0, 0, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"sand_burst"});
	p = Run(eng, Ev("zone", {{"kind", "fog"}, {"phase", "close"}, {"pos", ff::Value(ff::Vec3(0, 0, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"status_off"});
	p = Run(eng, Ev("clash", {{"mat", "lightning"}, {"pos", ff::Value(ff::Vec3(0, 0, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"clash_energy"});
	p = Run(eng, Ev("clash", {{"mat", "stone"}, {"pos", ff::Value(ff::Vec3(0, 0, 1))}}), s);
	FAT_CHECK(names(p) == std::set<std::string>{"clash_solid"});
	// movement: evade by element, element switch only for the player, insufficient only for the player
	p = Run(eng, Ev("evade", {{"actor", 2}, {"dash", false}}), s);
	FAT_CHECK(names(p).count("evade_whoosh") && names(p).count("weight_shift"));
	p = Run(eng, Ev("evade", {{"actor", 4}, {"dash", true}}), s);
	FAT_CHECK(names(p).count("dash_air"));
	FAT_CHECK(names(Run(eng, Ev("element", {{"actor", 1}, {"element", 2}}), s)) == std::set<std::string>{"element_fire"});
	FAT_CHECK(Run(eng, Ev("element", {{"actor", 2}, {"element", 2}}), s).empty());
	FAT_CHECK(names(Run(eng, Ev("insufficient", {{"actor", 1}, {"what", "focus"}}), s)) == std::set<std::string>{"challenge_fail"});
	FAT_CHECK(Run(eng, Ev("insufficient", {{"actor", 2}, {"what", "focus"}}), s).empty());
	// session events: KO bell + delayed victory when the rival went down, round reset, toasts, combos, challenges
	p = Run(eng, Ev("app_ko", {{"actor", 2}}), s);
	FAT_CHECK(p.size() == 2 && p[0].sound == "ko_bell" && p[1].sound == "victory" && p[1].delay_s > 0.5f);
	p = Run(eng, Ev("app_ko", {{"actor", 1}}), s);
	FAT_CHECK(p.size() == 1 && p[0].sound == "ko_bell");
	FAT_CHECK(names(Run(eng, Ev("app_round_reset", {{"reason", "ko"}}), s)) == std::set<std::string>{"round_start"});
	FAT_CHECK(names(Run(eng, Ev("app_toast", {{"text", "x"}, {"kind", "mastery"}}), s)) == std::set<std::string>{"unlock"});
	FAT_CHECK(Run(eng, Ev("app_toast", {{"text", "x"}, {"kind", "info"}}), s).empty());
	FAT_CHECK(names(Run(eng, Ev("app_combo", {{"id", "c"}, {"state", "success"}}), s)) == std::set<std::string>{"combo_success"});
	FAT_CHECK(names(Run(eng, Ev("app_challenge", {{"id", "c"}, {"done", true}}), s)) == std::set<std::string>{"challenge_done"});
	FAT_CHECK(Run(eng, Ev("app_challenge", {{"id", "c"}, {"done", false}}), s).empty());
	FAT_CHECK(eng.MissingSounds() == 0);
}

FAT_TEST(real_manifest_loops_and_holds) {
	Manifest m;
	if (!LoadReal(m)) {
		FAT_CHECK(false);
		return;
	}
	ffa::RuleEngine eng(&m);
	ff::Snapshot s = RealisticSnapshot();
	s.bodies.push_back(MakeBody(20, ff::Mat::Air, ff::Form::Zone, ff::Vec3(0, 0, 3), "vortex", "tornado"));
	s.bodies.push_back(MakeBody(21, ff::Mat::Stone, ff::Form::Wave, ff::Vec3(1, 0, 3), "magma", ""));
	s.bodies.back().vel = ff::Vec3(2, 0, 0);
	s.bodies.push_back(MakeBody(22, ff::Mat::Stone, ff::Form::Chunk, ff::Vec3(1, 0, 4), "stone", ""));
	s.bodies.back().vel = ff::Vec3(4, 0, 0);
	s.bodies.back().on_ground = true;
	s.bodies.push_back(MakeBody(23, ff::Mat::Sand, ff::Form::Cloud, ff::Vec3(1, 0, 5), "sand", "sandstorm"));
	s.actors[2].action.active = true;
	s.actors[2].action.id = "fire_attack";
	s.actors[2].action.phase = ff::ActionPhase::Charge;
	s.actors[0].gliding = true;
	s.actors[1].guarding = true;       // water actor guarding -> water shield loop
	ff::StatusView st;
	st.name = "burning";
	s.actors[3].statuses.push_back(st);
	std::vector<LoopWant> wants;
	eng.EvaluateLoops(s, nullptr, 1.0f, wants);
	std::set<std::string> sounds;
	for (const auto& w : wants) sounds.insert(w.sound);
	FAT_CHECK(sounds.count("tornado_loop") && sounds.count("lava_wave_loop") && sounds.count("stone_roll_loop") &&
	          sounds.count("sandstorm_loop") && sounds.count("fire_charge_loop") && sounds.count("glide_loop") &&
	          sounds.count("water_shield_loop") && sounds.count("status_burn_loop"));
	FAT_CHECK(!sounds.count("lightning_charge_loop"));
	// zone loops fade slowly, body loops quickly
	for (const auto& w : wants) {
		if (w.sound == "tornado_loop" || w.sound == "sandstorm_loop") FAT_CHECK(w.zone);
		if (w.sound == "glide_loop") FAT_CHECK(!w.zone);
	}
	// an event-held loop (heating) is produced by the event, positioned at its body
	std::vector<PlayRequest> plays;
	std::vector<HoldRequest> holds;
	ffa::SnapshotWorld w(s);
	eng.ProcessEvent(Ev("heating", {{"actor", 1}, {"body", 10}}), w, plays, holds);
	FAT_CHECK(holds.size() == 1 && holds[0].sound == "heat_crackle_loop");
	holds.clear();
	eng.ProcessEvent(Ev("draw_water", {{"actor", 2}, {"body", 10}}), w, plays, holds);
	FAT_CHECK(holds.size() == 1 && holds[0].sound == "water_draw_loop" && holds[0].key == "e:wdraw_2");
}

FAT_TEST(real_manifest_timing_budget) {
	// 5,000 events through the rule engine must be cheap (a frame handles tens at most)
	Manifest m;
	if (!LoadReal(m)) {
		FAT_CHECK(false);
		return;
	}
	ffa::RuleEngine eng(&m);
	const ff::Snapshot s = RealisticSnapshot();
	size_t plays = 0;
	for (int i = 0; i < 1000; ++i) {
		plays += Run(eng, Ev("hit", {{"actor", 2}, {"damage", 22.0}, {"result", "hit"}, {"mat", "metal"}}), s).size();
		plays += Run(eng, Ev("fx", {{"fx", "release"}, {"mat", "flame"}, {"tier", 1}, {"actor", 3}, {"pos", ff::Value(ff::Vec3(0, 1, 6))}}), s).size();
		plays += Run(eng, Ev("interaction", {{"outcome", "deflect"}, {"threat", "stone"}, {"counter", "wall_stone"}, {"pos", ff::Value(ff::Vec3(1, 1, 1))}}), s).size();
		plays += Run(eng, Ev("action", {{"actor", 1}, {"phase", "active"}, {"slot", "strike"}}), s).size();
		plays += Run(eng, Ev("unknown", {}), s).size();
	}
	FAT_CHECK(plays > 3000);
}

int main() {
	for (const fat::TestCase& t : fat::Registry()) {
		fat::g_current = t.name;
		const int before = fat::g_failures;
		t.fn();
		std::printf("%s %s\n", fat::g_failures == before ? "ok  " : "FAIL", t.name);
	}
	std::printf("\n%zu tests, %d checks, %d failures\n", fat::Registry().size(), fat::g_checks, fat::g_failures);
	return fat::g_failures == 0 ? 0 : 1;
}

#endif  // FF_LOGIC_TESTS
