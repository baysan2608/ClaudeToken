// Fourfold core - port of game/combat/verbs/verb_projectile.gd: spawn (or take) bodies and launch them.
#include "Combat/Verbs.h"

#include "Combat/Moves.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Hooks.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace VerbProjectile {
namespace {
const char* const kProjectileKeys[] = {"homing", "pierce", "ricochet", "on_impact", "impact_radius", "impact_power", "impact_pieces",
                                       "impact_zone", "impact_life", "impact_damage", "hit_status", "hit_status_t"};
}  // namespace

std::vector<MatBody*> fire(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over) {
	VerbParams P{&inst, over};
	std::vector<MatBody*> out;
	const std::string source = P.s("source", "none");
	const int count = maxi(1, P.i("count", 1));
	double mass = P.f("mass", 5.0);
	const Mat mat = Verbs::mat_id(P.get("mat", Value(_source_mat(source))));
	const double speed = P.f("speed", 20.0);
	const double gscale = P.f("gravity", 1.0);
	const double spread = deg_to_rad(P.f("spread", count > 1 ? 20.0 : 0.0));
	const Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", Value(a.forward()))));
	const Vec3 target = Verbs::target_point(w, a, inst, P.f("reach", 14.0));
	MatBody* held = nullptr;
	if (source == "held") {
		held = w.held(a);
		if (held == nullptr) {
			held = w.get_body(dint(inst.data, "morph_body", -1));
			if (held != nullptr && (!held->alive || (held->controller >= 0 && held->controller != a.id))) held = nullptr;
		}
		if (held == nullptr) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "material"}, {"move", inst.id}}));
			return out;
		}
		if (held->controller == a.id) {
			a.held_body = -1;
			held->controller = -1;
		}
		mass = held->mass / static_cast<double>(count);
	}
	double heat_total = 0.0;
	if (source == "heat") {
		heat_total = Verbs::take_heat(inst);
		if (heat_total <= 0.0) heat_total = w.pay_heat(a, P.f("heat", 60.0));
	}
	Array ids;
	for (int k = 0; k < count; ++k) {
		MatBody* b = nullptr;
		if (held != nullptr) b = k == count - 1 ? held : w.split_body(*held, mass, held->pos);
		else b = spawn(w, a, inst, source, mat, mass, heat_total / static_cast<double>(count), P);
		if (b == nullptr) break;
		b->tag = P.s("tag", b->tag);
		const std::string form = P.s("form", "");
		if (!form.empty()) {
			const int fi = Sim::form_index(form);
			if (fi >= 0) b->form = static_cast<Form>(fi);
		}
		if (P.b("frozen", false) && b->is_water()) {
			const double e0 = b->thermal_energy();
			b->liquid = 0.0;
			b->temp = -5.0;
			b->phase = Phase::Frozen;
			w.ledger.freeze_dump += b->thermal_energy() - e0;
			if (form.empty()) b->form = Form::Shard;
		}
		b->gravity_scale = gscale;
		for (const char* key : kProjectileKeys) {
			const Value v = P.get(key, Value());
			if (!v.is_nil()) b->props.set(key, v);
		}
		if (!P.get("charge", Value()).is_nil()) b->charge = P.f("charge", 0.0);
		if (mat == Mat::Air || mat == Mat::Fire) {
			b->power = P.f("power", b->power);
			b->max_life = P.f("life", 2.0);
		} else if (b->max_life < 0.0) {
			b->max_life = Sim::REMNANT_LIFETIME;
		}
		double spd = speed;
		if (P.b("mass_speed", false)) spd *= std::pow(20.0 / maxf(b->mass, 0.5), 0.4);
		const double ang = count == 1 ? 0.0 : lerpf(-spread * 0.5, spread * 0.5, static_cast<double>(k) / static_cast<double>(count - 1));
		const Vec3 d2 = rotated(dir, Vec3::Up(), ang);
		b->pos = a.hand_point() + d2 * 0.35f + V3(0, 0.05 * k, 0);
		const Vec3 aim_to = count == 1 ? target : a.chest() + rotated(target - a.chest(), Vec3::Up(), ang);
		if (P.b("arc", false) || gscale > 0.0) b->vel = Verbs::launch_vel(b->pos, aim_to, spd, gscale);
		else b->vel = (aim_to - b->pos).normalized() * spd;
		b->on_ground = false;
		Verbs::arm(w, a, inst, *b, P.f("damage", 8.0), P.f("balance", 16.0));
		out.push_back(b);
		ids.append(b->id);
		w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", spd}, {"kind", !b->tag.empty() ? b->tag : std::string(Sim::mat_name(b->mat))},
		                    {"tier", inst.tier()}}));
		Verbs::fx(w, a, inst, "release", D({{"body", b->id}, {"pos", b->pos}, {"dir", b->vel.normalized()}, {"power", b->mass * spd / 20.0}}));
	}
	inst.data.set("bodies", ids);
	return out;
}

std::string _source_mat(const std::string& source) {
	if (source == "waterskin" || source == "moisture") return "water";
	if (source == "metal") return "metal";
	if (source == "heat") return "fire";
	if (source == "none") return "air";
	return "stone";
}

