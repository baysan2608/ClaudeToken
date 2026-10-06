// Port of game/tests/sim/test_integration_matrix.gd: every element can answer every element (docs/MOVESET.md §8). For each
// thrown / travelling threat of the Lab spawner, a rival who owns one element (all four sub-elements) is asked for its
// counters through the planner the 1v1 AI uses (AiPlanner::counters -> Interactions::predict). Each element must have a
// non-evade answer with a full or partial band, and weak answers to heavy / hot threats are partial or fail, never full.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Lab/SpawnCatalog.h"
#include "Util/GdUtil.h"

#include <algorithm>

using namespace ff;
using namespace fft;

namespace {
const char* const THREATS[] = {"stone_20", "stone_45", "hot_rock", "magma_blob", "lava_wave", "metal_disc", "metal_lance", "metal_plate",
                               "sand_slug", "sand_surge", "water_blob", "water_stream", "water_wave", "ice_shard", "fireball", "comet", "fire_line",
                               "wind_crescent", "tornado", "spike_line", "tremor"};
const char* const ELEMENT_NAMES[] = {"Earth", "Water", "Fire", "Air"};

AiRig _setup(int element, int sub) {
	AiRig r;
	r.h = std::make_unique<SimHarness>(5);
	r.p = r.h->actor("thrower", V3(0, 0, 6.0), 0, Dict(), Sim::EARTH);
	r.o = r.h->actor("answer", V3(0, 0, -6.0), 1, D({{"heat_draw", true}, {"magma", true}, {"redirect_current", true}}), element);
	r.o->subs[static_cast<size_t>(element)] = sub;
	r.ai = std::make_unique<AiBrain>(*r.h->w, *r.o, Dict(), 5);
	r.ai->configure(D({{"preset", "master"}, {"elements", A({element})}, {"subs", D({{itos(element), A({0, 1, 2, 3})}})}, {"drill", "passive"},
	                   {"counter", 1.0}, {"misjudge", 0.0}}));
	r.h->step(5);
	return r;
}

std::vector<AiOption> _answers(int element, const std::string& threat_id) {
	AiRig d = _setup(element, 0);
	SimHarness& h = *d.h;
	SpawnCatalog::SpawnResult res = SpawnCatalog::spawn(*h.w, threat_id, Dict(), *d.o, d.p, Vec3(), true);
	if (!res.ok) return {};
	std::vector<BodyRef> bodies;
	for (MatBody* b : res.bodies)
		if (b != nullptr) bodies.push_back(b->shared_from_this());
	h.step(3);
	std::vector<AiOption> best;
	for (const BodyRef& mb : bodies) {
		if (!mb->alive) continue;
		const AiThreat th = AiPlanner::body_threat(*h.w, *d.o, *mb);
		if (!th.valid) continue;
		AiParams prm = d.ai->_planner_params();
		prm.only.clear();
		for (const AiOption& opt : AiPlanner::counters(*h.w, *d.o, th, d.ai->kit, prm, 0.0))
			if (opt.slot != "evade") best.push_back(opt);
		break;
	}
	return best;
}

AgentRef _thrown_agent(AiRig& d, const std::string& threat_id, const Dict& params = Dict()) {
	SimHarness& h = *d.h;
	SpawnCatalog::SpawnResult res = SpawnCatalog::spawn(*h.w, threat_id, params, *d.o, d.p, Vec3(), true);
	std::vector<BodyRef> bodies;
	for (MatBody* b : res.bodies)
		if (b != nullptr) bodies.push_back(b->shared_from_this());
	h.step(3);
	for (const BodyRef& b : bodies) {
		const AiThreat th = AiPlanner::body_threat(*h.w, *d.o, *b);
		if (th.valid) return th.agent;
	}
	return nullptr;
}
}  // namespace

