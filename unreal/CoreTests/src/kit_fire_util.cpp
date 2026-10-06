// Fourfold core tests - port of game/tests/sim/test_kit_fire_util.gd.
#include "kit_fire_util.h"

#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <set>

namespace fft {

using namespace ff;

namespace {
const char* const kSnapNames[5] = {"energy", "water", "earth", "metal", "plant"};
const char* const MODE_SHAPES[] = {"glide", "surf", "skate", "flight", "hover", "burrow", "run", "walk", "grounding", "stance"};
}  // namespace

FireCase::Snap FireCase::snap(CombatWorld& w) {
	Snap s;
	s.v = {w.system_energy() - w.ledger_balance(), w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	return s;
}

bool FireCase::ledgers_ok(const Snap& base, const std::string& label, double eps) {
	const Snap s = snap(*h().w);
	bool ok = true;
	for (size_t k = 0; k < 5; ++k) ok = near(s.v[k], base.v[k], eps, label + ": " + kSnapNames[k] + " ledger") && ok;
	return ok;
}

std::pair<ActorState*, ActorState*> FireCase::duel(int sub, int rival_element, uint64_t seed_value, double dist, const Dict& kit) {
	SimHarness& hh = H(seed_value);
	ActorState* f = hh.actor("F", V3(0, 0, 3.0 + dist * 0.5), 0, kit, Sim::FIRE);
	ActorState* r = hh.actor("R", V3(0, 0, 3.0 - dist * 0.5), 1, Dict(), rival_element);
	f->subs[Sim::FIRE] = sub;
	hh.step(20);
	hh.log.clear();
	return {f, r};
}

ActionRef FireCase::run_move(ActorState* p, const std::string& id, int tier, int hold_ticks, int after) {
	SimHarness& hh = h();
	const Dict d = Moves::defs().get(id).as_dict();
	const std::string slot = dstr(d, "slot", "strike");
	ActorIntent& it = hh.it(p);
	const bool attack = Sim::is_attack_slot(slot);
	ActionRef inst = hh.w->start_action(*p, slot == "guard" ? "guard" : id, it,
	                                    D({{"slot", slot}, {"tier", tier}, {"charge_frozen", true}, {"released", tier > 0 && attack},
	                                       {"spec", slot == "guard" ? id : std::string()}}));
	if (inst != nullptr && tier > 0 && attack && inst->phase == ActionPhase::Startup) inst->data.set("tier", tier);
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

std::vector<BodyRef> FireCase::bodies_of(Mat mat, int form) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->mat == mat && (form < 0 || static_cast<int>(b->form) == form)) out.push_back(b);
	return out;
}

std::vector<BodyRef> FireCase::zones_tagged(const std::string& tag) {
	std::vector<BodyRef> out;
	for (const BodyRef& b : h().w->bodies)
		if (b->alive && b->form == Form::Zone && b->tag == tag) out.push_back(b);
	return out;
}

std::vector<Dict> FireCase::events_of(const std::string& type, const std::string& key, const Value& value) {
	return h().filter(type, [&](const Dict& e) { return e.get(key) == value; });
}

AgentRef FireCase::threat(CombatWorld& w, const std::string& cls, double tp, double mass, const std::string& channel) {
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

MatBody* FireCase::bulwark(ActorState* owner, Vec3 p, double yaw, double mass) {
	SimHarness& hh = h();
	MatBody* b = hh.w->spawn_body(Mat::Stone, Form::Wall, mass, p, "test_wall");
	hh.w->mass_ledger.ground_taken += mass;
	b->wall_yaw = yaw;
	b->wall_half = V3(1.1, 0.75, 0.28);
	b->wall_rise = 1.0;
	b->static_body = true;
	b->props.set("standing", 30.0);
	b->touch(owner != nullptr ? owner->id : -1, "wall", hh.w->tick);
	return b;
}

void FireCase::fx_catalogued(const std::string& label) {
	std::set<std::string> bad;
	for (const Dict& e : h().log) {
		const std::string type = dstr(e, "type");
		if (type == "fx") {
			if (!FxEvents::is_known("fx", dstr(e, "fx"))) bad.insert("fx:" + dstr(e, "fx"));
			if (!FxEvents::is_known("mat", dstr(e, "mat"))) bad.insert("mat:" + dstr(e, "mat"));
			const std::string shape = dstr(e, "shape");
			if (!(FxEvents::is_known("shape", shape) || in_list(shape, MODE_SHAPES))) bad.insert("shape:" + shape);
		} else if (type == "interaction") {
			if (!FxEvents::is_known("outcome", dstr(e, "outcome"))) bad.insert("outcome:" + dstr(e, "outcome"));
		}
	}
	std::string keys;
	for (const std::string& k : bad) keys += k + " ";
	check(bad.empty(), label + ": uncatalogued keys " + keys);
}

}  // namespace fft
