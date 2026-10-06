// Port of game/tests/sim/test_flagship.gd: the flagship encounter: stone -> lava -> ground wave -> cooled rock, with the
// opponent countering by drawing heat. Each test plays the rules, not a script.
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
struct Flag : HarnessCase {
	ActorState* p = nullptr;
	ActorState* o = nullptr;
	void _setup(int p_elem = Sim::FIRE, const Dict& o_kit = D({{"heat_draw", true}})) {
		SimHarness& hh = H(11);
		p = hh.actor("player", V3(0, 0, 6), 0, D({{"magma", true}, {"heat_draw", true}}), p_elem);
		o = hh.actor("opponent", V3(0, 0, -6), 1, o_kit, Sim::EARTH);
	}
	BodyRef _opponent_throws() {
		SimHarness& hh = h();
		hh.press(o, "attack");
		hh.step();
		hh.release(o, "attack");
		const int t = hh.until([&]() { return hh.has_event("launch"); }, 60);
		check(t > 0, "opponent launched a stone");
		MatBody* b = hh.w->get_body(ev_i(hh.last_event("launch"), "body", -1));
		return b != nullptr ? b->shared_from_this() : BodyRef();
	}
	static double _dist(const MatBody& b, const ActorState* a) { return b.pos.distance_to(a->chest()); }
	void _intercept(const BodyRef& stone, double press_at) {
		h().until([&]() { return _dist(*stone, p) <= press_at; }, 120);
		h().press(p, "tech");
	}
	bool hit_on(const ActorState* a) {
		return h().any_event("hit", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id; });
	}
};
}  // namespace

FF_TEST_F(test_flagship, Flag, test_full_exchange_and_counter) {
	_setup();
	SimHarness& h = this->h();
	const double e0 = h.w->system_energy();
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	const int sid = stone->id;
	check(stone->attack_owner == o->id && stone->mass == Sim::STONE_SHOT_MASS, "stone is the opponent's 20 kg attack");
	_intercept(stone, 8.5);
	const int caught = h.until([&]() { return stone->controller == p->id; }, 60);
	check(caught > 0, "magma grip caught the stone in flight");
	check(h.has_event("intercept"), "intercept event (momentum absorbed)");
	const int melted = h.until([&]() { return stone->phase == Phase::Molten; }, 120);
	check(melted > 0, S("held stone becomes molten (", melted, " ticks)"));
	check(stone->id == sid && stone->alive, "same logical body after melting");
	check(stone->form == Form::Blob, "molten body is a held blob");
	near(stone->mass, 20.0, 1e-4, "mass preserved through melting");
	// Pour toward the opponent.
	h.aim(p, o->pos - p->pos);
	h.release(p, "tech");
	const int poured = h.until([&]() { return stone->form == Form::Wave; }, 40);
	check(poured > 0, "release turns molten blob into a ground wave");
	check(stone->attack_owner == p->id && stone->controller == -1, "wave is the player's attack");
	// Opponent recognises the wave and draws its heat.
	h.element(o, Sim::FIRE);
	h.step();
	h.until([&]() { return stone->pos.distance_to(o->pos) < 8.5f; }, 120);
	h.press(o, "tech");
	h.step();
	check(o->action != nullptr && dstr(o->action->data, "mode", "") == "DRAW", "opponent's thermal technique chose DRAW");
	const int cooled = h.until([&]() { return stone->phase == Phase::Solid || h.has_event("hit"); }, 240);
	check(cooled > 0, "wave resolved");
	check(stone->phase == Phase::Solid, "wave solidified before reaching the opponent");
	check(!hit_on(o), "opponent not hit by the wave");
	check(stone->form == Form::Chunk && stone->id == sid, "cooled into rock with the same identity");
	check(stone->temp >= Sim::HOT_ROCK_C, S("rock is still hot (", ftos(stone->temp, 0), " C)"));
	near(stone->mass, 20.0, 1e-4, "mass preserved through wave and cooling");
	check(o->heat_reserve > 50.0, S("extracted heat went into the opponent's reserve (", ftos(o->heat_reserve, 0), " HU)"));
	h.release(o, "tech");
	h.step(30);
	// Energy ledger balances: nothing created from nowhere.
	near(h.w->system_energy() - e0, h.w->ledger_balance(), 0.5, "energy ledger balances");
	// The rock is now a resource for either fighter: opponent seizes it with earth.
	h.element(o, Sim::EARTH);
	h.step();
	h.press(o, "tech");
	const int seized = h.until([&]() { return stone->controller == o->id; }, 60);
	check(seized > 0, "opponent can seize the cooled rock");
	note(S("melt ", melted, " ticks, pour->rock ", cooled, " ticks, reserve ", ftos(o->heat_reserve, 0), " HU"));
}

FF_TEST_F(test_flagship, Flag, test_wave_hits_when_not_countered) {
	_setup();
	SimHarness& h = this->h();
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	_intercept(stone, 8.5);
	h.until([&]() { return stone->phase == Phase::Molten; }, 180);
	h.aim(p, o->pos - p->pos);
	h.release(p, "tech");
	const int r = h.until([&]() { return hit_on(o); }, 240);
	check(r > 0, "uncountered wave hits the opponent");
	const std::vector<Dict> hit = h.filter("hit", [&](const Dict& e) { return ev_i(e, "actor", -1) == o->id; });
	check(hit.size() == 1, S("wave hits exactly once (dedup), got ", hit.size()));
	check(!hit.empty() && ev_s(hit[0], "kind") == "lava", "hit kind is lava");
}

