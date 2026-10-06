// Port of game/tests/sim/test_ai_drills.gd: the config API (AiBrain.configure / PRESETS / describe) and the Lab drills:
// element:<e>/<sub>, matrix, and the legacy drills through configure().
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <set>

using namespace ff;
using namespace fft;

namespace {
AiRig _brain(const Dict& opts, uint64_t seed_value = 4, int el = Sim::EARTH) {
	AiRig r;
	r.h = std::make_unique<SimHarness>(seed_value);
	r.p = r.h->actor("player", V3(0, 0, 5), 0, Dict(), Sim::EARTH);
	r.o = r.h->actor("opponent", V3(0, 0, -5), 1, Dict(), el);
	r.ai = std::make_unique<AiBrain>(*r.h->w, *r.o, Dict(), seed_value);
	r.ai->configure(opts);
	return r;
}

std::vector<Dict> _play(AiRig& d, double secs) {
	SimHarness& h = *d.h;
	std::vector<Dict> acts;
	for (int k = 0; k < static_cast<int>(secs * 60); ++k) {
		d.think();
		const size_t n0 = h.log.size();
		h.step();
		d.p->health = 100.0;
		d.o->health = 100.0;
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "action" && ev_i(e, "actor", -1) == d.o->id && ev_s(e, "phase") == "startup") acts.push_back(e);
		}
	}
	return acts;
}

std::vector<int> ints(const Value& v) {
	std::vector<int> out;
	for (const Value& x : v.as_array()) out.push_back(vint(x));
	return out;
}
}  // namespace

FF_TEST(test_ai_drills, test_presets_and_kit_breadth) {
	check(AiBrain::preset_names() == std::vector<std::string>{"novice", "adept", "master"}, "three presets");
	check(AiPresets::TABLE().has("master") && dnum(ddict(AiPresets::TABLE(), "master"), "reaction") < dnum(ddict(AiPresets::TABLE(), "novice"), "reaction"),
	      "preset table exposed");
	{
		AiRig n = _brain(D({{"preset", "novice"}}), 4, Sim::FIRE);
		const Dict nd = n.ai->describe();
		check(dbool(nd, "planner") && dstr(nd, "preset") == "novice", "novice configured");
		const std::vector<int> els = ints(nd.get("elements"));
		check(els.size() == 1 && els[0] == Sim::FIRE, "novice: one element, its own");
		check(ddict(nd, "subs").get(itos(Sim::FIRE)).as_array().size() == 2, "novice: two sub-elements");
		check(is_equal_approx(dnum(nd, "reaction"), 0.45) && is_equal_approx(dnum(nd, "counter"), 0.3), "novice numbers (MOVESET §13)");
	}
	{
		AiRig a = _brain(D({{"preset", "adept"}}));
		check(ints(a.ai->describe().get("elements")).size() == 2, "adept: two elements");
	}
	{
		AiRig m = _brain(D({{"preset", "master"}}));
		check(ints(m.ai->describe().get("elements")).size() == 4, "master: four elements");
		check(m.o->elements == std::array<bool, 4>{{true, true, true, true}}, "the master's fighter has every element unlocked");
	}
	AiRig x = _brain(D({{"preset", "master"}, {"elements", A({1, 3})}, {"subs", D({{"1", A({1, 2})}, {"3", A({3})}})}, {"reaction", 0.33}}));
	const Dict xd = x.ai->describe();
	check(ints(xd.get("elements")) == std::vector<int>{1, 3} && ints(ddict(xd, "subs").get("1")) == std::vector<int>{1, 2} &&
	          ints(ddict(xd, "subs").get("3")) == std::vector<int>{3},
	      "explicit kit");
	check(is_equal_approx(dnum(xd, "reaction"), 0.33), "a single number can be overridden");
	check(x.o->elements == std::array<bool, 4>{{false, true, false, true}}, "the fighter carries exactly the configured elements");
}

FF_TEST(test_ai_drills, test_element_drill_spams_that_kit) {
	AiRig d = _brain(D({{"preset", "adept"}, {"drill", "element:fire/blue"}, {"interval", 1.2}}));
	const std::vector<Dict> acts = _play(d, 20.0);
	std::set<std::string> ids;
	int n = 0;
	for (const Dict& e : acts) {
		const Dict def = Moves::defs().get(ev_s(e, "move")).as_dict();
		if (ev_i(e, "sub") == 1 && dint(def, "element", -1) == Sim::FIRE) {
			++n;
			ids.insert(ev_s(e, "move"));
		}
	}
	std::string idl;
	for (const std::string& s : ids) idl += s + " ";
	note(S("fire/blue drill: ", n, " launches, moves ", idl));
	check(n >= 8, S("the drill launches Fire/Blue threats on its rhythm (", n, ")"));
	check(ids.size() >= 3, "several of the kit's moves (" + idl + ")");
	check(AiPresets::parse_element_drill("element:2/1") == std::pair<int, int>{2, 1} && AiPresets::parse_element_drill("element:air/sound") == std::pair<int, int>{3, 3},
	      "drill specs parse");
}

FF_TEST(test_ai_drills, test_matrix_drill_cycles_threat_classes) {
	AiRig d = _brain(D({{"preset", "master"}, {"drill", "matrix"}, {"interval", 1.0}}));
	const std::vector<Dict> acts = _play(d, 40.0);
	std::set<std::string> classes;
	for (const Dict& e : acts) {
		const std::string cls = dstr(ddict(Moves::defs().get(ev_s(e, "move")).as_dict(), "threat"), "cls", "");
		if (!cls.empty()) classes.insert(cls);
	}
	std::string cl;
	for (const std::string& s : classes) cl += s + " ";
	note("matrix drill threat classes: " + cl);
	check(classes.size() >= 10, S("the matrix drill cycles through many threat classes (", classes.size(), ")"));
}

FF_TEST(test_ai_drills, test_legacy_drills_through_configure) {
	AiRig d = _brain(D({{"preset", "adept"}, {"elements", A({0})}, {"drill", "stone_rain"}, {"interval", 1.5}}));
	int throws = 0;
	for (const Dict& e : _play(d, 12.0))
		if (ev_s(e, "move") == "earth_attack") ++throws;
	check(throws >= 5, S("stone rain still throws stones on its rhythm (", throws, ")"));
}

FF_TEST(test_ai_drills, test_mixed_kit_refills_at_a_nearby_pool) {
	// Water + Fire kit fighting as Water with an empty waterskin, 3 m from the pool: short detour to refill.
	SimHarness h(6);
	ActorState* p = h.actor("player", V3(-5, 0, -1), 0, Dict(), Sim::EARTH);
	ActorState* o = h.actor("opponent", V3(4, 0, -1), 1, Dict(), Sim::WATER);
	AiBrain ai(*h.w, *o, Dict(), 6);
	ai.configure(D({{"preset", "adept"}, {"elements", A({Sim::WATER, Sim::FIRE})}, {"aggression", 0.0}}));
	o->element = Sim::WATER;
	o->water_carried = 0.0;
	const double d0 = AiTestAccess::pool_distance(ai);
	bool refilled = false;
	for (int k = 0; k < 240; ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		h.step();
		p->health = 100.0;
		o->health = 100.0;
		if (o->in_water || o->water_carried >= 1.5) {
			refilled = true;
			break;
		}
	}
	note(S("pool distance ", d0, " -> ", AiTestAccess::pool_distance(ai), ", water ", o->water_carried, " kg, in_water ", o->in_water));
	check(refilled, "the mixed kit walked into the nearby pool to refill");
}
