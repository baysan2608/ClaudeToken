// Fourfold core - port of game/scenarios/scenarios.gd.
#include "App/Scenarios.h"

#include "App/Progression.h"
#include "Combat/Moves.h"
#include "Data/GameData.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

#include <cctype>

namespace ff {
namespace Scenarios {

namespace {
std::vector<std::string> sc_strings(const char* key) {
	std::vector<std::string> out;
	for (const Value& v : GameData::scenarios().get(key).as_array()) out.push_back(v.as_string());
	return out;
}
bool sc_in(const std::vector<std::string>& v, const std::string& s) {
	for (const std::string& x : v)
		if (x == s) return true;
	return false;
}
const char* const kElementKeys[4] = {"earth", "water", "fire", "air"};
}  // namespace

const Array& LIST() {
	static const Array list = GameData::scenarios().get("list").as_array();
	return list;
}

Dict get_def(const std::string& id) {
	for (const Value& s : LIST())
		if (dstr(s, "id") == id) return s.as_dict();
	return LIST().empty() ? Dict() : LIST()[0].as_dict();
}

bool has(const std::string& id) {
	for (const Value& s : LIST())
		if (dstr(s, "id") == id) return true;
	return false;
}

std::vector<std::string> spar_difficulties() { return sc_strings("spar_difficulties"); }
std::vector<std::string> spar_difficulty_labels() { return sc_strings("spar_difficulty_labels"); }
std::vector<std::string> spar_kits() { return sc_strings("spar_kits"); }
std::vector<std::string> spar_kit_labels() { return sc_strings("spar_kit_labels"); }
bool is_spar_difficulty(const std::string& s) { return sc_in(spar_difficulties(), s); }
bool is_spar_kit(const std::string& s) { return sc_in(spar_kits(), s); }

std::vector<PracticeItem> practice_items(const Progression& progress) {
	std::vector<PracticeItem> out;
	const Dict& techs = Moves::techniques();
	for (const Value& sv : LIST()) {
		const Dict s = sv.as_dict();
		std::string sub = dstr(s, "subtitle");
		const Dict ch = ddict(s, "challenge");
		if (!ch.empty()) {
			const bool done = progress.is_done(dstr(ch, "id"));
			std::string tech = dstr(techs, dstr(ch, "unlock"), dstr(ch, "unlock"));
			const size_t colon = tech.find(':');
			if (colon != std::string::npos) tech = tech.substr(0, colon);
			sub = std::string(done ? "\xE2\x9C\x93 " : "\xE2\x97\x87 ") + dstr(ch, "text") + " \xE2\x86\x92 unlocks " + tech + ". " + sub;
		}
		PracticeItem item;
		item.id = dstr(s, "id");
		item.title = dstr(s, "group") + " \xC2\xB7 " + dstr(s, "title");
		item.subtitle = sub;
		item.locked = false;
		if (dbool(s, "spar", false)) {
			PracticeOption d;
			d.key = "spar_difficulty";
			d.label = "Rival";
			d.values = spar_difficulties();
			d.labels = spar_difficulty_labels();
			d.value = progress.spar_difficulty;
			PracticeOption k;
			k.key = "spar_kit";
			k.label = "Kit";
			k.values = spar_kits();
			k.labels = spar_kit_labels();
			k.value = progress.spar_kit;
			item.options = {d, k};
		}
		out.push_back(item);
	}
	PracticeItem lab;
	lab.id = "__lab_mode";
	lab.title = std::string("Testing \xC2\xB7 Lab mode ") + (progress.lab_mode ? "ON" : "OFF");
	lab.subtitle = "Toggle: every technique unlocked in Free Spar (for testing; progress is kept)";
	out.push_back(lab);
	return out;
}

Built build(const std::string& id, const Progression& progress, uint64_t seed_value) {
	Built r;
	const Dict d = get_def(id);
	r.def = d;
	r.world = std::make_unique<CombatWorld>(seed_value);
	CombatWorld& w = *r.world;
	const Dict pd = ddict(d, "player");
	Dict kit;
	const Value& pkit = pd.get("kit");
	if (pkit.is_string() && pkit.as_string() == "all") {
		kit = Progression::lab_kit();
	} else if (pkit.is_string()) {
		kit = progress.kit();
	} else {
		kit = pkit.as_dict().duplicate();
		kit.merge(progress.kit(), false);
	}
	const Vec3 ppos = dvec(pd, "pos", w.arena.player_spawn);
	ActorState* p = w.add_actor("You", ppos, 0, kit, dint(pd, "element"));
	p->facing = kPi;
	r.player = p;
	if (d.has("opponent")) {
		const Dict od = ddict(d, "opponent");
		Dict okit;
		const Value& ok = od.get("kit");
		if (ok.is_string() && ok.as_string() == "all") {
			for (const std::string& t : Progression::ALL()) okit.set(t, true);
		} else {
			okit = ok.as_dict();
		}
		Array elements = darr(od, "elements");
		int spar_sub = -1;
		if (dbool(d, "spar", false)) {
			const Dict sk = spar_kit(progress.spar_kit);
			elements = darr(sk, "elements");
			spar_sub = dint(sk, "sub");
		}
		ActorState* o = w.add_actor("Rival", dvec(od, "pos", w.arena.opponent_spawn), 1, okit, vint(elements.get(0)));
		o->elements = {false, false, false, false};
		for (const Value& e : elements) o->elements[static_cast<size_t>(clampi(vint(e), 0, 3))] = true;
		r.ai_cfg = ddict(od, "ai").duplicate();
		r.ai_cfg.set("elements", elements);
		if (dbool(d, "spar", false)) {
			r.ai_cfg.set("preset", progress.spar_difficulty);
			Dict subs;
			if (spar_sub >= 0) subs.set(itos(vint(elements.get(0))), A({spar_sub}));
			r.ai_cfg.set("subs", subs);
		}
		if (dbool(d, "lab", false)) {
			r.ai_cfg.set("preset", "adept");
			r.ai_cfg.set("elements", elements);
		}
		r.opponent = o;
	}
	int k = 0;
	for (const Value& dp : darr(d, "dummies")) {
		ActorState* dm = w.add_actor("Target " + itos(k + 1), dp.as_vec3(), 1, Dict(), 0);
		dm->is_dummy = true;
		dm->facing = 0.0;
		++k;
	}
	for (const Value& sp : darr(d, "stones")) {
		MatBody* b = w.spawn_body(Mat::Stone, Form::Chunk, Sim::STONE_SHOT_MASS, sp.as_vec3() + V3(0, 0.25, 0), "scenario");
		b->on_ground = true;
	}
	for (const Value& pdl : darr(d, "puddles")) {
		MatBody* b2 = w.spawn_body(Mat::Water, Form::Puddle, dnum(pdl, "mass"), dvec(pdl.as_dict(), "pos"), "scenario");
		b2->update_radius_puddle();
	}
	for (const Value& lpv : darr(d, "lava")) {
		const Vec3 lp = lpv.as_vec3();
		MatBody* b3 = w.spawn_body(Mat::Stone, Form::Blob, 25.0, lp + V3(0, 0.1, 0), "vent", Sim::STONE_MELT_C);
		b3->liquid = 1.0;
		b3->phase = Phase::Molten;
		b3->on_ground = true;
		b3->wave_path = {lp + V3(-0.6, 0, 0), lp + V3(0.6, 0, 0.2)};
	}
	w.take_events();
	r.launcher = ddict(d, "launcher");
	return r;
}

Dict spar_kit(const std::string& key) {
	if (key == "mixed") return D({{"elements", A({0, 2})}, {"sub", -1}});
	if (key == "all") return D({{"elements", A({0, 1, 2, 3})}, {"sub", -1}});
	const size_t slash = key.find('/');
	const std::string head = key.substr(0, slash);
	int e = -1;
	for (int i = 0; i < 4; ++i)
		if (head == kElementKeys[i]) e = i;
	if (e < 0) return D({{"elements", A({0, 2})}, {"sub", -1}});
	int sb = -1;
	if (slash != std::string::npos) {
		const std::string tail = key.substr(slash + 1);
		bool is_int = !tail.empty();
		for (char c : tail)
			if (!std::isdigit(static_cast<unsigned char>(c))) is_int = false;
		if (is_int) {
			sb = clampi(to_int(tail), 0, 3);
		} else {
			std::string cap = tail;
			for (size_t i = 0; i < cap.size(); ++i)
				cap[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(cap[i])) : std::tolower(static_cast<unsigned char>(cap[i])));
			for (int s = 0; s < 4; ++s)
				if (SubName(e, s) == cap) sb = s;
		}
	}
	return D({{"elements", A({e})}, {"sub", sb}});
}

}  // namespace Scenarios
}  // namespace ff