FF_TEST_F(test_flagship, Flag, test_early_press_whiffs) {
	_setup();
	SimHarness& h = this->h();
	h.press(o, "attack");
	h.step();
	h.release(o, "attack");
	h.step(3);
	h.press(p, "tech");   // during the telegraph: nothing in flight to grip yet
	const int r = h.until([&]() { return hit_on(p); }, 120);
	check(h.has_event("whiff") || h.has_event("insufficient"), "early grip fails");
	check(r > 0, "stone then hits the player");
	check(!h.has_event("intercept"), "no interception");
}

FF_TEST_F(test_flagship, Flag, test_late_press_is_hit) {
	_setup();
	SimHarness& h = this->h();
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	_intercept(stone, 1.6);
	h.until([&]() { return h.has_event("hit"); }, 60);
	check(hit_on(p), "late press: player is hit");
	check(!h.has_event("intercept"), "late press: no interception");
	check(p->action == nullptr, "player's technique was interrupted, no stuck state");
}

FF_TEST_F(test_flagship, Flag, test_boulder_too_heavy) {
	_setup();
	SimHarness& h = this->h();
	const BodyRef b = h.w->spawn_body(Mat::Stone, Form::Chunk, 200.0, V3(0, 1.4, -2), "scenario")->shared_from_this();
	b->vel = V3(0, 2, 15);
	b->attack_id = h.w->new_attack_id();
	b->attack_owner = o->id;
	b->damage = 25;
	b->balance_damage = 60;
	b->hit_set.add(o->id);
	h.press(p, "tech");
	h.until([&]() { return h.has_event("hit") || h.has_event("control_fail"); }, 90);
	const Dict cf = h.last_event("control_fail");
	check(!cf.empty() && ev_s(cf, "reason") == "mass", "huge boulder cannot be gripped (mass)");
	check(b->controller != p->id && b->phase == Phase::Solid, "boulder unchanged");
}

FF_TEST_F(test_flagship, Flag, test_insufficient_focus_partial_conversion) {
	_setup();
	SimHarness& h = this->h();
	p->focus = 16.0;
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	_intercept(stone, 8.5);
	h.until([&]() { return stone->controller == p->id; }, 60);
	h.step(90);
	check(h.any_event("insufficient", [&](const Dict& e) { return ev_i(e, "actor", -1) == p->id; }), "player told Focus ran out");
	check(stone->phase != Phase::Molten, S("not enough energy to fully melt (liquid ", stone->liquid, ")"));
	check(stone->controller == p->id, "running dry stalls the conversion with the stone still held");
	check(!h.has_event("control_lost"), "no control loss from spending the last Focus on heat");
	h.aim(p, o->pos - p->pos);
	h.release(p, "tech");
	h.step(3);
	check(stone->form != Form::Wave, "a not-molten stone is thrown, not poured");
	check(stone->attack_owner == p->id, "hot stone thrown as an attack");
}

FF_TEST_F(test_flagship, Flag, test_interrupted_conversion_keeps_state) {
	_setup();
	SimHarness& h = this->h();
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	_intercept(stone, 8.5);
	h.until([&]() { return stone->controller == p->id; }, 60);
	h.until([&]() { return stone->liquid > 0.3; }, 120);
	const double liq = stone->liquid;
	// Interrupt the player with a direct hit.
	const int aid = h.w->new_attack_id();
	h.w->hit_actor(*p, D({{"attacker", o->id}, {"attack_id", aid}, {"damage", 5.0}, {"balance", 30.0}, {"kind", "fire"}, {"from", o->pos}}));
	h.step();
	check(stone->controller == -1, "interruption drops the body");
	check(h.has_event("conversion_interrupted"), "interruption reported");
	check(stone->alive && std::fabs(stone->liquid - liq) < 0.05, S("partial melt state preserved (", stone->liquid, ")"));
	check(stone->phase == Phase::Softened, "phase is softened, not snapped to solid/molten");
	// It cools passively without flickering between labels.
	int changes = 0;
	Phase last = stone->phase;
	for (int k = 0; k < 900; ++k) {
		h.step();
		if (stone->phase != last) {
			++changes;
			last = stone->phase;
		}
	}
	check(stone->phase == Phase::Solid, "cooled back to solid");
	check(changes == 1, S("single clean phase change while cooling (", changes, ")"));
}

FF_TEST_F(test_flagship, Flag, test_partial_draw_slows_wave) {
	_setup();
	SimHarness& h = this->h();
	const BodyRef stone = _opponent_throws();
	if (!stone) return;
	_intercept(stone, 8.5);
	h.until([&]() { return stone->phase == Phase::Molten; }, 180);
	// Overheat a little so the wave stays fluid long enough.
	h.step(10);
	h.aim(p, o->pos - p->pos);
	h.release(p, "tech");
	h.until([&]() { return stone->form == Form::Wave; }, 40);
	h.step(2);
	const double v0 = stone->vel.length();
	h.element(o, Sim::FIRE);
	h.step();
	h.until([&]() { return stone->pos.distance_to(o->pos) < 8.5f; }, 120);
	h.press(o, "tech");
	h.until([&]() { return stone->liquid < 0.5; }, 120);
	h.release(o, "tech");
	h.step(2);
	if (stone->form == Form::Wave) check(stone->vel.length() < v0 * 0.85, S("partially cooled wave is slower (", v0, " -> ", stone->vel.length(), ")"));
	else check(stone->phase != Phase::Molten, "wave settled while partially cooled");
	check(stone->phase != Phase::Molten, "label left MOLTEN after partial cooling");
}
