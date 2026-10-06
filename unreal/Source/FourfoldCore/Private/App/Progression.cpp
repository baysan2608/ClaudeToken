// Fourfold core - port of game/scenarios/progression.gd.
#include "App/Progression.h"

#include "ff/Json.h"
#include "App/Scenarios.h"
#include "Util/GdUtil.h"

#include <algorithm>

namespace ff {

namespace {
bool pg_has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

const std::vector<std::string>& Progression::ALL() {
	static const std::vector<std::string> v = {"magma", "heat_draw", "lightning", "redirect_current", "glide"};
	return v;
}
const std::vector<std::string>& Progression::LAB_OMIT() {
	static const std::vector<std::string> v = {"lightning"};
	return v;
}

void Progression::reset() {
	unlocked.clear();
	done.clear();
	lab_mode = false;
}

Dict Progression::kit() const {
	Dict k;
	for (const std::string& t : ALL())
		if ((lab_mode && !pg_has(LAB_OMIT(), t)) || pg_has(unlocked, t)) k.set(t, true);
	return k;
}

Dict Progression::lab_kit() {
	Dict k;
	for (const std::string& t : ALL())
		if (!pg_has(LAB_OMIT(), t)) k.set(t, true);
	return k;
}

bool Progression::is_done(const std::string& challenge_id) const { return pg_has(done, challenge_id); }
bool Progression::has_unlocked(const std::string& t) const { return pg_has(unlocked, t); }

bool Progression::complete(const std::string& challenge_id, const std::string& unlock) {
	if (!pg_has(done, challenge_id)) done.push_back(challenge_id);
	const bool fresh = !pg_has(unlocked, unlock);
	if (fresh) unlocked.push_back(unlock);
	return fresh;
}

std::string Progression::to_json() const {
	Array u, d;
	for (const std::string& s : unlocked) u.append(s);
	for (const std::string& s : done) d.append(s);
	return ToJson(Value(D({{"schema", 1}, {"unlocked", u}, {"done", d}, {"lab_mode", lab_mode}, {"last_scenario", last_scenario},
	                         {"spar_difficulty", spar_difficulty}, {"spar_kit", spar_kit}})),
	              2);
}

bool Progression::from_json(const std::string& text) {
	Value v;
	if (!ParseJson(text, v) || !v.is_dict()) return false;
	Progression p;
	for (const Value& x : v.get("unlocked").as_array())
		if (x.is_string() && !pg_has(p.unlocked, x.as_string())) p.unlocked.push_back(x.as_string());
	for (const Value& x : v.get("done").as_array())
		if (x.is_string() && !pg_has(p.done, x.as_string())) p.done.push_back(x.as_string());
	p.lab_mode = dbool(v, "lab_mode", false);
	p.last_scenario = dstr(v, "last_scenario", "molten_exchange");
	p.spar_difficulty = dstr(v, "spar_difficulty", "adept");
	p.spar_kit = dstr(v, "spar_kit", "mixed");
	if (!Scenarios::is_spar_difficulty(p.spar_difficulty)) p.spar_difficulty = "adept";
	if (!Scenarios::is_spar_kit(p.spar_kit) && p.spar_kit.find('/') == std::string::npos) p.spar_kit = "mixed";
	*this = p;
	return true;
}

}  // namespace ff
