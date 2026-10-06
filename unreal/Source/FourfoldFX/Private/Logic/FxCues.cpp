// FourfoldFX logic island - event cues: ports of FxDirector._event (visual half) and FxCues (fx / interaction /
// charge / status / zone / clash / misc). Sounds, hit-stop, camera, haptics and screen flashes are other modules'.
// Owner: stream `fx`.
#include "FxActorFx.h"
#include "FxDirector.h"

#include <cmath>
#include <initializer_list>

namespace ffx {

namespace {

const Vec3 kUp(0.0f, 1.0f, 0.0f);
const Vec3 kBack(0.0f, 0.0f, 1.0f);   // Godot Vector3.BACK (billboard rings ignore it)

// Event fields are read through Value::operator[] (a reference into the event's dict, never a temporary copy).
float F(const ff::Value& d, std::string_view k, float def = 0.0f) { return d[k].as_f32(def); }
int I(const ff::Value& d, std::string_view k, int def = -1) { return static_cast<int>(d[k].as_int(def)); }
bool B(const ff::Value& d, std::string_view k, bool def = false) { return d[k].as_bool(def); }
std::string S(const ff::Value& d, std::string_view k) { return d[k].as_string(); }
Vec3 V(const ff::Value& d, std::string_view k, const Vec3& def = Vec3()) {
	const ff::Value& v = d[k];
	return v.is_vec3() ? v.as_vec3() : def;
}
std::vector<Vec3> Path(const ff::Value& v) {
	std::vector<Vec3> out;
	if (const ff::Array* a = v.array_ptr())
		for (const ff::Value& e : *a)
			if (e.is_vec3()) out.push_back(e.as_vec3());
	return out;
}
bool In(std::string_view s, std::initializer_list<std::string_view> l) {
	for (std::string_view x : l)
		if (s == x) return true;
	return false;
}

// FxCues.CLS_MAT: threat / counter class -> material family.
Fam ClsFam(std::string_view cls, Fam fallback) {
	struct E {
		const char* k;
		Fam f;
	};
	static const E kMap[] = {
		{"stone", Fam::Stone}, {"stone_heavy", Fam::Stone}, {"boulder", Fam::Stone}, {"hot_rock", Fam::Magma},
		{"magma", Fam::Magma}, {"lava_wave", Fam::Magma}, {"metal", Fam::Metal}, {"molten_metal", Fam::Metal},
		{"sand", Fam::Sand}, {"sand_cloud", Fam::Sand}, {"sand_surge", Fam::Sand}, {"glass", Fam::Glass},
		{"water", Fam::Water}, {"water_wave", Fam::Water}, {"puddle", Fam::Water}, {"pool", Fam::Water}, {"ice", Fam::Ice},
		{"mist", Fam::Mist}, {"steam", Fam::Steam}, {"vine", Fam::Plant}, {"flame", Fam::Flame}, {"blue_fire", Fam::Blue},
		{"fire_field", Fam::Flame}, {"ember", Fam::Blast}, {"lightning", Fam::Lightning}, {"blast", Fam::Blast},
		{"gust", Fam::Wind}, {"tornado", Fam::Vortex}, {"vacuum", Fam::Vacuum}, {"sound", Fam::Sound}, {"frost", Fam::Ice},
		{"guard_earth", Fam::Stone}, {"wall_stone", Fam::Stone}, {"wall_obsidian", Fam::Stone}, {"wall_glass", Fam::Glass},
		{"wall_sand", Fam::Sand}, {"wall_mud", Fam::Stone}, {"wall_ice", Fam::Ice}, {"wall_vine", Fam::Plant},
		{"plate_metal", Fam::Metal}, {"shield_water", Fam::Water}, {"screen_steam", Fam::Steam}, {"fog", Fam::Mist},
		{"aura_flame", Fam::Flame}, {"aura_blue", Fam::Blue}, {"ward_static", Fam::Lightning}, {"guard_blast", Fam::Blast},
		{"guard_wind", Fam::Wind}, {"wall_vortex", Fam::Vortex}, {"bubble_null", Fam::Vacuum}, {"barrier_sound", Fam::Sound},
		{"spikes", Fam::Stone}, {"rod", Fam::Metal}, {"anchor", Fam::Stone}, {"swallow", Fam::Stone}, {"quicksand", Fam::Sand},
		{"melt_pit", Fam::Magma}, {"heat_grip", Fam::Flame}, {"heat_ranged", Fam::Flame}, {"draw_heat", Fam::Flame},
		{"freeze", Fam::Ice}, {"condense", Fam::Water}, {"wave_water", Fam::Water}, {"wave_sand", Fam::Sand},
		{"wave_lava", Fam::Magma}, {"rime", Fam::Ice}, {"vacuum_well", Fam::Vacuum}, {"suction", Fam::Vacuum},
		{"water_jet", Fam::Water}, {"spray", Fam::Water}, {"plate", Fam::Metal}, {"arena_wall", Fam::Stone},
		{"ground", Fam::Stone}};
	for (const E& e : kMap)
		if (cls == e.k) return e.f;
	return fallback;
}

ShardMat ShardOf(Fam f) {
	switch (f) {
		case Fam::Ice: return ShardMat::Ice;
		case Fam::Glass: return ShardMat::Glass;
		case Fam::Metal: return ShardMat::Metal;
		case Fam::Plant: return ShardMat::Plant;
		default: return ShardMat::Stone;
	}
}

bool IsEnergy(Fam f) {
	return f == Fam::Flame || f == Fam::Blue || f == Fam::Lightning || f == Fam::Blast || f == Fam::Vacuum ||
	       f == Fam::Sound || f == Fam::Wind || f == Fam::Vortex;
}

RingOpts Opts(float cover, float glow, int count = 1, bool billboard = false, bool easeIn = false, float width = 0.08f,
              float alpha = 1.0f) {
	RingOpts o;
	o.cover = cover;
	o.glow = glow;
	o.count = count;
	o.billboard = billboard;
	o.easeIn = easeIn;
	o.width = width;
	o.alpha = alpha;
	return o;
}

}  // namespace

// ============================================================================================== dispatch
void FxDirector::HandleEvent(Ctx& c, const ff::Event& e) {
	const std::string& t = e.type;
	const ff::Value& d = e.data;
	if (t == "fx") CueFx(c, d);
	else if (t == "interaction") CueInteraction(c, d);
	else if (t == "charge") CueCharge(c, d);
	else if (t == "status") CueStatus(c, d, true);
	else if (t == "zone") CueZone(c, d);
	else if (t == "clash") CueClash(c, d);
	else if (In(t, {"morph", "chain", "weave", "counter_cancel", "slump", "convert", "capture", "ricochet", "stance", "mode",
	                "inrush", "extinguish", "current_grounded", "fork", "stick"}))
		CueMisc(c, t, d);
	else CueLegacy(c, t, d);
}

// ============================================================================================== legacy events
void FxDirector::CueLegacy(Ctx& c, const std::string& t, const ff::Value& d) {
	const int a = I(d, "actor");
	const int body = I(d, "body");
	auto chest = [&](int id) { return c.Chest(id); };
	auto feet = [&](int id) { return c.ActorPos(id); };
	const Color stoneDust = cfg_.DustColor(Fam::Stone);
	if (t == "hit") {
		const bool big = S(d, "result") == "knockdown" || F(d, "damage") >= 15.0f;
		int tier = I(d, "tier", 0);
		if (S(d, "result") == "knockdown") tier = 3;
		else if (big) tier = MaxI(tier, 1);
		const Vec3 dir = V(d, "dir");
		const Vec3 n = dir.length_squared() > 1e-6f ? Norm(dir * -1.0f) : kUp;
		if (S(d, "kind") == "lava") fx_.Ember(c, chest(a), kUp, 1.0f);
		fx_.Dust(c, feet(a), kUp, 0.6f, stoneDust);
		// hit sparks: a sharp burst at the struck point, stronger with the tier
		BurstM(c, chest(a), n, 0.4f + 0.2f * static_cast<float>(tier), Burst::Sparks);
		const std::string mat = S(d, "mat");
		if (!mat.empty() && mat != "stone")
			BurstM(c, chest(a), n, 0.6f + 0.15f * static_cast<float>(I(d, "tier", 0)), BurstOfFam(FamFromName(mat)));
		if (tier >= 3) fx_.Dust(c, feet(a), kUp, 1.3f, stoneDust);
		return;
	}
	if (t == "block") {
		const Vec3 at = a >= 0 ? chest(a) : BodyPos(c, body);
		const Vec3 dir = V(d, "dir");
		BurstM(c, at, dir.length_squared() > 1e-6f ? Norm(dir * -1.0f) : kUp, F(d, "power") >= 25.0f ? 0.7f : 0.4f, Burst::Dust);
		if (S(d, "kind") == "fire_water") fx_.Steam(c, at, 0.6f);
		return;
	}
	if (t == "perfect_deflect") {
		const Vec3 at = chest(a);
		const float fl = c.in.flashes * cfg_.flashScale;
		RingM(c, at, kBack, 0.1f, 1.1f, 0.3f, ActorFam(c, a), Opts(0.0f, 1.0f + 3.0f * fl, 2, true));
		if (fl > 0.05f) fx_.LightPulse(c, at, Color(1.0f, 0.97f, 0.9f), 2.0f * fl, 5.0f, 0.18f);
		return;
	}
	if (t == "deflect") {
		RingM(c, chest(a), kBack, 0.1f, 0.6f, 0.22f, ActorFam(c, a), Opts(0.1f, 2.5f, 1, true));
		return;
	}
	if (t == "rip") {
		fx_.Dust(c, BodyPos(c, body), kUp, 0.8f, stoneDust);
		return;
	}
	if (t == "launch") {
		const int tier = I(d, "tier", 0);
		if (tier > 0) tierHint_[body] = tier;
		if (auto it = views_.find(body); it != views_.end() && it->second.view) it->second.view->tierHint = tier;
		if (tier >= 2) {
			const Vec3 ft = a >= 0 ? feet(a) : BodyPos(c, body);
			const float ftr = static_cast<float>(tier - 2);
			RingOpts o = Opts(0.35f, 1.1f + 0.3f * ftr, tier >= 3 ? 2 : 1, false, false, 0.09f);
			fx_.Ring(c, ft + Vec3(0.0f, 0.05f, 0.0f), kUp, 0.4f, 1.6f + 0.9f * ftr, 0.35f + 0.1f * ftr, cfg_.TierColor(tier), o);
			if (tier >= 3) fx_.Dust(c, ft, kUp, 1.3f, stoneDust);
		}
		return;
	}
	if (t == "impact") {
		if (F(d, "speed") > 3.0f) fx_.Dust(c, BodyPos(c, body), kUp, Clamp(F(d, "mass") / 30.0f, 0.4f, 1.2f), stoneDust);
		return;
	}
	if (t == "wall") {
		fx_.Dust(c, BodyPos(c, body), kUp, 1.0f, stoneDust);
		return;
	}
	if (t == "wall_crumble") {
		const Vec3 p = BodyPos(c, body);
		fx_.Dust(c, p, kUp, 1.4f, stoneDust);
		fx_.Shards(c, p + Vec3(0.0f, 0.4f, 0.0f), kUp, 1.0f, ShardMat::Stone, static_cast<uint32_t>(body) * 31u + 7u, c.Ground(p));
		return;
	}
	if (t == "transform") {
		const std::string to = S(d, "to");
		const Vec3 p = d.has("at") ? V(d, "at") : BodyPos(c, body);
		if (to == "wave") fx_.Ember(c, p, kUp, 0.8f);
		else if (to == "rock") fx_.Steam(c, p + Vec3(0.0f, 0.2f, 0.0f), 0.5f);
		else if (to == "ice") BurstM(c, p, kUp, 0.6f, Burst::Frost);
		else if (to == "water") fx_.Splash(c, p, kUp, 0.35f);
		else if (to == "puddle") fx_.Splash(c, p, kUp, 0.7f);
		else if (to == "molten") fx_.Ember(c, p, kUp, 0.6f);
		return;
	}
	if (t == "shatter") {
		const Vec3 p = BodyPos(c, body);
		fx_.Splash(c, p, kUp, 0.5f);
		fx_.Shards(c, p, kUp, 0.8f, ShardMat::Ice, static_cast<uint32_t>(body) * 31u + static_cast<uint32_t>(c.in.curr->tick), p.y - 1.0f);
		return;
	}
	if (t == "wave_blocked" || t == "wave_drop") {
		fx_.Ember(c, BodyPos(c, body), kUp, 0.7f);
		return;
	}
	if (t == "steam" || t == "steam_block") {
		fx_.Steam(c, BodyPos(c, body) + Vec3(0.0f, 0.3f, 0.0f), 0.8f);
		return;
	}
	if (t == "flare") {
		fx_.FireBurst(c, c.Hands(a), V(d, "dir", Vec3(0, 0, 1)), F(d, "range", 3.0f), B(d, "heavy") ? 1.0f : 0.6f, false);
		return;
	}
	if (t == "vent") {
		fx_.FireBurst(c, chest(a), kUp, 2.5f, 0.8f, false);
		return;
	}
	if (t == "lightning") {
		const std::vector<Vec3> pts = Path(d["path"]);
		const uint32_t seed = static_cast<uint32_t>(c.in.curr->tick);
		if (pts.size() >= 2) fx_.Bolt(c, pts, seed);
		if (const ff::Array* arcs = d["arcs"].array_ptr()) {
			uint32_t k = 7;
			for (const ff::Value& arc : *arcs) {
				const std::vector<Vec3> ap = Path(arc);
				if (ap.size() >= 2) fx_.Bolt(c, ap, seed + k, 0.8f, false, 0.07f);
				k += 7;
			}
		}
		if (pts.size() >= 2) BurstM(c, pts.back(), kUp, 0.6f, Burst::Static);
		return;
	}
	if (t == "grounded") {
		fx_.Dust(c, feet(a), kUp, 0.6f, stoneDust);
		return;
	}
	if (t == "gust") {
		const bool heavy = B(d, "heavy");
		const Vec3 o = chest(a);
		const Vec3 gd = V(d, "dir", Vec3(0, 0, 1));
		const float rng = F(d, "range", 4.0f);
		if (heavy) {
			fx_.AirPush(c, o, gd, 2.4f, rng * 1.15f);
			fx_.AirPush(c, o, gd, 1.3f, rng * 0.8f);
			const ff::ActorView* ga = c.Actor(a);
			if (ga && ga->grounded) fx_.Dust(c, feet(a) + gd * 1.0f, Norm(gd + kUp), 1.2f, stoneDust);
		} else {
			fx_.AirPush(c, o, gd, 0.75f, rng * 0.8f);
		}
		return;
	}
	if (t == "lash") {
		const Vec3 dir = V(d, "dir", Vec3(0, 0, 1));
		LashTransient(c, a, dir, F(d, "range", 3.0f));
		fx_.Splash(c, chest(a) + dir * 2.5f, kUp, 0.4f);
		return;
	}
	if (t == "draw_water") {
		DrawStream(c, a, d.has("at") ? V(d, "at") : chest(a), body);
		return;
	}
	if (t == "evade") {
		if (B(d, "dash")) DashTrail(c, a, V(d, "dir"));
		return;
	}
	if (t == "updraft") {
		fx_.Dust(c, feet(a), kUp, 0.9f, stoneDust);
		RingM(c, feet(a) + Vec3(0.0f, 0.05f, 0.0f), kUp, 0.3f, 1.2f, 0.35f, Fam::Wind, Opts(0.4f, 1.2f));
		return;
	}
	if (t == "land") {
		if (F(d, "speed") > 3.0f) fx_.Dust(c, feet(a), kUp, 0.5f, stoneDust);
		return;
	}
	if (t == "reform") {
		fx_.Dust(c, BodyPos(c, body), kUp, 0.6f, stoneDust);
		return;
	}
	if (t == "burn") {
		fx_.Ember(c, chest(a), kUp, 0.4f);
		return;
	}
}

// ============================================================================================== fx cues
void FxDirector::CueFx(Ctx& c, const ff::Value& d) {
	const std::string key = S(d, "fx");
	const Fam mat = FamFromName(S(d, "mat"), Fam::Stone);
	const std::string shape = S(d, "shape");
	const int tier = I(d, "tier", 0);
	const Vec3 pos = V(d, "pos");
	Vec3 dir = V(d, "dir", Vec3(0, 0, -1));
	if (dir.length_squared() < 1e-6f) dir = Vec3(0.0f, 0.0f, -1.0f);
	dir = Norm(dir);
	const int actor = I(d, "actor");
	const float radius = F(d, "radius"), length = F(d, "length"), power = F(d, "power");
	const float k = Clamp(0.55f + 0.2f * static_cast<float>(tier), 0.4f, 1.4f);
	const Burst mb = BurstOfFam(mat);
	if (key == "cast") {
		RingM(c, pos, kBack, 0.08f, 0.35f + 0.08f * static_cast<float>(tier), 0.28f, mat, Opts(0.3f, 1.3f, 1, true, false, 0.08f, 0.7f));
		if (tier >= 2) BurstM(c, pos, kUp, 0.4f * k, mb);
		return;
	}
	if (key == "release") {
		switch (mat) {
			case Fam::Flame:
			case Fam::Blue:
				if (shape != "fireball" && shape != "comet") fx_.FireBurst(c, pos, dir, 1.2f * k, 0.5f * k, mat == Fam::Blue);
				BurstM(c, pos, dir, 0.4f * k, mat == Fam::Flame ? Burst::Ember : Burst::BlueSparks);
				break;
			case Fam::Lightning: BurstM(c, pos, dir, 0.6f * k, Burst::Static); break;
			case Fam::Wind:
			case Fam::Vortex: fx_.AirPush(c, pos, dir, 0.6f, 2.5f * k); break;
			case Fam::Sand: BurstM(c, pos, dir, 0.6f * k, Burst::Sand); break;
			case Fam::Metal: BurstM(c, pos, dir, 0.5f * k, Burst::Metal); break;
			case Fam::Sound: RingM(c, pos + dir * 0.4f, dir, 0.15f, 0.9f * k, 0.3f, Fam::Sound, Opts(0.3f, 1.4f, 1, false, false, 0.12f)); break;
			case Fam::Vacuum: RingM(c, pos, kBack, 0.6f, 0.1f, 0.25f, Fam::Vacuum, Opts(0.5f, 1.5f, 1, true, true)); break;
			case Fam::Water:
			case Fam::Ice:
			case Fam::Mist:
			case Fam::Steam: BurstM(c, pos, dir, 0.45f * k, mb); break;
			default: BurstM(c, pos, dir, 0.4f * k, mb); break;
		}
		return;
	}
	if (key == "cone") {
		const float len = MaxF(length, 2.5f), rad = MaxF(radius, 0.8f);
		switch (mat) {
			case Fam::Flame: fx_.FireBurst(c, pos, dir, len, k, false); break;
			case Fam::Blue: fx_.FireBurst(c, pos, dir, len, k, true); break;
			case Fam::Wind:
			case Fam::Vortex: fx_.AirPush(c, pos, dir, rad, len); break;
			case Fam::Sand:
				fx_.AirPush(c, pos, dir, rad, len);
				for (int i = 0; i < 2; ++i) BurstM(c, pos + dir * (len * (0.3f + 0.35f * static_cast<float>(i))), dir, 0.8f * k, Burst::Sand);
				break;
			case Fam::Water:
				fx_.Splash(c, pos + dir * 0.8f, dir, 0.9f * k);
				BurstM(c, pos + dir * (len * 0.5f), dir, 0.7f * k, Burst::Water);
				break;
			case Fam::Steam:
			case Fam::Mist:
				fx_.Steam(c, pos + dir * (len * 0.3f), k);
				BurstM(c, pos + dir * (len * 0.6f), dir, 0.8f * k, mat == Fam::Steam ? Burst::Steam : Burst::Mist);
				break;
			case Fam::Ice: BurstM(c, pos + dir * (len * 0.4f), dir, 0.9f * k, Burst::Frost); break;
			case Fam::Sound:
				for (int i = 0; i < 2; ++i) {
					const float fi = static_cast<float>(i);
					RingM(c, pos + dir * (0.6f + 1.2f * fi), dir, 0.3f + 0.4f * fi, rad * (0.8f + 0.6f * fi), 0.35f, Fam::Sound,
					      Opts(0.3f, 1.2f, 1, false, false, 0.1f));
				}
				break;
			case Fam::Vacuum: BurstM(c, pos + dir * (len * 0.5f), dir * -1.0f, k, Burst::Inflow); break;
			case Fam::Lightning: {
				const Vec3 tip = pos + dir * len;
				fx_.Bolt(c, {pos, LerpV(pos, tip, 0.5f) + Vec3(0.0f, 0.3f, 0.0f), tip}, rng_.Next());
				break;
			}
			default: BurstM(c, pos + dir, dir, 0.7f * k, mb); break;
		}
		return;
	}
	if (key == "beam") {
		const float len = MaxF(length, 4.0f);
		const std::vector<Vec3> path = Path(d["path"]);
		Vec3 tip = pos + dir * len;
		if (path.size() >= 2) tip = path.back();
		switch (mat) {
			case Fam::Lightning: {
				std::vector<Vec3> pts;
				if (shape == "down") {
					// sky-split: a vertical bolt from the sky onto the point
					pts = {pos + Vec3(rng_.Signed(), 14.0f, rng_.Signed()), pos + Vec3(0.0f, 6.0f, 0.0f), pos};
					RingM(c, Vec3(pos.x, c.Ground(pos) + 0.05f, pos.z), kUp, 0.3f, 2.5f, 0.4f, Fam::Lightning, Opts(0.2f, 3.0f));
					BurstM(c, pos, kUp, 1.2f, Burst::Static);
					fx_.Dust(c, Vec3(pos.x, c.Ground(pos), pos.z), kUp, 1.2f, cfg_.DustColor(Fam::Stone));
				} else if (path.size() >= 2) {
					pts = path;
				} else {
					pts = {pos, LerpV(pos, tip, 0.5f), tip};
				}
				fx_.Bolt(c, pts, rng_.Next(), shape == "down" ? 1.3f : 1.0f, true, shape == "down" ? 0.14f : 0.1f);
				break;
			}
			case Fam::Blue:
			case Fam::Flame:
			case Fam::Sand:
			case Fam::Water:
			case Fam::Sound:
			case Fam::Vacuum: {
				BeamStyle st = BeamStyle::Blue;
				if (mat == Fam::Blue && (shape == "needles" || shape == "lance")) st = BeamStyle::Needle;
				else if (mat == Fam::Flame) st = BeamStyle::Flame;
				else if (mat == Fam::Sand) st = BeamStyle::Sand;
				else if (mat == Fam::Water) st = BeamStyle::Water;
				else if (mat == Fam::Sound) st = BeamStyle::Sound;
				else if (mat == Fam::Vacuum) st = BeamStyle::Vacuum;
				fx_.Beam(c, pos, tip, 0.3f + 0.12f * k, st);
				if (mat == Fam::Blue) BurstM(c, tip, dir * -1.0f, 0.6f * k, Burst::BlueSparks);
				if (mat == Fam::Water) fx_.Splash(c, tip, dir * -1.0f, 0.5f * k);
				break;
			}
			default: BurstM(c, tip, dir * -1.0f, 0.6f * k, mb); break;
		}
		return;
	}
	if (key == "burst") {
		const float rad = MaxF(radius, 1.0f);
		const float gy = c.Ground(pos);
		switch (mat) {
			case Fam::Blast:
			case Fam::Flame:
			case Fam::Blue:
				if (mat == Fam::Blast || rad >= 1.2f) {
					fx_.Blast(c, pos, rad, Clamp(0.6f + power / 40.0f, 0.5f, 1.4f), mat == Fam::Blue, gy);
					fx_.Dust(c, Vec3(pos.x, gy, pos.z), kUp, MinF(0.6f + rad * 0.3f, 2.0f), cfg_.DustColor(Fam::Blast));
				} else {
					fx_.FireBurst(c, pos, kUp, rad * 1.5f, k, mat == Fam::Blue);
				}
				break;
			case Fam::Sand:
			case Fam::Stone:
				BurstM(c, pos, kUp, k, mb);
				BurstM(c, pos, kUp, k * 0.8f, mat == Fam::Sand ? Burst::Grit : Burst::Dust);
				if (mat == Fam::Stone) fx_.Shards(c, pos, kUp, k, ShardMat::Stone, rng_.Next() % 997u, gy);
				RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 0.3f, rad * 1.3f, 0.4f, mat, Opts(0.6f, 1.0f));
				break;
			case Fam::Water:
				fx_.Splash(c, pos, kUp, k);
				RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 0.3f, rad * 1.2f, 0.4f, Fam::Water, Opts(0.5f, 1.5f));
				break;
			case Fam::Steam:
			case Fam::Mist:
				fx_.Steam(c, pos, k);
				BurstM(c, pos, kUp, k, mat == Fam::Steam ? Burst::Steam : Burst::Mist);
				break;
			case Fam::Ice:
			case Fam::Glass:
			case Fam::Metal:
				fx_.Shards(c, pos, kUp, k, ShardOf(mat), rng_.Next() % 997u, gy);
				BurstM(c, pos, kUp, k * 0.7f, mb);
				break;
			case Fam::Sound:
				for (int i = 0; i < 2; ++i) {
					const float fi = static_cast<float>(i);
					RingM(c, Vec3(pos.x, gy + 0.05f, pos.z), kUp, 0.3f + 0.3f * fi, rad * (1.0f + 0.4f * fi), 0.4f, Fam::Sound, Opts(0.4f, 1.5f));
				}
				BurstM(c, Vec3(pos.x, gy, pos.z), kUp, k, Burst::Dust);
				break;
			case Fam::Vacuum:
				RingM(c, pos, kBack, rad, 0.1f, 0.3f, Fam::Vacuum, Opts(0.6f, 1.5f, 1, true, true));
				BurstM(c, pos, kUp, k, Burst::Inflow);
				break;
			case Fam::Lightning:
				for (int i = 0; i < 3; ++i) {
					const float a = kFxTau * static_cast<float>(i) / 3.0f + rng_.F01();
					const Vec3 tip = Vec3(pos.x + std::cos(a) * rad, gy + 0.05f, pos.z + std::sin(a) * rad);
					fx_.Bolt(c, {pos, LerpV(pos, tip, 0.5f) + Vec3(0.0f, 0.3f, 0.0f), tip}, rng_.Next(), 1.0f, i == 0);
				}
				break;
			default: BurstM(c, pos, kUp, k, mb); break;
		}
		return;
	}
	if (key == "ring") {
		const float gy = c.Ground(pos);
		RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 0.3f, MaxF(radius, 1.5f), 0.45f + 0.1f * static_cast<float>(tier), mat,
		      Opts(0.5f, 1.3f, (mat == Fam::Sound || tier >= 2) ? 2 : 1, false, false, 0.09f));
		if (mat == Fam::Sound || mat == Fam::Stone) BurstM(c, Vec3(pos.x, gy, pos.z), kUp, 0.6f * k, Burst::Dust);
		return;
	}
	if (key == "erupt") {
		const float rad = MaxF(radius, 0.8f);
		const float gy = c.Ground(pos);
		const Vec3 g(pos.x, gy, pos.z);
		switch (mat) {
			case Fam::Stone:
			case Fam::Magma:
				BurstM(c, g, kUp, k, Burst::Dust);
				fx_.Shards(c, g + Vec3(0.0f, 0.1f, 0.0f), kUp, k, ShardMat::Stone, rng_.Next() % 997u, gy);
				if (mat == Fam::Magma) fx_.Ember(c, g, kUp, k);
				break;
			case Fam::Water:
			case Fam::Steam:
			case Fam::Mist:
				fx_.Splash(c, g, kUp, 1.2f * k);
				fx_.Steam(c, g + Vec3(0.0f, 0.3f, 0.0f), k);
				break;
			case Fam::Ice:
				fx_.Shards(c, g + Vec3(0.0f, 0.1f, 0.0f), kUp, k, ShardMat::Ice, rng_.Next() % 997u, gy);
				BurstM(c, g, kUp, k, Burst::Frost);
				break;
			case Fam::Flame:
			case Fam::Blue: fx_.FireBurst(c, g, kUp, 2.0f + 0.5f * static_cast<float>(tier), k, mat == Fam::Blue); break;
			case Fam::Plant: BurstM(c, g, kUp, k, Burst::Leaves); break;
			case Fam::Sand: BurstM(c, g, kUp, k, Burst::Sand); break;
			default: BurstM(c, g, kUp, k, mb); break;
		}
		RingM(c, g + Vec3(0.0f, 0.04f, 0.0f), kUp, 0.2f, rad * 1.4f, 0.35f, mat, Opts(0.6f, 1.0f));
		return;
	}
	if (key == "trail") {
		if (mat == Fam::Flame || mat == Fam::Blue || mat == Fam::Blast) {
			if (const ff::ActorView* a = c.Actor(actor)) {
				(void)a;
				const Vec3 ft = c.ActorPos(actor);
				fx_.Ember(c, ft, Norm(dir * -1.0f + kUp * 0.3f), 0.7f);
				fx_.FireBurst(c, ft + Vec3(0.0f, 0.3f, 0.0f), Norm(dir * -1.0f + Vec3(0.0f, -0.5f, 0.0f)), 1.2f, 0.6f, mat == Fam::Blue);
			}
		}
		DashTrail(c, actor, dir);
		return;
	}
	if (key == "splash") {
		switch (mat) {
			case Fam::Water:
			case Fam::Ice: fx_.Splash(c, pos, kUp, 0.6f * k); break;
			case Fam::Steam:
			case Fam::Mist: fx_.Steam(c, pos, 0.7f * k); break;
			default: BurstM(c, pos, kUp, 0.7f * k, mb); break;
		}
		return;
	}
	if (key == "aura") Aura(c, actor, mat, B(d, "on", true));
}

