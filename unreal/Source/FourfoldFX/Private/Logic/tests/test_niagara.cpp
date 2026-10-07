// FourfoldFX logic island - Niagara cue requests (SystemReq) and the "replace" switch. Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxDirector.h"

#include <set>
#include <string>

using namespace ffx;

namespace {

ff::Event NEv(const std::string& type, std::initializer_list<std::pair<const char*, ff::Value>> kv) {
	ff::Event e;
	e.type = type;
	ff::Dict d;
	for (const auto& p : kv) d.set(p.first, p.second);
	e.data = ff::Value(d);
	return e;
}

// One frame with a spread of fx / interaction / lightning events (as director_events_spawn_and_finish_one_shots).
struct NFrame {
	ff::Snapshot prev, curr;
	std::vector<ff::Event> events;
	FxFrameIn in;
	NFrame() {
		in.prev = &prev;
		in.curr = &curr;
		in.events = &events;
		in.dt = 1.0f / 60.0f;
		ff::ActorView a;
		a.id = 1;
		a.element = 2;
		curr.actors = {a};
		const char* mats[] = {"stone", "metal", "sand", "glass", "magma", "water", "ice", "flame", "blue", "lightning",
		                      "blast", "wind", "plant"};
		for (const char* k : {"burst", "erupt", "splash", "release"})
			for (const char* m : mats)
				events.push_back(NEv("fx", {{"fx", k}, {"mat", m}, {"actor", 1}, {"tier", 3}, {"pos", Vec3(0, 1.2f, 0)},
				                           {"dir", Vec3(0, 0, 1)}, {"radius", 2.0f}, {"length", 4.0f}}));
		for (const char* o : {"shatter", "block", "transform", "ground"})
			events.push_back(NEv("interaction", {{"outcome", o}, {"threat", "stone"}, {"counter", "flame"},
			                                     {"pos", Vec3(1, 1, 0)}, {"dir", Vec3(1, 0, 0)}, {"to", "steam"}}));
		events.push_back(NEv("lightning", {{"actor", 1}, {"path", ff::Value(ff::Array({ff::Value(Vec3(0, 1, 0)), ff::Value(Vec3(4, 1, 0))}))}}));
		prev = curr;
		++curr.tick;
	}
};

}  // namespace

FXT_TEST(niagara_requests_cover_cues_and_are_valid) {
	FxDirector d;
	NFrame f;
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(!dl.systems.empty());
	std::set<int> cues;
	for (const SystemReq& s : dl.systems) {
		cues.insert(static_cast<int>(s.cue));
		FXT_CHECK(static_cast<int>(s.cue) < kNumNCues);
		FXT_NEAR(s.dir.length(), 1.0f, 1e-3);
		FXT_CHECK(s.scale > 0.0f && s.scale < 10.0f);
		FXT_CHECK(std::isfinite(s.pos.x) && std::isfinite(s.pos.y) && std::isfinite(s.pos.z));
	}
	FXT_CHECK(cues.count(static_cast<int>(NCue::BoltHit)) == 1);
	FXT_CHECK(cues.size() >= 6);
	// requests are one frame only
	f.events.clear();
	f.prev = f.curr;
	++f.curr.tick;
	FXT_CHECK(d.Update(f.in).systems.empty());
}

FXT_TEST(niagara_replace_skips_procedural_only_when_loaded) {
	auto run = [](uint64_t loaded, int& oneShots, size_t& systems) {
		FxDirector d;
		FxConfig cfg;
		for (NiagaraSlot& s : cfg.niagara) s.replace = !s.path.empty();
		d.SetConfig(cfg);
		NFrame f;
		f.in.niagaraLoaded = loaded;
		systems = d.Update(f.in).systems.size();
		oneShots = d.Stats().oneShots;
	};
	int shotsOff = 0, shotsOn = 0;
	size_t sysOff = 0, sysOn = 0;
	run(0, shotsOff, sysOff);
	run(~0ULL, shotsOn, sysOn);
	FXT_CHECK(shotsOff > 0);
	FXT_CHECK(shotsOn < shotsOff);   // loaded + replace: those cues play only through Niagara
	FXT_CHECK(sysOn > 0 && sysOn <= sysOff);
}

