// FourfoldFX logic island - mapping and director tests (synthetic snapshots and events). Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxDirector.h"

#include <set>
#include <string>

using namespace ffx;
using ff::Form;
using ff::Mat;
using ff::Phase;

namespace {

ff::BodyView Body(int id, Mat m, Form f, const std::string& tag = "", Phase ph = Phase::Solid) {
	ff::BodyView b;
	b.id = id;
	b.mat = m;
	b.form = f;
	b.tag = tag;
	b.phase = ph;
	b.pos = Vec3(0.0f, 0.5f, 0.0f);
	b.radius = 0.3f;
	b.mass = 10.0f;
	b.liquid = (m == Mat::Water) ? 1.0f : 0.0f;
	b.props = ff::Value(ff::Dict());
	return b;
}

ViewSel Sel(const ff::BodyView& b) { return SelectView(b); }

struct Frame {
	ff::Snapshot prev, curr;
	std::vector<ff::Event> events;
	FxFrameIn in;
	Frame() {
		in.prev = &prev;
		in.curr = &curr;
		in.events = &events;
		in.dt = 1.0f / 60.0f;
	}
	void Tick() {
		prev = curr;
		++curr.tick;
	}
};

ff::Event Ev(const std::string& type, std::initializer_list<std::pair<const char*, ff::Value>> kv) {
	ff::Event e;
	e.type = type;
	ff::Dict d;
	for (const auto& p : kv) d.set(p.first, p.second);
	e.data = ff::Value(d);
	return e;
}

const DrawItem* FindKeyed(const DrawList& dl, uint32_t key) {
	for (const DrawItem& it : dl.items)
		if (it.key == key) return &it;
	return nullptr;
}

}  // namespace

FXT_TEST(mapping_body_to_view_table) {
	// docs/VFX.md "Body -> view map"
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Chunk)).kind == ViewKind::Stone);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Chunk, "spear")).kind == ViewKind::Stone);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wall)).kind == ViewKind::Wall);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wall, "obsidian")).style == "obsidian");
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Wall)).style == "sand");
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wave)).kind == ViewKind::Wave);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wall, "spikes")).kind == ViewKind::Spikes);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wave, "spike_line")).kind == ViewKind::Spikes);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Wave, "tremor")).kind == ViewKind::Rings);
	FXT_CHECK(Sel(Body(1, Mat::Metal, Form::Chunk, "disc")).style == "disc");
	FXT_CHECK(Sel(Body(1, Mat::Metal, Form::Chunk, "lance")).style == "lance");
	FXT_CHECK(Sel(Body(1, Mat::Metal, Form::Wall, "plate")).style == "plate");
	FXT_CHECK(Sel(Body(1, Mat::Metal, Form::Zone, "caltrops")).style == "caltrops");
	FXT_CHECK(Sel(Body(1, Mat::Metal, Form::Zone, "rod")).style == "planted");
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Chunk, "slug")).style == "slug");
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Wave, "sand_surge")).kind == ViewKind::Strip);
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Wave, "sand_surge")).style == "sand");
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Wave, "water_wave")).style == "water");
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Wave, "rime", Phase::Frozen)).style == "rime");
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Zone, "sandstorm")).style == "sandstorm");
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Zone, "fog")).kind == ViewKind::Cloud);
	FXT_CHECK(Sel(Body(1, Mat::Steam, Form::Cloud)).style == "steam");
	FXT_CHECK(Sel(Body(1, Mat::Sand, Form::Zone, "quicksand")).kind == ViewKind::Decal);
	FXT_CHECK(Sel(Body(1, Mat::Stone, Form::Zone, "lava_pool")).kind == ViewKind::LavaPool);
	FXT_CHECK(Sel(Body(1, Mat::Glass, Form::Wall, "glass")).kind == ViewKind::CrystalWall);
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Wall, "ice", Phase::Frozen)).style == "ice");
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Shard, "needle")).kind == ViewKind::Crystal);
	FXT_CHECK(Sel(Body(1, Mat::Plant, Form::Wall, "vine")).style == "lattice");
	FXT_CHECK(Sel(Body(1, Mat::Plant, Form::Wave, "roots")).style == "roots");
	FXT_CHECK(Sel(Body(1, Mat::Plant, Form::Zone, "briar")).style == "briar");
	FXT_CHECK(Sel(Body(1, Mat::Fire, Form::Chunk, "fireball")).kind == ViewKind::Fireball);
	FXT_CHECK(Sel(Body(1, Mat::Fire, Form::Wave, "fire_line")).style == "line");
	FXT_CHECK(Sel(Body(1, Mat::Fire, Form::Zone, "fire_field")).style == "field");
	FXT_CHECK(Sel(Body(1, Mat::Fire, Form::Chunk, "bomb")).style == "mine");
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Zone, "vacuum_well")).kind == ViewKind::Shell);
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Zone, "static_field")).style == "static_field");
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Wave, "ground_current")).kind == ViewKind::Crackle);
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Chunk, "crescent")).kind == ViewKind::Blade);
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Wall, "wind_wall")).style == "wall");
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Zone, "tornado")).style == "tornado");
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Chunk, "twister")).kind == ViewKind::Vortex);
	FXT_CHECK(Sel(Body(1, Mat::Air, Form::Zone, "flight_field")).kind == ViewKind::Rings);
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Pool)).kind == ViewKind::None);
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Puddle)).kind == ViewKind::Puddle);
	FXT_CHECK(Sel(Body(1, Mat::Water, Form::Blob)).kind == ViewKind::Blob);
	ff::BodyView str = Body(1, Mat::Water, Form::Stream);
	FXT_CHECK(Sel(str).kind == ViewKind::Ribbon);
	str.controller = 2;
	FXT_CHECK(Sel(str).kind == ViewKind::Blob);
	ff::BodyView settled = Body(1, Mat::Stone, Form::Chunk);
	settled.wave_path = {Vec3(0, 0, 0), Vec3(0, 0, 1)};
	FXT_CHECK(Sel(settled).kind == ViewKind::Wave);
}