// ============================================================================================== interactions
void FxDirector::CueInteraction(Ctx& c, const ff::Value& d) {
	const std::string outcome = S(d, "outcome");
	const Vec3 pos = V(d, "pos");
	Vec3 dir = V(d, "dir", Vec3(0, 0, -1));
	if (dir.length_squared() < 1e-6f) dir = Vec3(0.0f, 0.0f, -1.0f);
	dir = Norm(dir);
	const Fam tm = ClsFam(S(d, "threat"), BodyFam(c, I(d, "threat_body"), Fam::Stone));
	const Fam cm = ClsFam(S(d, "counter"), BodyFam(c, I(d, "counter_body"), ActorFam(c, I(d, "counter_actor"))));
	const bool perfect = B(d, "perfect");
	const bool small = S(d, "band") == "partial";
	const float st = small ? 0.5f : 1.0f;
	const std::string to = S(d, "to");
	if (outcome == "block") {
		BurstM(c, pos, dir * -1.0f, 0.5f * st, BurstOfFam(tm));
		if (tm == Fam::Metal || cm == Fam::Metal) BurstM(c, pos, dir * -1.0f, 0.4f * st, Burst::Metal);
	} else if (outcome == "deflect" || outcome == "redirect" || outcome == "reflect") {
		const Vec3 nd = outcome == "reflect" ? dir * -1.0f : Norm(dir.cross(kUp), Vec3(1, 0, 0));
		RingM(c, pos, kBack, 0.1f, 0.7f, 0.25f, cm, Opts(0.1f, 3.0f, 1, true, false, 0.1f));
		BurstM(c, pos, nd, 0.7f, (tm == Fam::Stone || tm == Fam::Metal || tm == Fam::Glass) ? Burst::Sparks : BurstOfFam(tm));
	} else if (outcome == "reclaim") {
		RingM(c, pos, kBack, 0.5f, 0.15f, 0.35f, cm, Opts(0.2f, 2.5f, 1, true, true));
	} else if (outcome == "absorb" || outcome == "capture") {
		RingM(c, pos, kBack, 0.9f, 0.1f, 0.4f, cm, Opts(0.3f, 1.8f, 2, true, true));
		BurstM(c, pos, kUp, 0.6f * st, cm == Fam::Vacuum ? Burst::Inflow : BurstOfFam(tm));
	} else if (outcome == "transform") {
		if (to == "steam" || to == "mist") {
			fx_.Steam(c, pos, st);
			BurstM(c, pos, kUp, 0.7f * st, Burst::Steam);
		} else if (to == "glass") {
			BurstM(c, pos, kUp, 0.7f * st, Burst::Glass);
			fx_.Ember(c, pos, kUp, 0.5f * st);
		} else if (to == "ice" || to == "snow" || to == "rime") {
			BurstM(c, pos, kUp, 0.8f * st, Burst::Frost);
		} else if (to == "rock" || to == "obsidian" || to == "sandstone") {
			fx_.Steam(c, pos + Vec3(0.0f, 0.2f, 0.0f), 0.5f * st);
			BurstM(c, pos, kUp, 0.5f * st, Burst::Dust);
		} else if (to == "lava" || to == "molten_metal" || to == "hot_rock") {
			fx_.Ember(c, pos, kUp, st);
			BurstM(c, pos, kUp, 0.4f * st, Burst::Ember);
		} else if (to == "mud") {
			BurstM(c, pos, kUp, 0.6f * st, Burst::Dust);
		} else if (to == "ash") {
			BurstM(c, pos, kUp, 0.8f * st, Burst::Ash);
		} else if (to == "water") {
			fx_.Splash(c, pos, kUp, 0.4f * st);
		} else {
			BurstM(c, pos, kUp, 0.5f * st, Burst::Dust);
		}
	} else if (outcome == "shatter") {
		const ShardMat sm = (tm == Fam::Ice || tm == Fam::Glass || tm == Fam::Metal || tm == Fam::Plant) ? ShardOf(tm) : ShardMat::Stone;
		fx_.Shards(c, pos, Norm(dir * -1.0f + kUp * 0.5f), st, sm, rng_.Next() % 997u, c.Ground(pos));
		BurstM(c, pos, kUp, 0.5f * st, BurstOfFam(tm));
	} else if (outcome == "sink") {
		const float gy = c.Ground(pos);
		BurstM(c, Vec3(pos.x, gy, pos.z), kUp, 0.8f * st, tm != Fam::Sand ? Burst::Dust : Burst::Sand);
		RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 1.0f, 0.2f, 0.35f, Fam::Stone, Opts(0.7f, 0.8f, 1, false, true));
	} else if (outcome == "ground" || outcome == "conduct") {
		const Vec3 gp(pos.x, c.Ground(pos), pos.z);
		Vec3 tgt = gp;
		if (outcome == "conduct") {
			if (const ff::BodyView* cb = c.Body(I(d, "counter_body"))) tgt = cb->pos;
		}
		fx_.Bolt(c, {pos, LerpV(pos, tgt, 0.5f) + Vec3(0.2f, 0.1f, 0.0f), tgt}, rng_.Next(), 0.9f, false, 0.08f);
		BurstM(c, tgt, kUp, 0.4f, Burst::Static);
	} else if (outcome == "extinguish" || outcome == "neutralize" || outcome == "disperse") {
		const bool fire = tm == Fam::Flame || tm == Fam::Blue || tm == Fam::Blast || tm == Fam::Magma;
		BurstM(c, pos, kUp, 0.8f * st, fire ? Burst::Smoke : Burst::Mist);
	} else if (outcome == "amplify") {
		fx_.FireBurst(c, pos, kUp, 2.2f, 1.0f, tm == Fam::Blue);
	} else if (outcome == "weaken" || outcome == "bend" || outcome == "slow") {
		BurstM(c, pos, dir * -1.0f, 0.35f, BurstOfFam(cm));
		RingM(c, pos, kBack, 0.1f, 0.35f, 0.2f, cm, Opts(0.2f, 1.5f, 1, true));
	} else if (outcome == "overwhelm") {
		// the counter's own break effect
		switch (cm) {
			case Fam::Stone:
			case Fam::Sand:
			case Fam::Metal: BurstM(c, pos, dir, 1.2f, cm != Fam::Sand ? Burst::Dust : Burst::Sand); break;
			case Fam::Water: fx_.Splash(c, pos, dir, 0.9f); break;
			case Fam::Ice:
			case Fam::Glass: fx_.Shards(c, pos, dir, 1.0f, ShardOf(cm), rng_.Next() % 997u, c.Ground(pos)); break;
			case Fam::Plant: BurstM(c, pos, dir, 1.0f, Burst::Leaves); break;
			default:
				BurstM(c, pos, dir, 0.8f, BurstOfFam(cm));
				RingM(c, pos, kBack, 0.8f, 0.2f, 0.25f, cm, Opts(0.4f, 1.5f, 1, true));
				break;
		}
	} else if (outcome == "disrupt") {
		RingM(c, pos, kBack, 0.1f, 0.8f, 0.3f, Fam::Sound, Opts(0.2f, 1.5f, 2, true));
	} else if (outcome == "heat") {
		fx_.Ember(c, pos, kUp, 0.5f);
	} else if (outcome == "push") {
		BurstM(c, pos, dir, 0.5f, Burst::Dust);
	}
	if (perfect) {
		// perfect counter: a bright double ring, a white-gold flash light (both scaled by the Flashes setting)
		const float fl = c.in.flashes * cfg_.flashScale;
		RingM(c, pos, kBack, 0.1f, 1.1f, 0.3f, cm, Opts(0.0f, 1.0f + 3.0f * fl, 2, true));
		if (fl > 0.05f) {
			fx_.LightPulse(c, pos, Color(1.0f, 0.95f, 0.85f), 2.4f * fl, 5.0f, 0.2f);
			BurstM(c, pos, kUp, 0.6f, Burst::Sparks);
		}
	}
}

