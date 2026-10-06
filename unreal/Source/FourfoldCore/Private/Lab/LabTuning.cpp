// Fourfold core - port of game/ui/lab/lab_tuning.gd.
#include "Lab/LabTuning.h"

#include "ff/Json.h"
#include "Combat/Moves.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <map>

namespace ff {
namespace LabTuning {

namespace {
struct TuningState {
	std::map<std::string, std::map<std::string, double>> move_changes;   // id -> path -> value
	std::map<std::string, std::map<std::string, double>> rule_changes;   // "key#idx" -> field -> value
	std::map<std::string, Dict> rule_orig;                               // "key#idx" -> original rule copy
};
TuningState& lt_state() {
	static TuningState s;
	return s;
}
bool lt_is_num(const Value& v) { return v.is_int() || v.is_float(); }
std::vector<std::string> lt_split3(const std::string& path) {
	std::vector<std::string> out;
	size_t p = 0;
	while (true) {
		const size_t q = path.find('.', p);
		out.push_back(path.substr(p, q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 1;
	}
	return out;
}
Dict lt_rule_ref(const std::string& key, int idx) {
	const auto& all = Interactions::all_rules();
	auto it = all.find(key);
	if (it == all.end() || idx < 0 || static_cast<size_t>(idx) >= it->second.size()) return Dict();
	return it->second[static_cast<size_t>(idx)];   // shared handle: edits apply in place
}
std::string lt_fmt(const Value& v) {
	if (v.is_nil()) return "-";
	return Value(snappedf(vnum(v), 0.0001)).to_string();
}
}  // namespace

std::vector<Field> numeric_fields(const std::string& id) {
	std::vector<Field> out;
	const Dict def = Moves::defs().get(id).as_dict();
	std::vector<std::string> keys = def.keys();
	std::sort(keys.begin(), keys.end());
	for (const std::string& k : keys) {
		if (k == "tiers" || k == "id") continue;
		const Value& v = def.get(k);
		if (lt_is_num(v)) out.push_back({k, v.as_float(), -1});
	}
	const Dict tiers = ddict(def, "tiers");
	for (const char* t : {"t1", "t2", "t3"}) {
		if (!tiers.has(t)) continue;
		const Dict td = tiers.get(t).as_dict();
		std::vector<std::string> tk = td.keys();
		std::sort(tk.begin(), tk.end());
		for (const std::string& k : tk) {
			const Value& v2 = td.get(k);
			if (lt_is_num(v2)) out.push_back({std::string("tiers.") + t + "." + k, v2.as_float(), t[1] - '0'});
		}
	}
	return out;
}

Value get_value(const std::string& id, const std::string& path) {
	const Dict def = Moves::defs().get(id).as_dict();
	if (begins_with(path, "tiers.")) {
		const auto parts = lt_split3(path);
		if (parts.size() != 3) return Value();
		return ddict(ddict(def, "tiers"), parts[1]).get(parts[2]);
	}
	return def.get(path);
}

bool set_value(const std::string& id, const std::string& path, double value) {
	if (!Moves::defs().has(id)) return false;
	const Dict def = Moves::defs().get(id).as_dict();
	if (begins_with(path, "tiers.")) {
		const auto parts = lt_split3(path);
		if (parts.size() != 3) return false;
		Dict tiers = ddict(def, "tiers").duplicate(true);
		if (!tiers.has(parts[1]) || !tiers.get(parts[1]).as_dict().has(parts[2])) return false;
		Dict td = tiers.get(parts[1]).as_dict();
		td.set(parts[2], value);
		Moves::set_override(id, "tiers", tiers);
	} else {
		if (!def.has(path)) return false;
		Moves::set_override(id, path, value);
	}
	lt_state().move_changes[id][path] = value;
	return true;
}

Value original_value(const std::string& id, const std::string& path) {
	const Dict o = ddict(Moves::originals(), id);
	if (o.empty()) return get_value(id, path);
	if (begins_with(path, "tiers.")) {
		const auto parts = lt_split3(path);
		if (o.has("tiers") && o.get("tiers").is_dict() && parts.size() == 3) return ddict(ddict(o, "tiers"), parts[1]).get(parts[2]);
		return get_value(id, path);
	}
	if (o.has(path)) return o.get(path);
	return get_value(id, path);
}

bool is_changed(const std::string& id, const std::string& path) {
	auto it = lt_state().move_changes.find(id);
	return it != lt_state().move_changes.end() && it->second.count(path) > 0;
}

int change_count() {
	int n = 0;
	for (const auto& kv : lt_state().move_changes) n += static_cast<int>(kv.second.size());
	for (const auto& kv : lt_state().rule_changes) n += static_cast<int>(kv.second.size());
	return n;
}

std::vector<RuleCell> rule_cells() {
	std::vector<RuleCell> out;
	const auto& all = Interactions::all_rules();
	std::vector<std::string> keys;
	for (const auto& kv : all) keys.push_back(kv.first);
	std::sort(keys.begin(), keys.end());
	for (const std::string& k : keys) {
		const auto& lst = all.at(k);
		for (size_t i = 0; i < lst.size(); ++i)
			out.push_back({k, static_cast<int>(i), dstr(lst[i], "id", ""), k + (lst.size() == 1 ? std::string() : " #" + itos(static_cast<int64_t>(i)))});
	}
	return out;
}

std::vector<Field> rule_fields(const std::string& key, int idx) {
	std::vector<Field> out;
	const Dict r = lt_rule_ref(key, idx);
	std::vector<std::string> ks = r.keys();
	std::sort(ks.begin(), ks.end());
	for (const std::string& k : ks) {
		const Value& v = r.get(k);
		if (lt_is_num(v)) {
			out.push_back({k, v.as_float(), -1});
		} else if (k == "w" && v.is_dict()) {
			for (const char* c : {"K", "H", "C", "E", "P"})
				if (v.as_dict().has(c)) out.push_back({std::string("w.") + c, dnum(v, c), -1});
		}
	}
	// The common thresholds are always offered, even when the cell relies on the defaults.
	const std::pair<const char*, double> dflts[] = {{"eff", 1.0}, {"full_at", 1.0}, {"partial_at", 0.5}, {"perfect_mult", 1.5}, {"absorb_on_fail", 0.5}};
	for (const auto& d : dflts) {
		bool have = false;
		for (const Field& f : out)
			if (f.path == d.first) have = true;
		if (!have) out.push_back({d.first, d.second, -1});
	}
	return out;
}

bool set_rule_value(const std::string& key, int idx, const std::string& path, double value) {
	Dict r = lt_rule_ref(key, idx);
	if (r.empty()) return false;
	const std::string ck = key + "#" + itos(idx);
	TuningState& s = lt_state();
	if (!s.rule_orig.count(ck)) s.rule_orig[ck] = r.duplicate(true);
	if (begins_with(path, "w.")) {
		if (!r.get("w").is_dict()) r.set("w", Dict());
		Dict wd = r.get("w").as_dict();
		wd.set(path.substr(2), value);
	} else {
		r.set(path, value);
	}
	s.rule_changes[ck][path] = value;
	return true;
}

Value rule_original(const std::string& key, int idx, const std::string& path) {
	const std::string ck = key + "#" + itos(idx);
	auto it = lt_state().rule_orig.find(ck);
	const Dict r = it != lt_state().rule_orig.end() ? it->second : lt_rule_ref(key, idx);
	if (begins_with(path, "w.")) return ddict(r, "w").get(path.substr(2));
	return r.get(path);
}

bool rule_changed(const std::string& key, int idx, const std::string& path) {
	auto it = lt_state().rule_changes.find(key + "#" + itos(idx));
	return it != lt_state().rule_changes.end() && it->second.count(path) > 0;
}

void reset_all() {
	TuningState& s = lt_state();
	Moves::clear_overrides();
	s.move_changes.clear();
	for (const auto& kv : s.rule_orig) {
		const size_t h = kv.first.rfind('#');
		Dict r = lt_rule_ref(kv.first.substr(0, h), to_int(kv.first.substr(h + 1)));
		if (r.empty()) continue;
		const Dict& orig = kv.second;
		for (const std::string& k : r.keys())
			if (!orig.has(k)) r.erase(k);
		for (const auto& o : orig) r.set(o.first, o.second);
	}
	s.rule_orig.clear();
	s.rule_changes.clear();
}

std::string save_json() {
	Dict moves, rules;
	for (const auto& kv : lt_state().move_changes)
		for (const auto& p : kv.second) moves.set(kv.first + "/" + p.first, p.second);
	for (const auto& kv : lt_state().rule_changes)
		for (const auto& p : kv.second) rules.set(kv.first + "/" + p.first, p.second);
	return ToJson(Value(D({{"schema", 1}, {"moves", moves}, {"rules", rules}})), 2);
}

int load_json(const std::string& text) {
	Value v;
	if (!ParseJson(text, v) || !v.is_dict()) return -1;
	int n = 0;
	for (const auto& kv : ddict(v.as_dict(), "moves")) {
		const size_t i = kv.first.find('/');
		if (i != std::string::npos && i > 0 && set_value(kv.first.substr(0, i), kv.first.substr(i + 1), vnum(kv.second))) ++n;
	}
	for (const auto& kv : ddict(v.as_dict(), "rules")) {
		const size_t i2 = kv.first.rfind('/');
		if (i2 == std::string::npos || i2 == 0) continue;
		const std::string cell = kv.first.substr(0, i2);
		const size_t hp = cell.rfind('#');
		if (hp != std::string::npos && hp > 0 && set_rule_value(cell.substr(0, hp), to_int(cell.substr(hp + 1)), kv.first.substr(i2 + 1), vnum(kv.second))) ++n;
	}
	return n;
}

std::string export_text() {
	std::string out = "# Fourfold Lab tuning export\n";
	for (const auto& kv : lt_state().move_changes)
		for (const auto& p : kv.second)
			out += kv.first + "." + p.first + ": " + lt_fmt(original_value(kv.first, p.first)) + " -> " + lt_fmt(Value(p.second)) + "\n";
	for (const auto& kv : lt_state().rule_changes)
		for (const auto& p : kv.second) {
			auto it = lt_state().rule_orig.find(kv.first);
			const Value o = it == lt_state().rule_orig.end() ? Value() : it->second.get(p.first);
			out += "rule " + kv.first + "." + p.first + ": " + lt_fmt(o) + " -> " + lt_fmt(Value(p.second)) + "\n";
		}
	return out;
}

}  // namespace LabTuning
}  // namespace ff
