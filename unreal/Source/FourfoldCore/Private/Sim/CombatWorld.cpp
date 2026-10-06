// Fourfold core - port of game/core/combat_world.gd: registry, step loop, timers, cleanup and accounting.
// (Intents / actions: CombatWorldActions.cpp; movement / targeting / resources: CombatWorldMove.cpp; hits / grips:
// CombatWorldHits.cpp; bodies / thermal / waves / walls: CombatWorldBodies.cpp; zones / contacts: CombatWorldZones.cpp.)
#include "Sim/CombatWorld.h"

#include "Combat/Acts.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Agent.h"
#include "Sim/FxEvents.h"
#include "Sim/Hooks.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>

namespace ff {

double* MassLedger::field(std::string_view key) {
	if (key == "ground_taken") return &ground_taken;
	if (key == "ground_returned") return &ground_returned;
	if (key == "vapor") return &vapor;
	if (key == "evaporated") return &evaporated;
	if (key == "metal_taken") return &metal_taken;
	if (key == "metal_returned") return &metal_returned;
	if (key == "water_to_plant") return &water_to_plant;
	if (key == "plant_from_ground") return &plant_from_ground;
	if (key == "plant_returned") return &plant_returned;
	if (key == "moisture_taken") return &moisture_taken;
	if (key == "burned") return &burned;
	if (key == "sand_to_glass") return &sand_to_glass;
	if (key == "glass_to_sand") return &glass_to_sand;
	if (key == "sand_to_sandstone") return &sand_to_sandstone;
	return nullptr;
}

CombatWorld::CombatWorld(uint64_t seed_value) {
	Moves::ensure();
	arena = ArenaMap::make_lab();
	rng.set_seed(seed_value);
	pool = spawn_body(Mat::Water, Form::Pool, 2000.0, V3(10.0, arena.pool_level, -1.0), "pool");
	pool->static_body = true;
	pool->phase = Phase::Liquid;
	pool->liquid = 1.0;
}

CombatWorld::~CombatWorld() = default;

// ============================================================== registry

ActorState* CombatWorld::add_actor(const std::string& nm, Vec3 p, int team, const Dict& kit, int element) {
	auto a = std::make_unique<ActorState>();
	a->id = static_cast<int>(actors.size()) + 1;
	a->name = nm;
	a->team = team;
	a->pos = p;
	a->kit = kit.duplicate();
	a->element = element;
	a->ground_y = arena.ground_height(p.x, p.z, p.y);
	a->pos.y = f32(a->ground_y);
	ActorState* raw = a.get();
	actors.push_back(std::move(a));
	return raw;
}

ActorState* CombatWorld::get_actor(int id) const {
	if (id >= 1 && id <= static_cast<int>(actors.size())) return actors[static_cast<size_t>(id - 1)].get();
	return nullptr;
}

MatBody* CombatWorld::get_body(int id) const {
	auto it = _body_by_id.find(id);
	return it == _body_by_id.end() ? nullptr : it->second;
}

BodyRef CombatWorld::body_ref(int id) const {
	MatBody* b = get_body(id);
	return b != nullptr ? b->shared_from_this() : nullptr;
}

MatBody* CombatWorld::spawn_body(Mat mat, Form form, double mass, Vec3 p, const std::string& origin, double temp) {
	_enforce_cap();
	BodyRef b = std::make_shared<MatBody>();
	b->id = _next_body;
	_next_body += 1;
	b->mat = mat;
	b->form = form;
	b->mass = mass;
	b->pos = p;
	b->temp = temp;
	b->origin = origin;
	b->born_tick = tick;
	if (mat == Mat::Water) {
		b->phase = Phase::Liquid;
		b->liquid = 1.0;
	} else if (mat == Mat::Fire || mat == Mat::Air || mat == Mat::Steam) {
		b->phase = Phase::Gas;
	}
	b->update_radius();
	MatBody* raw = b.get();
	bodies.push_back(std::move(b));
	_body_by_id[raw->id] = raw;
	if (_record_events) emit("spawn", D({{"body", raw->id}, {"origin", origin}}));
	return raw;
}

MatBody* CombatWorld::split_body(MatBody& parent, double mass, Vec3 p) {
	mass = minf(mass, parent.mass);
	MatBody* c = spawn_body(parent.mat, parent.form, mass, p, "split:" + itos(parent.id), parent.temp);
	c->parent_id = parent.id;
	c->lineage = parent.lineage;
	c->lineage.push_back(parent.id);
	c->liquid = parent.liquid;
	c->phase = parent.phase;
	c->tag = parent.tag;
	c->sub = parent.sub;
	c->tier = parent.tier;
	c->owner = parent.owner;
	if (parent.heat_payload > 0.0 && parent.mass > 0.0) {
		const double hp = parent.heat_payload * mass / parent.mass;
		c->heat_payload = hp;
		parent.heat_payload -= hp;
	}
	parent.mass -= mass;
	parent.update_radius();
	c->update_radius();
	emit("split", D({{"parent", parent.id}, {"child", c->id}, {"mass", mass}}));
	return c;
}

void CombatWorld::merge_bodies(MatBody& into, MatBody& other) {
	const double e = into.thermal_energy() + other.thermal_energy();
	const double m = into.mass + other.mass;
	if (m > 1e-9) {
		into.vel = (into.vel * into.mass + other.vel * other.mass) / m;
		into.liquid = (into.liquid * into.mass + other.liquid * other.mass) / m;
	}
	into.heat_payload += other.heat_payload;
	other.heat_payload = 0.0;
	into.mass = m;
	into.absorbed.push_back(other.id);
	into.update_radius();
	_set_energy(into, e);
	other.mass = 0.0;
	other.alive = false;
	emit("merge", D({{"into", into.id}, {"absorbed", other.id}}));
}

void CombatWorld::_set_energy(MatBody& b, double e) {
	if (b.mat == Mat::Fire) {
		b.heat_payload = maxf(0.0, e);
		return;
	}
	if (b.mass <= 1e-9) return;
	e -= b.heat_payload;
	if (Materials::is_fusible(b.mat) && b.mat != Mat::Stone) {
		const double sc = Materials::c(b.mat);
		const double mc = Materials::melt(b.mat);
		const double lat = Materials::latent(b.mat);
		const double sens = b.mass * sc * (mc - Sim::AMBIENT_C);
		if (e > sens && lat > 0.0) {
			b.temp = mc;
			b.liquid = clampf((e - sens) / (b.mass * lat), 0.0, 1.0);
			b.temp += (e - sens - b.liquid * b.mass * lat) / (b.mass * sc);
		} else {
			b.liquid = 0.0;
			b.temp = Sim::AMBIENT_C + e / (b.mass * sc);
		}
		return;
	}
	if (b.mat == Mat::Plant) {
		b.temp = Sim::AMBIENT_C + e / (b.mass * Materials::c(b.mat));
		return;
	}
	if (b.mat == Mat::Stone) {
		if (b.liquid > 0.0) {
			b.temp = Sim::STONE_MELT_C;
			const double latent = b.mass * Sim::STONE_LATENT;
			b.liquid = clampf((e - b.mass * Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C)) / latent, 0.0, 1.0);
			const double rest = e - b.mass * Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C) - b.liquid * latent;
			b.temp += rest / (b.mass * Sim::STONE_C);
		} else {
			b.temp = Sim::AMBIENT_C + e / (b.mass * Sim::STONE_C);
		}
	} else if (b.mat == Mat::Water) {
		b.temp = Sim::AMBIENT_C + (e + Sim::WATER_LATENT_FUSION * b.mass * (1.0 - b.liquid)) / (b.mass * Sim::WATER_C);
	}
}

