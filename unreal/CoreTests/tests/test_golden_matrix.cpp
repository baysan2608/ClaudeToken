// Golden counter matrix: every row of Data/golden_matrix.json (13,300 MatrixQuery.predict results exported from
// Godot) through the C++ MatrixQuery: band, outcome, rule_id, to, classes exact; tp / cp / cp_eff / ratio within
// 1e-3 relative.
#include "ff_test.h"

#include "ff/Json.h"
#include "Lab/MatrixQuery.h"
#include "Util/GdUtil.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace ff;
using fft::S;

namespace {
std::string gm_read(const char* path) {
	std::string out;
	FILE* f = std::fopen(path, "rb");
	if (!f) return out;
	char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	std::fclose(f);
	return out;
}
bool gm_close(double a, double b) {
	const double d = std::fabs(a - b);
	return d <= 1e-3 * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}
}  // namespace

FF_TEST(test_golden_matrix, test_every_row_matches_godot) {
	const std::string text = gm_read(FF_DATA_DIR "/golden_matrix.json");
	check(!text.empty(), "golden_matrix.json readable");
	Value root;
	if (!check(ParseJson(text, root), "golden_matrix.json parses")) return;
	const Array rows = root.get("rows").as_array();
	check(rows.size() == 13300, S("13300 rows (", rows.size(), ")"));
	int bad = 0, shown = 0;
	std::map<std::string, int> by_counter, by_threat;
	for (const Value& rv : rows) {
		const Dict r = rv.as_dict();
		const Dict got = MatrixQuery::predict(dstr(r, "threat"), ddict(r, "params"), dstr(r, "counter"), dint(r, "tier"), dbool(r, "perfect"));
		std::string why;
		if (!dbool(got, "ok")) why = "not ok: " + dstr(got, "msg");
		for (const char* k : {"band", "outcome", "rule_id", "to", "threat_cls", "counter_cls"})
			if (why.empty() && dstr(got, k) != dstr(r, k)) why = S(k, " ", dstr(got, k), " != ", dstr(r, k));
		for (const char* k : {"tp", "cp", "cp_eff", "ratio"})
			if (why.empty() && !gm_close(dnum(got, k), dnum(r, k))) why = S(k, " ", dnum(got, k), " != ", dnum(r, k));
		if (!why.empty()) {
			++bad;
			by_counter[dstr(r, "counter")]++;
			by_threat[dstr(r, "threat")]++;
			if (shown < 5) {
				++shown;
				check(false, S(dstr(r, "threat"), " ", ddict(r, "params"), " vs ", dstr(r, "counter"), " T", dint(r, "tier"),
				               dbool(r, "perfect") ? " perfect" : "", ": ", why));
			}
		}
	}
	if (bad > 0) {
		std::string top;
		for (const auto& kv : by_counter) top += kv.first + ":" + std::to_string(kv.second) + " ";
		std::string tt;
		for (const auto& kv : by_threat) tt += kv.first + ":" + std::to_string(kv.second) + " ";
		check(false, S(bad, " of ", rows.size(), " rows differ; by counter: ", top, "; by threat: ", tt));
	}
	note(S(rows.size() - bad, " / ", rows.size(), " golden rows match"));
}
