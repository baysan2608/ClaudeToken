// Fourfold core - port of game/combat/verbs/verb_grip.gd: technique grip (seize / draw with a control contest, hold
// and aim, T+A shape, release = throw). Target legality goes through Interactions::allows(body, ccls).
#include "Combat/Verbs.h"

#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace VerbGrip {

bool filter(const ActorState& a, const Dict& d, MatBody& b) {
	const std::string ccls = dstr(d, "ccls", "grip_stone");
	return b.controller != a.id && b.form != Form::Wall && b.form != Form::Pool && b.form != Form::Zone && b.captured_by < 0 &&
	       Interactions::allows(b, ccls);
}

Dict preview(CombatWorld& w, ActorState& a, const Dict& d, Vec3 dir) {
	const std::string label = dstr(d, "mode_label", "GRIP");
	MatBody* b = w.find_body(a, dir, dnum(d, "reach", 7.5), dnum(d, "cone", 60.0), [&a, &d](MatBody& x) { return filter(a, d, x); });
	if (b != nullptr) {
		if (b->mass > dnum(d, "max_mass", a.max_control_mass)) return D({{"mode", label}, {"body", b->id}, {"ok", true}, {"reason", "mass"}});
		return D({{"mode", label}, {"body", b->id}, {"ok", true}, {"reason", ""}});
	}
	if (dstr(d, "rip_source", "none") != "none") return D({{"mode", "RIP"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	return D({{"mode", label}, {"body", -1}, {"ok", false}, {"reason", "target"}});
}

void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const Dict d = Charge::pdef(inst);
	if (it.tech_cancel) {
		drop(w, a, inst);
		w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("face", inst.data.get("aim"));
	inst.data.set("aim_point", w.aim_point(a, it));
	const double upkeep = Charge::paramf(inst, "upkeep", 0.0) * Sim::DT;
	if (upkeep > 0.0 && w.held(a) != nullptr && !w.spend_focus(a, upkeep)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		drop(w, a, inst);
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	MatBody* b = w.held(a);
	if (b == nullptr) {
		_seek(w, a, inst, d);
		if (a.action.get() == &inst && inst.phase == ActionPhase::Channel && !Charge::held(inst, it) && w.held(a) == nullptr) {
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
		return;
	}
	const Vec3 dir = dvec(inst.data, "aim");
	b->hold_point = a.pos + Vec3(0, 1.2f, 0) + a.forward() * (0.3 + b->radius) + dir * 0.15f;
	if (it.attack_pressed && !dtruthy(inst.data, "shaped")) shape(w, a, inst, *b);
	if (!Charge::held(inst, it)) {
		w.set_phase(a, inst, ActionPhase::Active);
		throw_(w, a, inst);
	}
}

void _seek(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& d) {
	const double reach = Charge::paramf(inst, "reach", 7.5);
	MatBody* tb = w.get_body(dint(inst.data, "target", -1));
	auto f = [&a, &d](MatBody& x) { return filter(a, d, x); };
	if (tb == nullptr || !tb->alive || !f(*tb)) {
		tb = w.find_body(a, dvec(inst.data, "aim", a.forward()), reach, Charge::paramf(inst, "cone", 60.0), f);
		if (tb != nullptr) {
			inst.data.set("target", tb->id);
			w.emit("target_body", D({{"actor", a.id}, {"body", tb->id}}));
		}
	}
	if (tb != nullptr) {
		if (tb->mass > minf(a.max_control_mass, Charge::paramf(inst, "max_mass", a.max_control_mass))) {
			if (tb->mass > a.max_control_mass) w.request_grip(a, *tb, 0.0, "grip");
			else w.emit("control_fail", D({{"actor", a.id}, {"body", tb->id}, {"reason", "mass"}, {"mass", tb->mass}}));
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"body", tb->id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		const double s = w.grip_strength(a, *tb, Charge::paramf(inst, "base", 0.85), reach) * Charge::paramf(inst, "grip_mult", 1.0);
		w.request_grip(a, *tb, s, Charge::params(inst, "verb_name", "grip"));
		return;
	}
	std::string src = Charge::params(inst, "rip_source", "none");
	if (src == "none" || dtruthy(inst.data, "ripped") || inst.t < Charge::paramf(inst, "rip_time", 0.28)) return;
	if (src == "metal_plate") {
		const double mx = clampf(a.pos.x, w.arena.metal_min.x, w.arena.metal_max.x);
		const double mz = clampf(a.pos.z, w.arena.metal_min.y, w.arena.metal_max.y);
		if (V2(mx - a.pos.x, mz - a.pos.z).length() > PLATE_REACH) src = "metal";
	}
	if (!w.spend_focus(a, Charge::paramf(inst, "rip_cost", 0.0))) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("ripped", true);
	const Mat mat = Verbs::mat_id(Charge::param(inst, "rip_mat", Value(VerbProjectile::_source_mat(src != "metal_plate" ? src : std::string("metal")))));
	const double mass = Charge::paramf(inst, "rip_mass", src == "metal_plate" ? PLATE_RIP : 20.0);
	MatBody* b = nullptr;
	if (src == "metal_plate") {
		w.mass_ledger.metal_taken += mass;
		b = w.spawn_body(Mat::Metal, Form::Chunk, mass, a.pos + a.forward() * 0.8f, "plate");
	} else {
		VerbParams P{&inst, Dict()};
		b = VerbProjectile::spawn(w, a, inst, src, mat, mass, 0.0, P);
	}
	if (b == nullptr) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	b->max_life = Sim::REMNANT_LIFETIME;
	w.take_control(a, *b, 0.9, "rip");
	w.emit("rip", D({{"actor", a.id}, {"body", b->id}, {"source", src}}));
}

void shape(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	const std::string kind = Charge::params(inst, "shape", "");
	if (kind.empty() || !w.spend_focus(a, Charge::paramf(inst, "shape_cost", 3.0))) return;
	inst.data.set("shaped", true);
	if (kind == "split") {
		inst.data.set("split", Charge::parami(inst, "pieces", 3));
	} else if (kind == "freeze") {
		if (b.is_water()) {
			const double e0 = b.thermal_energy();
			b.liquid = 0.0;
			b.temp = minf(b.temp, -5.0);
			b.phase = Phase::Frozen;
			w.ledger.freeze_dump += b.thermal_energy() - e0;
		}
	} else if (kind == "compress") {
		if (b.mat == Mat::Sand) {
			w.convert_mat(b, Mat::Stone, "sand_to_sandstone");
			b.tag = "sandstone";
		}
	} else if (kind == "cool") {
		const double e = maxf(0.0, b.thermal_energy() - b.heat_payload);
		const double got = -Thermal::heat(b, -e);
		w.ledger.removed += got;
	} else if (kind == "condense") {
		if (b.mat == Mat::Steam || (b.is_water() && b.form == Form::Cloud)) {
			const double e1 = b.thermal_energy();
			b.mat = Mat::Water;
			b.form = Form::Blob;
			b.phase = Phase::Liquid;
			b.liquid = 1.0;
			b.temp = Sim::AMBIENT_C;
			w.ledger.removed += e1 - b.thermal_energy();
			b.max_life = -1.0;
			b.update_radius();
		}
	} else if (kind == "retag") {
		b.tag = Charge::params(inst, "shape_tag", b.tag);
	}
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", kind}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", b.id}, {"shape", kind}}));
}

void throw_(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr) return;
	const int n = dint(inst.data, "split", 1);
	const double mass = b->mass / static_cast<double>(n);
	const Vec3 target = Verbs::target_point(w, a, inst);
	const Vec3 dir = dvec(inst.data, "aim", a.forward());
	const double spread = deg_to_rad(Charge::paramf(inst, "spread", 30.0));
	double speed = Charge::paramf(inst, "speed", 18.0);
	if (!inst.data.has("split")) speed /= maxf(1.0, std::sqrt(b->mass / Sim::STONE_SHOT_MASS) * 0.8);
	a.held_body = -1;
	b->controller = -1;
	const double gs = Charge::paramf(inst, "gravity", 1.0);
	for (int k = 0; k < n; ++k) {
		MatBody* p = k == n - 1 ? b : w.split_body(*b, mass, b->pos);
		const double ang = n == 1 ? 0.0 : lerpf(-spread * 0.5, spread * 0.5, static_cast<double>(k) / static_cast<double>(n - 1));
		const Vec3 to = a.chest() + rotated(target - a.chest(), Vec3::Up(), ang);
		p->vel = Verbs::launch_vel(p->pos, to, speed, gs);
		p->gravity_scale = gs;
		p->on_ground = false;
		const double sc = std::sqrt(p->mass / Sim::STONE_SHOT_MASS);
		Verbs::arm(w, a, inst, *p, Charge::paramf(inst, "damage", 13.0) * sc, Charge::paramf(inst, "balance", 26.0) * sc);
		w.emit("launch", D({{"actor", a.id}, {"body", p->id}, {"speed", speed}, {"kind", "grip"}}));
		Verbs::fx(w, a, inst, "release", D({{"body", p->id}, {"dir", dir}}));
	}
}

void drop(CombatWorld& w, ActorState& a, ActionInst&) {
	if (w.held(a) != nullptr) w.release_body(a, Vec3(0, -1.0f, 0), false);
}

}  // namespace VerbGrip
}  // namespace ff