FXT_TEST(director_lifecycle_fade_and_keys) {
	FxDirector d;
	Frame f;
	ff::BodyView rock = Body(7, Mat::Stone, Form::Chunk);
	rock.temp = 900.0f;   // glowing
	f.curr.bodies = {rock};
	f.Tick();
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(dl.items.size() == 1u);
	const uint32_t key = dl.items[0].key;
	FXT_CHECK(dl.items[0].kind == DrawKind::StaticMesh);
	FXT_CHECK(dl.items[0].mesh && dl.items[0].mesh->Valid());
	FXT_NEAR(dl.items[0].params.Get(P::Heat), (900.0f - 250.0f) / 750.0f, 1e-4);
	FXT_CHECK(dl.lights.size() == 1u);
	// the body disappears: the view keeps drawing with a falling fade, then stops
	f.events.clear();
	f.curr.bodies.clear();
	f.Tick();
	const DrawList& d2 = d.Update(f.in);
	const DrawItem* it = FindKeyed(d2, key);
	FXT_CHECK(it != nullptr);
	if (it) FXT_CHECK(it->params.Get(P::Fade) < 1.0f);
	for (int i = 0; i < 30; ++i) {
		f.Tick();
		d.Update(f.in);
	}
	FXT_CHECK(d.Update(f.in).items.empty());
	// a new body gets new keys
	f.curr.bodies = {Body(8, Mat::Stone, Form::Chunk)};
	f.Tick();
	const DrawList& d3 = d.Update(f.in);
	FXT_CHECK(d3.items.size() == 1u && d3.items[0].key > key);
}

