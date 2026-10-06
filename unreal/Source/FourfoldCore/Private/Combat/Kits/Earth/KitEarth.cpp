// Fourfold core - port of game/combat/kits/earth/kit_earth.gd.
#include "Combat/Kits/Earth/KitEarth.h"

#include "Combat/Kits/Kits.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

namespace {
const EarthPartMap& ke_parts() {
	static const EarthPartMap m = [] {
		EarthPartMap p;
		RegisterEarthParts(p);
		return p;
	}();
	return m;
}
const KitStages* ke_part(const ActionInst& inst) {
	auto it = ke_parts().find(KitPart(inst));
	return it == ke_parts().end() ? nullptr : &it->second;
}
}  // namespace

namespace KitEarth {

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = ke_part(inst);
	if (s != nullptr && s->start != nullptr) s->start(w, a, inst, it);
	else Verbs::on_start(w, a, inst, it);
	if (!dbool(inst.data, "fizzle", false) && a.action.get() == &inst && dstr(inst.data, "mode", "") != "burrow")
		KitEarthUtil::_aura(w, a, inst, true);
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = ke_part(inst);
	if (s != nullptr && s->after != nullptr) return s->after(w, a, inst, it);
	return Verbs::after_startup(w, a, inst, it);
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) {
		KitEarthUtil::_aura(w, a, inst, false);
		KitEarthUtil::_return_kept(w, a, inst);
	}
	const KitStages* s = ke_part(inst);
	if (s != nullptr && s->phase != nullptr) s->phase(w, a, inst, p);
	else Verbs::on_phase(w, a, inst, p);
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = ke_part(inst);
	if (s != nullptr && s->tick != nullptr) s->tick(w, a, inst, it);
	else Verbs::on_tick(w, a, inst, it);
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	KitEarthUtil::_aura(w, a, inst, false);
	KitEarthUtil::_return_kept(w, a, inst);
	const KitStages* s = ke_part(inst);
	if (s != nullptr && s->interrupt != nullptr) s->interrupt(w, a, inst, reason);
	else Verbs::on_interrupt(w, a, inst, reason);
}

}  // namespace KitEarth

namespace KitEarthUtil {

void _return_kept(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (!dbool(inst.data, "from_guard", false)) return;
	MatBody* b = w.held(a);
	ToSatchelFn fn = EarthToSatchel();
	if (b != nullptr && b->id == dint(inst.data, "held", -1) && b->mat == Mat::Metal && fn != nullptr) fn(w, a, *b, "returned");
}

void _aura(CombatWorld& w, ActorState& a, ActionInst& inst, bool on) {
	if (inst.id == "guard" || !inst.def.has("aura_mat")) return;
	if (dbool(inst.data, "aura_on", false) == on) return;
	inst.data.set("aura_on", on);
	FxEvents::fx_for(w, a, inst, "aura", dstr(inst.def, "aura_mat"), D({{"on", on}}));
}

float flat_dist2(Vec3 p, Vec3 q) { return Vec2(p.x - q.x, p.z - q.z).length(); }

void fizzle(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& what) {
	inst.data.set("fizzle", true);
	w.emit("insufficient", D({{"actor", a.id}, {"what", what}, {"move", inst.id}}));
}

int guard_tier(const ActionInst& inst) {
	int t = dint(inst.data, "tier", 0);
	if (inst.data.has("guard_t")) t = maxi(t, Charge::tier_for(inst.def, dnum(inst.data, "guard_t")));
	return clampi(t, 0, Charge::max_tier(inst.def));
}

MatBody* pour_wave(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b, Vec3 dir, double mult, const std::string& why) {
	const Dict d = Moves::defs().get("pour").as_dict();
	dir = flatv(dir);
	dir = dir.length() > 0.01f ? dir.normalized() : a.forward();
	if (b.controller >= 0) {
		ActorState* h = w.get_actor(b.controller);
		if (h != nullptr && h->held_body == b.id) h->held_body = -1;
		b.controller = -1;
	}
	if (b.form == Form::Zone) {
		FxEvents::zone(w, b, "close");
		b.zone_radius = 0.0;
	}
	b.static_body = false;
	b.captured_by = -1;
	b.props.erase("face_of");
	const Form old_form = b.form;
	b.form = Form::Wave;
	b.tag = "";
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4));
	b.vel = Vec3();
	b.gravity_scale = 1.0;
	b.wave_dir = dir;
	b.wave_budget = (dnum(d, "base_budget") + dnum(d, "budget_per_kg") * b.mass) * mult;
	b.wave_width = 1.1 + b.mass * 0.025;
	b.wave_path.assign(1, b.pos);
	b.wave_stalled = false;
	b.max_life = -1.0;
	b.age = 0.0;
	b.update_radius();
	const double sc = clampf(std::sqrt(b.mass / Sim::STONE_SHOT_MASS), 0.5, 1.6);
	Verbs::arm(w, a, inst, b, dnum(d, "damage") * sc, dnum(d, "balance") * sc);
	b.touch(a.id, "pour", w.tick);
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", Sim::form_name(old_form)}, {"to", "wave"}, {"why", why}}));
	return &b;
}

Vec3 ground_point(CombatWorld& w, const ActorState& a, Vec3 dir, double dist) {
	Vec3 p = a.pos + flatv(dir).normalized() * dist;
	p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y + 0.4));
	return p;
}

}  // namespace KitEarthUtil
}  // namespace ff
