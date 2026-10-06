// Port of game/tests/sim/test_thermal.gd: thermal transitions on MatBody via Thermal (heat / boil / update_phase).
// Unit conventions (Sim): stone 0.01 HU/kg/degC, melts at 1000 degC with 10 HU/kg latent; water 0.05 HU/kg/degC, fusion
// 3.3 HU/kg, vaporisation 22 HU/kg. apply_heat / flash_boil are GDScript compatibility wrappers returning a float32
// Vector2; the local helpers below rebuild them on Thermal::heat / boil with the same float32 rounding.
#include "ff_test.h"

#include "Sim/CombatWorld.h"
#include "Sim/MatBody.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

#include <cmath>
#include <memory>

using namespace ff;
using fft::S;

namespace {
constexpr double EPS = 1e-6;

using BodyPtr = std::shared_ptr<MatBody>;

BodyPtr _stone(double mass = 10.0, double temp = Sim::AMBIENT_C, double liquid = 0.0) {
	BodyPtr b = std::make_shared<MatBody>();
	b->mat = Mat::Stone;
	b->mass = mass;
	b->temp = temp;
	b->liquid = liquid;
	return b;
}

BodyPtr _water(double mass = 4.0, double temp = Sim::AMBIENT_C, double liquid = 1.0) {
	BodyPtr b = std::make_shared<MatBody>();
	b->mat = Mat::Water;
	b->phase = liquid >= 1.0 ? Phase::Liquid : Phase::Frozen;
	b->mass = mass;
	b->temp = temp;
	b->liquid = liquid;
	return b;
}

// Thermal.apply_heat: Vector2(absorbed HU, vapour kg) (float32 components).
Vec2 apply_heat(MatBody& b, double e) {
	const double a = Thermal::heat(b, e);
	return Vec2(f32(a), f32(Thermal::last_vapor()));
}
// Thermal.flash_boil: Vector2(vaporised kg, HU used).
Vec2 flash_boil(MatBody& b, double e) {
	const double kg = Thermal::boil(b, e);
	return Vec2(f32(kg), f32(Thermal::last_used()));
}
double X(const Vec2& v) { return static_cast<double>(v.x); }
double Y(const Vec2& v) { return static_cast<double>(v.y); }

// Sensible HU to bring a stone from ambient to its melting point.
double _stone_to_melt(double m) { return (Sim::STONE_MELT_C - Sim::AMBIENT_C) * m * Sim::STONE_C; }

double sgn(double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); }

struct Th : fft::TestCase {
	// Alternately adds and removes amp_hu; counts label changes.
	int _label_changes_while_oscillating(MatBody& b, double amp_hu, int steps) {
		int changes = 0;
		for (int k = 0; k < steps; ++k) {
			apply_heat(b, k % 2 == 0 ? amp_hu : -amp_hu);
			if (Thermal::update_phase(b)) ++changes;
		}
		return changes;
	}
};
}  // namespace

// ---------------------------------------------------------------- stone heating

FF_TEST_F(test_thermal, Th, test_stone_heats_to_melting_point_before_latent) {
	BodyPtr b = _stone(10.0);
	const double need = _stone_to_melt(10.0);
	const Vec2 r = apply_heat(*b, need * 0.5);
	near(X(r), need * 0.5, EPS, "all energy absorbed while below the melting point");
	near(b->temp, Sim::AMBIENT_C + (Sim::STONE_MELT_C - Sim::AMBIENT_C) * 0.5, 1e-4, "temperature rose proportionally");
	check(b->liquid == 0.0, "no latent heat absorbed below the melting point");
	apply_heat(*b, need * 0.5 - 1.0);
	check(b->temp < Sim::STONE_MELT_C && b->liquid == 0.0, S("1 HU short of melting: still solid at ", b->temp, " degC"));
	apply_heat(*b, 1.0);
	near(b->temp, Sim::STONE_MELT_C, 1e-3, "exactly the sensible budget reaches the melting point");
	check(b->liquid < 1e-6, S("no melting yet (liquid ", b->liquid, ")"));
}