FF_TEST(test_integration_matrix, test_every_element_answers_every_thrown_threat) {
	std::vector<std::string> gaps;
	for (const char* tid : THREATS) {
		std::string row;
		for (int e = 0; e < 4; ++e) {
			std::vector<AiOption> good;
			for (const AiOption& o : _answers(e, tid))
				if (o.band == "full" || o.band == "partial") good.push_back(o);
			std::stable_sort(good.begin(), good.end(), [](const AiOption& x, const AiOption& y) { return x.value > y.value; });
			if (good.empty()) {
				gaps.push_back(std::string(ELEMENT_NAMES[e]) + " vs " + tid);
				row += std::string(ELEMENT_NAMES[e]) + ": - | ";
			} else {
				const AiOption& g = good[0];
				row += S(ELEMENT_NAMES[e], ": ", g.label, " ", g.outcome, "/", g.band, " r", ftos(g.ratio, 2), " | ");
			}
		}
		note(std::string(tid) + " " + row);
	}
	std::string all;
	for (const std::string& g : gaps) all += g + "; ";
	check(gaps.empty(), "every element has a full/partial answer: missing " + all);
}

FF_TEST(test_integration_matrix, test_counter_strength_scales_with_the_threat) {
	{
		// Lava (a molten blob) vs wind: a palm gust (T0) only bends it; the T3 Hurricane sets it to rock.
		AiRig d = _setup(Sim::AIR, 0);
		AgentRef ag = _thrown_agent(d, "magma_blob");
		check(ag != nullptr, "magma blob is a threat");
		if (ag != nullptr) {
			const IxResult t0 = Interactions::predict(d.h->w, *ag, *Agent::of_move(d.h->w, d.o, "air_attack", 0, false));
			const IxResult t3 = Interactions::predict(d.h->w, *ag, *Agent::of_move(d.h->w, d.o, "air_attack", 3, false));
			check(t0.outcome != "redirect" && t0.outcome != "transform", "a palm gust does not stop lava (" + t0.outcome + ")");
			check(t3.band != "fail" && t3.ratio > t0.ratio * 3.0 && begins_with(t3.rule_id, "air_"),
			      S("a hurricane palm (T3) cools / weakens the 25 kg blob (", t3.outcome, "/", t3.band, " r", t3.ratio, " vs T0 r", t0.ratio, ")"));
		}
	}
	{
		// A lighter splash of lava is set to rock outright by the same hurricane.
		AiRig dl = _setup(Sim::AIR, 0);
		AgentRef lt = _thrown_agent(dl, "magma_blob", D({{"mass", 8.0}, {"speed", 9.0}}));
		if (check(lt != nullptr, "light blob is a threat")) {
			const IxResult t3l = Interactions::predict(dl.h->w, *lt, *Agent::of_move(dl.h->w, dl.o, "air_attack", 3, false));
			check(t3l.outcome == "transform" && t3l.to == "rock", S("hurricane sets 8 kg of lava to rock (", t3l.outcome, "/", t3l.band, " r", t3l.ratio, ")"));
		}
	}
	{
		// A palm gust (7 x2 = 14) only bends a 20 kg thrown stone (TP 17); the cyclone push (11 x2 = 22) sends it back.
		AiRig d2 = _setup(Sim::AIR, 0);
		AgentRef st = _thrown_agent(d2, "stone_20");
		if (check(st != nullptr, "stone is a threat")) {
			const IxResult r0 = Interactions::predict(d2.h->w, *st, *Agent::of_move(d2.h->w, d2.o, "air_attack", 0, false));
			const IxResult r1c = Interactions::predict(d2.h->w, *st, *Agent::of_move(d2.h->w, d2.o, "air_attack", 1, false));
			check(r0.outcome != "redirect" && r1c.outcome == "redirect",
			      S("palm gust bends a 20 kg stone (", r0.outcome, " r", r0.ratio, "), cyclone sends it back (", r1c.outcome, " r", r1c.ratio, ")"));
		}
	}
	// A heavier stone needs more: 80 kg is only bent by the same gust.
	AiRig d3 = _setup(Sim::AIR, 0);
	AgentRef hv = _thrown_agent(d3, "stone_80");
	if (check(hv != nullptr, "80 kg stone is a threat")) {
		const IxResult r1 = Interactions::predict(d3.h->w, *hv, *Agent::of_move(d3.h->w, d3.o, "air_attack", 0, false));
		check(r1.outcome != "redirect", "an 80 kg stone is not sent back by a palm gust (" + r1.outcome + ")");
	}
}
