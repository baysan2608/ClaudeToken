// Fourfold core tests - kit counter cells rebuilt from the exported rules (see kit_cells.h).
#include "kit_cells.h"

#include "ff/Json.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <cstdio>
#include <unordered_map>

namespace fft {

using namespace ff;

std::string gd_int_array_str(const std::vector<int>& v) {
	std::string s = "[";
	for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
	return s + "]";
}

std::vector<KitCell> kit_cells(const std::string& owner) {
	Interactions::ensure_ready();
	std::vector<KitCell> out;
	std::unordered_map<std::string, size_t> at;
	const auto& all = Interactions::all_rules();
	for (const std::string& key : Interactions::rule_keys_in_order()) {
		auto it = all.find(key);
		if (it == all.end()) continue;
		const size_t bar = key.find('|');
		const std::string t = key.substr(0, bar);
		const std::string c = bar == std::string::npos ? std::string() : key.substr(bar + 1);
		for (const Dict& r : it->second) {
			if (dstr(r, "owner", "") != owner) continue;
			KitCell cell;
			cell.threat = t;
			cell.counter = c;
			cell.id = dstr(r, "id", "");
			for (const Value& tv : darr(r, "tiers")) cell.tiers.push_back(vint(tv));
			if (owner == "air") cell.key = t + "|" + c + "|" + gd_int_array_str(cell.tiers);
			else if (owner == "earth") cell.key = t + "|" + c;
			else cell.key = t + "|" + c + (r.has("tiers") ? "|" + gd_int_array_str(cell.tiers) : std::string());
			auto f = at.find(cell.key);
			if (f != at.end()) {
				out[f->second] = cell;
			} else {
				at[cell.key] = out.size();
				out.push_back(cell);
			}
		}
	}
	return out;
}

Dict kit_cell_refs(const std::string& owner) {
#if defined(FF_TEST_DATA_DIR)
	static Value root;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		std::string text;
		if (FILE* f = std::fopen(FF_TEST_DATA_DIR "/kit_cell_refs.json", "rb")) {
			char buf[65536];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
			std::fclose(f);
		}
		if (!text.empty()) ParseJson(text, root);
	}
	return root.get(owner).as_dict();
#else
	(void)owner;
	return Dict();
#endif
}

}  // namespace fft
