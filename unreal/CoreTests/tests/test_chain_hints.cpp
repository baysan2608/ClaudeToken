// Chain prompts (App/ChainHints, docs/game/CONTROLS_HUD_PLAN.md part C): the HUD learns which attack slots would chain
// out of the player's recovery right now, with the same rule as the sim (MOVESET §9.1) and without changing anything.
#include "ff_test.h"
#include "sim_harness.h"

#include "App/ChainHints.h"
#include "App/HudBuilder.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct ChainFx : HarnessCase {
	ActorState* p = nullptr;
	ActorState* o = nullptr;
	SimHarness& _setup() {
		SimHarness& h = H(1);
		h.begin_scope();
		h.legacy_bindings_only();
		Moves::register_def("t_jab", D({{"element", 0}, {"verb", "cone"}, {"startup", 0.05}, {"active", 0.05}, {"recovery", 0.4}, {"cancel", 0.8},
		                                {"chain", 0.25}, {"range", 12.0}, {"angle", 30.0}, {"damage", 2.0}, {"balance", 2.0}, {"cls", "gust"}, {"power", 5.0}}));
		Moves::register_def("t_thrust", D({{"element", 0}, {"verb", "projectile"}, {"slot", "thrust"}, {"startup", 0.2}, {"active", 0.05},
		                                   {"recovery", 0.3}, {"cost", 6.0}, {"source", "ground"}, {"mat", "stone"}, {"mass", 6.0}, {"speed", 26.0},
		                                   {"gravity", 0.4}, {"tag", "spear"}}));
		Moves::bind(0, 1, "strike", "t_jab");
		Moves::bind(0, 1, "thrust", "t_thrust");
		p = h.actor("P", Vec3(0, 0, 4), 0, Dict(), Sim::EARTH);
		o = h.actor("O", Vec3(0, 0, -6), 1, Dict(), Sim::FIRE);
		o->is_dummy = true;
		h.step(10);
		h.sub(p, 1);
		h.step();
		h.log.clear();
		return h;
	}
	void _done() { h().end_scope(); }
	static bool has_slot(const ChainHints::Result& r, const std::string& s) {
		for (const std::string& x : r.slots)
			if (x == s) return true;
		return false;
	}
};
}  // namespace

FF_TEST_F(test_chain_hints, ChainFx, test_window_opens_after_contact_and_lists_unused_slots) {
	SimHarness& h = _setup();
	check(!ChainHints::query(*h.w, *p).open, "idle: no chain window");
	h.press(p, "attack");
	h.release(p, "attack");
	h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 30);
	check(p->action != nullptr && dbool(p->action->data, "contact", false), "the jab made contact");
	check(!ChainHints::query(*h.w, *p).open, "early recovery: the window is not open yet");
	h.step(static_cast<int>(0.4 * 0.3 * 60));
	const Dict chain_before = p->chain;
	const ChainHints::Result r = ChainHints::query(*h.w, *p);
	check(r.open && r.n == 1 && !r.weave, S("window open after contact (n ", r.n, ")"));
	check(has_slot(r, "thrust") && !has_slot(r, "strike"), "the thrust may chain; the strike was used");
	check(r.slots.size() == r.moves.size() && r.left > 0.0 && r.window >= r.left - 1e-9, "names and timing");
	check(Value(p->chain).to_string() == Value(chain_before).to_string() && !p->chain.has("pending"), "the query marks no pending chain");
	// the HUD model carries it
	HudBuilder::Context c;
	c.world = h.w;
	c.player = p;
	const HudModel hm = HudBuilder::build(c);
	check(hm.chain.open && hm.chain.slots == r.slots, "HUD model gets the chain prompt");
	// chaining into the thrust (no chain key of its own) ends the prompt
	h.flick(p, "attack", static_cast<int>(Gesture::Up));
	h.step();
	check(p->action != nullptr && p->action->id == "t_thrust", "chained into the thrust");
	h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 60);
	check(!ChainHints::query(*h.w, *p).open, "the thrust does not chain further");
	_done();
}
