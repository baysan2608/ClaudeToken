// Fourfold core - Snapshot / ArenaView builder.
#include "App/SnapshotBuilder.h"

#include "App/HudBuilder.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

namespace ff {
namespace SnapshotBuilder {

namespace {
constexpr size_t kWavePathMax = 32;
std::string sb_first_name(const Dict& def, const std::string& id) {
	const std::string nm = dstr(def, "name", id);
	const size_t sep = nm.find(" / ");
	return sep == std::string::npos ? nm : nm.substr(0, sep);
}
}  // namespace

ActionView action_view(const CombatWorld& w, const ActorState& a) {
	ActionView v;
	const ActionInst* inst = a.action.get();
	if (inst == nullptr) return v;
	v.active = true;
	v.id = inst->id;
	v.name = sb_first_name(inst->def, inst->id);
	v.element = inst->element;
	v.sub = inst->sub;
	v.slot = SlotFromName(inst->slot);
	v.phase = inst->phase;
	v.t = static_cast<float>(inst->t);
	v.total = static_cast<float>(inst->total);
	v.tier = inst->tier();
	v.heavy = inst->heavy;
	v.startup = static_cast<float>(w._startup_of(a, *inst));
	v.active_time = static_cast<float>(w._active_of(*inst));
	v.recovery = static_cast<float>(dnum(inst->def, "recovery") * Status::recovery_mult(a));
	v.aim = dvec(inst->data, "aim", dvec(inst->data, "face", a.forward()));
	v.aim_point = dvec(inst->data, "aim_point", a.chest() + a.forward() * 8.0);
	v.aim_active = dbool(inst->data, "aim_active", false);
	v.held_body = a.held_body;
	v.data = Value(inst->data.duplicate(true));
	return v;
}

void build(const CombatWorld& w, const Roles& roles, Snapshot& out) {
	out.tick = w.tick;
	out.sim_time = static_cast<double>(w.tick) * kSimDt;
	out.player_id = roles.player_id;
	out.rival_id = roles.rival_id;
	out.actors.clear();
	out.actors.reserve(w.actors.size());
	for (const auto& ap : w.actors) {
		const ActorState& a = *ap;
		ActorView v;
		v.id = a.id;
		v.name = a.name;
		v.team = a.team;
		v.is_dummy = a.is_dummy;
		v.is_player = a.id == roles.player_id;
		v.is_rival = a.id == roles.rival_id;
		v.ai_controlled = (v.is_player && roles.player_ai) || (v.is_rival && roles.rival_ai);
		v.pos = a.pos;
		v.vel = a.vel;
		v.facing = static_cast<float>(a.facing);
		v.grounded = a.grounded;
		v.ground_y = static_cast<float>(a.ground_y);
		v.in_water = a.in_water;
		v.surface = a.surface;
		v.gliding = a.gliding;
		v.flying = a.flying;
		v.health = static_cast<float>(a.health);
		v.balance = static_cast<float>(a.balance);
		v.focus = static_cast<float>(a.focus);
		v.heat_reserve = static_cast<float>(a.heat_reserve);
		v.wetness = static_cast<float>(a.wetness);
		v.water_carried = static_cast<float>(a.water_carried);
		v.metal_carried = static_cast<float>(a.metal_carried);
		v.static_charge = static_cast<float>(a.static_charge);
		v.element = a.element;
		v.sub = a.sub();
		v.subs = a.subs;
		v.elements = a.elements;
		v.subs_unlocked = a.subs_unlocked;
		v.action = action_view(w, a);
		v.stun = static_cast<float>(a.stun);
		v.stun_kind = a.stun > 0.0 ? a.stun_kind : std::string();
		v.iframes = static_cast<float>(a.iframes);
		v.guarding = a.guarding;
		v.held_body = a.held_body;
		v.lock_target = a.lock_target;
		v.wall_body = a.wall_body;
		v.last_hit_dir = a.last_hit_dir;
		v.last_result = a.last_result;
		v.stance = a.stance;
		v.armor = static_cast<float>(a.armor);
		v.anchored = a.anchored;
		v.statuses = HudBuilder::statuses_of(a);
		v.charge = HudBuilder::charge_view(a);
		out.actors.push_back(std::move(v));
	}
	out.bodies.clear();
	out.bodies.reserve(w.bodies.size());
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (!b.alive) continue;
		BodyView v;
		v.id = b.id;
		v.mat = b.mat;
		v.form = b.form;
		v.phase = b.phase;
		v.tag = b.tag;
		v.fx_mat = FxEvents::mat_of(b);
		v.pos = b.pos;
		v.vel = b.vel;
		v.radius = static_cast<float>(b.radius);
		v.mass = static_cast<float>(b.mass);
		v.temp = static_cast<float>(b.temp);
		v.liquid = static_cast<float>(b.liquid);
		v.charge = static_cast<float>(b.charge);
		v.power = static_cast<float>(b.power);
		v.heat_payload = static_cast<float>(b.heat_payload);
		v.spin = static_cast<float>(b.spin);
		v.zone_radius = static_cast<float>(b.zone_radius);
		v.age = static_cast<float>(b.age);
		v.max_life = static_cast<float>(b.max_life);
		v.tier = b.tier;
		v.sub = b.sub;
		v.owner = b.owner;
		v.controller = b.controller;
		v.attack_id = b.attack_id;
		v.attack_owner = b.attack_owner;
		v.on_ground = b.on_ground;
		v.static_body = b.static_body;
		v.wall_half = b.wall_half;
		v.wall_yaw = static_cast<float>(b.wall_yaw);
		v.wall_rise = static_cast<float>(b.wall_rise);
		v.wall_damage = static_cast<float>(b.wall_damage);
		v.wave_dir = b.wave_dir;
		v.wave_width = static_cast<float>(b.wave_width);
		v.wave_budget = static_cast<float>(b.wave_budget);
		v.wave_stalled = b.wave_stalled;
		const size_t n = b.wave_path.size();
		const size_t from = n > kWavePathMax ? n - kWavePathMax : 0;
		v.wave_path.assign(b.wave_path.begin() + static_cast<std::ptrdiff_t>(from), b.wave_path.end());
		v.captured = b.captured;
		v.captured_by = b.captured_by;
		v.props = Value(b.props.duplicate(true));
		out.bodies.push_back(std::move(v));
	}
}

ArenaView arena(const CombatWorld& w) {
	ArenaView v;
	const ArenaMap& m = w.arena;
	v.half_size = static_cast<float>(m.half_size);
	for (const ArenaSolid& s : m.solids) {
		ArenaBox b;
		b.min = s.min;
		b.max = s.max;
		b.kind = s.kind;
		b.surface = s.surface;
		b.name = s.name;
		v.solids.push_back(b);
	}
	v.pool_min = m.pool_min;
	v.pool_max = m.pool_max;
	v.pool_floor = static_cast<float>(m.pool_floor);
	v.pool_level = static_cast<float>(m.pool_level);
	v.metal_min = m.metal_min;
	v.metal_max = m.metal_max;
	v.metal_top = static_cast<float>(m.metal_top);
	v.player_spawn = m.player_spawn;
	v.opponent_spawn = m.opponent_spawn;
	return v;
}

}  // namespace SnapshotBuilder
}  // namespace ff
