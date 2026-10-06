// Fourfold core - port of game/combat/kits/earth/earth_magma.gd (Earth / Magma, sub 3; MOVESET §7.4): Ember Clot /
// Spatter (heat into the globs), Magma Bomb pools, Lava Lash / Molten Lance (wall face pour), Magma Surge / Lava Tide,
// Melt Pit, Magma Curtain (guard spec), Slag Wave and Magma Hold (Cool & Set, Reverse Tide). Heat moves only through
// heat_body / Thermal with the remainder booked (spent / removed).
#include "Combat/Kits/Earth/Earth.h"

#include "Combat/Acts.h"
#include "Combat/Kits/Earth/KitEarth.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace EarthMagma {

using KitEarthUtil::flat_dist2;
using KitEarthUtil::flatv;

namespace {

Vec3 pour_start_pos(CombatWorld& w, const ActorState& a, Vec3 dir) {
	bool blocked = false;
	return ActFire::_pour_start(w, a, dir, &blocked);
}

void _unkit(ActorState& a, ActionInst& inst) {
	if (dbool(inst.data, "kit_magma", false)) {
		a.kit.erase("magma");
		inst.data.set("kit_magma", false);
	}
}

void _slag(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "curtain", -1));
	if (wall == nullptr || !wall->alive) return;
	const double m = minf(dnum(inst.def, "mass", 15.0), wall->mass * 0.5);
	const Vec3 dir = dvec(inst.data, "face");
	const Vec3 p = wall->pos + flatv(dir).normalized() * (static_cast<double>(wall->wall_half.z) + 0.7);
	MatBody* slag = w.split_body(*wall, m, p);
	slag->form = Form::Blob;
	slag->tag = "";
	slag->wall_half = V3(1.0, 0.6, 0.25);
	slag->static_body = false;
	const double hp = wall->heat_payload + slag->heat_payload;   // split_body gave the slag its share of the face
	wall->heat_payload = 0.0;
	slag->heat_payload = 0.0;
	wall->props.erase("face");
	const double hu = hp + Verbs::take_heat(inst);
	const double used = w.heat_body(*slag, hu);
	w.ledger.spent += hu - used;
	Thermal::update_phase(*slag);
	MatBody* wv = KitEarthUtil::pour_wave(w, a, inst, *slag, dir, 1.0, "slag_wave");
	Verbs::fx(w, a, inst, "release", D({{"body", wv->id}, {"pos", wv->pos}, {"dir", wv->wave_dir}, {"length", wv->wave_budget}}));
}

void _cool(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	if (!w.spend_focus(a, dnum(inst.def, "shape_cost", 3.0))) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "shape"}}));
		return;
	}
	inst.data.set("shaped", true);
	const double e = maxf(0.0, b.thermal_energy() - b.heat_payload);
	const double got = -Thermal::heat(b, -e);
	w.ledger.removed += got;
	Thermal::update_phase(b);
	if (b.form == Form::Blob) b.form = Form::Chunk;
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", "cool"}}));
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "lava"}, {"to", "rock"}, {"why", "cool_and_set"}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", b.id}, {"shape", "ground"}}));
}

MatBody* _seek_target(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double reach = dnum(inst.def, "reach");
	const Vec3 dir = dvec(inst.data, "aim", a.forward());
	const int aid = a.id;
	auto f = [aid](MatBody& x) {
		if (x.controller == aid || x.form == Form::Pool || x.form == Form::Wall || x.captured_by >= 0) return false;
		if (x.form == Form::Zone) return x.is_stone() && x.tag == "lava_pool";
		return Interactions::allows(x, "grip_magma");
	};
	return w.find_body(a, dir, reach, dnum(inst.def, "cone", 60.0), f);
}

