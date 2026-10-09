// FourfoldFX logic island - world reaction tests (FxWorld: footfall dust, ground scars, pool, gusts, MPC wind).
// Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxDirector.h"

#include <string>

using namespace ffx;
using ff::Form;
using ff::Mat;

namespace {

struct WFrame {
	ff::Snapshot prev, curr;
	std::vector<ff::Event> events;
	ff::ArenaView arena;
	FxFrameIn in;
	explicit WFrame(int quality = 2) {
		arena.pool_min = ff::Vec2(7.0f, -5.0f);
		arena.pool_max = ff::Vec2(13.0f, 3.0f);
		arena.pool_floor = -0.3f;
		arena.pool_level = -0.05f;
		in.prev = &prev;
		in.curr = &curr;
		in.events = &events;
		in.arena = &arena;
		in.dt = 1.0f / 60.0f;
		in.quality = quality;
		in.cam.pos = Vec3(0.0f, 4.0f, 9.0f);
	}
	void Tick() {
		prev = curr;
		++curr.tick;
	}
};

ff::Event WEv(const std::string& type, std::initializer_list<std::pair<const char*, ff::Value>> kv) {
	ff::Event e;
	e.type = type;
	ff::Dict d;
	for (const auto& p : kv) d.set(p.first, p.second);
	e.data = ff::Value(d);
	return e;
}

ff::BodyView WBody(int id, Mat m, Form f, const Vec3& pos, float mass = 20.0f) {
	ff::BodyView b;
	b.id = id;
	b.mat = m;
	b.form = f;
	b.pos = pos;
	b.radius = 0.3f;
	b.mass = mass;
	b.liquid = m == Mat::Water ? 1.0f : 0.0f;
	b.props = ff::Value(ff::Dict());
	return b;
}

ff::ActorView WActor(int id, const Vec3& pos) {
	ff::ActorView a;
	a.id = id;
	a.pos = pos;
	a.grounded = true;
	a.surface = "stone";
	return a;
}

// Ground decals of one style this frame.
int CountStyle(const DrawList& dl, MatSlot m, float style) {
	int n = 0;
	for (const DrawItem& it : dl.items)
		if (it.mat == m && std::fabs(it.params.Get(P::Style, -1.0f) - style) < 0.01f) ++n;
	return n;
}
int CountMat(const DrawList& dl, MatSlot m) {
	int n = 0;
	for (const DrawItem& it : dl.items) n += it.mat == m ? 1 : 0;
	return n;
}
const DrawItem* FirstStyle(const DrawList& dl, MatSlot m, float style) {
	for (const DrawItem& it : dl.items)
		if (it.mat == m && std::fabs(it.params.Get(P::Style, -1.0f) - style) < 0.01f) return &it;
	return nullptr;
}

void Run(FxDirector& d, WFrame& f, int frames) {
	for (int i = 0; i < frames; ++i) {
		f.Tick();
		d.Update(f.in);
		f.events.clear();
	}
}

}  // namespace