FF_TEST_F(test_thermal, Th, test_stone_latent_heat_holds_temperature) {
	BodyPtr b = _stone(10.0);
	apply_heat(*b, _stone_to_melt(10.0));
	const double latent_total = 10.0 * Sim::STONE_LATENT;
	const Vec2 r = apply_heat(*b, latent_total * 0.25);
	near(X(r), latent_total * 0.25, EPS, "energy absorbed as latent heat");
	near(b->liquid, 0.25, 1e-6, "liquid fraction follows latent energy");
	near(b->temp, Sim::STONE_MELT_C, 1e-6, "temperature pinned at the melting point during melting");
	apply_heat(*b, latent_total * 0.75);
	near(b->liquid, 1.0, 1e-6, "fully liquid");
	near(b->temp, Sim::STONE_MELT_C, 1e-6, "still at the melting point");
	// Superheating only after fully liquid, up to STONE_MAX_C.
	apply_heat(*b, 10.0);
	near(b->temp, Sim::STONE_MELT_C + 10.0 / (10.0 * Sim::STONE_C), 1e-4, "superheat after the latent budget");
}

FF_TEST_F(test_thermal, Th, test_stone_heat_cap_returns_only_what_was_absorbed) {
	BodyPtr b = _stone(10.0);
	const double cap = _stone_to_melt(10.0) + 10.0 * Sim::STONE_LATENT + (Thermal::STONE_MAX_C - Sim::STONE_MELT_C) * 10.0 * Sim::STONE_C;
	const Vec2 r = apply_heat(*b, cap + 500.0);
	near(X(r), cap, 1e-3, "excess beyond STONE_MAX_C is refused, not absorbed");
	near(b->temp, Thermal::STONE_MAX_C, 1e-6, "temperature capped");
	near(b->liquid, 1.0, 1e-9, "liquid fraction capped at 1");
	near(X(apply_heat(*b, 100.0)), 0.0, EPS, "a body at the cap absorbs nothing more");
}

FF_TEST_F(test_thermal, Th, test_apply_heat_zero_cases) {
	BodyPtr s = _stone(10.0);
	check(apply_heat(*s, 0.0) == Vec2(), "zero energy is a no-op");
	check(s->temp == Sim::AMBIENT_C && s->liquid == 0.0, "no state change for zero energy");
	BodyPtr massless = _stone(0.0);
	check(apply_heat(*massless, 100.0) == Vec2(), "massless body absorbs nothing");
	BodyPtr steam = std::make_shared<MatBody>();
	steam->mat = Mat::Steam;
	steam->mass = 1.0;
	check(apply_heat(*steam, 100.0) == Vec2(), "steam is not modelled by apply_heat");
	check(apply_heat(*steam, -100.0) == Vec2(), "steam is not cooled by apply_heat");
}

// ---------------------------------------------------------------- stone cooling

FF_TEST_F(test_thermal, Th, test_cooling_removes_latent_before_sensible) {
	BodyPtr b = _stone(10.0, Sim::STONE_MELT_C, 1.0);
	const double latent_total = 10.0 * Sim::STONE_LATENT;
	const Vec2 r = apply_heat(*b, -latent_total * 0.4);
	near(X(r), -latent_total * 0.4, EPS, "returns the (negative) energy removed");
	near(b->liquid, 0.6, 1e-6, "latent heat removed first (liquid fraction fell)");
	near(b->temp, Sim::STONE_MELT_C, 1e-6, "temperature unchanged while any liquid remains");
	apply_heat(*b, -latent_total * 0.6);
	check(b->liquid == 0.0, "fully solidified");
	near(b->temp, Sim::STONE_MELT_C, 1e-6, "still at the melting point right after solidifying");
	const Vec2 r2 = apply_heat(*b, -50.0);
	near(X(r2), -50.0, EPS, "then sensible heat comes out");
	near(b->temp, Sim::STONE_MELT_C - 50.0 / (10.0 * Sim::STONE_C), 1e-4, "temperature finally falls");
}