MatBody* spawn(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& source, Mat mat, double mass, double heat, const VerbParams& P) {
	Vec3 p = a.hand_point();
	std::string origin = source + ":" + itos(a.id);
	if (source == "ground") {
		p = a.pos + a.forward() * 0.85f;
		origin = "ground@" + ftos(p.x, 1) + "," + ftos(p.z, 1);
		w.mass_ledger.ground_taken += mass;
	} else if (source == "waterskin") {
		double m = minf(mass, a.water_carried);
		if (m < 0.05) {
			if (a.in_water) {
				Verbs::_take_pool(w, a, mass);
				m = minf(mass, a.water_carried);
			}
			if (m < 0.05) {
				w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
				return nullptr;
			}
		}
		a.water_carried -= m;
		mass = m;
	} else if (source == "metal") {
		const double mm = minf(mass, a.metal_carried);
		if (mm < 0.05) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "metal"}, {"move", inst.id}}));
			return nullptr;
		}
		a.metal_carried -= mm;
		mass = mm;
	} else if (source == "moisture") {
		w.mass_ledger.moisture_taken += mass;
	}
	MatBody* b = w.spawn_body(mat, Form::Chunk, mass, p, origin, P.f("temp", Sim::AMBIENT_C));
	if (mat == Mat::Fire) b->heat_payload = heat;
	else if (heat > 0.0) w.heat_body(*b, heat);
	if (source == "waterskin" && mat == Mat::Water) b->form = Form::Blob;
	b->update_radius();
	return b;
}

void on_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	const std::string kind = dstr(b.props, "on_impact", "");
	if (kind.empty()) return;
	const Value hook_v = Moves::defs().get(dstr(b.props, "move", "")).get("hook_impact");
	if (const ImpactHook hook = Hooks::impact_of(hook_v)) {
		if (hook(w, b, what)) {
			b.props.erase("on_impact");
			return;
		}
	}
	b.props.erase("on_impact");
	ActorState* owner = w.get_actor(b.attack_owner);
	if (kind == "shatter") {
		const int n = dint(b.props, "impact_pieces", 3);
		const Vec3 v = b.vel;
		w.emit("shatter", D({{"body", b.id}, {"mass", b.mass}, {"on", what}}));
		b.attack_id = 0;
		const double piece = b.mass / static_cast<double>(n);
		for (int k = 0; k < n - 1; ++k) {
			const double ang = kTau * static_cast<double>(k + 1) / static_cast<double>(n);
			const Vec3 off = V3(std::cos(ang), 0.3, std::sin(ang)) * 0.3f;
			MatBody* c = w.split_body(b, piece, b.pos + off);
			c->vel = v * 0.2f + off.normalized() * 4.0f;
			c->attack_id = 0;
			c->max_life = Sim::REMNANT_LIFETIME;
		}
		b.vel = v * 0.2f + Vec3(0, 2.0f, 0);
	} else if (kind == "stick") {
		b.vel = Vec3();
		b.on_ground = true;
		b.attack_id = 0;
		b.gravity_scale = 0.0;
		b.static_body = what != "actor";
		if (b.mat == Mat::Metal) b.tag = "rod";
		w.emit("stick", D({{"body", b.id}, {"on", what}}));
	} else if (kind == "burst") {
		VerbVolume::burst_at(w, owner, nullptr, b.pos,
		                     D({{"radius", dnum(b.props, "impact_radius", 2.0)},
		                        {"power", dnum(b.props, "impact_power", 8.0)},
		                        {"damage", dnum(b.props, "impact_damage", 6.0)},
		                        {"cls", b.mat != Mat::Fire ? "blast" : "flame"},
		                        {"heat_hu", b.mat == Mat::Fire ? b.heat_payload : 0.0},
		                        {"mat", FxEvents::mat_of(b)}}));
		if (b.mat == Mat::Fire) {
			b.heat_payload = 0.0;
			w.decay_body(b, "burst");
		}
	} else if (kind == "puddle") {
		if (b.is_water()) {
			b.phase = b.liquid > 0.5 ? Phase::Liquid : b.phase;
			w._water_to_puddle(b);
		}
	} else if (kind == "sprout" || kind == "zone") {
		const std::string tag = dstr(b.props, "impact_zone", "zone");
		const double r = dnum(b.props, "impact_radius", 1.0);
		const double life = dnum(b.props, "impact_life", 3.0);
		if (kind == "sprout") {
			b.form = Form::Zone;
			b.tag = tag;
			b.zone_radius = r;
			b.radius = r;
			b.attack_id = 0;
			b.vel = Vec3();
			b.gravity_scale = 0.0;
			b.owner = owner != nullptr ? owner->id : -1;
			b.max_life = b.age + life;
			b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3));
			FxEvents::zone(w, b, "open");
		} else {
			w.spawn_zone(tag, V3(b.pos.x, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3), b.pos.z), r, owner != nullptr ? owner->id : -1,
			             dnum(b.props, "impact_power", 0.0), Mat::Air, 0.0, life);
		}
	}
}

}  // namespace VerbProjectile
}  // namespace ff