// ============================================================================================== charge / status
void FxDirector::CueCharge(Ctx& c, const ff::Value& d) {
	const int a = I(d, "actor");
	const int tier = I(d, "tier", 0);
	if (tier < 1 || !c.Actor(a)) return;
	const Fam mat = FamOfSub(I(d, "element", 0), I(d, "sub", 0));
	auto it = charges_.find(a);
	if (it == charges_.end()) {
		auto ch = std::make_unique<ChargeFx>();
		ch->Init(c, a);
		ch->fam = mat;
		it = charges_.emplace(a, std::move(ch)).first;
	}
	ChargeFx& ch = *it->second;
	ch.Set(tier, 0.0f, mat);
	ch.move = S(d, "move");
	// the tier step itself: a quick flash ring at the hands
	RingM(c, c.Hands(a), kBack, 0.05f, 0.35f + 0.1f * static_cast<float>(tier), 0.2f, mat, Opts(0.0f, 2.0f + static_cast<float>(tier), 1, true));
}

void FxDirector::CueStatus(Ctx& c, const ff::Value& d, bool fromEvent) {
	const int a = I(d, "actor");
	const std::string nm = S(d, "status");
	const std::string key = std::to_string(a) + ":" + nm;
	const bool on = B(d, "on", true);
	const std::string style(StatusStyle(nm));
	auto it = status_.find(key);
	if (!on) {
		if (it != status_.end()) status_.erase(it);
		return;
	}
	if (style.empty() || it != status_.end()) return;
	const ff::ActorView* ac = c.Actor(a);
	if (!ac) return;
	auto s = std::make_unique<StatusFx>();
	s->actor = a;
	s->name = nm;
	s->style = style;
	const Vec3 feet = c.ActorPos(a);
	const Vec3 chest = c.Chest(a);
	if (style == "flames") {
		s->flames = std::make_unique<FlameTongues>();
		s->flames->Setup(c, FlameTongues::Mode::Burning, static_cast<uint32_t>(a) * 31u + 3u, 0.32f, 0.5f, false);
	} else if (style == "frost") {
		s->keyShell = c.keys.New();
		if (fromEvent) BurstM(c, feet + Vec3(0.0f, 0.4f, 0.0f), kUp, 0.7f, Burst::Frost);
	} else if (style == "crackle") {
		s->crackle = std::make_unique<Crackle>();
		s->crackle->Setup(c, Crackle::Mode::Actor, 0.6f, static_cast<uint32_t>(a) * 13u + 1u);
	} else if (style == "vines") {
		s->vines = std::make_unique<VineTubes>();
		s->vines->Setup(c, "rooted", static_cast<uint32_t>(a) * 17u + 5u, Vec3(0.38f, 0.38f, 0.38f));
	} else if (style == "veil") {
		s->veil = std::make_unique<PuffCloud>();
		s->veil->Configure(c, CloudStyle::Veil, static_cast<uint32_t>(a));
		s->veil->SetShape(0.7f, 1.9f);
	} else if (style == "aura") {
		s->keyShell = c.keys.New();
	} else if (style == "dust") {
		if (fromEvent) {
			BurstM(c, feet, kUp, 0.8f, Burst::Dust);
			RingM(c, feet + Vec3(0.0f, 0.04f, 0.0f), kUp, 0.3f, 1.2f, 0.4f, Fam::Stone, Opts(0.6f, 0.8f));
		}
	} else if (style == "ring") {
		if (fromEvent) RingM(c, chest + Vec3(0.0f, 0.6f, 0.0f), kBack, 0.1f, 0.6f, 0.3f, Fam::Sound, Opts(0.3f, 1.5f, 2, true));
	} else if (style == "frostfeet") {
		if (fromEvent) {
			RingM(c, feet + Vec3(0.0f, 0.03f, 0.0f), kUp, 0.15f, 0.7f, 0.35f, Fam::Ice, Opts(0.7f, 1.0f));
			BurstM(c, feet, kUp, 0.5f, Burst::Frost);
		}
	} else if (style == "steam") {
		if (fromEvent) BurstM(c, chest, kUp, 0.4f, Burst::Steam);
	}
	status_[key] = std::move(s);
}