void CombatWorld::remove_body(MatBody& b, const std::string& reason) {
	if (!b.alive) return;
	b.alive = false;
	if (b.controller >= 0) {
		ActorState* h = get_actor(b.controller);
		if (h != nullptr && h->held_body == b.id) h->held_body = -1;
	}
	emit("despawn", D({{"body", b.id}, {"reason", reason}}));
}

int CombatWorld::new_attack_id() {
	_next_attack += 1;
	return _next_attack;
}

void CombatWorld::emit(const std::string& type, Dict d) {
	if (!_record_events) return;
	d.set("type", type);
	d.set("tick", tick);
	events.push_back(std::move(d));
}

std::vector<Dict> CombatWorld::take_events() {
	std::vector<Dict> e;
	e.swap(events);
	return e;
}

// ============================================================== step

void CombatWorld::step(const std::map<int, ActorIntent>& intents) {
	_intents = &intents;
	for (auto& a : actors) _timers(*a);
	for (auto& a : actors) _process_intent(*a, _intent_for(*a));
	for (auto& a : actors) _tick_action(*a, _intent_for(*a));
	for (auto& a : actors) _move_actor(*a, _intent_for(*a));
	_separate_actors();
	_resolve_grips();
	_update_bodies();
	_contacts();
	for (auto& a : actors) _regen(*a);
	_cleanup();
	tick += 1;
}

