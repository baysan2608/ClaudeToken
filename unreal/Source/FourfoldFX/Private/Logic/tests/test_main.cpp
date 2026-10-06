// FourfoldFX logic island - test runner. Empty in the Unreal build (FF_LOGIC_TESTS is defined only by CMake).
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxConfig.h"

#include <cstring>
#include <fstream>

namespace fxt {
std::vector<TestCase>& Registry() {
	static std::vector<TestCase> r;
	return r;
}
int g_failures = 0;
int g_checks = 0;
const char* g_current = "";
void Fail(const char* file, int line, const std::string& msg) {
	++g_failures;
	std::printf("  FAIL %s (%s:%d): %s\n", g_current, file, line, msg.c_str());
}
}  // namespace fxt

int main(int argc, char** argv) {
	if (argc > 2 && std::strcmp(argv[1], "--write-config") == 0) {
		std::ofstream f(argv[2], std::ios::binary);
		f << ffx::FxConfig().ToJson();
		std::printf("wrote %s\n", argv[2]);
		return f.good() ? 0 : 1;
	}
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int run = 0;
	for (const fxt::TestCase& t : fxt::Registry()) {
		if (filter && !std::strstr(t.name, filter)) continue;
		fxt::g_current = t.name;
		const int before = fxt::g_failures;
		t.fn();
		++run;
		if (fxt::g_failures != before) std::printf("FAILED %s\n", t.name);
	}
	std::printf("%d tests, %d checks, %d failures\n", run, fxt::g_checks, fxt::g_failures);
	return fxt::g_failures == 0 ? 0 : 1;
}
#endif
