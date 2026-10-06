// Fourfold core - port of game/ui/lab/spawn_catalog.gd.
#include "Lab/SpawnCatalog.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Data/GameData.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Materials.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace SpawnCatalog {

namespace {
bool sc_usable(const Dict& e) {
	const std::string build = dstr(e, "build");
	if (build == "verb") return Moves::defs().has(dstr(e, "move"));
	if (build == "perform") return Moves::defs().has(Moves::resolve(dint(e, "element"), dint(e, "sub"), dstr(e, "slot")));
	return true;
}

void sc_book_mass(CombatWorld& w, const MatBody& b) {
	switch (b.mat) {
		case Mat::Stone:
		case Mat::Sand:
		case Mat::Glass: w.mass_ledger.ground_taken += b.mass; break;
		case Mat::Metal: w.mass_ledger.metal_taken += b.mass; break;
		case Mat::Water:
		case Mat::Steam: w.mass_ledger.moisture_taken += b.mass; break;
		case Mat::Plant: w.mass_ledger.plant_from_ground += b.mass; break;
		default: break;
	}
}

Vec3 sc_ground_point(CombatWorld& w, Vec3 p) { return V3(p.x, w.arena.ground_height(p.x, p.z, p.y + 0.6), p.z); }

void sc_arm_and_throw(CombatWorld& w, MatBody& b, const Dict& e, const Dict& p, ActorState& target, ActorState* owner) {
	const double speed = dnum(p, "speed", 14.0);
	b.attack_id = w.new_attack_id();
	b.attack_owner = owner != nullptr ? owner->id : -1;
	b.hit_set.clear();
	if (owner != nullptr) b.hit_set.add(owner->id);
	b.damage = dnum(e, "damage", 10.0);
	b.balance_damage = dnum(e, "balance", 20.0);
	b.on_ground = false;
	if (dbool(e, "wave", false)) {
		// A poured wave: the molten body becomes a ground wave aimed at the target.
		Vec3 dir = target.pos - b.pos;
		dir.y = 0.0f;
		dir = dir.length() > 0.01f ? dir.normalized() : Vec3(0, 0, -1);
		b.form = Form::Wave;
		b.pos = sc_ground_point(w, b.pos);
		b.vel = Vec3();
		b.wave_dir = dir;
		b.wave_budget = 14.0 + 0.2 * b.mass;
		b.wave_width = 1.1 + b.mass * 0.025;
		b.wave_path.assign(1, b.pos);
		b.max_life = -1.0;
		return;
	}
	const double gs = (b.mat == Mat::Fire || b.mat == Mat::Air || b.mat == Mat::Steam) ? 0.0 : 1.0;
	b.gravity_scale = gs;
	b.vel = Verbs::launch_vel(b.pos, target.chest(), maxf(speed, 0.5), gs);
}

MatBody* sc_spawn_body(CombatWorld& w, const Dict& e, const Dict& p, ActorState& target, ActorState* owner, Vec3 at, bool launch) {
	const int mat = Sim::mat_index(dstr(e, "mat"));
	const int form = Sim::form_index(dstr(e, "form"));
	const double mass = dnum(p, "mass", 5.0);
	const Vec3 pos = launch ? launch_origin(target, owner) : sc_ground_point(w, at) + V3(0, 0.35, 0);
	MatBody* b = w.spawn_body(static_cast<Mat>(mat < 0 ? 0 : mat), static_cast<Form>(form < 0 ? 0 : form), mass, pos, "lab");
	sc_book_mass(w, *b);
	b->tag = dstr(e, "tag");
	if (dbool(e, "frozen", false) && b->is_water()) {
		WaterUtil::freeze_body(w, b);
	} else if (p.has("temp")) {
		if (b->is_water()) {
			const double e0 = b->thermal_energy();
			b->temp = dnum(p, "temp");
			const double d = b->thermal_energy() - e0;
			if (d >= 0.0) w.ledger.generated += d;
			else w.ledger.freeze_dump += d;
		} else {
			heat_to(w, *b, dnum(p, "temp"));
		}
	}
	if (dbool(e, "molten", false)) Thermal::update_phase(*b);
	b->update_radius();
	if (b->form == Form::Puddle) {
		b->pos = sc_ground_point(w, at);
		w._water_to_puddle(*b);
		return b->alive ? b : nullptr;
	}
	if (b->form == Form::Wall) {
		b->pos = sc_ground_point(w, at);
		b->wall_yaw = target.facing;
		b->wall_half = Vec3(1.1f, 0.7f, 0.25f);
		b->static_body = true;
		b->wall_rise = 1.0;
		b->props.set("standing", 45.0);
		return b;
	}
	if (b->mat == Mat::Steam) b->max_life = dnum(e, "life", 5.0);
	else if (b->max_life < 0.0) b->max_life = Sim::REMNANT_LIFETIME;
	if (launch) {
		sc_arm_and_throw(w, *b, e, p, target, owner);
	} else {
		b->on_ground = true;
		b->vel = Vec3();
		b->attack_id = 0;
	}
	return b;
}

MatBody* sc_spawn_zone(CombatWorld& w, const Dict& e, const Dict& p, ActorState* owner, Vec3 at, ActorState& target, bool launch) {
	const int mat = Sim::mat_index(dstr(e, "mat"));
	Vec3 pos = sc_ground_point(w, at);
	if (launch) pos = sc_ground_point(w, target.pos);
	const int tier = dint(p, "tier", 0);
	MatBody* z = w.spawn_zone(dstr(e, "tag"), pos, dnum(e, "radius") * (1.0 + 0.15 * tier), owner != nullptr ? owner->id : -1,
	                          dnum(e, "power") * (1.0 + 0.4 * tier), static_cast<Mat>(mat < 0 ? 8 : mat), 0.0, dnum(e, "life"));
	z->tier = tier;
	if (z->mat == Mat::Fire && dnum(e, "heat", 0.0) > 0.0) {
		z->heat_payload = dnum(e, "heat");
		w.ledger.generated += z->heat_payload;
	}
	return z;
}

ActionRef sc_synthetic(CombatWorld& w, const ActorState& owner, const std::string& move_id, int tier, Vec3 dir, const Dict& def) {
	ActionRef inst = std::make_shared<ActionInst>();
	inst->id = move_id;
	inst->def = def;
	inst->element = dint(def, "element", owner.element);
	inst->sub = dint(def, "sub", 0);
	inst->slot = dstr(def, "slot", "strike");
	inst->attack_id = w.new_attack_id();
	inst->data = D({{"tier", tier}, {"aim", dir}, {"face", dir}, {"aim_active", false}, {"aim_point", owner.chest() + dir * 8.0}});
	return inst;
}

Dict sc_scaled_def(const Dict& def, const std::string& key, double ratio) {
	Dict d = def.duplicate(true);
	if (d.has(key)) d.set(key, dnum(d, key) * ratio);
	for (const auto& t : ddict(d, "tiers")) {
		Dict td = t.second.as_dict();
		if (td.has(key)) td.set(key, dnum(td, key) * ratio);
	}
	return d;
}

void sc_freeze_in_place(MatBody& b, Vec3 p) {
	b.pos = p;
	b.vel = Vec3();
	b.attack_id = 0;
	b.on_ground = true;
	b.gravity_scale = 1.0;
}

std::vector<MatBody*> sc_spawn_verb(CombatWorld& w, const Dict& e, const Dict& p, ActorState& target, ActorState& owner, Vec3 at, bool launch) {
	std::vector<MatBody*> out;
	const std::string move_id = dstr(e, "move");
	Dict def = Moves::defs().get(move_id).as_dict();
	const int tier = clampi(dint(p, "tier", 0), 0, 3);
	Vec3 dir = launch ? (target.pos - owner.pos) : (at - owner.pos);
	dir.y = 0.0f;
	dir = dir.length() > 0.01f ? dir.normalized() : owner.forward();
	owner.lock_target = target.id;
	owner.facing = std::atan2(dir.x, dir.z);
	top_up(w, owner);
	const Dict dflt = defaults(e);
	for (const char* key : {"mass", "speed"}) {
		if (p.has(key) && dflt.has(key) && dnum(dflt, key) > 0.0 && absf(dnum(p, key) - dnum(dflt, key)) > 1e-4)
			def = sc_scaled_def(def, key, dnum(p, key) / dnum(dflt, key));
	}
	ActionRef inst = sc_synthetic(w, owner, move_id, tier, dir, def);
	const int first_id = w.next_body_id();
	Verbs::execute(w, owner, *inst);
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody* b = w.bodies[i].get();
		if (b->alive && b->id >= first_id && b != w.pool) out.push_back(b);
	}
	if (launch) return out;
	// Inert: shots are frozen where the aim point is; fields are placed there.
	const Vec3 ground = sc_ground_point(w, at);
	int k = 0;
	for (MatBody* b2 : out) {
		if (b2->form == Form::Wave) continue;
		if (b2->form == Form::Zone || b2->form == Form::Cloud) {
			b2->pos = ground;
			continue;
		}
		sc_freeze_in_place(*b2, ground + V3(0.0, 0.4, 0.0) + V3(0.6 * k, 0.0, 0.0));
		++k;
	}
	return out;
}
}  // namespace

