// Fourfold core - port of game/combat/verbs/verb_ground_line.gd: a travelling WAVE-form body with a tag that follows
// the lava-wave ground rules (CombatWorld::_update_wave).
#include "Combat/Verbs.h"

#include "Combat/Acts.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

namespace ff {
namespace VerbGroundLine {
namespace {
const char* const kLineKeys[] = {"kind", "leave_zone", "zone_radius", "zone_life", "zone_power", "trail_zone", "trail_radius", "trail_life",
                                 "hit_status", "hit_status_t", "power", "channel", "viscous"};
}  // namespace

MatBody* launch(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over) {
	VerbParams P{&inst, over};
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", Value(a.forward()))));
	dir.y = 0.0f;
	dir = dir.length() > 0.01f ? dir.normalized() : a.forward();
	bool blocked = false;
	const Vec3 start = ActFire::_pour_start(w, a, dir, &blocked);
	const std::string source = P.s("source", "none");
	const Mat mat = Verbs::mat_id(P.get("mat", Value(VerbProjectile::_source_mat(source))));
	const double mass = P.f("mass", 8.0);
	MatBody* b = nullptr;
	if (source == "held") {
		b = w.held(a);
		if (b == nullptr) b = w.get_body(dint(inst.data, "morph_body", -1));
		if (b == nullptr || !b->alive) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "material"}, {"move", inst.id}}));
			return nullptr;
		}
		if (b->controller == a.id) a.held_body = -1;
		b->controller = -1;
	} else {
		double heat = 0.0;
		if (source == "heat") heat = Verbs::take_heat(inst);
		b = VerbProjectile::spawn(w, a, inst, source, mat, mass, heat, P);
		if (b == nullptr) return nullptr;
	}
	b->form = Form::Wave;
	b->tag = P.s("tag", "wave");
	b->pos = start;
	b->vel = Vec3();
	b->wave_dir = dir;
	b->wave_budget = P.f("budget", 10.0);
	b->wave_width = P.f("width", 2.0);
	b->wave_path.clear();
	b->wave_path.push_back(start);
	b->max_life = -1.0;
	b->age = 0.0;
	b->gravity_scale = 0.0;
	b->props.set("speed", P.f("speed", 9.0));
	b->props.set("steer", P.f("steer", 0.0));
	b->props.set("knock", P.f("knock", 4.0));
	b->props.set("lift", P.f("lift", 3.0));
	for (const char* key : kLineKeys) {
		const Value v = P.get(key, Value());
		if (!v.is_nil()) b->props.set(key, v);
	}
	if (b->props.has("power")) b->power = dnum(b->props, "power");
	if (mat == Mat::Water && b->phase == Phase::Liquid) b->form = Form::Wave;
	Verbs::arm(w, a, inst, *b, P.f("damage", 10.0), P.f("balance", 30.0));
	b->residual_authority = 0.0;
	b->residual_owner = -1;
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "ground"}, {"to", "wave"}, {"why", "ground_line"}}));
	Verbs::fx(w, a, inst, "release", D({{"body", b->id}, {"pos", start}, {"dir", dir}, {"length", b->wave_budget}, {"radius", b->wave_width}}));
	inst.data.set("bodies", A({b->id}));
	if (blocked) {
		w.emit("wave_blocked", D({{"body", b->id}, {"at", start}}));
		w._settle_wave(*b, "blocked");
	}
	return b;
}

void on_end(CombatWorld& w, MatBody& b, const std::string&) {
	if (b.props.has("leave_zone")) {
		MatBody* z = w.spawn_zone(dstr(b.props, "leave_zone"), V3(b.pos.x, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3), b.pos.z),
		                          dnum(b.props, "zone_radius", 2.0), b.attack_owner, dnum(b.props, "zone_power", b.power),
		                          b.mat == Mat::Fire ? Mat::Fire : Mat::Air, 0.0, dnum(b.props, "zone_life", 3.0));
		if (b.mat == Mat::Fire) {
			z->heat_payload = b.heat_payload;
			b.heat_payload = 0.0;
		}
	}
	switch (b.mat) {
		case Mat::Water:
			if (b.phase == Phase::Liquid) w._water_to_puddle(b);
			break;
		case Mat::Fire:
		case Mat::Air: w.decay_body(b, "faded"); break;
		case Mat::Plant: b.form = Form::Chunk; break;
		default: break;
	}
}

void leave_trail(CombatWorld& w, MatBody& b) {
	const std::string tag = dstr(b.props, "trail_zone", "");
	if (tag.empty()) return;
	MatBody* z = w.spawn_zone(tag, b.pos, dnum(b.props, "trail_radius", 0.8), b.attack_owner, dnum(b.props, "zone_power", 0.0), Mat::Air, 0.0,
	                          dnum(b.props, "trail_life", 1.5));
	z->props.set("spare_owner", true);
}

}  // namespace VerbGroundLine
}  // namespace ff