FXT_TEST(director_representation_change_crossfades) {
	FxDirector d;
	Frame f;
	ff::BodyView b = Body(3, Mat::Stone, Form::Chunk);
	f.curr.bodies = {b};
	f.Tick();
	const uint32_t stoneKey = d.Update(f.in).items.at(0).key;
	// the stone melts and pours into a wave: same id, new representation
	b.form = Form::Wave;
	b.liquid = 1.0f;
	b.wave_dir = Vec3(0, 0, 1);
	b.wave_width = 1.2f;
	b.wave_path = {Vec3(0, 0, 0), Vec3(0, 0, 0.5f)};
	b.pos = Vec3(0, 0, 1.0f);
	f.curr.bodies = {b};
	f.Tick();
	const DrawList& dl = d.Update(f.in);
	bool hasStone = false, hasStrip = false;
	for (const DrawItem& it : dl.items) {
		hasStone = hasStone || (it.key == stoneKey && it.params.Get(P::Fade) < 1.0f);
		hasStrip = hasStrip || it.mat == MatSlot::LavaStrip;
	}
	FXT_CHECK(hasStone);
	FXT_CHECK(hasStrip);
	FXT_CHECK(d.Stats().views == 1 && d.Stats().dyingViews == 1);
}

FXT_TEST(director_every_family_draws_valid_meshes) {
	FxDirector d;
	Frame f;
	std::vector<ff::BodyView> bs;
	int id = 1;
	auto add = [&](ff::BodyView b) {
		b.id = id++;
		b.pos = Vec3(static_cast<float>(id) * 1.5f - 20.0f, 0.5f, 0.0f);
		bs.push_back(b);
	};
	add(Body(0, Mat::Stone, Form::Chunk, "spear"));
	{
		ff::BodyView w = Body(0, Mat::Stone, Form::Wall);
		w.wall_rise = 0.6f;
		add(w);
	}
	{
		ff::BodyView w = Body(0, Mat::Stone, Form::Wave);
		w.liquid = 1.0f;
		w.wave_width = 1.4f;
		w.wave_path = {Vec3(-1, 0, 0), Vec3(-1, 0, 1), Vec3(-1, 0, 2)};
		add(w);
	}
	add(Body(0, Mat::Water, Form::Blob));
	add(Body(0, Mat::Water, Form::Puddle));
	add(Body(0, Mat::Metal, Form::Chunk, "disc"));
	add(Body(0, Mat::Metal, Form::Chunk, "lance"));
	{
		ff::BodyView z = Body(0, Mat::Metal, Form::Zone, "caltrops");
		z.zone_radius = 1.2f;
		add(z);
	}
	{
		ff::BodyView z = Body(0, Mat::Sand, Form::Zone, "sandstorm");
		z.zone_radius = 2.0f;
		z.age = 1.0f;
		add(z);
	}
	{
		ff::BodyView w = Body(0, Mat::Water, Form::Wave, "water_wave");
		w.wave_width = 1.4f;
		w.wave_path = {Vec3(3, 0, 0), Vec3(3, 0, 1)};
		add(w);
	}
	add(Body(0, Mat::Glass, Form::Wall, "glass"));
	add(Body(0, Mat::Water, Form::Shard, "needle"));
	{
		ff::BodyView s = Body(0, Mat::Stone, Form::Wall, "spikes");
		s.wall_half = Vec3(1.0f, 0.5f, 0.3f);
		s.wall_rise = 1.0f;
		add(s);
	}
	{
		ff::BodyView v = Body(0, Mat::Plant, Form::Wall, "vine");
		v.wall_rise = 1.0f;
		add(v);
	}
	{
		ff::BodyView z = Body(0, Mat::Fire, Form::Zone, "fire_field");
		z.zone_radius = 1.5f;
		z.age = 1.0f;
		add(z);
	}
	{
		ff::BodyView fb = Body(0, Mat::Fire, Form::Chunk, "fireball");
		fb.vel = Vec3(0, 0, 10);
		add(fb);
	}
	{
		ff::BodyView z = Body(0, Mat::Air, Form::Zone, "vacuum_well");
		z.zone_radius = 1.5f;
		add(z);
	}
	{
		ff::BodyView z = Body(0, Mat::Air, Form::Zone, "tornado");
		z.zone_radius = 1.6f;
		z.spin = 6.0f;
		z.age = 1.0f;
		add(z);
	}
	{
		ff::BodyView cr = Body(0, Mat::Air, Form::Chunk, "crescent");
		cr.vel = Vec3(0, 0, 8);
		add(cr);
	}
	{
		ff::BodyView g = Body(0, Mat::Air, Form::Wave, "ground_current");
		g.wave_path = {Vec3(5, 0, 0), Vec3(5, 0, 1), Vec3(5, 0, 2)};
		g.power = 20.0f;
		add(g);
	}
	{
		ff::BodyView z = Body(0, Mat::Sand, Form::Zone, "quicksand");
		z.zone_radius = 1.5f;
		add(z);
	}
	{
		ff::BodyView ch = Body(0, Mat::Metal, Form::Chunk, "rod");
		ch.charge = 20.0f;   // crackle overlay
		add(ch);
	}
	f.curr.bodies = bs;
	std::set<MatSlot> mats;
	for (int frame = 0; frame < 40; ++frame) {
		f.Tick();
		const DrawList& dl = d.Update(f.in);
		std::set<uint32_t> keys;
		for (const DrawItem& it : dl.items) {
			FXT_CHECK(keys.insert(it.key).second);
			FXT_CHECK(it.mesh != nullptr);
			if (it.mesh) FXT_CHECK(it.mesh->Valid());
			mats.insert(it.mat);
		}
		FXT_CHECK(dl.lights.size() <= 4u);
	}
	FXT_CHECK(d.Stats().unmappedBodies == 0);
	FXT_CHECK(d.Stats().views == static_cast<int>(bs.size()));
	for (MatSlot m : {MatSlot::Rock, MatSlot::LavaStrip, MatSlot::Metal, MatSlot::Crystal, MatSlot::Water, MatSlot::Vine,
	                  MatSlot::Flame, MatSlot::Smoke, MatSlot::Lightning, MatSlot::Shell, MatSlot::Vortex, MatSlot::Wind,
	                  MatSlot::Ground, MatSlot::Ring})
		if (mats.count(m) != 1u) {
			fxt::Fail(__FILE__, __LINE__, "material never drawn: " + std::string(MatSlotName(m)));
		}
}

