// Port of game/tests/sim/test_core_world.gd: moveset engine in the world - projectile clash, zones (ticks, rules,
// surfaces), statuses, hooks, event catalogue (COMBAT_SPEC "Engine" E7-E8).
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Conduction.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

using namespace ff;
using fft::S;

namespace {
struct CoreWorldFx : fft::TestCase {
	std::unique_ptr<fft::SimHarness> hp;
	fft::SimHarness& H(uint64_t seed = 1) {
		hp = std::make_unique<fft::SimHarness>(seed);
		return *hp;
	}
	MatBody* _shot(fft::SimHarness& h, ActorState* owner, Vec3 pos, Vec3 vel, double mass) {
		MatBody* b = h.w->spawn_body(Mat::Stone, Form::Chunk, mass, pos, "test");
		h.w->mass_ledger.ground_taken += mass;
		b->vel = vel;
		b->gravity_scale = 0.0;
		b->attack_id = h.w->new_attack_id();
		b->attack_owner = owner->id;
		b->hit_set.add(owner->id);
		b->damage = 10.0;
		return b;
	}
};
}  // namespace

FF_TEST_F(test_core_world, CoreWorldFx, test_projectiles_of_different_owners_clash) {
	auto& h = H(1);
	ActorState* a = h.actor("A", Vec3(0, 0, 8), 0, Dict(), Sim::EARTH);
	ActorState* b = h.actor("B", Vec3(0, 0, -8), 1, Dict(), Sim::EARTH);
	a->is_dummy = true;
	b->is_dummy = true;
	MatBody* light = _shot(h, a, Vec3(3, 1.5f, 2), Vec3(0, 0, -17), 20.0);
	MatBody* heavy = _shot(h, b, Vec3(3, 1.5f, -2), Vec3(0, 0, 14), 45.0);
	h.until([&] { return h.has_event("clash"); }, 30);
	const Dict ev = h.last_event("clash");
	check(!ev.empty() && ev.get("winner") == heavy->id, S("the heavier shot wins (", ev, ")"));
	check(light->attack_id == 0, "the lighter one is knocked aside");
	check(heavy->attack_id != 0 && heavy->vel.z > 0.0f && heavy->vel.length() < 14.0f, "the heavier keeps going, slower");
	check(h.any_event("interaction", [](const Dict& e) { return e.get("outcome") == "clash"; }), "through the rules (clash default)");
	// Same owner never clash; near-equal powers both drop.
	MatBody* s1 = _shot(h, a, Vec3(5, 1.5f, 2), Vec3(0, 0, -17), 20.0);
	MatBody* s2 = _shot(h, b, Vec3(5, 1.5f, -2), Vec3(0, 0, 17), 20.0);
	h.until([&] { return s1->attack_id == 0 || s2->attack_id == 0; }, 30);
	check(s1->attack_id == 0 && s2->attack_id == 0, "even clash: both drop");
}

FF_TEST_F(test_core_world, CoreWorldFx, test_zone_effects_rules_and_lifetime) {
	auto& h = H(1);
	h.begin_scope();
	ActorState* a = h.actor("A", Vec3(0, 0, 0), 0, Dict(), Sim::EARTH);
	ActorState* o = h.actor("O", Vec3(6, 0, 0), 1, Dict(), Sim::EARTH);
	o->is_dummy = true;
	int calls = 0;
	Hooks::register_zone_effect("t_field", [&calls](CombatWorld&, MatBody&, double) { calls += 1; });
	Interactions::add_rule("stone", "t_field", D({{"bands", A({A({0.0, "sink"})})}, {"full_at", 0.0}}));
	MatBody* z = h.spawn_zone("t_field", Vec3(6, 0, 0), 2.5, a);
	z->max_life = 0.5;
	z->props.set("actor_status", "slowed");
	z->props.set("status_t", 0.3);
	MatBody* loose = h.w->spawn_body(Mat::Stone, Form::Chunk, 10.0, Vec3(6.5f, 0.3f, 0.5f), "test");
	h.w->mass_ledger.ground_taken += 10.0;
	const double m0 = h.w->stone_mass();
	h.step(3);
	check(h.any_event("zone", [](const Dict& e) { return e.get("phase") == "open" && e.get("kind") == "t_field"; }), "zone open event");
	check(calls >= 3, S("zone effect hook runs every tick (", calls, ")"));
	check(Status::has(*o, "slowed") && !Status::has(*a, "slowed"), "actor inside gets the zone status, the owner is spared");
	check(h.any_event("status", [&](const Dict& e) { return e.get("actor") == o->id && e.get("status") == "slowed" && e.get("on").truthy(); }),
	      "status event");
	check(!loose->alive, "body <-> zone rule (sink) applied");
	near(h.w->stone_mass(), m0, 1e-9, "booked");
	h.step(40);
	check(!z->alive && h.any_event("zone", [](const Dict& e) { return e.get("phase") == "close"; }), "expired: zone close");
	h.step(30);
	check(!Status::has(*o, "slowed"), "status timed out");
	h.end_scope();
}