FF_TEST_F(test_thermal, Th, test_cooling_removes_superheat_first) {
	BodyPtr b = _stone(10.0, 1200.0, 1.0);
	const double superheat = 200.0 * 10.0 * Sim::STONE_C;
	apply_heat(*b, -superheat * 0.5);
	near(b->temp, 1100.0, 1e-4, "superheat comes off first");
	near(b->liquid, 1.0, 1e-9, "still fully liquid while superheated");
	apply_heat(*b, -superheat * 0.5);
	near(b->temp, Sim::STONE_MELT_C, 1e-4, "back at the melting point");
	near(b->liquid, 1.0, 1e-9, "latent heat untouched until the superheat is gone");
}

FF_TEST_F(test_thermal, Th, test_cannot_extract_below_ambient) {
	BodyPtr b = _stone(10.0, 100.0);
	const double avail = (100.0 - Sim::AMBIENT_C) * 10.0 * Sim::STONE_C;
	const Vec2 r = apply_heat(*b, -avail - 500.0);
	near(X(r), -avail, 1e-6, "only the heat above ambient can be extracted");
	near(b->temp, Sim::AMBIENT_C, 1e-6, "stone stops at ambient");
	const Vec2 r2 = apply_heat(*b, -100.0);
	near(X(r2), 0.0, EPS, "nothing to extract from a body at ambient");
	near(b->temp, Sim::AMBIENT_C, 1e-9, "no refrigeration below ambient");
	check(b->thermal_energy() >= -1e-9, "stone never holds negative thermal energy");
	// A hot stone drained with one enormous request stops exactly at ambient too.
	BodyPtr hot = _stone(10.0, 1100.0, 1.0);
	apply_heat(*hot, -1e9);
	near(hot->temp, Sim::AMBIENT_C, 1e-6, "even a molten stone cannot be pulled below ambient");
	check(hot->liquid == 0.0, "and ends solid");
}

// ---------------------------------------------------------------- conservation

FF_TEST_F(test_thermal, Th, test_energy_returned_equals_energy_applied_stone) {
	BodyPtr b = _stone(7.5);
	Rng rng;
	rng.set_seed(42);
	double total = 0.0;
	const double e0 = b->thermal_energy();
	int bad = 0;
	for (int k = 0; k < 500; ++k) {
		const double e = rng.randf_range(-120.0f, 150.0f);
		const Vec2 r = apply_heat(*b, e);
		total += X(r);
		if (!(sgn(X(r)) == sgn(e) || X(r) == 0.0)) ++bad;
		if (!(absf(X(r)) <= absf(e) + 1e-9)) ++bad;
		if (!(b->liquid >= 0.0 && b->liquid <= 1.0)) ++bad;
		if (!(b->temp >= Sim::AMBIENT_C - 1e-6 && b->temp <= Thermal::STONE_MAX_C + 1e-6)) ++bad;
		// apply_heat returns float32 components: allow 1e-3 HU of drift over the whole walk.
		if (k % 50 == 49) near(b->thermal_energy() - e0, total, 1e-3, S("step ", k, ": thermal_energy() delta equals the sum of returned HU"));
	}
	check(bad == 0, S("every step: sign, bound, liquid and temperature invariants (", bad, " violations)"));
	near(b->thermal_energy() - e0, total, 1e-3, "stone: delta energy == sum of returns after random walk");
}