void _reverse_tide(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wave) {
	if (inst.data.get("tide_tried", Value(-1)) == Value(wave.id)) return;
	inst.data.set("tide_tried", wave.id);
	const double s = w.grip_strength(a, wave, dnum(inst.def, "base", 0.85), dnum(inst.def, "reach")) * dnum(inst.def, "grip_mult", 1.3);
	// The pourer's authority: the wave's cohesion plus its momentum of mass (bigger waves are harder to turn).
	const double auth = Interactions::cohesion(wave.tier) + 0.25 * clampf(wave.mass / 40.0, 0.0, 1.5);
	ActorState* pourer = w.get_actor(wave.attack_owner);
	const bool ok = s > auth + CombatWorld::GRIP_MARGIN && wave.mass <= a.max_control_mass;
	w.emit("interaction",
	       D({{"threat", "lava_wave"}, {"counter", "grip_magma"}, {"outcome", ok ? "reclaim" : "overwhelm"}, {"band", ok ? "full" : "fail"},
	          {"ratio", s / maxf(auth + CombatWorld::GRIP_MARGIN, 1e-3)}, {"tp", auth}, {"cp", s}, {"perfect", false}, {"pos", wave.pos},
	          {"dir", wave.wave_dir}, {"threat_actor", pourer != nullptr ? pourer->id : -1}, {"counter_actor", a.id}, {"threat_body", wave.id},
	          {"counter_body", -1}, {"to", ""}, {"rule", "reverse_tide"}, {"tier", inst.tier()}}));
	if (!ok) {
		w.emit("control_fail", D({{"actor", a.id}, {"body", wave.id}, {"reason", "contest"}}));
		w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"body", wave.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	Vec3 back = -wave.wave_dir;
	if (pourer != nullptr) {
		const Vec3 to = flatv(pourer->pos - wave.pos);
		if (to.length() > 0.5) back = to.normalized();
	}
	KitEarthUtil::pour_wave(w, a, inst, wave, back, 1.0, "reverse_tide");
	w.emit("reverse_tide", D({{"actor", a.id}, {"body", wave.id}, {"from", pourer != nullptr ? pourer->id : -1}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", wave.id}, {"pos", wave.pos}, {"dir", back}}));
	w.set_phase(a, inst, ActionPhase::Recovery);
}

void _hold_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	MatBody* b = w.held(a);
	if (b == nullptr && !it.tech_cancel) {
		MatBody* tgt = _seek_target(w, a, inst);
		if (tgt != nullptr && tgt->form == Form::Wave && tgt->attack_id != 0 && tgt->attack_owner != a.id) {
			_reverse_tide(w, a, inst, *tgt);
			return;
		}
		if (tgt != nullptr && tgt->form == Form::Zone) {
			// A lava pool gathers into a molten blob that can be seized.
			FxEvents::zone(w, *tgt, "close");
			tgt->form = Form::Blob;
			tgt->tag = "";
			tgt->zone_radius = 0.0;
			tgt->update_radius();
		}
		if (tgt != nullptr && tgt->static_body && tgt->props.has("face_of")) tgt->static_body = false;
	}
	if (b == nullptr) {
		VerbGrip::tick(w, a, inst, it);
		return;
	}
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("face", inst.data.get("aim"));
	inst.data.set("aim_point", w.aim_point(a, it));
	if (it.tech_cancel) {
		VerbGrip::drop(w, a, inst);
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	const Vec3 dir = dvec(inst.data, "aim");
	b->hold_point = a.pos + V3(0, 1.2, 0) + a.forward() * (0.3 + b->radius) + dir * 0.15;
	if (it.attack_pressed && !dbool(inst.data, "shaped", false)) _cool(w, a, inst, *b);
	if (!it.tech_held) {
		w.set_phase(a, inst, ActionPhase::Active);
		if (b->is_stone() && b->liquid > 0.3) {
			const Vec3 tp = Verbs::target_point(w, a, inst);
			MatBody* wv = KitEarthUtil::pour_wave(w, a, inst, *b, tp - b->pos, 1.0, "magma_hold");
			wv->pos = pour_start_pos(w, a, flatv(tp - a.pos).normalized());
			wv->wave_path.assign(1, wv->pos);
			Verbs::fx(w, a, inst, "release", D({{"body", wv->id}, {"pos", wv->pos}, {"dir", wv->wave_dir}, {"length", wv->wave_budget}}));
		} else {
			VerbGrip::throw_(w, a, inst);
		}
	}
}

}  // namespace

// ================================================================ Ember Clot / Spatter (heat into the globs)

bool clot_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const std::vector<MatBody*> bodies = VerbProjectile::fire(w, a, inst);
	const double hu = Verbs::take_heat(inst);
	if (bodies.empty()) {
		w.ledger.spent += hu;
		return true;
	}
	const double share = hu / static_cast<double>(bodies.size());
	for (MatBody* b : bodies) {
		const double used = w.heat_body(*b, share);
		w.ledger.spent += share - used;
		Thermal::update_phase(*b);
		if (b->liquid > 0.5) b->form = Form::Blob;
		const double pr = Charge::paramf(inst, "pool_r", 0.0);
		if (pr > 0.0) {
			b->props.set("pool_r", pr);
			b->props.set("on_impact", "zone");
		}
	}
	return true;
}

bool bomb_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	const double r = dnum(b.props, "pool_r", 0.0);
	if (r <= 0.0 || !b.alive || !b.is_stone()) return true;
	ActorState* owner = w.get_actor(b.attack_owner);
	b.form = Form::Zone;
	b.tag = "lava_pool";
	b.zone_radius = r;
	b.radius = r;
	b.owner = owner != nullptr ? owner->id : -1;
	b.vel = Vec3();
	b.attack_id = 0;
	b.gravity_scale = 0.0;
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4));
	b.max_life = -1.0;
	b.props.set("height", 0.6);
	b.props.set("rate", 0.25);
	b.props.set("drag", 12.0);
	b.props.set("landed_on", what);
	FxEvents::zone(w, b, "open");
	FxEvents::fx(w, "splash", "magma", D({{"actor", b.owner}, {"body", b.id}, {"pos", b.pos}, {"radius", r}, {"element", 0}, {"sub", 3}}));
	return true;
}