// ============================================================================================== zones / clash / misc
void FxDirector::CueZone(Ctx& c, const ff::Value& d) {
	const std::string kind = S(d, "kind");
	const Vec3 pos = V(d, "pos");
	const float r = F(d, "radius", 1.0f);
	const bool open = S(d, "phase").empty() || S(d, "phase") == "open";
	const Vec3 g(pos.x, c.Ground(pos), pos.z);
	Fam mat = Fam::Wind;
	Burst style = Burst::Dust;
	if (In(kind, {"sand_cloud", "sandstorm", "quicksand"})) {
		mat = Fam::Sand;
		style = Burst::Sand;
	} else if (In(kind, {"fog", "mist", "steam", "steam_screen", "geyser"})) {
		mat = Fam::Mist;
		style = kind != "geyser" ? Burst::Mist : Burst::Steam;
	} else if (In(kind, {"fire_field", "melt_pit", "lava_pool", "mine", "fuse"})) {
		mat = Fam::Flame;
		style = Burst::Ember;
	} else if (kind == "ice_floor") {
		mat = Fam::Ice;
		style = Burst::Frost;
	} else if (kind == "vacuum_well" || kind == "null_bubble") {
		mat = Fam::Vacuum;
		style = Burst::Inflow;
	} else if (kind == "static_field" || kind == "corona") {
		mat = kind == "static_field" ? Fam::Lightning : Fam::Blue;
		style = Burst::Static;
	} else if (kind == "sound_barrier") {
		mat = Fam::Sound;
	} else if (kind == "briar") {
		mat = Fam::Plant;
		style = Burst::Leaves;
	} else if (kind == "caltrops") {
		mat = Fam::Metal;
		style = Burst::Metal;
	}
	if (open) {
		RingM(c, g + Vec3(0.0f, 0.04f, 0.0f), kUp, 0.2f, MaxF(r, 0.5f), 0.4f, mat, Opts(0.5f, 1.2f));
		BurstM(c, g, kUp, Clamp(r / 2.0f, 0.4f, 1.2f), style);
	} else {
		Burst closeStyle = style;
		if (mat == Fam::Flame) closeStyle = Burst::Smoke;
		else if (mat == Fam::Mist || mat == Fam::Ice || mat == Fam::Vacuum) closeStyle = Burst::Mist;
		BurstM(c, g, kUp, Clamp(r / 2.5f, 0.3f, 1.0f), closeStyle);
	}
}