FXT_TEST(world_impacts_scar_the_floor_by_material) {
	FxDirector d;
	WFrame f;
	f.curr.actors = {WActor(1, Vec3(0, 0, 4))};
	// a heavy stone hitting the floor, a blast, a water burst, a lightning strike: four scars at four places
	f.events.push_back(WEv("impact", {{"body", 77}, {"on", "ground"}, {"speed", 12.0f}, {"mass", 40.0f}, {"mat", "stone"}}));
	f.events.push_back(WEv("fx", {{"fx", "burst"}, {"mat", "blast"}, {"tier", 2}, {"pos", Vec3(-3, 0.5f, 0)}, {"radius", 2.0f}}));
	f.events.push_back(WEv("fx", {{"fx", "burst"}, {"mat", "water"}, {"tier", 1}, {"pos", Vec3(3, 0.3f, -2)}, {"radius", 1.5f}}));
	f.events.push_back(WEv("lightning", {{"actor", 1}, {"path", ff::Value(ff::Array({ff::Value(Vec3(0, 6, -4)), ff::Value(Vec3(0.5f, 0.1f, -4))}))}}));
	f.Tick();
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(CountStyle(dl, MatSlot::Ground, 9.0f) == 1);    // crack (body 77 has no view: at the origin)
	FXT_CHECK(CountStyle(dl, MatSlot::Ground, 10.0f) == 1);   // scorch
	FXT_CHECK(CountStyle(dl, MatSlot::Ground, 8.0f) == 1);    // wet mark
	FXT_CHECK(CountStyle(dl, MatSlot::Ground, 11.0f) == 1);   // lightning burn
	FXT_CHECK(dl.lights.size() >= 1u);                        // the strike lights its surroundings
	FXT_CHECK(d.Stats().worldFx >= 6);                        // + dust rings
	f.events.clear();
	// cracks outlive the dust; everything is gone after the longest life (10 s)
	Run(d, f, 60 * 4);
	FXT_CHECK(CountStyle(d.Update(f.in), MatSlot::Ground, 9.0f) == 1);
	Run(d, f, 60 * 7);
	FXT_CHECK(d.Stats().worldFx == 0);
}

FXT_TEST(world_scars_merge_and_respect_the_quality_cap) {
	for (int q = 0; q < 3; ++q) {
		FxDirector d;
		WFrame f(q);
		f.curr.actors = {WActor(1, Vec3(0, 0, 4))};
		f.Tick();
		d.Update(f.in);
		// the same spot twice: one scar
		for (int i = 0; i < 2; ++i)
			f.events.push_back(WEv("fx", {{"fx", "erupt"}, {"mat", "stone"}, {"tier", 2}, {"pos", Vec3(-2, 0, -2)}, {"radius", 1.0f}}));
		f.Tick();
		FXT_CHECK(CountStyle(d.Update(f.in), MatSlot::Ground, 9.0f) == 1);
		f.events.clear();
		// thirty different places: never more than the quality's cap
		for (int i = 0; i < 30; ++i)
			f.events.push_back(WEv("fx", {{"fx", "erupt"}, {"mat", "stone"}, {"tier", 2},
			                              {"pos", Vec3(-12.0f + 0.8f * static_cast<float>(i), 0, 5)}, {"radius", 1.0f}}));
		f.Tick();
		const DrawList& dl = d.Update(f.in);
		FXT_CHECK(CountStyle(dl, MatSlot::Ground, 9.0f) == d.Config().Q(q).scars);
		FXT_CHECK(CountMat(dl, MatSlot::Ground) <= 16);
	}
}

FXT_TEST(world_pool_splashes_ripples_and_steam) {
	FxDirector d;
	WFrame f;
	f.curr.actors = {WActor(1, Vec3(0, 0, 4))};
	// a stone dropping into the pool
	ff::BodyView st = WBody(5, Mat::Stone, Form::Chunk, Vec3(10.0f, 1.0f, -1.0f), 40.0f);
	st.vel = Vec3(0.0f, -6.0f, 0.0f);
	f.curr.bodies = {st};
	int ripples = 0, splashes = 0, cracks = 0;
	for (int i = 0; i < 30; ++i) {
		f.Tick();
		f.curr.bodies[0].pos.y = MaxF(1.0f - 0.1f * static_cast<float>(i), -0.25f);
		const DrawList& dl = d.Update(f.in);
		ripples = MaxI(ripples, CountStyle(dl, MatSlot::Ring, 5.0f));
		splashes = MaxI(splashes, CountMat(dl, MatSlot::Splash));
		cracks += CountStyle(dl, MatSlot::Ground, 9.0f);
	}
	FXT_CHECK(ripples >= 1);
	FXT_CHECK(splashes >= 1);
	FXT_CHECK(cracks == 0);   // no floor scars on water
	// an impact event inside the pool leaves no scar either
	f.events.push_back(WEv("fx", {{"fx", "burst"}, {"mat", "stone"}, {"tier", 2}, {"pos", Vec3(9, 0.2f, 0)}, {"radius", 1.0f}}));
	f.Tick();
	FXT_CHECK(CountStyle(d.Update(f.in), MatSlot::Ground, 9.0f) == 0);
	f.events.clear();
	// a fireball skimming low over the water raises steam
	FxDirector d2;
	WFrame g;
	g.curr.actors = {WActor(1, Vec3(0, 0, 4))};
	ff::BodyView fb = WBody(9, Mat::Fire, Form::Chunk, Vec3(8.0f, 0.6f, 0.0f), 2.0f);
	fb.tag = "fireball";
	fb.vel = Vec3(6.0f, 0.0f, 0.0f);
	g.curr.bodies = {fb};
	int steam = 0;
	for (int i = 0; i < 40; ++i) {
		g.Tick();
		g.curr.bodies[0].pos.x = 8.0f + 0.1f * static_cast<float>(i);
		const DrawList& dl = d2.Update(g.in);
		for (const DrawItem& it : dl.items) steam += it.params.flipbook == Flipbook::SteamPuff ? 1 : 0;
	}
	FXT_CHECK(steam > 0);
}