FF_TEST_F(test_thermal, Th, test_energy_returned_equals_energy_applied_water) {
	// Without vaporisation: delta thermal energy equals the sum of returned HU.
	BodyPtr b = _water(6.0, 10.0);
	Rng rng;
	rng.set_seed(7);
	double total = 0.0;
	const double e0 = b->thermal_energy();
	int bad = 0;
	for (int k = 0; k < 600; ++k) {
		double e = rng.randf_range(-4.0f, 4.0f);
		// Keep the walk between the ice floor and the boiling point (it may cross 0 degC freely).
		if (b->temp > 80.0) e = -absf(e);
		else if (b->temp < -15.0) e = absf(e);
		const Vec2 r = apply_heat(*b, e);
		total += X(r);
		if (Y(r) != 0.0) ++bad;
		if (!(b->liquid >= 0.0 && b->liquid <= 1.0)) ++bad;
		if (!(b->temp <= Sim::WATER_BOIL_C + 1e-6)) ++bad;
	}
	check(bad == 0, S("no vapour, liquid in 0..1, below boiling (", bad, " violations)"));
	near(b->thermal_energy() - e0, total, 1e-3, "water: delta energy == sum of returned HU");
}

FF_TEST_F(test_thermal, Th, test_merge_conserves_mass_and_thermal_energy) {
	CombatWorld w(1);
	MatBody* hot = w.spawn_body(Mat::Stone, Form::Chunk, 10.0, V3(0, 1, 0), "t");
	hot->temp = 600.0;
	MatBody* cold = w.spawn_body(Mat::Stone, Form::Chunk, 30.0, V3(1, 1, 0), "t");
	const double e = hot->thermal_energy() + cold->thermal_energy();
	w.merge_bodies(*hot, *cold);
	near(hot->mass, 40.0, 1e-9, "merged mass is the sum");
	near(hot->thermal_energy(), e, 1e-6, "merged thermal energy is the sum");
	check(!cold->alive && cold->mass == 0.0, "absorbed body is gone");
	check(std::find(hot->absorbed.begin(), hot->absorbed.end(), cold->id) != hot->absorbed.end(), "absorption recorded");
	// Molten + frozen water.
	MatBody* m1 = w.spawn_body(Mat::Water, Form::Puddle, 3.0, V3(3, 0, 0), "t");
	m1->temp = 60.0;
	MatBody* m2 = w.spawn_body(Mat::Water, Form::Puddle, 1.0, V3(3.2, 0, 0), "t");
	m2->liquid = 0.0;
	m2->temp = -5.0;
	m2->phase = Phase::Frozen;
	const double ew = m1->thermal_energy() + m2->thermal_energy();
	w.merge_bodies(*m1, *m2);
	near(m1->mass, 4.0, 1e-9, "water merge conserves mass");
	near(m1->thermal_energy(), ew, 1e-4, S("water merge conserves thermal energy (got ", m1->thermal_energy(), " want ", ew, ")"));
}

FF_TEST_F(test_thermal, Th, test_ambient_step_return_matches_energy_change) {
	BodyPtr hot = _stone(10.0, 500.0);
	const double e0 = hot->thermal_energy();
	const double moved = Thermal::ambient_step(*hot, Sim::DT);
	check(moved < 0.0, S("a hot stone loses heat to the air (", moved, ")"));
	near(hot->thermal_energy() - e0, moved, 1e-9, "returned HU equals the energy change");
	BodyPtr cold = _stone(10.0, Sim::AMBIENT_C);
	near(Thermal::ambient_step(*cold, Sim::DT), 0.0, EPS, "a stone at ambient exchanges nothing");
	BodyPtr molten = _stone(10.0, Sim::STONE_MELT_C, 1.0);
	const double em = molten->thermal_energy();
	const double m2 = Thermal::ambient_step(*molten, Sim::DT);
	near(molten->thermal_energy() - em, m2, 1e-9, "molten stone: ambient loss equals the energy change");
	check(molten->liquid < 1.0 && molten->temp == Sim::STONE_MELT_C, "a molten stone cools by solidifying first");
	BodyPtr ice = _water(2.0, -5.0, 0.0);
	const double ei = ice->thermal_energy();
	const double gain = Thermal::ambient_step(*ice, Sim::DT);
	check(gain > 0.0, S("ice warms from the air (", gain, ")"));
	near(ice->thermal_energy() - ei, gain, 1e-9, "ice: returned HU equals the energy change");
}