FXT_TEST(director_events_spawn_and_finish_one_shots) {
	FxDirector d;
	Frame f;
	ff::ActorView a;
	a.id = 1;
	a.pos = Vec3(0, 0, 0);
	a.element = 2;
	f.curr.actors = {a};
	const char* mats[] = {"stone", "metal", "sand", "glass", "magma", "water", "ice", "mist", "steam", "plant",
	                      "flame", "blue", "lightning", "blast", "wind", "vortex", "vacuum", "sound"};
	const char* keys[] = {"cast", "release", "cone", "beam", "burst", "ring", "erupt", "trail", "splash", "aura"};
	for (const char* k : keys)
		for (const char* m : mats)
			f.events.push_back(Ev("fx", {{"fx", k}, {"mat", m}, {"actor", 1}, {"tier", 2}, {"pos", Vec3(0, 1.2f, 0)},
			                             {"dir", Vec3(0, 0, 1)}, {"radius", 1.5f}, {"length", 4.0f}}));
	const char* outcomes[] = {"block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform",
	                          "shatter", "sink", "conduct", "ground", "pass", "amplify", "extinguish", "weaken", "bend",
	                          "slow", "overwhelm", "clash", "disrupt", "neutralize", "heat", "push", "disperse"};
	for (const char* o : outcomes)
		f.events.push_back(Ev("interaction", {{"outcome", o}, {"threat", "ice"}, {"counter", "flame"}, {"pos", Vec3(1, 1, 0)},
		                                      {"dir", Vec3(1, 0, 0)}, {"perfect", true}, {"to", "steam"}}));
	f.events.push_back(Ev("lightning", {{"actor", 1}, {"path", ff::Value(ff::Array({ff::Value(Vec3(0, 1, 0)), ff::Value(Vec3(4, 1, 0))}))}}));
	f.events.push_back(Ev("charge", {{"actor", 1}, {"tier", 3}, {"element", 2}, {"sub", 2}}));
	f.events.push_back(Ev("status", {{"actor", 1}, {"status", "burning"}, {"on", true}}));
	f.events.push_back(Ev("zone", {{"kind", "fire_field"}, {"pos", Vec3(2, 0, 2)}, {"radius", 1.5f}, {"phase", "open"}}));
	f.events.push_back(Ev("clash", {{"pos", Vec3(0, 1, 1)}, {"mat", "flame"}}));
	for (const char* t : {"hit", "block", "perfect_deflect", "launch", "impact", "wall", "wall_crumble", "shatter", "flare",
	                      "vent", "gust", "lash", "evade", "updraft", "land", "steam", "wave_blocked", "slump", "inrush",
	                      "extinguish", "current_grounded", "fork", "stick", "morph", "weave", "counter_cancel"})
		f.events.push_back(Ev(t, {{"actor", 1}, {"dir", Vec3(0, 0, 1)}, {"range", 3.0f}, {"speed", 6.0f}, {"mass", 30.0f},
		                          {"tier", 3}, {"dash", true}, {"heavy", true}, {"pos", Vec3(0, 1, 0)}, {"at", Vec3(0, 1, 0)}}));
	f.Tick();
	d.Update(f.in);
	FXT_CHECK(d.Stats().oneShots > 20);
	FXT_CHECK(d.Stats().lights <= 4);
	f.events.clear();
	for (int i = 0; i < 60 * 3; ++i) {
		f.Tick();
		d.Update(f.in);
	}
	FXT_CHECK(d.Stats().oneShots - d.Stats().worldFx == 0);
	// ground scars (cracks, scorch, wet marks) fade within their lives (<= 10 s)
	for (int i = 0; i < 60 * 8; ++i) {
		f.Tick();
		d.Update(f.in);
	}
	FXT_CHECK(d.Stats().oneShots == 0);
	// the burning status follows the actor until its status list drops it
	FXT_CHECK(d.Stats().actorFx >= 1);
}