void pool_effect(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (!z.is_stone()) return;
	if (z.liquid <= POOL_SET_LIQUID) {
		FxEvents::zone(w, z, "close");
		z.form = Form::Chunk;
		z.tag = "";
		z.zone_radius = 0.0;
		z.update_radius();
		z.on_ground = true;
		z.max_life = z.age + Sim::REMNANT_LIFETIME;
		w.emit("transform", D({{"body", z.id}, {"at", z.pos}, {"from", "lava"}, {"to", "rock"}, {"why", "cooled"}}));
		return;
	}
	for (const auto& ap : w.actors) {
		ActorState& a = *ap;
		if (a.health <= 0.0 || !a.grounded || Status::immune(a, "burn")) continue;
		if (flat_dist2(a.pos, z.pos) <= z.zone_radius && absf(a.pos.y - z.pos.y) < 0.5) Status::apply(w, a, "burning", 0.6, 1.0, z.owner);
	}
}

// ================================================================ Lava Lash / Molten Lance

bool lash_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (Charge::paramf(inst, "pulse", 0.0) <= 0.0) {
		VerbVolume::cone(w, a, inst);
		return true;
	}
	inst.data.set("active", Charge::paramf(inst, "active_t", 0.6));
	const double face_hu = Charge::paramf(inst, "face_hu", 0.0);
	if (face_hu > 0.0) {
		const Vec3 dir = dvec(inst.data, "face", a.forward());
		const Vec3 from = a.chest();
		const Vec3 to = from + dir * Charge::paramf(inst, "range", 10.0);
		MatBody* best = nullptr;
		double bt = kInf;
		for (size_t i = 0; i < w.bodies.size(); ++i) {
			MatBody& b = *w.bodies[i];
			if (b.alive && b.form == Form::Wall && b.wall_rise > 0.5 && b.mat == Mat::Stone) {
				const double t = w.wall_segment_t(from, to, b);
				if (t >= 0.0 && t < bt) {
					bt = t;
					best = &b;
				}
			}
		}
		if (best != nullptr) {
			const double hu = minf(face_hu, dnum(inst.data, "heat_paid", 0.0));
			inst.data.set("heat_paid", dnum(inst.data, "heat_paid", 0.0) - hu);
			pour_face(w, a, inst, *best, hu);
		}
	}
	return false;
}

void pour_face(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall, double hu) {
	if (hu <= 0.0 || !wall.alive) return;
	const double used = w.heat_body(wall, hu);
	w.ledger.spent += hu - used;
	wall.props.set("face_hu", dnum(wall.props, "face_hu", 0.0) + used);
	w.emit("heating", D({{"actor", a.id}, {"body", wall.id}, {"temp", wall.temp}, {"wall", wall.id}, {"face_hu", wall.props.get("face_hu")}}));
	const double shell = wall.mass * 0.25;
	const double need = shell * (Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C) + Sim::STONE_LATENT * 0.5);
	if (dnum(wall.props, "face_hu") < need) return;
	Vec3 n = V3(std::sin(wall.wall_yaw), 0.0, std::cos(wall.wall_yaw));
	if (n.dot(a.pos - wall.pos) < 0.0f) n = -n;
	const Vec3 p = wall.pos + n * (static_cast<double>(wall.wall_half.z) + 0.05) + Vec3(0.0f, wall.wall_half.y, 0.0f);
	MatBody* face = w.split_body(wall, shell, p);
	face->form = Form::Chunk;
	face->vel = Vec3();
	face->props.set("face_n", n);
	face->update_radius();
	// The heat poured into the face concentrates there (moved from the rest of the wall, exact): up to 80 % molten.
	const double want = shell * (Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C) + Sim::STONE_LATENT * 0.8) - maxf(0.0, face->thermal_energy());
	const double avail = maxf(0.0, wall.thermal_energy());
	const double move = clampf(want, 0.0, avail);
	if (move > 0.0) {
		const double got = -Thermal::heat(wall, -move);
		const double put = w.heat_body(*face, got);
		w.ledger.spent += got - put;
	}
	Thermal::update_phase(*face);
	wall.props.erase("face_hu");
	VerbHeat::_slump(w, a, inst, wall, *face);
}