// ---------------------------------------------------------------- phase labels

FF_TEST_F(test_thermal, Th, test_stone_phase_thresholds_and_hysteresis) {
	BodyPtr b = _stone(10.0, Sim::STONE_MELT_C, 0.0);
	b->phase = Phase::Solid;
	b->liquid = Sim::SOFTEN_UP - 0.0001;
	check(!Thermal::update_phase(*b) && b->phase == Phase::Solid, "just below SOFTEN_UP stays SOLID");
	b->liquid = Sim::SOFTEN_UP;
	check(Thermal::update_phase(*b) && b->phase == Phase::Softened, "exactly SOFTEN_UP becomes SOFTENED");
	b->liquid = Sim::MOLTEN_UP - 0.0001;
	check(!Thermal::update_phase(*b) && b->phase == Phase::Softened, "just below MOLTEN_UP stays SOFTENED");
	b->liquid = Sim::MOLTEN_UP;
	check(Thermal::update_phase(*b) && b->phase == Phase::Molten, "exactly MOLTEN_UP becomes MOLTEN");
	b->liquid = Sim::MOLTEN_UP - 0.1;   // between the thresholds: must stay MOLTEN
	check(!Thermal::update_phase(*b) && b->phase == Phase::Molten, "0.70 stays MOLTEN (hysteresis)");
	b->liquid = Sim::MOLTEN_DOWN + 0.0001;
	check(!Thermal::update_phase(*b) && b->phase == Phase::Molten, "just above MOLTEN_DOWN stays MOLTEN");
	b->liquid = Sim::MOLTEN_DOWN;
	check(Thermal::update_phase(*b) && b->phase == Phase::Softened, "exactly MOLTEN_DOWN drops to SOFTENED");
	b->liquid = 0.3;   // between SOLID_DOWN and SOFTEN_UP going down: stays SOFTENED
	check(!Thermal::update_phase(*b) && b->phase == Phase::Softened, "0.30 stays SOFTENED on the way down");
	b->liquid = Sim::SOLID_DOWN + 0.0001;
	check(!Thermal::update_phase(*b) && b->phase == Phase::Softened, "just above SOLID_DOWN stays SOFTENED");
	b->liquid = Sim::SOLID_DOWN;
	check(Thermal::update_phase(*b) && b->phase == Phase::Solid, "exactly SOLID_DOWN returns to SOLID");
	// A big jump skips straight to MOLTEN from SOLID.
	b->liquid = 0.95;
	check(Thermal::update_phase(*b) && b->phase == Phase::Molten, "SOLID -> MOLTEN directly when liquid jumps past both thresholds");
	b->liquid = 0.0;
	check(Thermal::update_phase(*b) && b->phase == Phase::Solid, "MOLTEN -> SOLID directly when liquid collapses");
}

FF_TEST_F(test_thermal, Th, test_stone_phase_label_does_not_flicker_near_thresholds) {
	// Latent heat for 10 kg is 100 HU: 2 HU = 0.02 liquid fraction.
	for (double center : {Sim::SOFTEN_UP, Sim::MOLTEN_UP, Sim::MOLTEN_DOWN, Sim::SOLID_DOWN}) {
		BodyPtr b = _stone(10.0, Sim::STONE_MELT_C, center - 0.01);
		// Start on the correct side of the threshold for the label.
		b->phase = center == Sim::SOFTEN_UP ? Phase::Solid : ((center == Sim::MOLTEN_UP || center == Sim::SOLID_DOWN) ? Phase::Softened : Phase::Molten);
		const int changes = _label_changes_while_oscillating(*b, 2.0, 400);
		check(changes <= 1, S("oscillating +-0.02 around ", center, " flips the label at most once, got ", changes));
	}
	// Oscillation straddling the whole hysteresis band of the molten label never flips it.
	BodyPtr m = _stone(10.0, Sim::STONE_MELT_C, 0.9);
	m->phase = Phase::Molten;
	int flips = 0;
	for (int k = 0; k < 600; ++k) {
		apply_heat(*m, k % 2 == 0 ? 20.0 : -20.0);   // liquid swings 0.9 <-> 0.7
		if (Thermal::update_phase(*m)) ++flips;
	}
	check(flips == 0, S("liquid swinging between 0.7 and 0.9 never changes a MOLTEN label (", flips, " flips)"));
	check(m->phase == Phase::Molten, "label is still MOLTEN");
}