FXT_TEST(world_fighter_wading_and_drawing_ripple_the_pool) {
	FxDirector d;
	WFrame f;
	ff::ActorView a = WActor(1, Vec3(6.0f, 0.0f, 0.0f));
	a.vel = Vec3(3.0f, 0.0f, 0.0f);
	f.curr.actors = {a};
	int ripples = 0, splashes = 0;
	for (int i = 0; i < 90; ++i) {
		f.Tick();
		ff::ActorView& ac = f.curr.actors[0];
		ac.pos.x = 6.0f + 0.05f * static_cast<float>(i);
		ac.in_water = ac.pos.x > 7.0f;
		if (ac.in_water) ac.pos.y = -0.3f;
		const DrawList& dl = d.Update(f.in);
		ripples += CountStyle(dl, MatSlot::Ring, 5.0f);
		splashes += CountMat(dl, MatSlot::Splash);
	}
	FXT_CHECK(ripples > 10);
	FXT_CHECK(splashes > 0);   // stepping in
	// drawing water out of the pool: rings close in on the draw point
	FxDirector d2;
	WFrame g;
	g.curr.actors = {WActor(1, Vec3(6.0f, 0.0f, 0.0f))};
	g.events.push_back(WEv("draw_water", {{"actor", 1}, {"at", Vec3(9.0f, -0.05f, 0.0f)}}));
	g.Tick();
	FXT_CHECK(CountStyle(d2.Update(g.in), MatSlot::Ring, 5.0f) >= 2);
}

FXT_TEST(world_footfalls_skids_and_quality) {
	for (int q : {0, 2}) {
		FxDirector d;
		WFrame f(q);
		ff::ActorView a = WActor(1, Vec3(-6.0f, 0.0f, 0.0f));
		f.curr.actors = {a};
		Run(d, f, 20);
		// start running: a kick at once, then puffs at the stride clock while running
		int puffs = 0;
		for (int i = 0; i < 120; ++i) {
			f.Tick();
			ff::ActorView& ac = f.curr.actors[0];
			ac.vel = Vec3(5.0f, 0.0f, 0.0f);
			ac.pos.x += 5.0f / 60.0f;
			const DrawList& dl = d.Update(f.in);
			if (i == 12) FXT_CHECK(CountMat(dl, MatSlot::Smoke) >= 1);   // the start kick
			if (i > 70) puffs += CountMat(dl, MatSlot::Smoke) > 0 ? 1 : 0;
		}
		if (q == 0) FXT_CHECK(puffs == 0);   // low quality: no per-step puffs
		else FXT_CHECK(puffs > 25);
		// a sudden stop skids
		for (int i = 0; i < 12; ++i) {
			f.Tick();
			f.curr.actors[0].vel = Vec3(0.0f, 0.0f, 0.0f);
			d.Update(f.in);
		}
		FXT_CHECK(d.Stats().worldFx >= 1);
	}
}

