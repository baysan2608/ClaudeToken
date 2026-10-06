// Fourfold core - port of game/ui/lab/matrix_query.gd.
#include "Lab/MatrixQuery.h"

#include "Combat/Moves.h"
#include "Lab/SpawnCatalog.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <algorithm>

namespace ff {
namespace MatrixQuery {

namespace {
const char* const kEnvCounters[] = {"pool", "puddle", "plate", "arena_wall", "ground"};
const char* const kLegacyGuards[][2] = {{"wall_stone", "Earth: Bulwark stone wall (120 kg)"},
                                        {"guard_earth", "Earth guard (plain)"},
                                        {"shield_water", "Water shield (held, 6 kg)"},
                                        {"aura_flame", "Flame guard (aura)"},
                                        {"guard_wind", "Wind guard"}};
std::string mq_first_name(const Dict& def, const std::string& id) {
	std::string nm = dstr(def, "name", id);
	const size_t sep = nm.find(" / ");
	return sep == std::string::npos ? nm : nm.substr(0, sep);
}
}  // namespace

const std::vector<Dict>& counters() {
	static std::vector<Dict> out;
	static bool built = false;
	if (built) return out;
	built = true;
	Moves::ensure_ready();
	for (const auto& g : kLegacyGuards)
		out.push_back(D({{"id", g[0]}, {"label", g[1]}, {"kind", "legacy"}, {"max_tier", 0}, {"element", -1}, {"sub", 0}, {"slot", "guard"}}));
	std::vector<std::string> seen;
	for (int e = 0; e < 4; ++e) {
		for (int s = 0; s < 4; ++s) {
			for (const char* slot : Sim::SLOTS) {
				const std::string id = Moves::resolve(e, s, slot);
				if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end() || !Moves::defs().has(id)) continue;
				const Dict def = Moves::defs().get(id).as_dict();
				const Dict c = ddict(def, "counter");
				if (!(c.has("cls") || dstr(def, "verb", "") == "barrier")) continue;
				seen.push_back(id);
				out.push_back(D({{"id", id},
				                 {"label", std::string(ElementName(e)) + " / " + std::string(SubName(e, s)) + ": " + mq_first_name(def, id)},
				                 {"kind", "move"},
				                 {"max_tier", Charge::max_tier(def)},
				                 {"element", e},
				                 {"sub", s},
				                 {"slot", slot}}));
			}
		}
	}
	for (const char* k : kEnvCounters)
		out.push_back(D({{"id", k}, {"label", std::string("Environment: ") + k}, {"kind", "env"}, {"max_tier", 0}, {"element", -1}, {"sub", 0}, {"slot", ""}}));
	return out;
}

Dict find_counter(const std::string& id) {
	for (const Dict& c : counters())
		if (dstr(c, "id") == id) return c;
	return Dict();
}

Threat make_threat(const std::string& entry_id, const Dict& params) {
	Threat t;
	const Dict e = SpawnCatalog::find(entry_id);
	if (e.empty()) return t;
	t.world = std::make_unique<CombatWorld>(1);
	CombatWorld& sw = *t.world;
	ActorState* p = sw.add_actor("You", Vec3(0, 0, 7), 0, Dict(), 0);
	ActorState* o = sw.add_actor("Rival", Vec3(0, 0, -7), 1, Dict(), 0);
	for (auto& a : sw.actors) a->elements = {true, true, true, true};
	t.player = p;
	t.rival = o;
	if (dstr(e, "build") == "perform") {
		const std::string id = Moves::resolve(dint(e, "element"), dint(e, "sub"), dstr(e, "slot"));
		const Dict def = Moves::defs().get(id).as_dict();
		const int tier = dint(params, "tier", vint(e.get("tier").as_array().get(0), 1));
		const std::string cls = dstr(ddict(def, "threat"), "cls", dstr(ddict(def, "counter"), "cls", "blast"));
		double power = Charge::counter_power(def, tier);
		if (power < 0.0) power = 10.0;
		const std::string chn = Interactions::class_channel(cls, "P");
		AgentRef ag = Agent::of_volume(&sw, o, nullptr, cls, o->hand_point(), (p->chest() - o->hand_point()).normalized(), D({{chn, power}}));
		ag->tier = tier;
		t.agent = ag;
		t.summary = mq_first_name(def, id) + " T" + itos(tier) + ", " + chn + " power " + ftos(power, 0);
		t.ok = true;
		return t;
	}
	SpawnCatalog::SpawnResult r = SpawnCatalog::spawn(sw, entry_id, params, *p, o, p->pos + p->forward() * 5.0, true);
	if (!r.ok || r.bodies.empty()) {
		t.world.reset();
		return t;
	}
	MatBody* b = r.bodies[0];
	AgentRef ag2 = Agent::of_body(sw, *b, p);
	ag2->hostile = true;
	t.agent = ag2;
	t.body = b;
	t.summary = dstr(e, "label") + ": " + ag2->cls + " " + ftos(b->mass, 0) + " kg at " + ftos(b->vel.length(), 1) + " m/s, " + ftos(ag2->heat, 0) + " HU";
	t.ok = true;
	return t;
}

AgentRef make_counter(CombatWorld& sw, ActorState& p, const Dict& c, int tier, bool perfect) {
	const std::string id = dstr(c, "id");
	if (id == "wall_stone") {
		MatBody* wall = sw.spawn_body(Mat::Stone, Form::Wall, Sim::WALL_MASS, p.pos + p.forward() * 2.0, "lab");
		wall->static_body = true;
		wall->wall_rise = 1.0;
		AgentRef wg = Agent::of_body(sw, *wall);
		wg->perfect = perfect;
		wg->hostile = false;
		return wg;
	}
	const std::string kind = dstr(c, "kind");
	if (kind == "env") {
		AgentRef g = Agent::of_env(&sw, id, p.pos);
		g->perfect = false;
		return g;
	}
	if (kind == "legacy") {
		AgentRef ag = std::make_shared<Agent>();
		ag->kind = "guard";
		ag->actor = &p;
		ag->perfect = perfect;
		ag->pos = p.chest();
		ag->dir = p.forward();
		ag->ccls = id;
		ag->cls = ag->ccls;
		ag->tier = tier;
		if (id == "shield_water") ag->power = 6.0 * 1.0;
		else if (id == "guard_wind") ag->power = Interactions::WIND_GUARD_CP;
		else ag->power = Interactions::PLAIN_GUARD_CP;
		return ag;
	}
	return Agent::of_move(&sw, &p, id, tier, perfect);
}

Dict predict(const std::string& entry_id, const Dict& threat_params, const std::string& counter_id, int counter_tier, bool perfect) {
	Threat t = make_threat(entry_id, threat_params);
	if (!t.ok) return D({{"ok", false}, {"msg", "this threat cannot be built"}});
	const Dict c = find_counter(counter_id);
	if (c.empty()) return D({{"ok", false}, {"msg", "unknown counter"}});
	CombatWorld& sw = *t.world;
	AgentRef cg = make_counter(sw, *t.player, c, counter_tier, perfect);
	const IxResult res = Interactions::predict(&sw, *t.agent, *cg);
	const double cp_eff = res.cp_eff;
	const double tp = res.tp;
	const double mult = cp_eff / maxf(res.cp, 1e-6);
	const double needs = tp / maxf(mult, 1e-6);
	return D({{"ok", true}, {"msg", ""}, {"tp", tp}, {"cp", res.cp}, {"cp_eff", cp_eff}, {"ratio", res.ratio}, {"band", res.band}, {"perfect", perfect},
	          {"outcome", res.outcome}, {"rule_id", res.rule_id}, {"to", res.to}, {"threat_cls", t.agent->cls}, {"counter_cls", cg->ccls},
	          {"needs", needs}, {"summary", t.summary}});
}

MatrixCounter to_view(const Dict& c) {
	MatrixCounter v;
	v.id = dstr(c, "id");
	v.label = dstr(c, "label");
	v.kind = dstr(c, "kind");
	v.max_tier = dint(c, "max_tier");
	v.element = dint(c, "element", -1);
	v.sub = dint(c, "sub");
	v.slot = SlotFromName(dstr(c, "slot"));
	return v;
}

MatrixResult result_view(const Dict& r) {
	MatrixResult v;
	v.ok = dbool(r, "ok");
	v.msg = dstr(r, "msg");
	v.tp = static_cast<float>(dnum(r, "tp"));
	v.cp = static_cast<float>(dnum(r, "cp"));
	v.cp_eff = static_cast<float>(dnum(r, "cp_eff"));
	v.ratio = static_cast<float>(dnum(r, "ratio"));
	v.needs = static_cast<float>(dnum(r, "needs"));
	v.perfect = dbool(r, "perfect");
	v.band = dstr(r, "band");
	v.outcome = dstr(r, "outcome");
	v.rule_id = dstr(r, "rule_id");
	v.to = dstr(r, "to");
	v.threat_cls = dstr(r, "threat_cls");
	v.counter_cls = dstr(r, "counter_cls");
	v.summary = dstr(r, "summary");
	return v;
}

}  // namespace MatrixQuery
}  // namespace ff