FXT_TEST(niagara_slot_rules_and_json) {
	FxConfig c;
	const NiagaraSlot& blast = c.Niagara(NCue::Blast);
	FXT_CHECK(blast.path.find("/Game/NiagaraExamples/") == 0);
	FXT_CHECK(blast.Wants(1.0f, 2));
	FXT_CHECK(!blast.Wants(1.0f, 0));                       // min quality 1
	FXT_CHECK(!c.Niagara(NCue::BurstDust).Wants(0.2f, 2));  // periodic status puffs stay procedural
	FXT_CHECK(c.Niagara(NCue::BurstDust).Wants(0.8f, 2));
	FXT_CHECK(!c.Niagara(NCue::AirPush).Wants(1.0f, 2));    // no system configured
	std::string err, warn;
	FXT_CHECK(c.LoadJson(R"({"niagara": {"blast": {"path": "", "params": {"Foo": 2, "Bar": "color2"}},
	                                     "air_push": {"path": "/Game/X/NS_Y", "min_quality": 0, "bogus": 1},
	                                     "nope": {}}})",
	                     &err, &warn));
	FXT_CHECK(c.Niagara(NCue::Blast).path.empty());
	FXT_CHECK(c.Niagara(NCue::Blast).params.size() == 2);
	FXT_CHECK(c.Niagara(NCue::Blast).params[0].first == "Foo" && c.Niagara(NCue::Blast).params[0].second == "2");
	FXT_CHECK(c.Niagara(NCue::AirPush).Wants(0.5f, 0));
	FXT_CHECK(warn.find("niagara.air_push.bogus") != std::string::npos);
	FXT_CHECK(warn.find("niagara.nope") != std::string::npos);
	FxConfig r;
	FXT_CHECK(r.LoadJson(c.ToJson(), &err, &warn));
	FXT_CHECK(r.ToJson() == c.ToJson());
}

FXT_TEST(niagara_prewarm_point_behind_floor) {
	// third-person camera 3 m up looking 20 degrees down at a flat floor (no arena: ground 0)
	const Vec3 cam(0.0f, 3.0f, 8.0f);
	const Vec3 fwd = Norm(Vec3(0.0f, -0.364f, -1.0f));
	Vec3 p;
	FXT_CHECK(PointBehindFloor(nullptr, cam, fwd, 3.5f, 1.5f, 40.0f, p));
	FXT_NEAR(p.y, -3.5f, 0.15f);                         // depth below the floor
	FXT_NEAR(Norm(p - cam).dot(fwd), 1.0f, 1e-4);        // on the view ray: screen centre, inside the frustum
	// the distance cap trades depth for range, down to minDepth
	FXT_CHECK(PointBehindFloor(nullptr, cam, fwd, 3.5f, 1.5f, 14.0f, p));
	FXT_CHECK((p - cam).length() <= 14.0f + 1e-3f && p.y < -1.5f && p.y > -3.5f);
	FXT_CHECK(!PointBehindFloor(nullptr, cam, fwd, 3.5f, 1.5f, 10.0f, p));
	// looking at the horizon or up: nothing to hide behind
	FXT_CHECK(!PointBehindFloor(nullptr, cam, Vec3(0.0f, 0.0f, -1.0f), 3.5f, 1.5f, 40.0f, p));
	FXT_CHECK(!PointBehindFloor(nullptr, cam, Norm(Vec3(0.0f, 0.3f, -1.0f)), 3.5f, 1.5f, 40.0f, p));
}

namespace {

ff::BodyView LBody(int id, ff::Mat m, ff::Form f, const std::string& tag) {
	ff::BodyView b;
	b.id = id;
	b.mat = m;
	b.form = f;
	b.tag = tag;
	b.pos = Vec3(1.0f, 0.5f, -2.0f);
	b.radius = 0.4f;
	b.mass = 10.0f;
	b.props = ff::Value(ff::Dict());
	return b;
}

}  // namespace