FF_TEST_F(test_thermal, Th, test_water_phase_hysteresis) {
	BodyPtr b = _water(4.0, 0.0, 1.0);
	const double fusion_total = 4.0 * Sim::WATER_LATENT_FUSION;
	apply_heat(*b, -fusion_total * 0.85);   // liquid 0.15
	check(!Thermal::update_phase(*b) && b->phase == Phase::Liquid, "liquid 0.15 still LIQUID");
	apply_heat(*b, -fusion_total * 0.06);   // liquid 0.09 -> frozen
	check(Thermal::update_phase(*b) && b->phase == Phase::Frozen, S("liquid <= ICE_DOWN freezes (liquid ", b->liquid, ")"));
	apply_heat(*b, fusion_total * 0.46);    // 0.55
	check(!Thermal::update_phase(*b) && b->phase == Phase::Frozen, "liquid 0.55 stays FROZEN (hysteresis)");
	apply_heat(*b, fusion_total * 0.06);    // 0.61
	check(Thermal::update_phase(*b) && b->phase == Phase::Liquid, S("liquid >= ICE_UP melts (liquid ", b->liquid, ")"));
	// Oscillating across a threshold flips the label once, never back and forth.
	BodyPtr up = _water(4.0, 0.0, 0.55);
	up->phase = Phase::Frozen;
	int flips_up = 0;
	for (int k = 0; k < 400; ++k) {
		apply_heat(*up, fusion_total * (k % 2 == 0 ? 0.06 : -0.06));   // 0.55 <-> 0.61
		if (Thermal::update_phase(*up)) ++flips_up;
	}
	check(flips_up == 1, S("0.55 <-> 0.61 around ICE_UP flips FROZEN->LIQUID exactly once (", flips_up, ")"));
	check(up->phase == Phase::Liquid, "and stays LIQUID");
	BodyPtr down = _water(4.0, 0.0, 0.14);
	down->phase = Phase::Liquid;
	int flips_down = 0;
	for (int k = 0; k < 400; ++k) {
		apply_heat(*down, fusion_total * (k % 2 == 0 ? -0.06 : 0.06));   // 0.14 <-> 0.08
		if (Thermal::update_phase(*down)) ++flips_down;
	}
	check(flips_down == 1, S("0.14 <-> 0.08 around ICE_DOWN flips LIQUID->FROZEN exactly once (", flips_down, ")"));
	check(down->phase == Phase::Frozen, "and stays FROZEN");
	// Exact thresholds.
	BodyPtr c = _water(4.0, 0.0, 1.0);
	c->liquid = Sim::ICE_DOWN;
	check(Thermal::update_phase(*c) && c->phase == Phase::Frozen, "exactly ICE_DOWN freezes");
	c->liquid = Sim::ICE_UP - 0.0001;
	check(!Thermal::update_phase(*c) && c->phase == Phase::Frozen, "just below ICE_UP stays FROZEN");
	c->liquid = Sim::ICE_UP;
	check(Thermal::update_phase(*c) && c->phase == Phase::Liquid, "exactly ICE_UP melts");
	c->liquid = Sim::ICE_DOWN + 0.0001;
	check(!Thermal::update_phase(*c) && c->phase == Phase::Liquid, "just above ICE_DOWN stays LIQUID");
}