const ActorIntent& CombatWorld::_intent_for(const ActorState& a) const {
	if (a.is_dummy || _intents == nullptr) return _null_intent;
	auto it = _intents->find(a.id);
	return it == _intents->end() ? _null_intent : it->second;
}

void CombatWorld::_timers(ActorState& a) {
	a.iframes = maxf(0.0, a.iframes - Sim::DT);
	a.burn_cd = maxf(0.0, a.burn_cd - Sim::DT);
	a.wetness = maxf(0.0, a.wetness - 0.05 * Sim::DT);
	Status::tick(*this, a, Sim::DT);
	if (a.stun > 0.0) {
		a.stun -= Sim::DT;
		if (a.stun <= 0.0) {
			a.stun = 0.0;
			if (a.stun_kind == "knockdown") {
				a.stun = 0.75;
				a.stun_kind = "getup";
				a.iframes = 0.75;
				emit("getup", D({{"actor", a.id}}));
			} else {
				a.stun_kind = "";
			}
		}
	}
	if (tick % 120 == 0) {
		for (auto it = a.hits_taken.begin(); it != a.hits_taken.end();) {
			if (tick - it->second > 600) it = a.hits_taken.erase(it);
			else ++it;
		}
	}
}

// ============================================================== cleanup & caps

void CombatWorld::_cleanup() {
	if (tick % 30 == 0) trim_remnants();
	bool any_dead = false;
	for (const BodyRef& b : bodies)
		if (!b->alive) {
			any_dead = true;
			break;
		}
	if (any_dead) {
		std::vector<BodyRef> keep;
		keep.reserve(bodies.size());
		for (BodyRef& b : bodies) {
			if (b->alive) keep.push_back(b);
			else _body_by_id.erase(b->id);
		}
		bodies.swap(keep);
	}
}

int CombatWorld::alive_count() const {
	int n = 0;
	for (const BodyRef& b : bodies)
		if (b->alive) ++n;
	return n;
}

void CombatWorld::_enforce_cap() {
	if (alive_count() < Sim::MAX_BODIES) return;
	// Decay the oldest inert remnant (never a held / attacking / static body); zones and captured bodies last.
	for (int pass_i = 0; pass_i < 2; ++pass_i) {
		for (size_t i = 0; i < bodies.size(); ++i) {
			MatBody& b = *bodies[i];
			if (b.alive && b.controller < 0 && b.attack_id == 0 && !b.static_body && b.form != Form::Wall) {
				if (pass_i == 0 && (b.form == Form::Zone || b.captured_by >= 0)) continue;
				if (b.form == Form::Zone) close_zone(b, "cap");
				else decay_body(b, "cap");
				return;
			}
		}
	}
}