// ================================================================ Magma Surge / Lava Tide

bool surge_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double reach = dnum(inst.def, "reach", SURGE_REACH);
	const double mult = Charge::paramf(inst, "budget_mult", 1.0);
	std::vector<MatBody*> src;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || !b.is_stone() || b.liquid < 0.25 || b.controller >= 0 || b.captured_by >= 0) continue;
		if (b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Puddle) continue;
		if (b.form == Form::Wave && b.attack_id != 0 && b.attack_owner != a.id && b.tag != "magma_rift") continue;
		if (b.static_body && !b.props.has("face_of")) continue;
		if (flat_dist2(b.pos, a.pos) > reach + b.radius) continue;
		src.push_back(&b);
	}
	const Vec3 apos = a.pos;
	std::stable_sort(src.begin(), src.end(), [apos](MatBody* x, MatBody* y) {
		const double dx = flat_dist2(x->pos, apos);
		const double dy = flat_dist2(y->pos, apos);
		return dx < dy - 1e-6 || (absf(dx - dy) <= 1e-6 && x->id < y->id);
	});
	const Vec3 target = Verbs::target_point(w, a, inst);
	std::vector<MatBody*> waves;
	if (src.empty()) {
		// No lava near: raise an 8 kg vein from the ground and melt it (+160 HU, reserve first).
		const double hu = w.pay_heat(a, VEIN_HU, false);
		if (hu <= 0.0) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "vein"}}));
			return true;
		}
		const Vec3 ps = pour_start_pos(w, a, dvec(inst.data, "face", a.forward()));
		MatBody* v = w.spawn_body(Mat::Stone, Form::Chunk, VEIN_MASS, ps, "ground@" + ftos(ps.x, 1) + "," + ftos(ps.z, 1));
		w.mass_ledger.ground_taken += VEIN_MASS;
		const double used = w.heat_body(*v, hu);
		w.ledger.spent += hu - used;
		Thermal::update_phase(*v);
		src.push_back(v);
	} else if (Charge::paramb(inst, "merge", false) && src.size() > 1) {
		// Lava Tide: everything flows together into the largest body first.
		MatBody* big = src[0];
		for (MatBody* b : src)
			if (b->mass > big->mass) big = b;
		for (MatBody* b : src) {
			if (b != big && b->alive) {
				if (b->form == Form::Zone) FxEvents::zone(w, *b, "close");
				w.merge_bodies(*big, *b);
			}
		}
		src.assign(1, big);
	}
	for (MatBody* b : src) {
		Vec3 dir = flatv(target - b->pos);
		if (dir.length() < 0.5) dir = flatv(dvec(inst.data, "face", a.forward()));
		MatBody* wv = KitEarthUtil::pour_wave(w, a, inst, *b, dir, mult, "magma_surge");
		waves.push_back(wv);
		Verbs::fx(w, a, inst, "release",
		          D({{"body", wv->id}, {"pos", wv->pos}, {"dir", wv->wave_dir}, {"length", wv->wave_budget}, {"radius", wv->wave_width}}));
	}
	Array ids;
	for (MatBody* x : waves) ids.append(x->id);
	inst.data.set("bodies", ids);
	w.emit("magma_surge", D({{"actor", a.id}, {"waves", ids}, {"tier", inst.tier()}}));
	return true;
}

// ================================================================ Melt Pit

bool pit_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	inst.data.set("tier", KitEarthUtil::guard_tier(inst));
	const double extra = Charge::paramf(inst, "pit_heat_add", 0.0);
	if (extra > 0.0) Verbs::pay_dict(w, a, inst, D({{"heat", extra}}));
	MatBody* z = VerbZone::spawn(w, a, inst);
	z->static_body = true;
	z->heat_payload = Verbs::take_heat(inst);
	z->props.set("spare_owner", true);
	return true;
}