// ---------------------------------------------------------------- water

FF_TEST_F(test_thermal, Th, test_water_freezes_and_melts_with_latent_fusion) {
	BodyPtr b = _water(4.0, 20.0);
	const double sensible = 20.0 * 4.0 * Sim::WATER_C;
	const double fusion = 4.0 * Sim::WATER_LATENT_FUSION;
	const Vec2 r = apply_heat(*b, -sensible);
	near(X(r), -sensible, EPS, "cooling to 0 degC removes only sensible heat");
	near(b->temp, 0.0, 1e-6, "water at the freezing point");
	near(b->liquid, 1.0, 1e-9, "not frozen yet");
	apply_heat(*b, -fusion * 0.5);
	near(b->temp, 0.0, 1e-6, "temperature pinned at 0 degC while freezing");
	near(b->liquid, 0.5, 1e-6, "half frozen after half the fusion energy");
	apply_heat(*b, -fusion * 0.5);
	near(b->liquid, 0.0, 1e-6, "fully frozen");
	const Vec2 r2 = apply_heat(*b, -2.0);
	near(X(r2), -2.0, EPS, "ice cools further");
	check(b->temp < 0.0, S("ice below zero (", b->temp, ")"));
	// Heating back: sensible to 0, then latent, then liquid warms.
	const double to_zero = (0.0 - b->temp) * 4.0 * Sim::WATER_C;
	const Vec2 r3 = apply_heat(*b, to_zero);
	near(X(r3), to_zero, 1e-9, "ice warms to 0 degC");
	near(b->temp, 0.0, 1e-6, "at 0 degC");
	check(b->liquid == 0.0, "still frozen at 0 degC");
	apply_heat(*b, fusion * 0.25);
	near(b->liquid, 0.25, 1e-6, "melting follows latent energy");
	near(b->temp, 0.0, 1e-6, "pinned at 0 degC while melting");
	apply_heat(*b, fusion * 0.75);
	near(b->liquid, 1.0, 1e-6, "fully melted");
	check(b->mass == 4.0, "no mass change from freezing/melting");
}

FF_TEST_F(test_thermal, Th, test_water_cooling_floor_is_ice_min) {
	BodyPtr b = _water(2.0, 0.0, 0.0);
	const double avail = (0.0 - Thermal::ICE_MIN_C) * 2.0 * Sim::WATER_C;
	const Vec2 r = apply_heat(*b, -avail - 100.0);
	near(X(r), -avail, 1e-6, "ice cannot be cooled below ICE_MIN_C");
	near(b->temp, Thermal::ICE_MIN_C, 1e-6, "temperature floor");
}

FF_TEST_F(test_thermal, Th, test_water_vaporisation_reduces_mass_and_reports_kg) {
	BodyPtr b = _water(2.0, 20.0);
	const double to_boil = (Sim::WATER_BOIL_C - 20.0) * 2.0 * Sim::WATER_C;   // 8 HU
	const double e0 = b->thermal_energy();
	const Vec2 r = apply_heat(*b, to_boil);
	near(X(r), to_boil, EPS, "heating to the boiling point is all sensible");
	near(Y(r), 0.0, EPS, "no vapour yet");
	near(b->temp, Sim::WATER_BOIL_C, 1e-6, "boiling");
	near(b->mass, 2.0, 1e-9, "mass intact");
	const Vec2 r2 = apply_heat(*b, Sim::WATER_LATENT_VAPOR);   // 22 HU = 1 kg
	near(Y(r2), 1.0, 1e-6, "22 HU vaporises exactly 1 kg");
	near(X(r2), Sim::WATER_LATENT_VAPOR, 1e-6, "all of it was absorbed");
	near(b->mass, 1.0, 1e-9, "liquid mass fell by the vaporised amount");
	near(b->temp, Sim::WATER_BOIL_C, 1e-9, "boiling water does not superheat");
	// Conservation: heat in == heat left in the water + heat carried off by the vapour.
	near(e0 + X(r) + X(r2), b->thermal_energy() + Thermal::vapor_energy(Y(r2)), 1e-6, "energy conserved across vaporisation");
	// Vapour is capped by the mass available.
	const Vec2 r3 = apply_heat(*b, 1000.0);
	near(Y(r3), 1.0, 1e-6, "cannot vaporise more than the remaining mass");
	near(b->mass, 0.0, 1e-9, "all water gone");
	near(X(r3), Sim::WATER_LATENT_VAPOR, 1e-6, "only the energy actually needed is absorbed");
}