const std::vector<Dict>& entries() {
	static std::vector<Dict> cache;
	static bool built = false;
	if (!built) {
		built = true;
		Moves::ensure_ready();
		for (const Value& v : GameData::lab().get("spawn_entries").as_array())
			if (sc_usable(v.as_dict())) cache.push_back(v.as_dict());
	}
	return cache;
}

Dict find(const std::string& id) {
	for (const Dict& e : entries())
		if (dstr(e, "id") == id) return e;
	return Dict();
}

std::vector<std::string> ids() {
	std::vector<std::string> out;
	for (const Dict& e : entries()) out.push_back(dstr(e, "id"));
	return out;
}

Dict param_specs(const Dict& e) {
	Dict out;
	for (const char* k : {"mass", "speed", "temp", "tier"})
		if (e.get(k).is_array()) out.set(k, e.get(k).as_array().duplicate());
	if (dstr(e, "build") == "verb") {
		const Dict def = Moves::defs().get(dstr(e, "move")).as_dict();
		const int tier = e.get("tier").is_array() ? vint(e.get("tier").as_array().get(0)) : 0;
		if (!out.has("tier")) {
			out.set("tier", A({0, 0, maxi(0, Charge::max_tier(def))}));
		} else {
			Array t = out.get("tier").as_array();
			t[2] = maxi(vint(t[2]), Charge::max_tier(def));
		}
		const Value m = Charge::pget(def, tier, "mass", Value());
		if (!m.is_nil() && vnum(m) > 0.0 && !out.has("mass")) out.set("mass", A({vnum(m), vnum(m) * 0.25, vnum(m) * 4.0}));
		const Value s = Charge::pget(def, tier, "speed", Value());
		if (!s.is_nil() && vnum(s) > 0.0 && dstr(def, "verb", "") != "zone" && !out.has("speed"))
			out.set("speed", A({vnum(s), vnum(s) * 0.4, vnum(s) * 2.0}));
	}
	return out;
}