FXT_TEST(world_foot_plants_from_bone_anchors_and_wet_steps) {
	// animated feet (the glue's bones): a puff each time a foot comes down; in a puddle the steps splash instead
	for (const char* surface : {"stone", "puddle"}) {
		FxDirector d;
		WFrame f;
		ff::ActorView a = WActor(1, Vec3(-6.0f, 0.0f, 0.0f));
		a.surface = surface;
		a.vel = Vec3(5.0f, 0.0f, 0.0f);
		f.curr.actors = {a};
		std::vector<FxAnchors> anchors(1);
		anchors[0].actor = 1;
		f.in.anchors = &anchors;
		Run(d, f, 10);   // the run start is over
		int smoke = 0, splash = 0;
		for (int i = 0; i < 120; ++i) {
			f.Tick();
			ff::ActorView& ac = f.curr.actors[0];
			ac.pos.x += 5.0f / 60.0f;
			const float ph = static_cast<float>(i) / 60.0f * kFxTau * 1.6f;   // 1.6 strides per second
			for (Vec3& b : anchors[0].bones) b = ac.pos + Vec3(0.0f, 1.0f, 0.0f);
			anchors[0].bones[static_cast<size_t>(Bone::FootL)] = ac.pos + Vec3(0.0f, 0.1f + 0.25f * MaxF(std::sin(ph), 0.0f), -0.14f);
			anchors[0].bones[static_cast<size_t>(Bone::FootR)] = ac.pos + Vec3(0.0f, 0.1f + 0.25f * MaxF(-std::sin(ph), 0.0f), 0.14f);
			const DrawList& dl = d.Update(f.in);
			smoke += CountMat(dl, MatSlot::Smoke);
			splash += CountMat(dl, MatSlot::Splash);
		}
		if (std::string(surface) == "stone") {
			FXT_CHECK(smoke > 30);
			FXT_CHECK(splash == 0);
		} else {
			FXT_CHECK(splash > 10);
		}
	}
}

FXT_TEST(world_landings_and_stomps) {
	FxDirector d;
	WFrame f;
	ff::ActorView a = WActor(1, Vec3(-3.0f, 0.0f, 0.0f));
	a.element = 0;
	f.curr.actors = {a};
	f.events.push_back(WEv("land", {{"actor", 1}, {"speed", 9.0f}}));   // a heavy landing cracks the floor
	f.Tick();
	FXT_CHECK(CountStyle(d.Update(f.in), MatSlot::Ground, 9.0f) == 1);
	f.events.clear();
	Run(d, f, 30);
	// an Earth stance stamps (a dust ring; a crack only when strong enough), with a cooldown
	f.curr.actors[0].pos = Vec3(3.0f, 0.0f, 0.0f);
	f.events.push_back(WEv("stance", {{"actor", 1}, {"stance", "anchor"}, {"on", true}}));
	f.events.push_back(WEv("stance", {{"actor", 1}, {"stance", "anchor"}, {"on", true}}));
	f.Tick();
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(CountStyle(dl, MatSlot::Ground, 9.0f) == 2);
}