FF_TEST_F(test_thermal, Th, test_ice_must_melt_before_it_can_boil) {
	BodyPtr b = _water(1.0, -10.0, 0.0);
	const double need = 10.0 * Sim::WATER_C + Sim::WATER_LATENT_FUSION + 100.0 * Sim::WATER_C;   // to 0, melt, to 100
	const Vec2 r = apply_heat(*b, need - 0.01);
	near(Y(r), 0.0, EPS, "no vapour while ice is still thawing / warming");
	near(b->mass, 1.0, 1e-12, "mass untouched");
	check(b->temp < Sim::WATER_BOIL_C, S("not yet boiling (", b->temp, ")"));
	const Vec2 r2 = apply_heat(*b, 0.01 + Sim::WATER_LATENT_VAPOR * 0.5);
	near(Y(r2), 0.5, 1e-6, "once boiling, the surplus vaporises 0.5 kg");
}

FF_TEST_F(test_thermal, Th, test_flash_boil_returns_kg_and_energy) {
	BodyPtr b = _water(2.0, 20.0);
	const double per_kg = Sim::WATER_LATENT_VAPOR + Sim::WATER_C * (Sim::WATER_BOIL_C - 20.0);
	const Vec2 r = flash_boil(*b, per_kg * 0.5);
	near(X(r), 0.5, 1e-9, "x = kg boiled off");
	near(Y(r), per_kg * 0.5, 1e-9, "y = HU actually used");
	near(b->mass, 1.5, 1e-9, "contact layer removed from the body");
	near(b->temp, 20.0, 1e-9, "the bulk is not warmed by flash boiling");
	// Energy beyond the available mass is not consumed.
	const Vec2 r2 = flash_boil(*b, 1000.0);
	near(X(r2), 1.5, 1e-9, "capped at the remaining mass");
	near(Y(r2), 1.5 * per_kg, 1e-9, "uses only what the mass needs");
	near(b->mass, 0.0, 1e-9, "everything boiled");
	// Consistency with vapor_energy: steam leaves carrying exactly the energy spent at 20 degC.
	near(Thermal::vapor_energy(X(r)), Y(r), 1e-9, "vapor_energy matches the flash-boil cost for ambient water");
}

FF_TEST_F(test_thermal, Th, test_flash_boil_costs_more_for_ice_and_rejects_invalid_input) {
	BodyPtr liquid = _water(1.0, 20.0);
	BodyPtr ice = _water(1.0, -5.0, 0.0);
	const Vec2 kl = flash_boil(*liquid, 1000.0);
	const Vec2 ki = flash_boil(*ice, 1000.0);
	check(kl.y < ki.y, S("boiling ice costs more energy per kg (", kl.y, " vs ", ki.y, ")"));
	BodyPtr stone = _stone(5.0);
	check(flash_boil(*stone, 100.0) == Vec2(), "stone cannot flash boil");
	BodyPtr w = _water(1.0);
	check(flash_boil(*w, 0.0) == Vec2(), "zero energy boils nothing");
	check(flash_boil(*w, -50.0) == Vec2(), "negative energy boils nothing");
	near(w->mass, 1.0, 1e-12, "mass untouched by rejected calls");
	BodyPtr empty = _water(0.0);
	check(flash_boil(*empty, 100.0) == Vec2(), "massless water boils nothing");
}
