// Port of game/tests/sim/test_core_ledgers.gd: ledgers stay exact with the new materials (MOVESET §15.9).
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct LedgerFx : TestCase {
	std::unique_ptr<SimHarness> hp;
	struct Snap {
		double energy = 0, water = 0, earth = 0, metal = 0, plant = 0;
	} base;
	Snap _snap() {
		CombatWorld& w = *hp->w;
		return {w.system_energy() - w.ledger_balance(), w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	}
	void _check_ledgers(const std::string& label) {
		const Snap s = _snap();
		near(s.energy, base.energy, 1e-5, label + ": energy ledger");
		near(s.water, base.water, 1e-6, label + ": water mass");
		near(s.earth, base.earth, 1e-6, label + ": earth mass (stone + sand + glass)");
		near(s.metal, base.metal, 1e-6, label + ": metal mass (field + satchels)");
		near(s.plant, base.plant, 1e-6, label + ": plant mass");
	}
};
}  // namespace

FF_TEST_F(test_core_ledgers, LedgerFx, test_scripted_exchange_with_new_materials) {
	hp = std::make_unique<SimHarness>(4);
	SimHarness& h = *hp;
	h.begin_scope();
	Moves::register_def("t_slug", D({{"element", 0}, {"verb", "projectile"}, {"startup", 0.15}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 5.0},
	                                 {"source", "ground"}, {"mat", "sand"}, {"mass", 5.0}, {"speed", 22.0}, {"on_impact", "shatter"}}));
	Moves::register_def("t_disc", D({{"element", 0}, {"verb", "projectile"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 5.0},
	                                 {"source", "metal"}, {"mat", "metal"}, {"mass", 2.0}, {"count", 2}, {"speed", 24.0}, {"homing", 10.0}, {"tag", "disc"}}));
	Moves::register_def("t_ball", D({{"element", 2}, {"verb", "projectile"}, {"startup", 0.12}, {"active", 0.05}, {"recovery", 0.2}, {"heat", 120.0},
	                                 {"source", "heat"}, {"mat", "fire"}, {"mass", 1.0}, {"speed", 18.0}, {"tag", "fireball"}, {"on_impact", "burst"},
	                                 {"impact_radius", 2.0}, {"gravity", 0.3}}));
	Moves::register_def("t_icewall", D({{"element", 1}, {"verb", "barrier"}, {"barrier", "wall"}, {"mat", "water"}, {"tag", "ice"}, {"source", "moisture"},
	                                    {"mass", 50.0}, {"cost", 8.0}, {"tiers", D({{"t1", D({{"mass", 65.0}})}})}}));
	Moves::register_def("t_rip", D({{"element", 0}, {"verb", "grip"}, {"startup", 0.1}, {"active", 0.06}, {"recovery", 0.2}, {"ccls", "grip_metal"},
	                                {"rip_source", "metal_plate"}, {"rip_time", 0.1}, {"speed", 16.0}}));
	Moves::bind(0, 2, "strike", "t_slug");
	Moves::bind(0, 1, "strike", "t_disc");
	Moves::bind(0, 1, "tech", "t_rip");
	Moves::bind(2, 1, "thrust", "t_ball");
	Moves::bind(1, 1, "guard", "t_icewall");
	ActorState* e = h.actor("E", Vec3(-8, 0, 1), 0, Dict(), Sim::EARTH);
	ActorState* f = h.actor("F", Vec3(-3, 0, 6), 1, Dict(), Sim::FIRE);
	ActorState* wa = h.actor("W", Vec3(4, 0, -4), 1, Dict(), Sim::WATER);
	h.step(10);
	base = _snap();
	// Sand slug and metal discs (satchel).
	h.sub(e, 2);
	h.step();
	h.hold(e, "attack", 3);
	h.step(40);
	_check_ledgers("sand slug");
	h.sub(e, 1);
	h.step();
	h.hold(e, "attack", 3);
	h.step(40);
	near(e->metal_carried, 8.0, 1e-9, "two 2 kg discs left the satchel");
	_check_ledgers("discs");
	// Rip scrap from the arena plate (E stands next to it).
	h.press(e, "tech");
	h.step(20);
	check(h.w->mass_ledger.metal_taken == 10.0, S("10 kg ripped from the plate (", h.w->mass_ledger.metal_taken, ")"));
	h.release(e, "tech");
	h.step(30);
	_check_ledgers("plate scrap");
	// Fireball (heat paid -> payload -> burst / fades).
	h.sub(f, 1);
	h.step();
	h.flick(f, "attack", static_cast<int>(Gesture::Up));
	h.step();
	h.release(f, "attack");
	h.step(90);
	_check_ledgers("fireball");
	// Ice wall from ambient moisture, held past T1, released.
	h.sub(wa, 1);
	h.step();
	h.press(wa, "guard");
	h.step(40);
	MatBody* iw = h.w->get_body(wa->wall_body);
	check(iw != nullptr && iw->mat == Mat::Water && iw->phase == Phase::Frozen && iw->mass == 65.0, "65 kg ice wall");
	check(h.w->mass_ledger.moisture_taken == 65.0, "booked as moisture_taken");
	_check_ledgers("ice wall");
	h.release(wa, "guard");
	h.step(40);
	_check_ledgers("ice wall sank");
	// Sand fused by heat sets as glass.
	MatBody* sand = h.w->spawn_body(Mat::Sand, Form::Chunk, 10.0, Vec3(2, 0.3f, 8), "test");
	h.w->mass_ledger.ground_taken += 10.0;
	h.w->ledger.generated += h.w->heat_body(*sand, 10.0 * (0.01 * 1180.0 + 10.0) + 20.0);
	h.step();
	check(sand->phase == Phase::Molten, "sand fused");
	h.w->ledger.ambient += Thermal::heat(*sand, -sand->thermal_energy() + 5.0);
	h.step();
	check(sand->mat == Mat::Glass && h.w->mass_ledger.sand_to_glass == 10.0, "it set as glass");
	_check_ledgers("glass");
	// Water -> plant, then the vine burns away.
	MatBody* vine = h.w->grow_plant(nullptr, 3.0, Vec3(1, 0.3f, 8), wa);
	check(vine != nullptr && vine->mass == 3.0 && h.w->mass_ledger.water_to_plant == 3.0, "3 kg of waterskin grew a vine");
	_check_ledgers("plant grown");
	if (vine != nullptr) h.w->ledger.generated += h.w->heat_body(*vine, 3.0 * 0.04 * 570.0);
	h.step(150);
	check(vine != nullptr && !vine->alive && h.w->mass_ledger.burned > 2.99, S("the vine burned away (", h.w->mass_ledger.burned, " kg)"));
	_check_ledgers("plant burned");
	h.step(400);
	_check_ledgers("end");
	h.end_scope();
}

FF_TEST_F(test_core_ledgers, LedgerFx, test_convert_mat_keeps_energy) {
	hp = std::make_unique<SimHarness>(1);
	SimHarness& h = *hp;
	MatBody* b = h.w->spawn_body(Mat::Sand, Form::Chunk, 8.0, Vec3(), "test", 400.0);
	const double e = b->thermal_energy();
	h.w->convert_mat(*b, Mat::Stone, "sand_to_sandstone");
	near(b->thermal_energy(), e, 1e-9, "sand -> sandstone keeps its heat");
	h.w->convert_mat(*b, Mat::Metal);
	near(b->thermal_energy(), e, 1e-9, "any conversion keeps its heat");
	MatBody* fb = h.w->spawn_body(Mat::Fire, Form::Chunk, 1.0, Vec3(), "test");
	fb->heat_payload = 100.0;
	near(fb->thermal_energy(), 100.0, 1e-9, "fire payload is thermal energy");
	const double e0 = h.w->system_energy() - h.w->ledger_balance();
	h.step(30);
	check(fb->heat_payload < 100.0, "payload fades");
	near(h.w->system_energy() - h.w->ledger_balance(), e0, 1e-6, "into the ambient ledger");
}
