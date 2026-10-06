// Fourfold core tests - runner: ff_tests [filter] [-q]   (filter = substring of "suite.name")
#include "ff_test.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace fft {

std::vector<Entry>& registry() {
	static std::vector<Entry> r;
	return r;
}

int run_all(const std::string& filter, bool verbose) {
	std::vector<Entry> all = registry();
	std::stable_sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return a.suite < b.suite; });
	int total = 0, failed = 0;
	const auto t0 = std::chrono::steady_clock::now();
	std::string last_suite;
	int suite_n = 0, suite_f = 0;
	auto flush_suite = [&] {
		if (!last_suite.empty()) std::printf("SUITE %-34s %3d tests %3d failed\n", last_suite.c_str(), suite_n, suite_f);
	};
	for (const Entry& e : all) {
		const std::string full = e.suite + "." + e.name;
		if (!filter.empty() && full.find(filter) == std::string::npos) continue;
		if (e.suite != last_suite) {
			flush_suite();
			last_suite = e.suite;
			suite_n = suite_f = 0;
		}
		std::unique_ptr<TestCase> tc = e.make();
		tc->current = full;
		tc->run();
		++total;
		++suite_n;
		for (const std::string& n : tc->notes) std::printf("  note  %s\n", n.c_str());
		if (tc->failures.empty()) {
			if (verbose) std::printf("PASS  %s\n", full.c_str());
		} else {
			++failed;
			++suite_f;
			std::printf("FAIL  %s\n", full.c_str());
			for (const std::string& f : tc->failures) std::printf("      %s\n", f.c_str());
		}
		std::fflush(stdout);
	}
	flush_suite();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	std::printf("\n%d tests, %d failed (%lld ms)\n", total, failed, static_cast<long long>(ms));
	return failed;
}

}  // namespace fft

int main(int argc, char** argv) {
	std::string filter;
	bool verbose = true;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "-q") == 0) verbose = false;
		else filter = argv[i];
	}
	return fft::run_all(filter, verbose) > 0 ? 1 : 0;
}
