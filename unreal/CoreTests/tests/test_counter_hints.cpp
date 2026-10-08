// Context counters (App/CounterHints, docs/game/CONTROLS_HUD_PLAN.md part A): the HUD's answers to an incoming threat
// come from the counter rule itself, per element / sub-element, without changing the world.
#include "ff_test.h"
#include "sim_harness.h"

#include "App/CounterHints.h"
#include "App/HudBuilder.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
const CounterHintView* find_slot(const CounterHints::Result& r, const std::string& slot) {
	for (const CounterHintView& v : r.hints)
		if (v.slot == slot) return &v;
	return nullptr;
}
}  // namespace

FF_TEST(test_counter_hints, test_labels) {
	check(CounterHints::label_for("reflect", "") == "Send back", "reflect reads as Send back");
	check(CounterHints::label_for("transform", "lava") == "To lava", "transform names its result");
	check(CounterHints::label_for("water_freeze", "") == "Freeze", "kit outcomes drop the element prefix");
	check(CounterHints::label_for("earth_melt_in", "") == "Melt in", "underscores become spaces");
	check(CounterHints::label_for("pass", "") == "No effect", "pass reads as No effect");
	check(CounterHints::threat_label("stone_heavy") == "Heavy stone", "weight reads first");
	check(CounterHints::threat_label("wall_stone") == "Stone wall", "form prefixes read last");
	check(CounterHints::threat_label("water_jet") == "Water jet", "plain classes keep their order");
	check(CounterHints::threat_label("fireball") == "Fireball", "single words are capitalised");
	check(CounterHints::threat_label("") == "", "empty stays empty");
}

FF_TEST(test_counter_hints, test_stone_incoming_per_element) {
	int attack_answers = 0;
	for (int el = 0; el < 4; ++el) {
		SimHarness h(5);
		ActorState* p = h.actor("player", V3(0, 0, 4), 0, Dict(), el);
		ActorState* o = h.actor("opponent", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
		h.step(5);
		MatBody* st = h.launch_at(p, "stone", 20.0, 15.0, Sim::AMBIENT_C, "", o, 8.0);
		const Vec3 before = st->pos;
		const size_t bodies = h.w->bodies.size();
		const CounterHints::Result r = CounterHints::query(*h.w, *p);
		check(r.valid, S("element ", el, ": a stone on its way is a threat"));
		check(r.tti > 0.0 && r.tti < CounterHints::kHorizon, S("element ", el, ": time to impact ", r.tti));
		const CounterHintView* g = find_slot(r, "guard");
		check(g != nullptr, S("element ", el, ": the guard always has an answer"));
		std::string line;
		for (const CounterHintView& v : r.hints) line += v.slot + "=" + v.label + "/" + v.band + "@T" + std::to_string(v.tier) + (v.perfect ? "P " : " ");
		note(S("element ", el, " vs 20 kg stone: ", line));
		int attacks = 0;
		for (const CounterHintView& v : r.hints) {
			if (v.slot == "thrust" || v.slot == "ground" || v.slot == "sweep" || v.slot == "strike") {
				++attacks;
				check(v.band == "full" || v.band == "partial", S("element ", el, ": an attack shown as an answer works (", v.band, ")"));
			}
		}
		check(attacks <= 1, S("element ", el, ": at most one attack answer"));
		attack_answers += attacks;
		check(st->pos.distance_to(before) < 1e-6 && h.w->bodies.size() == bodies, S("element ", el, ": the query changes nothing"));
		// the HUD model carries the same answers
		HudBuilder::Context c;
		c.world = h.w;
		c.player = p;
		const HudModel hm = HudBuilder::build(c);
		check(hm.has_threat && hm.counters.size() == r.hints.size(), S("element ", el, ": HUD model gets the counters"));
	}
	check(attack_answers > 0, "some element answers a stone with an attack slot");
}

FF_TEST(test_counter_hints, test_nothing_coming) {
	SimHarness h(5);
	ActorState* p = h.actor("player", V3(0, 0, 4), 0, Dict(), Sim::WATER);
	h.actor("opponent", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(5);
	check(!CounterHints::query(*h.w, *p).valid, "no threat, no counters");
}
