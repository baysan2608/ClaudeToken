// Fourfold core - port of game/ui/lab/move_list_data.gd.
#include "Lab/MoveListData.h"

#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Sim/Sim.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace MoveListData {

namespace {
const char* const kSlotsMl[10] = {"strike", "thrust", "ground", "sweep", "guard", "push", "sink", "tech", "evade", "evade_hold"};
const char* const kTouchMl[10] = {"ATTACK tap (hold = charge)", "ATTACK flick up", "ATTACK flick down", "ATTACK flick left / right",
                                  "GUARD hold (just before contact = perfect)", "GUARD flick up", "GUARD flick down",
                                  "TECH hold, drag, lift (ATTACK tap = shape)", "EVADE tap", "EVADE hold"};
const char* const kKeysMl[10] = {"J (hold = charge)", "U", "N", "H", "K (tap before contact = perfect)", "K + J", "K + N", "L hold (J = shape)",
                                 "Space", "Space hold"};
const char* const kPadMl[10] = {"X (hold = charge)", "Y", "LT", "B", "RB (tap before contact = perfect)", "RB + X", "RB + LT", "RT hold (X = shape)",
                                "A", "A hold"};
const char* const kKeyOrderMl[] = {"count", "mass", "damage", "power", "speed", "radius", "range", "width", "budget", "pieces", "heat_add",
                                   "cost_add", "balance"};

std::string ml_n(double v) { return absf(v - std::round(v)) < 0.05 ? itos(static_cast<int64_t>(v)) : ftos(v, 1); }
bool ml_in_order(const std::string& k) {
	for (const char* x : kKeyOrderMl)
		if (k == x) return true;
	return false;
}
std::vector<std::string> ml_split_names(const std::string& s) {
	std::vector<std::string> out;
	size_t p = 0;
	while (true) {
		const size_t q = s.find(" / ", p);
		out.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 3;
	}
	return out;
}
std::string ml_underscores(std::string s) {
	for (char& c : s)
		if (c == '_') c = ' ';
	return s;
}
}  // namespace

std::string input_text(const std::string& slot, const std::string& device) {
	const char* const* table = device == "touch" ? kTouchMl : (device == "gamepad" ? kPadMl : kKeysMl);
	for (int i = 0; i < 10; ++i)
		if (slot == kSlotsMl[i]) return table[i];
	return slot;
}

std::string cost_text(const Dict& def) {
	std::vector<std::string> bits;
	if (dnum(def, "cost", 0.0) > 0.0) bits.push_back("Focus " + ml_n(dnum(def, "cost")));
	if (dnum(def, "heat", 0.0) > 0.0) bits.push_back("heat " + ml_n(dnum(def, "heat")) + " HU");
	else if (dnum(def, "cost_hu", 0.0) > 0.0) bits.push_back("heat " + ml_n(dnum(def, "cost_hu")) + " HU");
	if (dnum(def, "water", 0.0) > 0.0) bits.push_back("water " + ml_n(dnum(def, "water")) + " kg");
	if (dnum(def, "metal", 0.0) > 0.0) bits.push_back("metal " + ml_n(dnum(def, "metal")) + " kg");
	if (bits.empty()) return "free";
	std::string out;
	for (size_t i = 0; i < bits.size(); ++i) out += (i ? " / " : "") + bits[i];
	return out;
}

std::string frames_text(const Dict& def) {
	auto fr = [&](const char* k) { return itos(static_cast<int64_t>(std::round(dnum(def, k, 0.0) * Sim::HZ))); };
	return "S" + fr("startup") + " A" + fr("active") + " R" + fr("recovery");
}