FXT_TEST(niagara_loops_follow_views) {
	ff::BodyView field = LBody(1, ff::Mat::Fire, ff::Form::Zone, "fire_field");
	field.zone_radius = 2.0f;
	field.age = 1.0f;   // past its 0.3 s fade-in
	field.max_life = 6.0f;
	ff::BodyView ball = LBody(2, ff::Mat::Fire, ff::Form::Chunk, "fireball");
	ball.vel = Vec3(0.0f, 0.0f, 8.0f);
	ff::BodyView steam = LBody(3, ff::Mat::Steam, ff::Form::Cloud, "");
	auto run = [&](uint64_t loaded, DrawList& out1, DrawList& out2) {
		FxDirector d;
		ff::Snapshot prev, curr;
		curr.bodies = {field, ball, steam};
		prev = curr;
		FxFrameIn in;
		in.prev = &prev;
		in.curr = &curr;
		in.niagaraLoopsLoaded = loaded;
		out1 = d.Update(in);
		++curr.tick;
		out2 = d.Update(in);
	};
	DrawList a1, a2, b1, b2;
	run(0, a1, a2);
	FXT_CHECK(a1.loops.empty());   // nothing loaded: procedural only
	run(~0ULL, b1, b2);
	std::set<int> cues;
	for (const LoopReq& l : b1.loops) {
		cues.insert(static_cast<int>(l.cue));
		FXT_CHECK(l.scale > 0.0f && l.scale < 3.0f);
		FXT_NEAR(l.dir.length(), 1.0f, 1e-3);
	}
	FXT_CHECK(cues.count(static_cast<int>(LCue::Fire)) == 1);
	FXT_CHECK(cues.count(static_cast<int>(LCue::TrailFire)) == 1);
	FXT_CHECK(cues.count(static_cast<int>(LCue::Steam)) == 1);
	int fires = 0;
	for (const LoopReq& l : b1.loops) fires += l.cue == LCue::Fire ? 1 : 0;
	FXT_CHECK(fires >= 2 && fires <= 4);   // a 2 m field: centre + ring (GPU budget: <= 4 systems)
	// keys are stable frame to frame (the glue keeps one component per key)
	FXT_CHECK(b1.loops.size() == b2.loops.size());
	for (size_t i = 0; i < b1.loops.size() && i < b2.loops.size(); ++i) FXT_CHECK(b1.loops[i].key == b2.loops[i].key);
	// the fireball trail turns back along the flight (smoothed from straight up on its first frame)
	for (const LoopReq& l : b2.loops)
		if (l.cue == LCue::TrailFire) FXT_CHECK(l.dir.z < -0.3f);
}

FXT_TEST(niagara_bolt_arcs_and_wind_ribbons) {
	// a bolt asks for an arc from its first node to its last
	{
		FxDirector d;
		NFrame f;
		f.events.clear();
		f.events.push_back(NEv("lightning", {{"actor", 1}, {"path", ff::Value(ff::Array({ff::Value(Vec3(0, 1, 0)), ff::Value(Vec3(4, 1, 0))}))}}));
		f.in.niagaraLoaded = ~0ULL;
		const DrawList& dl = d.Update(f.in);
		bool arc = false;
		for (const SystemReq& r : dl.systems)
			if (r.cue == NCue::BoltArc) arc = (r.target - r.pos).length() > 2.0f;
		FXT_CHECK(arc);
	}
	// a tornado gets three circling ribbons, a crescent two tip ribbons
	ff::BodyView tw = LBody(5, ff::Mat::Air, ff::Form::Zone, "tornado");
	tw.zone_radius = 1.5f;
	tw.age = 1.0f;
	ff::BodyView cr = LBody(6, ff::Mat::Air, ff::Form::Chunk, "crescent");
	cr.vel = Vec3(6.0f, 0.0f, 0.0f);
	FxDirector d;
	FxConfig cfg;   // the wind slot ships empty until it is look-checked: give it a system here
	cfg.niagaraLoops[static_cast<size_t>(LCue::Wind)].path = "/Game/NiagaraExamples/FX_Weapons/Trails/NS_SimpleRibbonTrail";
	d.SetConfig(cfg);
	ff::Snapshot prev, curr;
	curr.bodies = {tw, cr};
	prev = curr;
	FxFrameIn in;
	in.prev = &prev;
	in.curr = &curr;
	in.niagaraLoopsLoaded = ~0ULL;
	const DrawList& dl = d.Update(in);
	int wind = 0;
	for (const LoopReq& l : dl.loops) wind += l.cue == LCue::Wind ? 1 : 0;
	FXT_CHECK(wind >= 3);
}

FXT_TEST(niagara_loops_json) {
	FxConfig c;
	FXT_CHECK(c.Loop(LCue::Fire).path.find("NS_Fire") != std::string::npos);
	std::string err, warn;
	FXT_CHECK(c.LoadJson(R"({"niagara_loops": {"steam": {"path": "", "scale": 2}, "lava": {}}})", &err, &warn));
	FXT_CHECK(c.Loop(LCue::Steam).path.empty());
	FXT_NEAR(c.Loop(LCue::Steam).scale, 2.0f, 1e-6);
	FXT_CHECK(warn.find("niagara_loops.lava") != std::string::npos);
	FxConfig r;
	FXT_CHECK(r.LoadJson(c.ToJson(), &err, &warn));
	FXT_CHECK(r.ToJson() == c.ToJson());
}

#endif  // FF_LOGIC_TESTS