FXT_TEST(world_gusts_sweep_dust_and_drive_the_arena_wind) {
	FxDirector d;
	WFrame f;
	f.curr.actors = {WActor(1, Vec3(-4.0f, 0.0f, 0.0f))};
	f.Tick();
	FXT_CHECK(d.Update(f.in).env.windGust == 0.0f);
	f.events.push_back(WEv("gust", {{"actor", 1}, {"dir", Vec3(0, 0, -1)}, {"range", 5.0f}, {"heavy", true}}));
	f.Tick();
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(dl.env.windGust > 0.8f);
	FXT_CHECK(dl.env.windDirY < -0.5f);               // Unreal Y = sim z
	FXT_CHECK(std::fabs(dl.env.windDirX) < 0.5f);
	FXT_CHECK(CountMat(dl, MatSlot::Smoke) >= 1);     // floor dust swept along
	f.events.clear();
	Run(d, f, 60 * 5);
	FXT_CHECK(d.Update(f.in).env.windGust < 0.05f);  // calm again
	// a tornado keeps a steady gust while it lives
	ff::BodyView tw = WBody(3, Mat::Air, Form::Zone, Vec3(2.0f, 0.0f, 0.0f));
	tw.tag = "tornado";
	tw.zone_radius = 1.3f;
	tw.vel = Vec3(1.0f, 0.0f, 0.0f);
	tw.age = 1.0f;
	f.curr.bodies = {tw};
	Run(d, f, 30);
	FXT_CHECK(d.Update(f.in).env.windGust > 0.2f);
	// the MPC switch off: no gust is sent
	FxConfig cfg = d.Config();
	cfg.world.mpc = false;
	d.SetConfig(cfg);
	FXT_CHECK(d.Update(f.in).env.windGust == 0.0f);
}

FXT_TEST(world_puddles_leave_drying_wet_marks) {
	FxDirector d;
	WFrame f;
	f.curr.actors = {WActor(1, Vec3(-4.0f, 0.0f, 0.0f))};
	ff::BodyView pd = WBody(4, Mat::Water, Form::Puddle, Vec3(-1.0f, 0.0f, -1.0f));
	pd.radius = 0.8f;
	f.curr.bodies = {pd};
	Run(d, f, 10);
	f.curr.bodies.clear();   // the puddle is gone (soaked away)
	f.Tick();
	const DrawItem* wet = FirstStyle(d.Update(f.in), MatSlot::Ground, 8.0f);
	FXT_CHECK(wet != nullptr);
	const float dry0 = wet ? wet->params.Get(P::Heat) : 1.0f;
	Run(d, f, 60 * 4);
	const DrawItem* later = FirstStyle(d.Update(f.in), MatSlot::Ground, 8.0f);
	FXT_CHECK(later != nullptr);
	if (later) FXT_CHECK(later->params.Get(P::Heat) > dry0 + 0.3f);   // drying from the edges
	Run(d, f, 60 * 5);
	FXT_CHECK(FirstStyle(d.Update(f.in), MatSlot::Ground, 8.0f) == nullptr);
}

FXT_TEST(world_showcase_cues_play) {
	// ff.fx.Showcase "world/<kind>" events: every kind shows something; the pool cue lands on the water
	const char* kinds[] = {"crack", "scorch", "bolt", "wet", "pool", "gust"};
	for (const char* k : kinds) {
		FxDirector d;
		WFrame f;
		f.curr.actors = {WActor(1, Vec3(0, 0, 4))};
		f.events.push_back(WEv("fx_test_world", {{"kind", k}, {"pos", Vec3(0, 0, 0)}, {"dir", Vec3(1, 0, 0)}}));
		f.Tick();
		const DrawList& dl = d.Update(f.in);
		FXT_CHECK(d.Stats().worldFx >= 1);
		if (std::string(k) == "pool") FXT_CHECK(CountStyle(dl, MatSlot::Ring, 5.0f) >= 1);
		if (std::string(k) == "gust") FXT_CHECK(dl.env.windGust > 0.5f);
	}
}

FXT_TEST(world_config_json_roundtrip) {
	FxConfig a;
	a.world.scarLife = 4.5f;
	a.world.chips = false;
	a.quality[1].scars = 3;
	a.mpcPath = "/Game/X/MPC_Test.MPC_Test";
	FxConfig b;
	std::string err;
	FXT_CHECK(b.LoadJson(a.ToJson(), &err));
	FXT_NEAR(b.world.scarLife, 4.5f, 1e-4f);
	FXT_CHECK(!b.world.chips);
	FXT_CHECK(b.quality[1].scars == 3);
	FXT_CHECK(b.mpcPath == a.mpcPath);
}

#endif  // FF_LOGIC_TESTS