FXT_TEST(director_deterministic) {
	auto run = [](std::vector<std::pair<uint32_t, float>>& out) {
		FxDirector d;
		Frame f;
		ff::BodyView fb = Body(4, Mat::Fire, Form::Chunk, "fireball");
		fb.vel = Vec3(3, 0, 6);
		f.curr.bodies = {fb};
		f.events.push_back(Ev("fx", {{"fx", "burst"}, {"mat", "stone"}, {"pos", Vec3(0, 1, 0)}, {"radius", 1.2f}}));
		for (int i = 0; i < 20; ++i) {
			f.Tick();
			f.curr.bodies[0].pos += Vec3(0.05f, 0, 0.1f);
			const DrawList& dl = d.Update(f.in);
			f.events.clear();
			for (const DrawItem& it : dl.items)
				out.emplace_back(it.key, it.mesh ? it.mesh->pos.empty() ? 0.0f : it.mesh->pos.back().x + it.xform.pos.z : 0.0f);
		}
	};
	std::vector<std::pair<uint32_t, float>> a, b;
	run(a);
	run(b);
	FXT_CHECK(a.size() == b.size());
	bool same = a.size() == b.size();
	for (size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
	FXT_CHECK(same);
}

FXT_TEST(director_paused_and_quality) {
	FxDirector d;
	Frame f;
	ff::BodyView z = Body(2, Mat::Water, Form::Zone, "fog");
	z.zone_radius = 3.0f;
	z.age = 2.0f;
	f.curr.bodies = {z};
	f.in.quality = 0;
	f.Tick();
	const DrawList& lo = d.Update(f.in);
	int puffsLow = 0;
	for (const DrawItem& it : lo.items) puffsLow += it.mesh ? it.mesh->NumVerts() / 4 : 0;
	FxDirector d2;
	f.in.quality = 2;
	const DrawList& hi = d2.Update(f.in);
	int puffsHigh = 0;
	for (const DrawItem& it : hi.items) puffsHigh += it.mesh ? it.mesh->NumVerts() / 4 : 0;
	FXT_CHECK(puffsLow < puffsHigh);
	FXT_CHECK(puffsHigh <= 21);
	f.in.paused = true;
	const DrawList& p1 = d2.Update(f.in);
	const float ph1 = p1.items.empty() ? 0.0f : p1.items[0].params.Get(P::Phase);
	const DrawList& p2 = d2.Update(f.in);
	const float ph2 = p2.items.empty() ? 0.0f : p2.items[0].params.Get(P::Phase);
	FXT_NEAR(ph1, ph2, 1e-6);
}

FXT_TEST(director_cooled_lava_ridge_glows_then_sinks) {
	// a magma wave flows, settles, keeps the sim's heat as a glow that dies with a visual cooling clock, then sinks into
	// the floor and stops drawing while the sim body lives on (FxViews.cpp StripView); a plain stone ridge stays
	FxDirector d;
	Frame f;
	ff::BodyView w = Body(5, Mat::Stone, Form::Wave);
	w.liquid = 1.0f;
	w.temp = 1150.0f;
	w.wave_dir = Vec3(0.0f, 0.0f, 1.0f);
	w.wave_width = 1.2f;
	w.wave_path = {Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 2.0f)};
	w.pos = Vec3(0.0f, 0.0f, 2.0f);
	ff::BodyView plain = Body(6, Mat::Stone, Form::Chunk);   // never molten, settled ridge shape
	plain.wave_path = {Vec3(3.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 1.5f)};
	plain.pos = Vec3(3.0f, 0.0f, 1.5f);
	f.curr.bodies = {w, plain};
	auto lava = [&](const DrawList& dl, const DrawItem** out) {
		int n = 0;
		*out = nullptr;
		for (const DrawItem& it : dl.items)
			if (it.mat == MatSlot::LavaStrip) {
				++n;
				if (it.params.Get(P::Melt) > 0.5f || it.params.Get(P::Heat) > 0.0f || !*out) *out = &it;
			}
		return n;
	};
	const DrawItem* it = nullptr;
	for (int i = 0; i < 30; ++i) {
		f.Tick();
		d.Update(f.in);
	}
	f.Tick();
	FXT_CHECK(lava(d.Update(f.in), &it) == 2);
	FXT_CHECK(it && it->params.Get(P::Heat) > 0.9f && it->params.Get(P::Fade) > 0.99f);
	// settles and sets: solid, still 900 C in the sim (slow cooling)
	w.form = Form::Chunk;
	w.liquid = 0.0f;
	w.temp = 900.0f;
	f.curr.bodies = {w, plain};
	float heat1s = -1.0f, heat8s = -1.0f, fade9s = -1.0f;
	int lavaAt12s = -1;
	for (int i = 1; i <= 12 * 60; ++i) {
		f.Tick();
		const DrawList& dl = d.Update(f.in);
		const int n = lava(dl, &it);
		const DrawItem* ridge = nullptr;
		for (const DrawItem& x : dl.items)
			if (x.mat == MatSlot::LavaStrip && x.params.Get(P::Heat) > 0.0f) ridge = &x;
		if (i == 60) heat1s = ridge ? ridge->params.Get(P::Heat) : 0.0f;
		if (i == 8 * 60) heat8s = ridge ? ridge->params.Get(P::Heat) : 0.0f;
		if (i == 9 * 60 + 30) {
			// 3 s cold after the glow died at 6 s: sinking (the plain ridge keeps Fade 1)
			float minFade = 1.0f;
			for (const DrawItem& x : dl.items)
				if (x.mat == MatSlot::LavaStrip) minFade = MinF(minFade, x.params.Get(P::Fade, 1.0f));
			fade9s = minFade;
		}
		if (i == 12 * 60) lavaAt12s = n;
	}
	FXT_CHECK(heat1s > 0.5f);         // still glowing a second after it set
	FXT_NEAR(heat8s, 0.0f, 1e-4);     // cold by the visual clock
	FXT_CHECK(fade9s > 0.05f && fade9s < 0.95f);
	FXT_CHECK(lavaAt12s == 1);        // the cooled ridge is gone, the plain stone ridge still draws
	// re-melted: the ridge rises again
	w.liquid = 0.6f;
	w.temp = 1100.0f;
	f.curr.bodies = {w, plain};
	for (int i = 0; i < 60; ++i) {
		f.Tick();
		d.Update(f.in);
	}
	f.Tick();
	FXT_CHECK(lava(d.Update(f.in), &it) == 2);
}

#endif
