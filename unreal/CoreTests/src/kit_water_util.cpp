// Fourfold core tests - port of game/tests/sim/test_kit_water_base.gd.
#include "kit_water_util.h"

#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

namespace fft {

using namespace ff;

namespace {
const char* const kSnapNames[5] = {"energy", "water", "earth", "metal", "plant"};
// VerbMotion emits the mode kind as the shape of its aura cue; VerbGrip emits the T+A shaping kind as its cast cue shape.
const char* const MODE_SHAPES[] = {"glide", "surf", "skate", "flight", "hover", "burrow", "run", "walk", "roots", "stone_skin", "iron",
                                   "anchor", "split", "freeze", "compress", "cool", "condense", "retag"};
}  // namespace

WaterCase::Snap WaterCase::snap(CombatWorld& w) {
	Snap s;
	s.v = {w.system_energy() - w.ledger_balance(), w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	return s;
}

void WaterCase::ledgers_ok(const Snap& base, const std::string& label, double eps) {
	const Snap s = snap(*h().w);
	for (size_t k = 0; k < 5; ++k) near(s.v[k], base.v[k], eps, label + ": " + kSnapNames[k] + " ledger");
}

std::pair<ActorState*, ActorState*> WaterCase::duel(int sub, int rival_element, uint64_t seed_value, double dist) {
	SimHarness& hh = H(seed_value);
	ActorState* w = hh.actor("W", V3(0, 0, 3.0 + dist * 0.5), 0, Dict(), Sim::WATER);
	ActorState* r = hh.actor("R", V3(0, 0, 3.0 - dist * 0.5), 1, Dict(), rival_element);
	w->subs[Sim::WATER] = sub;
	hh.step(20);
	hh.log.clear();
	return {w, r};
}

ActionRef WaterCase::run_move(ActorState* p, const std::string& id, int tier, int hold_ticks, int after) {
	SimHarness& hh = h();
	const Dict d = Moves::defs().get(id).as_dict();
	std::string slot = Moves::slot_of(dint(d, "element", 1), dint(d, "sub", 0), id);
	if (slot.empty()) slot = dstr(d, "slot", "strike");
	ActorIntent& it = hh.it(p);
	ActionRef inst = hh.w->start_action(*p, slot == "guard" ? "guard" : id, it,
	                                    D({{"slot", slot}, {"tier", tier}, {"charge_frozen", true}, {"spec", slot == "guard" ? id : std::string()}}));
	it.attack_held = true;
	it.guard_held = true;
	it.tech_held = true;
	it.evade_held = true;
	hh.step(hold_ticks);
	ActorIntent& it2 = hh.it(p);
	it2.attack_held = false;
	it2.guard_held = false;
	it2.tech_held = false;
	it2.evade_held = false;
	hh.step(after);
	return inst;
}

std::vector<BodyRef> WaterCase::bodies_of(Mat mat, int form) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->mat == mat && (form < 0 || static_cast<int>(b->form) == form)) out.push_back(b);
	return out;
}

std::vector<BodyRef> WaterCase::zones_tagged(const std::string& tag) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->form == Form::Zone && b->tag == tag) out.push_back(b);
	return out;
}

AgentRef WaterCase::threat(CombatWorld& w, const std::string& cls, double tp, double mass, const std::string& channel) {
	(void)w;
	AgentRef g = std::make_shared<Agent>();
	g->kind = "volume";
	g->cls = cls;
	g->ccls = cls;
	g->mass = mass;
	g->hostile = true;
	g->ch.set(channel, tp);
	return g;
}

void WaterCase::fx_catalogued(const std::string& label) {
	for (const Dict& e : h().log) {
		const std::string type = dstr(e, "type");
		if (type == "fx") {
			const std::string fx = ev_s(e, "fx"), mat = ev_s(e, "mat"), shape = ev_s(e, "shape");
			check(FxEvents::is_known("fx", fx), label + ": fx key '" + fx + "' catalogued");
			check(FxEvents::is_known("mat", mat), label + ": fx mat '" + mat + "' catalogued");
			check(FxEvents::is_known("shape", shape) || in_list(shape, MODE_SHAPES), label + ": fx shape '" + shape + "' catalogued");
		} else if (type == "interaction") {
			const std::string oc = ev_s(e, "outcome");
			check(FxEvents::is_known("outcome", oc), label + ": outcome '" + oc + "' catalogued");
		}
	}
}

bool WaterCase::mode_shape(const std::string& shape) { return in_list(shape, MODE_SHAPES); }

BodyRef WaterCase::keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : BodyRef(); }

}  // namespace fft