std::vector<Dict> tier_rows(const Dict& def) {
	std::vector<Dict> out;
	const int mt = Charge::max_tier(def);
	if (mt <= 0) return out;
	const auto times = Charge::tier_times(def);
	const std::vector<std::string> names = ml_split_names(dstr(def, "name", ""));
	const Dict tiers = ddict(def, "tiers");
	for (int t = 1; t <= mt; ++t) {
		const Dict td = ddict(tiers, "t" + itos(t));
		std::vector<std::string> bits;
		for (const char* k : kKeyOrderMl)
			if (td.has(k) && td.get(k).is_number()) bits.push_back(ml_underscores(k) + " " + ml_n(dnum(td, k)));
		for (const auto& kv : td) {
			if (bits.size() >= 5) break;
			if (!ml_in_order(kv.first) && kv.second.is_number()) bits.push_back(ml_underscores(kv.first) + " " + ml_n(kv.second.as_float()));
			else if (!ml_in_order(kv.first) && kv.second.is_bool() && kv.second.as_bool()) bits.push_back(ml_underscores(kv.first));
		}
		const double cp = Charge::counter_power(def, t);
		if (cp >= 0.0) bits.push_back("counter " + ml_n(cp));
		std::string text;
		for (size_t i = 0; i < bits.size(); ++i) text += (i ? ", " : "") + bits[i];
		out.push_back(D({{"tier", t}, {"hold", times[static_cast<size_t>(t - 1)]}, {"name", names.size() > static_cast<size_t>(t) ? names[static_cast<size_t>(t)] : ""},
		                 {"text", text}}));
	}
	return out;
}

std::vector<Dict> rows(int element, int sub, const std::string& device) {
	Moves::ensure_ready();
	std::vector<Dict> out;
	for (const char* slot : kSlotsMl) {
		const std::string id = Moves::resolve(element, sub, slot);
		if (id.empty() || !Moves::defs().has(id)) continue;
		const Dict def = Moves::defs().get(id).as_dict();
		const std::vector<std::string> names = ml_split_names(dstr(def, "name", id));
		const Dict counter = ddict(def, "counter");
		std::string ctext;
		if (counter.has("cls")) {
			const Value& p = counter.get("power");
			ctext = "counters as " + dstr(counter, "cls");
			if (p.is_array() && !p.as_array().empty()) {
				std::string ps;
				for (const Value& x : p.as_array()) ps += (ps.empty() ? "" : ", ") + ml_n(vnum(x));
				ctext += " (power T0-T3: " + ps + ")";
			} else if (!p.is_nil()) {
				ctext += " (power " + ml_n(vnum(p)) + ")";
			}
		}
		Array tn;
		for (const std::string& n : names) tn.append(n);
		Array tr;
		for (const Dict& r : tier_rows(def)) tr.append(r);
		out.push_back(D({{"slot", slot}, {"id", id}, {"name", names[0]}, {"tier_names", tn}, {"desc", dstr(def, "desc", "")},
		                 {"input", input_text(slot, device)}, {"cost", cost_text(def)}, {"frames", frames_text(def)}, {"tiers", tr},
		                 {"counter", ctext}, {"max_tier", Charge::max_tier(def)}, {"role", dstr(ddict(def, "ai"), "role", "")}}));
	}
	return out;
}

int count_all(const std::string& device) {
	int n = 0;
	for (int e = 0; e < 4; ++e)
		for (int s = 0; s < 4; ++s) n += static_cast<int>(rows(e, s, device).size());
	return n;
}

MoveInfo move_info(const std::string& id) {
	MoveInfo m;
	if (!Moves::defs().has(id)) return m;
	const Dict def = Moves::defs().get(id).as_dict();
	m.id = id;
	m.name = ml_split_names(dstr(def, "name", id))[0];
	m.desc = dstr(def, "desc", "");
	m.element = dint(def, "element", 0);
	m.sub = dint(def, "sub", 0);
	m.slot = SlotFromName(dstr(def, "slot", ""));
	m.verb = dstr(def, "verb", "");
	m.startup = static_cast<float>(dnum(def, "startup"));
	m.active = static_cast<float>(dnum(def, "active"));
	m.recovery = static_cast<float>(dnum(def, "recovery"));
	m.cost_focus = static_cast<float>(dnum(def, "cost"));
	m.cost_heat = static_cast<float>(dnum(def, "heat", 0.0) > 0.0 ? dnum(def, "heat") : dnum(def, "cost_hu"));
	m.cost_water = static_cast<float>(dnum(def, "water"));
	m.cost_metal = static_cast<float>(dnum(def, "metal", dnum(def, "metal_cost")));
	m.max_tier = Charge::max_tier(def);
	const auto times = Charge::tier_times(def);
	for (int t = 0; t < m.max_tier && t < 3; ++t) m.tier_times.push_back(static_cast<float>(times[static_cast<size_t>(t)]));
	m.legacy = Moves::is_base(id);
	m.counter_cls = dstr(ddict(def, "counter"), "cls", "");
	m.threat_cls = dstr(ddict(def, "threat"), "cls", "");
	return m;
}

}  // namespace MoveListData
}  // namespace ff
