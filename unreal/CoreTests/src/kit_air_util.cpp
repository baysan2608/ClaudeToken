// Fourfold core tests - port of game/tests/sim/test_kit_air_util.gd.
#include "kit_air_util.h"

#include "Sim/FxEvents.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace fft {

using namespace ff;

namespace {
const char* const kSnapNames[5] = {"energy", "water", "earth", "metal", "plant"};
const char* const MODE_SHAPES[] = {"glide", "surf", "skate", "flight", "hover", "burrow", "run", "walk", "roots", "stone_skin", "iron", "anchor",
                                   "split", "freeze", "compress", "cool", "condense", "retag", "ground", "down", "wind", "storm", "sound", "vacuum"};
bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}
}  // namespace

AirCase::Snap AirCase::snap(CombatWorld& w) {
	Snap s;
	s.v = {w.system_energy() - w.ledger_balance(), w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	return s;
}

void AirCase::ledgers_ok(const Snap& base, const std::string& label, double eps) {
	const Snap s = snap(*h().w);
	for (size_t k = 0; k < 5; ++k) near(s.v[k], base.v[k], eps, label + ": " + kSnapNames[k] + " ledger");
}

std::pair<ActorState*, ActorState*> AirCase::duel(int sub, int rival_element, uint64_t seed_value, double dist) {
	SimHarness& hh = H(seed_value);
	ActorState* a = hh.actor("A", V3(0, 0, 3.0 + dist * 0.5), 0, D({{"glide", true}}), Sim::AIR);
	ActorState* r = hh.actor("R", V3(0, 0, 3.0 - dist * 0.5), 1, Dict(), rival_element);
	r->is_dummy = true;
	a->subs[Sim::AIR] = sub;
	hh.step(20);
	hh.log.clear();
	return {a, r};
}

namespace {
std::string slot_for(const std::string& id) {
	const Dict d = Moves::defs().get(id).as_dict();
	std::string slot = Moves::slot_of(dint(d, "element", 3), dint(d, "sub", 0), id);
	if (slot.empty()) slot = dstr(d, "slot", "strike");
	return slot;
}
}  // namespace

ActionRef AirCase::run_move(ActorState* p, const std::string& id, int tier, int hold_ticks, int after) {
	SimHarness& hh = h();
	const std::string slot = slot_for(id);
	ActionRef inst = hh.w->start_action(*p, slot == "guard" ? "guard" : id, hh.it(p),
	                                    D({{"slot", slot}, {"tier", tier}, {"charge_frozen", true}, {"spec", slot == "guard" ? id : std::string()}}));
	ActorIntent& it = hh.it(p);
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

ActionRef AirCase::run_when(ActorState* p, const std::string& id, int tier, const std::function<void()>& on_ready, int hold, int after) {
	SimHarness& hh = h();
	const std::string slot = slot_for(id);
	ActionRef inst = hh.w->start_action(*p, slot == "guard" ? "guard" : id, hh.it(p),
	                                    D({{"slot", slot}, {"tier", tier}, {"charge_frozen", true}, {"spec", slot == "guard" ? id : std::string()}}));
	if (tier > 0) {
		ActorIntent& it = hh.it(p);
		it.attack_held = true;
		it.guard_held = true;
		it.tech_held = true;
		hh.step(hold);
	}
	if (on_ready) on_ready();
	ActorIntent& it2 = hh.it(p);
	it2.attack_held = false;
	it2.guard_held = false;
	it2.tech_held = false;
	hh.step(after);
	return inst;
}

std::vector<BodyRef> AirCase::bodies_of(Mat mat, int form) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->mat == mat && (form < 0 || static_cast<int>(b->form) == form)) out.push_back(b);
	return out;
}

std::vector<BodyRef> AirCase::bodies_tagged(const std::string& tag) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->tag == tag) out.push_back(b);
	return out;
}

std::vector<BodyRef> AirCase::zones_tagged(const std::string& tag) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->form == Form::Zone && b->tag == tag) out.push_back(b);
	return out;
}

AgentRef AirCase::threat(CombatWorld& w, const std::string& cls, double tp, double mass, const std::string& channel) {
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

BodyRef AirCase::lava_wave(double mass, Vec3 pos) {
	SimHarness& hh = h();
	MatBody* b = hh.w->spawn_body(Mat::Stone, Form::Wave, mass, pos, "test");
	hh.w->mass_ledger.ground_taken += mass;
	hh.w->ledger.generated += Thermal::heat(*b, mass * (Sim::STONE_C * 980.0 + Sim::STONE_LATENT));
	Thermal::update_phase(*b);
	b->wave_dir = V3(0, 0, 1);
	b->wave_budget = 12.0;
	b->vel = V3(0, 0, 7.5);
	b->attack_id = hh.w->new_attack_id();
	return keep(b);
}

void AirCase::fx_catalogued(const std::string& label) {
	for (const Dict& e : h().log) {
		const std::string type = dstr(e, "type");
		if (type == "fx") {
			const std::string fx = ev_s(e, "fx"), mat = ev_s(e, "mat"), shape = ev_s(e, "shape");
			check(FxEvents::is_known("fx", fx), label + ": fx key '" + fx + "' catalogued");
			check(FxEvents::is_known("mat", mat), label + ": fx mat '" + mat + "' catalogued");
			check(FxEvents::is_known("shape", shape) || mode_shape(shape), label + ": fx shape '" + shape + "' catalogued");
		} else if (type == "interaction") {
			const std::string oc = ev_s(e, "outcome");
			check(FxEvents::is_known("outcome", oc), label + ": outcome '" + oc + "' catalogued");
		}
	}
}

void AirCase::finite_world(const std::string& label) {
	for (const auto& ap : h().w->actors) check(finite3(ap->pos) && finite3(ap->vel), label + ": actor " + ap->name + " finite");
	for (const BodyRef& b : h().w->bodies)
		if (b->alive) check(finite3(b->pos) && finite3(b->vel) && std::isfinite(b->mass), label + ": body #" + itos(b->id) + " finite");
}

bool AirCase::mode_shape(const std::string& shape) { return in_list(shape, MODE_SHAPES); }

BodyRef AirCase::keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : BodyRef(); }

}  // namespace fft