Dict defaults(const Dict& e) {
	Dict out;
	for (const auto& kv : param_specs(e)) out.set(kv.first, kv.second.as_array().get(0));
	return out;
}

std::string describe(const Dict& e, const Dict& params) {
	Dict p = defaults(e);
	p.merge(params, true);
	std::string out;
	auto add = [&out](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
	if (p.has("mass")) add(ftos(dnum(p, "mass"), 0) + " kg");
	if (p.has("speed")) add(ftos(dnum(p, "speed"), 0) + " m/s");
	if (p.has("temp")) add(ftos(dnum(p, "temp"), 0) + " \xC2\xB0" "C");
	if (p.has("tier") && dint(p, "tier") > 0) add("T" + itos(dint(p, "tier")));
	return out;
}

ActorState* owner_of(CombatWorld& w, const ActorState& player) {
	for (auto& a : w.actors)
		if (a.get() != &player && !a->is_dummy && a->team != player.team) return a.get();
	for (auto& a : w.actors)
		if (a.get() != &player && a->is_dummy) return a.get();
	return nullptr;
}

void heat_to(CombatWorld& w, MatBody& b, double temp) {
	if (temp <= b.temp + 0.5) return;
	double need = 0.0;
	if (Materials::is_fusible(b.mat)) {
		const double c = b.mat == Mat::Stone ? Sim::STONE_C : Materials::c(b.mat);
		const double melt = b.mat == Mat::Stone ? Sim::STONE_MELT_C : Materials::melt(b.mat);
		const double lat = b.mat == Mat::Stone ? Sim::STONE_LATENT : Materials::latent(b.mat);
		if (temp < melt) need = b.mass * c * (temp - b.temp);
		else need = b.mass * c * maxf(melt - b.temp, 0.0) + b.mass * lat * (1.0 - b.liquid) + b.mass * c * (temp - melt);
	} else if (b.mat == Mat::Water) {
		need = b.mass * Sim::WATER_C * (minf(temp, 99.0) - b.temp);
	}
	if (need <= 0.0) return;
	w.ledger.generated += w.heat_body(b, need);
}

Vec3 launch_origin(const ActorState& target, const ActorState* owner) {
	if (owner != nullptr) return owner->hand_point() + owner->forward() * 0.4;
	return target.chest() + target.forward() * 9.0;
}

SpawnResult spawn(CombatWorld& w, const std::string& id, const Dict& params, ActorState& target, ActorState* owner, Vec3 at, bool launch) {
	SpawnResult res;
	const Dict e = find(id);
	if (e.empty()) {
		res.msg = "unknown entry " + id;
		return res;
	}
	Dict p = defaults(e);
	p.merge(params, true);
	if (dbool(e, "inert_only", false) && launch) launch = false;
	if (dbool(e, "launch_only", false) && !launch) launch = true;
	const std::string build = dstr(e, "build");
	if (build == "body") {
		MatBody* b = sc_spawn_body(w, e, p, target, owner, at, launch);
		if (b != nullptr) res.bodies.push_back(b);
	} else if (build == "zone") {
		MatBody* z = sc_spawn_zone(w, e, p, owner, at, target, launch);
		if (z != nullptr) res.bodies.push_back(z);
	} else if (build == "verb") {
		if (owner == nullptr) {
			res.msg = "needs a rival to throw it";
			return res;
		}
		res.bodies = sc_spawn_verb(w, e, p, target, *owner, at, launch);
	} else if (build == "perform") {
		if (owner == nullptr) {
			res.msg = "needs a rival to perform it";
			return res;
		}
		res.script = std::make_unique<LabScript>(LabScript::for_move(dint(e, "element"), dint(e, "sub"), dstr(e, "slot"), dint(p, "tier", 1)));
		res.ok = true;
		return res;
	}
	res.ok = !res.bodies.empty();
	if (!res.ok && res.msg.empty()) res.msg = "nothing spawned";
	return res;
}

void top_up(CombatWorld& w, ActorState& a) {
	a.focus = Sim::FOCUS_MAX;
	const double dh = Sim::RESERVE_MAX - a.heat_reserve;
	if (dh > 0.0) {
		w.ledger.generated += dh;
		a.heat_reserve = Sim::RESERVE_MAX;
	}
	const double dw = 6.0 - a.water_carried;
	if (dw > 0.0) {
		w.mass_ledger.moisture_taken += dw;
		a.water_carried = 6.0;
	}
	const double dm = 12.0 - a.metal_carried;
	if (dm > 0.0) {
		w.mass_ledger.metal_taken += dm;
		a.metal_carried = 12.0;
	}
	a.static_charge = maxf(a.static_charge, 30.0);
}

}  // namespace SpawnCatalog
}  // namespace ff