FF_TEST_F(test_core_world, CoreWorldFx, test_statuses_modify_movement_hits_and_targeting) {
	auto& h = H(1);
	ActorState* a = h.actor("A", Vec3(0, 0, 0), 0, Dict(), Sim::EARTH);
	ActorState* b = h.actor("B", Vec3(0, 0, -6), 1, Dict(), Sim::EARTH);
	b->is_dummy = true;
	h.it(a).move = Vec3(1, 0, 0);
	h.step(60);
	const float free_x = a->pos.x;
	a->pos = Vec3(0, 0, 0);
	a->vel = Vec3();
	Status::apply(*h.w, *a, "slowed", 2.0);
	h.step(60);
	check(a->pos.x < free_x * 0.75f, S("slowed: ", a->pos.x, " m vs ", free_x, " m"));
	Status::remove(*h.w, *a, "slowed");
	a->pos = Vec3(0, 0, 0);
	a->vel = Vec3();
	Status::apply(*h.w, *a, "rooted", 1.0);
	h.step(30);
	check(a->pos.length() < 0.05f, "rooted: no walking");
	h.it(a).move = Vec3();
	// Anchored: no knockback; armored: less kinetic damage.
	b->anchored = true;
	Status::apply(*h.w, *b, "armored", 5.0);
	const double hp0 = b->health;
	h.w->hit_actor(*b, D({{"attacker", a->id}, {"attack_id", h.w->new_attack_id()}, {"damage", 10.0}, {"balance", 5.0},
	                      {"knock", Vec3(0, 0, -6)}, {"kind", "stone"}, {"from", a->chest()}}));
	check(Vec2(b->vel.x, b->vel.z).length() < 1e-6f, "anchored: no knockback");
	near(hp0 - b->health, 6.0, 1e-6, "armored 40 %: 10 -> 6");
	b->anchored = false;
	// Targeting
	Status::apply(*h.w, *a, "blinded", 0.5);
	h.step();
	check(a->lock_target == -1, "blinded: no lock-on");
	h.step(40);
	check(a->lock_target == b->id, "lock returns after the blind");
	Status::apply(*h.w, *b, "concealed", 1.0);
	h.step();
	check(a->lock_target == -1, "concealed beyond 2 m: not lockable");
	// Burning damage over time, wet mirrors wetness.
	const double hb = b->health;
	Status::apply(*h.w, *b, "burning", 1.0);
	h.step(60);
	check(hb - b->health > 2.0, S("burning hurts (", hb - b->health, ")"));
	b->wetness = 1.0;
	h.step();
	check(Status::has(*b, "wet") && h.any_event("status", [](const Dict& e) { return e.get("status") == "wet" && e.get("on").truthy(); }),
	      "wet status from wetness");
	check(Status::immune(*b, "lift") == false && arr_has_str(Status::spec("anchored").get("immune").as_array(), "pull"), "immunity flags");
}

FF_TEST_F(test_core_world, CoreWorldFx, test_zone_surface_walkable_over_the_pool) {
	auto& h = H(1);
	ActorState* a = h.actor("A", Vec3(5.5f, 0, -1), 0, Dict(), Sim::WATER);
	MatBody* z = h.spawn_zone("ice_floor", V3(10, h.w->arena.pool_level, -1), 4.0, a);
	z->props.set("walk_height", 0.0);
	z->props.set("friction", 0.2);
	z->props.set("surface", "ice");
	h.it(a).move = Vec3(1, 0, 0);
	h.step(70);
	check(h.w->arena.in_pool(a->pos.x, a->pos.z), S("walked over the pool (x ", a->pos.x, ")"));
	check(!a->in_water && std::fabs(a->pos.y - h.w->arena.pool_level) < 0.05, S("on the ice floor, not in the water (y ", a->pos.y, ")"));
	check(a->surface == "zone:ice", "surface reports the zone (" + a->surface + ")");
	h.it(a).move = Vec3();
	h.w->close_zone(*z, "test");
	h.step(30);
	check(a->in_water, "without it: in the water");
}