void FxDirector::CueClash(Ctx& c, const ff::Value& d) {
	const Vec3 pos = V(d, "pos");
	const Fam mat = FamFromName(S(d, "mat"), Fam::Stone);
	BurstM(c, pos, kUp, 0.8f, IsEnergy(mat) ? BurstOfFam(mat) : Burst::Sparks);
	RingM(c, pos, kBack, 0.1f, 0.8f, 0.22f, mat, Opts(0.1f, 3.0f, 1, true));
	fx_.LightPulse(c, pos, cfg_.MatColor(mat), 1.2f, 4.0f, 0.15f);
}

void FxDirector::CueMisc(Ctx& c, const std::string& t, const ff::Value& d) {
	const int a = I(d, "actor");
	const int body = I(d, "body");
	if (t == "morph" || t == "chain") {
		RingM(c, c.Hands(a), kBack, 0.06f, 0.3f, 0.18f, ActorFam(c, a), Opts(0.1f, 2.0f, 1, true));
	} else if (t == "weave") {
		RingM(c, c.Hands(a), kBack, 0.06f, 0.5f, 0.25f, ActorFam(c, a), Opts(0.1f, 2.5f, 2, true));
	} else if (t == "counter_cancel") {
		RingM(c, c.Chest(a), kBack, 0.2f, 1.0f, 0.25f, ActorFam(c, a), Opts(0.0f, 3.0f, 1, true));
	} else if (t == "slump") {
		const Vec3 p = BodyPos(c, body);
		fx_.Ember(c, p, kUp, 1.0f);
		BurstM(c, p, kUp, 0.8f, Burst::Smoke);
	} else if (t == "convert") {
		const Vec3 p = BodyPos(c, body);
		std::string to = S(d, "to");
		for (char& ch : to) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
		ff::Dict fake;
		fake.set("outcome", ff::Value("transform"));
		fake.set("to", ff::Value(to));
		fake.set("pos", ff::Value(p));
		CueInteraction(c, ff::Value(fake));
	} else if (t == "capture") {
		RingM(c, BodyPos(c, body), kBack, 0.6f, 0.1f, 0.3f, Fam::Vortex, Opts(0.3f, 1.5f, 1, true, true));
	} else if (t == "ricochet") {
		const Vec3 at = V(d, "at");
		BurstM(c, at, V(d, "dir", kUp), 0.6f, Burst::Metal);
	} else if (t == "inrush") {
		// air slams back into a collapsed vacuum: rings closing in + an inward puff
		const Vec3 ip = V(d, "pos");
		const Vec3 g(ip.x, c.Ground(ip), ip.z);
		const float rr = MaxF(F(d, "radius", 1.5f), 0.6f);
		RingM(c, g + Vec3(0.0f, 0.05f, 0.0f), kUp, rr * 1.2f, 0.15f, 0.32f, Fam::Vacuum, Opts(0.55f, 1.4f, 2, false, true));
		BurstM(c, g + Vec3(0.0f, 0.6f, 0.0f), kUp, Clamp(rr / 2.0f, 0.5f, 1.2f), Burst::Inflow);
	} else if (t == "extinguish") {
		BurstM(c, BodyPos(c, body), kUp, 0.7f, Burst::Smoke);
	} else if (t == "current_grounded") {
		const Vec3 cg = V(d, "at");
		BurstM(c, cg, kUp, 0.6f, Burst::Static);
		RingM(c, Vec3(cg.x, c.Ground(cg) + 0.04f, cg.z), kUp, 0.1f, 0.9f, 0.25f, Fam::Lightning, Opts(0.3f, 2.5f));
	} else if (t == "fork") {
		BurstM(c, V(d, "at"), kUp, 0.5f, Burst::Static);
	} else if (t == "stick") {
		BurstM(c, BodyPos(c, body), kUp, 0.35f, BurstOfFam(BodyFam(c, body, Fam::Stone)));
	} else if (t == "stance" || t == "mode") {
		const bool on = B(d, "on", true);
		const std::string what = d.has("stance") ? S(d, "stance") : S(d, "kind");
		Aura(c, a, ActorFam(c, a), on && !what.empty());
	}
}

}  // namespace ffx
