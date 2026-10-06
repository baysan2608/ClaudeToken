// Port of game/tests/sim/test_ai_flagship.gd: the sparring AI must counter a player-created lava wave in ordinary play
// (its own throws, its own reaction delay and resources), across seeds.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct Play {
	bool intercepted = false, poured = false, countered = false, hit = false, drew = false;
	std::string dbg;
};

Play _play(uint64_t seed_value, double counter, double reaction = 0.28) {
	SimHarness h(seed_value);
	ActorState* p = h.actor("player", V3(0, 0, 6), 0, D({{"magma", true}}), Sim::FIRE);
	ActorState* o = h.actor("opponent", V3(0, 0, -6), 1, D({{"heat_draw", true}}), Sim::EARTH);
	AiBrain ai(*h.w, *o, D({{"aggression", 0.8}, {"counter", counter}, {"reaction", reaction}, {"drill", "stone_rain"}, {"interval", 3.5}}), seed_value);
	Play res;
	BodyRef stone;
	std::string phase = "wait";
	for (int k = 0; k < 1200; ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		if (phase == "wait") {
			for (const BodyRef& b : h.w->bodies) {
				if (b->alive && b->is_projectile() && b->attack_owner == o->id && b->pos.distance_to(p->chest()) < 8.0f) {
					stone = b;
					h.press(p, "tech");
					phase = "catch";
					break;
				}
			}
		} else if (phase == "catch") {
			if (stone->controller == p->id) {
				res.intercepted = true;
				phase = "melt";
			} else if (p->action == nullptr) {
				phase = "wait";
			}
		} else if (phase == "melt") {
			if (stone->phase == Phase::Molten) {
				h.aim(p, o->pos - p->pos);
				h.release(p, "tech");
				phase = "wave";
			} else if (stone->controller != p->id) {
				phase = "wait";
			}
		} else if (phase == "wave") {
			if (stone->form == Form::Wave) res.poured = true;
			if (res.poured && (stone->phase == Phase::Solid || !stone->alive)) break;
		}
		h.step();
		if (stone != nullptr && res.poured &&
		    h.any_event("hit", [&](const Dict& e) { return ev_i(e, "actor", -1) == o->id && ev_s(e, "kind") == "lava"; })) {
			res.hit = true;
			break;
		}
	}
	res.drew = h.any_event("drawing", [&](const Dict& e) { return ev_i(e, "actor", -1) == o->id; });
	res.countered = res.poured && !res.hit && stone != nullptr && stone->phase == Phase::Solid;
	res.dbg = S("seed ", seed_value, ": intercepted ", res.intercepted, " poured ", res.poured, " countered ", res.countered, " hit ", res.hit, " drew ",
	            res.drew, " ai=", ai.debug_state, " o.focus=", ftos(o->focus, 0), " reserve=", ftos(o->heat_reserve, 0), " stone=",
	            stone ? stone->describe() : std::string("-"));
	return res;
}
}  // namespace

FF_TEST(test_ai_flagship, test_ai_counters_player_wave) {
	int ok = 0, tried = 0;
	for (uint64_t s : {3u, 5u, 8u, 13u, 21u}) {
		const Play r = _play(s, 1.0);
		note(r.dbg);
		if (r.poured) {
			++tried;
			if (r.countered && r.drew) ++ok;
		}
	}
	note(S("AI drew heat and solidified ", ok, " of ", tried, " player waves"));
	check(tried >= 4, S("player managed to pour in most runs (", tried, ")"));
	check(ok >= tried - 1 && ok > 0, S("AI counters the wave in ordinary play (", ok, "/", tried, ")"));
}

FF_TEST(test_ai_flagship, test_ai_without_counter_gets_hit) {
	int hits = 0, tried = 0;
	for (uint64_t s : {3u, 5u, 8u}) {
		const Play r = _play(s, 0.0, 9.0);
		if (r.poured) {
			++tried;
			if (r.hit) ++hits;
		}
	}
	check(tried > 0 && hits >= 1, S("with no reaction the wave connects (", hits, "/", tried, ")"));
}