void CombatWorld::trim_remnants() {
	std::vector<MatBody*> rem;
	for (const BodyRef& bp : bodies) {
		MatBody& b = *bp;
		if (b.alive && b.is_stone() && b.controller < 0 && b.attack_id == 0 && b.on_ground && b.form != Form::Wall) rem.push_back(&b);
	}
	if (static_cast<int>(rem.size()) <= Sim::MAX_REMNANTS) return;
	std::stable_sort(rem.begin(), rem.end(), [](const MatBody* x, const MatBody* y) {
		if (x->age != y->age) return x->age > y->age;
		return x->id < y->id;
	});
	size_t front = 0;
	while (rem.size() - front > static_cast<size_t>(Sim::MAX_REMNANTS)) {
		decay_body(*rem[front], "remnant_cap");
		++front;
	}
}

// ============================================================== accounting

double CombatWorld::system_energy() const {
	double e = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive) e += b->thermal_energy();
	for (const auto& a : actors) {
		e += a->heat_reserve;
		if (a->action != nullptr) e += dnum(a->action->data, "heat_paid", 0.0);
	}
	return e;
}

double CombatWorld::ledger_balance() const {
	return ledger.generated + ledger.ambient - ledger.vapor - ledger.reserve_dissipated - ledger.vented - ledger.spent +
	       ledger.freeze_dump - ledger.removed;
}

double CombatWorld::water_mass() const {
	double m = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive && (b->mat == Mat::Water || b->mat == Mat::Steam)) m += b->mass;
	for (const auto& a : actors) m += a->water_carried;
	return m + mass_ledger.vapor + mass_ledger.evaporated + mass_ledger.water_to_plant - mass_ledger.moisture_taken;
}

double CombatWorld::stone_mass() const {
	double m = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive && b->mat == Mat::Stone) m += b->mass;
	return m + mass_ledger.ground_returned - mass_ledger.ground_taken;
}

double CombatWorld::earth_mass() const {
	double m = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive && (b->mat == Mat::Stone || b->mat == Mat::Sand || b->mat == Mat::Glass)) m += b->mass;
	return m + mass_ledger.ground_returned - mass_ledger.ground_taken;
}

double CombatWorld::metal_mass() const {
	double m = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive && b->mat == Mat::Metal) m += b->mass;
	for (const auto& a : actors) m += a->metal_carried;
	return m + mass_ledger.metal_returned - mass_ledger.metal_taken;
}

double CombatWorld::plant_mass() const {
	double m = 0.0;
	for (const BodyRef& b : bodies)
		if (b->alive && b->mat == Mat::Plant) m += b->mass;
	return m + mass_ledger.burned + mass_ledger.plant_returned - mass_ledger.water_to_plant - mass_ledger.plant_from_ground;
}

// ============================================================== hooks

Dict CombatWorld::tech_preview(ActorState& a, Vec3 dir) {
	const std::string k = itos(a.element) + "/" + itos(a.sub());
	auto& previews = Hooks::tech_previews();
	auto it = previews.find(k);
	if (it != previews.end() && it->second) return it->second(*this, a, dir);
	if (a.element == Sim::FIRE && a.sub() == 0) return ActFire::preview(*this, a, dir);
	const std::string tid = Moves::resolve(a.element, a.sub(), "tech");
	const Dict d = Moves::defs().get(tid).as_dict();
	if (dstr(d, "verb", "") == "grip") return Verbs::grip_preview(*this, a, d, dir);
	return Dict();
}

std::vector<MatBody*> CombatWorld::body_list() const {
	std::vector<MatBody*> out;
	out.reserve(bodies.size());
	for (const BodyRef& b : bodies) out.push_back(b.get());
	return out;
}

}  // namespace ff