void pit_effect(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	for (const auto& ap : w.actors) {
		ActorState& a = *ap;
		if (a.health <= 0.0 || a.id == z.owner || !a.grounded || Status::immune(a, "burn")) continue;
		if (flat_dist2(a.pos, z.pos) <= z.zone_radius && absf(a.pos.y - z.pos.y) < 0.5) {
			Status::apply(w, a, "burning", 0.5, 1.0, z.owner);
			Status::apply(w, a, "slowed", 0.2, 1.0, z.owner);
		}
	}
}

// ================================================================ module lifecycle (curtain spec, slag_wave, magma_hold)

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		VerbBarrier::start(w, a, inst, it);
		MatBody* wall = w.get_body(dint(inst.data, "wall", -1));
		const double hu = Verbs::take_heat(inst);
		if (wall != nullptr && wall->alive) {
			wall->heat_payload += hu;   // the molten face (counted by thermal_energy, booked when it sinks)
			wall->props.set("face", true);
		} else {
			w.ledger.spent += hu;
		}
		return;
	}
	inst.data.set("face", w.aim_dir(a, it));
	inst.data.set("aim", inst.data.get("face"));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("aim_point", w.aim_point(a, it));
	if (inst.id == "slag_wave") {
		MatBody* wall = w.get_body(dint(inst.data, "wall", a.wall_body));
		if (wall == nullptr || !wall->alive || wall->form != Form::Wall || wall->last_actor != a.id || wall->heat_payload <= 1.0) {
			KitEarthUtil::fizzle(w, a, inst, "material");
			return;
		}
		if (!Verbs::pay(w, a, inst, "start")) {
			inst.data.set("fizzle", true);
			return;
		}
		inst.data.set("curtain", wall->id);
		inst.data.set("keep_wall", wall->id);
		Verbs::fx(w, a, inst, "cast", D({{"body", wall->id}}));
	} else if (inst.id == "magma_hold") {
		Verbs::on_start(w, a, inst, it);
		if (!dbool(inst.data, "fizzle", false) && !a.has("magma")) {
			a.kit.set("magma", true);   // insulated hold: the core magma-hold upkeep (3 Focus/s)
			inst.data.set("kit_magma", true);
		}
	} else {
		Verbs::on_start(w, a, inst, it);
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	if (inst.id == "slag_wave") return ActionPhase::Active;
	return Verbs::after_startup(w, a, inst, it);
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "guard") {
		if (p == ActionPhase::Recovery) VerbBarrier::end(w, a, inst, "release");
		return;
	}
	if (inst.id == "slag_wave") {
		if (p == ActionPhase::Active) {
			_slag(w, a, inst);
		} else if (p == ActionPhase::Recovery) {
			inst.data.set("keep_wall", -1);
			w.ledger.spent += Verbs::take_heat(inst);
		}
	} else if (inst.id == "magma_hold") {
		Verbs::on_phase(w, a, inst, p);
		if (p == ActionPhase::Recovery || p == ActionPhase::Active) _unkit(a, inst);
	} else {
		Verbs::on_phase(w, a, inst, p);
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		VerbBarrier::tick(w, a, inst, it);
		return;
	}
	if (inst.id == "slag_wave") {
	} else if (inst.id == "magma_hold") {
		if (inst.phase == ActionPhase::Channel) _hold_tick(w, a, inst, it);
	} else {
		Verbs::on_tick(w, a, inst, it);
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (inst.id == "guard") {
		VerbBarrier::end(w, a, inst, reason);
		return;
	}
	if (inst.id == "slag_wave") {
		inst.data.set("keep_wall", -1);
		w.ledger.spent += Verbs::take_heat(inst);
	} else if (inst.id == "magma_hold") {
		Verbs::on_interrupt(w, a, inst, reason);
		_unkit(a, inst);
	} else {
		Verbs::on_interrupt(w, a, inst, reason);
	}
}

Dict preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	ActionInst inst;
	inst.def = Moves::defs().get("magma_hold").as_dict();
	inst.data.set("aim", dir);
	MatBody* b = _seek_target(w, a, inst);
	if (b == nullptr) return D({{"mode", "MAGMA"}, {"body", -1}, {"ok", false}, {"reason", "target"}});
	if (b->form == Form::Wave && b->attack_id != 0 && b->attack_owner != a.id) return D({{"mode", "REVERSE"}, {"body", b->id}, {"ok", true}, {"reason", ""}});
	const bool ok = b->mass <= a.max_control_mass;
	return D({{"mode", "MAGMA"}, {"body", b->id}, {"ok", ok}, {"reason", ok ? "" : "mass"}});
}

}  // namespace EarthMagma
}  // namespace ff