FF_TEST_F(test_core_world, CoreWorldFx, test_hooks_body_tick_and_tech_preview) {
	auto& h = H(1);
	h.begin_scope();
	ActorState* a = h.actor("A", Vec3(0, 0, 0), 0, Dict(), Sim::EARTH);
	Hooks::register_body_tick("t_hover", [](CombatWorld&, MatBody& b, double dt) {
		b.spin += 3.0 * dt;
		return true;
	});
	MatBody* b = h.w->spawn_body(Mat::Air, Form::Chunk, 1.0, Vec3(0, 3, 2), "test");
	b->tag = "t_hover";
	h.step(30);
	check(std::fabs(b->pos.y - 3.0f) < 1e-6f && b->spin > 1.4, "custom body tick replaced the default motion");
	Hooks::register_tech_preview(0, 2, [](CombatWorld&, ActorState&, Vec3) { return D({{"mode", "SANDFORM"}, {"body", -1}, {"ok", true}, {"reason", ""}}); });
	a->subs[0] = 2;
	check(h.w->tech_preview(*a, a->forward()).get("mode") == "SANDFORM", "registered preview");
	a->subs[0] = 0;
	check(h.w->tech_preview(*a, a->forward()).empty(), "nothing registered for Earth/Stone");
	a->element = Sim::FIRE;
	check(h.w->tech_preview(*a, a->forward()).has("mode"), "legacy Fire thermal preview");
	h.end_scope();
}

FF_TEST_F(test_core_world, CoreWorldFx, test_event_catalogue) {
	for (const char* k : {"cast", "release", "cone", "beam", "burst", "ring", "erupt", "trail", "splash", "aura"})
		check(FxEvents::is_known("fx", k), S("fx ", k));
	check(FxEvents::is_known("mat", "vacuum") && FxEvents::is_known("shape", "crescent") && FxEvents::is_known("tag", "quicksand"), "mats, shapes, tags");
	check(!FxEvents::is_known("fx", "explode") && !FxEvents::is_known("mat", "plasma"), "unknown keys rejected");
	for (const char* k : {"charge", "fx", "interaction", "status", "zone"}) check(FxEvents::is_known("event", k), S("event ", k));
	auto& h = H(1);
	ActorState* a = h.actor("A", Vec3(0, 0, 4), 0, Dict(), Sim::FIRE);
	h.actor("B", Vec3(0, 0, 1), 1, Dict(), Sim::EARTH);
	h.step(10);
	h.press(a, "attack");
	h.step(1);
	h.release(a, "attack");
	h.step(20);
	const auto fx = h.events("fx");
	check(!fx.empty() && fx[0].get("fx") == "cone" && fx[0].get("mat") == "flame" && fx[0].has("seed"), "legacy flare emits a cone fx");
	const Dict hit = h.last_event("hit");
	for (const char* k : {"power", "mat", "tier", "dir"}) check(hit.has(k), S("hit gains ", k));
}

FF_TEST_F(test_core_world, CoreWorldFx, test_lightning_into_fog_hits_everyone_inside_at_60_percent) {
	auto& h = H(1);
	ActorState* c = h.actor("C", Vec3(0, 0, 9), 0, D({{"lightning", true}}), Sim::FIRE);
	ActorState* v1 = h.actor("V1", Vec3(1, 0, 0), 0, Dict(), Sim::EARTH);
	ActorState* v2 = h.actor("V2", Vec3(-1, 0, 1), 0, Dict(), Sim::EARTH);
	ActorState* outside = h.actor("V3", Vec3(6, 0, 0), 0, Dict(), Sim::EARTH);
	h.spawn_zone("fog", Vec3(0, 0, 0), 3.0, nullptr, 4.0);
	h.step(5);
	const Dict def = D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	const Dict out = Conduction::discharge(*h.w, *c, Vec3(0, 0, 0.5f), def, h.w->new_attack_id(), false);
	const Array hits = out.get("hits").as_array();
	check(hits.has(v1->id) && hits.has(v2->id) && !hits.has(outside->id), S("everyone inside the fog (hits ", hits, ")"));
	near(100.0 - v1->health, 13.0 * 0.6, 1e-6, "budget shared, x0.6 in fog");
	check(outside->health == 100.0, "outside the fog: untouched");
}

FF_TEST_F(test_core_world, CoreWorldFx, test_held_water_conducts_into_its_holder_from_a_puddle) {
	auto& h = H(1);
	ActorState* c = h.actor("C", Vec3(0, 0, 9), 0, D({{"lightning", true}}), Sim::FIRE);
	ActorState* holder = h.actor("H", Vec3(3, 0, 0), 0, Dict(), Sim::WATER);
	MatBody* pd = h.w->spawn_body(Mat::Water, Form::Puddle, 6.0, Vec3(0, 0.0f, 0), "test");
	pd->update_radius_puddle();
	MatBody* blob = h.w->spawn_body(Mat::Water, Form::Blob, 4.0, Vec3(0.4f, 0.2f, 0), "test");
	h.w->take_control(*holder, *blob, 0.9, "test");
	h.step();
	blob->pos = Vec3(0.4f, 0.2f, 0);
	const Dict out = Conduction::discharge(*h.w, *c, Vec3(0, 0, 0),
	                                       D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}}),
	                                       h.w->new_attack_id(), false);
	check(out.get("hits").as_array().has(holder->id), S("the water in hand touched the puddle: the holder is shocked (", out.get("hits"), ")"));
}
