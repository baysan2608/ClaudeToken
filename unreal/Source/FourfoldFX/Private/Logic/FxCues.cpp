// FourfoldFX logic island - event cues: ports of FxDirector._event (visual half) and FxCues (fx / interaction /
// charge / status / zone / clash / misc). Sounds, hit-stop, camera, haptics and screen flashes are other modules'.
// Owner: stream `fx`.
#include "FxActorFx.h"
#include "FxDirector.h"
#include "FxFracture.h"

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
		const float speed = F(d, "speed");
		const Vec3 p = BodyPos(c, body);
		if (speed > 3.0f && !world_.InPool(c, p)) fx_.Dust(c, p, kUp, Clamp(F(d, "mass") / 30.0f, 0.4f, 1.2f), stoneDust);
		// the floor answers the hit: cracks / scorch / wet marks by material (the pool is handled by the body tracker)
		if (S(d, "on") == "ground" && !world_.InPool(c, p)) {
			const float k = Sat((speed - 3.0f) / 8.0f) * Clamp(std::sqrt(MaxF(F(d, "mass"), 1.0f)) / 4.5f, 0.25f, 1.8f);
			world_.Impact(c, p, FamFromName(S(d, "mat"), BodyFam(c, body, Fam::Stone)), k);
		}
		return;
	}
	if (t == "wall") {
		const Vec3 p = BodyPos(c, body);
		fx_.Dust(c, p, kUp, 1.0f, stoneDust);
		// an earth wall / spike line rising out of the floor cracks it around its base (sand: a drift)
		const ff::BodyView* wb = c.Body(body);
		if (wb && (wb->mat == ff::Mat::Stone || wb->mat == ff::Mat::Sand || wb->mat == ff::Mat::Metal)) {
			const float half = MaxF(wb->form == ff::Form::Wall ? wb->wall_half.x : wb->radius, 0.5f);
			world_.Scar(c, p, wb->mat == ff::Mat::Sand ? ScarKind::Sand : ScarKind::Crack, half * 0.9f);
			world_.DustRing(c, p, half, 0.7f, wb->mat == ff::Mat::Sand ? cfg_.DustColor(Fam::Sand) : cfg_.world.dust);
		}
		return;
	}
	if (t == "wall_crumble") {
		const Vec3 p = BodyPos(c, body);
		fx_.Dust(c, p, kUp, 1.4f, stoneDust);
		world_.DustRing(c, p, 1.2f, 1.0f, cfg_.world.dust);
		// physics pieces replace the sinking wall (the view stops drawing); otherwise chips fly
		if (!B(d, "melted") && BreakView(c, body)) return;
		fx_.Shards(c, p + Vec3(0.0f, 0.4f, 0.0f), kUp, 1.0f, ShardMat::Stone, static_cast<uint32_t>(body) * 31u + 7u, c.Ground(p));
		return;
	}
	if (t == "fx_test_break") {
		// look checks (ff.fx.Showcase "break/rock|wall"): a stone or a wall breaks with no sim body behind it
		const bool wall = S(d, "kind") == "wall";
		const Vec3 p = V(d, "pos");
		const uint32_t seed = static_cast<uint32_t>(I(d, "seed", 7));
		const float gy = c.Ground(p);
		fx_.Dust(c, Vec3(p.x, gy, p.z), kUp, wall ? 1.4f : 0.8f, stoneDust);
		const int n = wall ? c.q.wallPieces : c.q.rockPieces;
		if (!c.in.physicsDebris || n < (wall ? 1 : 2)) {
			fx_.Shards(c, p, kUp, 1.0f, ShardMat::Stone, seed, gy);
			return;
		}
		const FractureSettings& fs = cfg_.fracture;
		FractureReq& r = c.out.Fracture();
		r.mat = MatSlot::Rock;
		r.params.Set(P::Seed, static_cast<float>(seed % 977u) + 0.5f);
		r.params.Set(P::Rise, 1.0f);
		r.params.Set(P::RiseHeight, 1.25f);
		r.params.Set(P::Detail, wall ? 2.2f : 1.0f);
		r.params.Set(P::Fade, 1.0f);
		r.params.Set(P::Glass, 0.0f);
		r.params.Set(PV::Tint, wall ? Color(1.35f, 1.25f, 1.1f) : Color(1.0f, 1.0f, 1.0f));   // as WallView's stone wall
		if (wall) {
			r.pieces = &meshlib::WallPieces(seed, n);
			r.xform.basis = Basis::YawY(F(d, "yaw")).Scaled(1.2f, 1.2f, 1.0f);   // 2.4 m long, 1.2 m high, 0.54 m thick
			r.xform.pos = Vec3(p.x, gy, p.z);
			r.origin = r.xform.pos - kUp * 0.7f;
			r.burst = fs.wallBurst;
			r.scale = fs.wallScale;
			r.life = fs.wallLife;
		} else {
			r.pieces = &meshlib::RockPieces(seed, n);
			r.xform.basis = Basis::AxisAngle(Norm(Vec3(0.3f, 1.0f, 0.2f)), 0.7f).Scaled(0.35f);
			r.xform.pos = p;
			r.origin = p;
			r.burst = fs.rockBurst;
			r.scale = fs.rockScale;
			r.life = fs.rockLife;
		}
		r.seed = HashCombine(seed, static_cast<uint32_t>(c.in.curr->tick));
		return;
	}
	if (t == "fx_test_world") {
		// look checks (ff.fx.Showcase "world/<kind>"): world reactions with no sim body behind them
		const std::string kind = S(d, "kind");
		const Vec3 p = V(d, "pos");
		const Vec3 dir = V(d, "dir", Vec3(1, 0, 0));
		const float gy = c.Ground(p + Vec3(0.0f, 0.5f, 0.0f));
		const Vec3 g(p.x, gy, p.z);
		if (kind == "crack") {
			fx_.Dust(c, g, kUp, 1.0f, stoneDust);
			fx_.Shards(c, g + Vec3(0.0f, 0.1f, 0.0f), kUp, 1.0f, ShardMat::Stone, 7u, gy);
			world_.Impact(c, g, Fam::Stone, 1.4f, 0.9f);
		} else if (kind == "scorch") {
			fx_.FireBurst(c, g, kUp, 1.8f, 1.0f, false);
			world_.Impact(c, g, Fam::Flame, 1.0f, 1.1f);
		} else if (kind == "bolt") {
			fx_.Bolt(c, {g + Vec3(0.6f, 9.0f, -0.4f), g + Vec3(-0.2f, 4.0f, 0.1f), g}, 11u, 1.3f, true, 0.14f);
			world_.LightningStrike(c, g, 1.3f);
		} else if (kind == "wet") {
			fx_.Splash(c, g, kUp, 1.0f);
			world_.Impact(c, g, Fam::Water, 1.0f, 1.0f);
		} else if (kind == "pool" && c.in.arena) {
			// the pool point nearest to p, a metre in from the rim
			const ff::ArenaView& ar = *c.in.arena;
			const Vec3 q(Clamp(p.x, ar.pool_min.x + 1.0f, ar.pool_max.x - 1.0f), ar.pool_level,
			             Clamp(p.z, ar.pool_min.y + 1.0f, ar.pool_max.y - 1.0f));
			world_.PoolHit(c, q, Fam::Stone, 1.3f);
			world_.PoolHit(c, q + Vec3(1.2f, 0.0f, 0.6f), Fam::Flame, 0.8f);   // and a hiss of steam beside it
		} else if (kind == "gust") {
			fx_.AirPush(c, p + Vec3(0.0f, 1.2f, 0.0f) - dir * 2.0f, dir, 1.6f, 6.0f);
			world_.Gust(c, p + Vec3(0.0f, 1.2f, 0.0f) - dir * 2.0f, dir, 6.0f, 1.0f);
		}
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
		const uint32_t seed = static_cast<uint32_t>(body) * 31u + static_cast<uint32_t>(c.in.curr->tick);
		const Fam fam = BodyFam(c, body, Fam::Ice);
		if (fam == Fam::Ice || fam == Fam::Water) {
			// a frozen body bursts (the original look of this event)
			fx_.Splash(c, p, kUp, 0.5f);
			fx_.Shards(c, p, kUp, 0.8f, ShardMat::Ice, seed, p.y - 1.0f);
			return;
		}
		// stones break into physics pieces (the sim's rubble bodies fly beside them); others / no physics: chips
		if (BreakView(c, body)) return;
		fx_.Shards(c, p, kUp, 0.8f, ShardOf(fam), seed, c.Ground(p));
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
		if (pts.size() >= 2) {
			BurstM(c, pts.back(), kUp, 0.6f, Burst::Static);
			// where it reaches the floor: a burn, sparks, a flash on the surroundings
			const Vec3& tip = pts.back();
			if (tip.y - c.Ground(tip) < 0.6f) world_.LightningStrike(c, tip, 1.0f);
		}
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
		world_.Gust(c, o, gd, rng, heavy ? 1.0f : 0.55f);
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
		world_.Impact(c, feet(a) + Flat(dir) * 2.5f, Fam::Water, 0.35f, 0.45f);   // the whip's spray wets the floor
		return;
	}
	if (t == "draw_water") {
		const Vec3 at = d.has("at") ? V(d, "at") : chest(a);
		DrawStream(c, a, at, body);
		world_.DrawFromPool(c, at, a);
		return;
	}
	if (t == "evade") {
		if (B(d, "dash")) {
			DashTrail(c, a, V(d, "dir"));
			const ff::ActorView* da = c.Actor(a);
			if (da && da->grounded) world_.Kick(c, feet(a), V(d, "dir") * -1.0f, 0.8f, 1.8f, cfg_.world.dust);
		}
		return;
	}
	if (t == "updraft") {
		fx_.Dust(c, feet(a), kUp, 0.9f, stoneDust);
		world_.DustRing(c, feet(a), 1.1f, 0.8f, cfg_.world.dust);
		world_.Gust(c, feet(a), kUp, 1.0f, 0.5f);
		RingM(c, feet(a) + Vec3(0.0f, 0.05f, 0.0f), kUp, 0.3f, 1.2f, 0.35f, Fam::Wind, Opts(0.4f, 1.2f));
		return;
	}
	if (t == "land") {
		const float speed = F(d, "speed");
		const ff::ActorView* la = c.Actor(a);
		if (speed > 5.0f && la && !la->in_water) fx_.Dust(c, feet(a), kUp, 0.5f, stoneDust);
		world_.Landing(c, a, speed);
		return;
	}
	if (t == "slam") {
		// a fighter slammed into the floor
		const Vec3 p = feet(a);
		fx_.Dust(c, p, kUp, 1.0f, stoneDust);
		world_.Impact(c, p, Fam::Stone, 1.0f, 0.7f);
		return;
	}
	if (t == "fire_burst") {
		// a fireball / comet bursting: scorch under it when it burst near the floor
		const Vec3 p = d.has("pos") ? V(d, "pos") : BodyPos(c, body);
		world_.Impact(c, p, B(d, "blue") ? Fam::Blue : Fam::Flame, 1.0f, 1.1f);
		return;
	}
	if (t == "action") {
		// Earth ground / sink / guard moves going active: the fighter stamps the floor
		const ff::ActorView* aa = c.Actor(a);
		if (aa && S(d, "phase") == "active" && aa->action.active && aa->action.element == 0 &&
		    (aa->action.slot == ff::Slot::Ground || aa->action.slot == ff::Slot::Sink || aa->action.slot == ff::Slot::Guard))
			world_.Stomp(c, a, (B(d, "heavy") ? 1.0f : 0.6f) + 0.12f * static_cast<float>(I(d, "tier", 0)));
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
			case Fam::Vortex:
				fx_.AirPush(c, pos, dir, 0.6f, 2.5f * k);
				world_.Gust(c, pos, dir, 2.5f * k, 0.35f * k);
				break;
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
			case Fam::Vortex:
				fx_.AirPush(c, pos, dir, rad, len);
				world_.Gust(c, pos, dir, len, 0.6f * k + 0.2f);
				break;
			case Fam::Sand:
				fx_.AirPush(c, pos, dir, rad, len);
				world_.Gust(c, pos, dir, len, 0.4f * k);
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
					world_.LightningStrike(c, Vec3(pos.x, c.Ground(pos), pos.z), 1.4f);
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
				// a beam ending on the floor marks it (nothing when it ends in the air)
				if (mat == Fam::Water || mat == Fam::Flame || mat == Fam::Blue) world_.Impact(c, tip, mat, 0.4f * k, 0.45f);
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
				if (mat == Fam::Blast && (shape == "small" || radius < 0.8f)) {
					// small pops (hover pops, chain sparks): a short fire burst, no explosion + dust cloud each time
					fx_.FireBurst(c, pos, kUp, MaxF(radius, 0.4f) * 1.6f, 0.6f * k, false);
				} else if (mat == Fam::Blast || rad >= 1.2f) {
					fx_.Blast(c, pos, rad, Clamp(0.6f + power / 40.0f, 0.5f, 1.4f), mat == Fam::Blue, gy);
					// floor dust kicked up by the shock: stone dust, lighter than the blast itself
					fx_.Dust(c, Vec3(pos.x, gy, pos.z), kUp, MinF(0.35f + rad * 0.2f, 1.2f), cfg_.DustColor(Fam::Stone));
					world_.Impact(c, pos, mat, k, MinF(rad * 0.75f, 2.2f));   // scorch + dust shockwave
				} else {
					fx_.FireBurst(c, pos, kUp, rad * 1.5f, k, mat == Fam::Blue);
					world_.Impact(c, pos, mat, 0.6f * k, rad * 0.6f);
				}
				break;
			case Fam::Sand:
			case Fam::Stone:
				BurstM(c, pos, kUp, k, mb);
				BurstM(c, pos, kUp, k * 0.8f, mat == Fam::Sand ? Burst::Grit : Burst::Dust);
				if (mat == Fam::Stone) fx_.Shards(c, pos, kUp, k, ShardMat::Stone, rng_.Next() % 997u, gy);
				RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 0.3f, rad * 1.3f, 0.4f, mat, Opts(0.6f, 1.0f));
				world_.Impact(c, pos, mat, k, MinF(rad * 0.7f, 2.0f));
				break;
			case Fam::Water:
				fx_.Splash(c, pos, kUp, k);
				RingM(c, Vec3(pos.x, gy + 0.04f, pos.z), kUp, 0.3f, rad * 1.2f, 0.4f, Fam::Water, Opts(0.5f, 1.5f));
				world_.Impact(c, pos, Fam::Water, k, MinF(rad * 0.8f, 2.2f));
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
				world_.Impact(c, pos, mat, 0.7f * k, MinF(rad * 0.5f, 1.4f));
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
					if (i == 0) world_.LightningStrike(c, tip, 0.7f);
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
		if (mat == Fam::Stone || mat == Fam::Sound || mat == Fam::Wind || mat == Fam::Sand)
			world_.DustRing(c, Vec3(pos.x, gy, pos.z), MaxF(radius, 1.5f) * 0.6f, 0.6f * k, mat == Fam::Sand ? cfg_.DustColor(Fam::Sand) : cfg_.world.dust);
		if (mat == Fam::Stone && tier >= 1) world_.Scar(c, Vec3(pos.x, gy, pos.z), ScarKind::Crack, 0.5f + 0.15f * static_cast<float>(tier));
		if (world_.InPool(c, pos)) world_.Ripples(c, pos, MaxF(radius, 1.5f), 0.7f, 2);
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
		// the floor it erupted from: cracks / scorch / wet / frost / sand by material
		if (mat != Fam::Plant) world_.Impact(c, g, mat, 1.1f * k, MinF(rad * 0.9f, 2.2f));
		return;
	}
	if (key == "trail") {
		if (mat == Fam::Flame || mat == Fam::Blue || mat == Fam::Blast) {
			if (const ff::ActorView* a = c.Actor(actor)) {
				(void)a;
				const Vec3 ft = c.ActorPos(actor);
				fx_.Ember(c, ft, Norm(dir * -1.0f + kUp * 0.3f), 0.7f);
				fx_.FireBurst(c, ft + Vec3(0.0f, 0.3f, 0.0f), Norm(dir * -1.0f + Vec3(0.0f, -0.5f, 0.0f)), 1.2f, 0.6f, mat == Fam::Blue);
				world_.Scar(c, ft, ScarKind::Scorch, 0.4f, 0.6f);   // the fire dash singes the floor it pushed off
			}
		}
		DashTrail(c, actor, dir);
		return;
	}
	if (key == "splash") {
		switch (mat) {
			case Fam::Water:
			case Fam::Ice:
				fx_.Splash(c, pos, kUp, 0.6f * k);
				world_.Impact(c, pos, Fam::Water, 0.5f * k, 0.5f);
				break;
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
		world_.DustRing(c, Vec3(pos.x, gy, pos.z), 0.8f, 0.5f * st, tm != Fam::Sand ? cfg_.world.dust : cfg_.DustColor(Fam::Sand));
	} else if (outcome == "ground" || outcome == "conduct") {
		const Vec3 gp(pos.x, c.Ground(pos), pos.z);
		Vec3 tgt = gp;
		if (outcome == "conduct") {
			if (const ff::BodyView* cb = c.Body(I(d, "counter_body"))) tgt = cb->pos;
		}
		fx_.Bolt(c, {pos, LerpV(pos, tgt, 0.5f) + Vec3(0.2f, 0.1f, 0.0f), tgt}, rng_.Next(), 0.9f, false, 0.08f);
		BurstM(c, tgt, kUp, 0.4f, Burst::Static);
		if (outcome == "ground") world_.LightningStrike(c, gp, 0.5f);
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
		// what the zone leaves on the floor
		if (kind == "fire_field" || kind == "lava_pool") world_.Scar(c, g, ScarKind::Scorch, MaxF(r, 0.6f) * 0.9f, 0.4f);
		else if (kind == "ice_floor") world_.Scar(c, g, ScarKind::Wet, MaxF(r, 0.6f));
		else if (kind == "sand_cloud" || kind == "sandstorm") world_.Scar(c, g, ScarKind::Sand, MinF(MaxF(r, 0.8f), 3.0f));
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
		// Earth stances root the fighter: a stamp into the floor
		const ff::ActorView* sa = c.Actor(a);
		if (t == "stance" && on && sa && sa->element == 0) world_.Stomp(c, a, 0.8f);
	}
}

}  // namespace ffx
