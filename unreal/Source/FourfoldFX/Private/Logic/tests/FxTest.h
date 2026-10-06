// FourfoldFX logic island - minimal test harness (only built by tests/CMakeLists.txt with FF_LOGIC_TESTS).
#pragma once
#if defined(FF_LOGIC_TESTS)

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fxt {

struct TestCase {
	const char* name;
	void (*fn)();
};
std::vector<TestCase>& Registry();
struct Reg {
	Reg(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
extern int g_failures;
extern int g_checks;
extern const char* g_current;
void Fail(const char* file, int line, const std::string& msg);

}  // namespace fxt

#define FXT_TEST(name)                               \
	static void name();                              \
	static const fxt::Reg name##_reg(#name, &name);  \
	static void name()
#define FXT_CHECK(cond)                                                 \
	do {                                                                \
		++fxt::g_checks;                                                \
		if (!(cond)) fxt::Fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
	} while (0)
#define FXT_NEAR(a, b, eps)                                                                                        \
	do {                                                                                                           \
		++fxt::g_checks;                                                                                           \
		const double fa_ = static_cast<double>(a), fb_ = static_cast<double>(b);                                   \
		if (!(std::fabs(fa_ - fb_) <= static_cast<double>(eps))) {                                                 \
			char b_[256];                                                                                          \
			std::snprintf(b_, sizeof(b_), "NEAR(%s, %s): %.6f vs %.6f (eps %.6f)", #a, #b, fa_, fb_,              \
			              static_cast<double>(eps));                                                               \
			fxt::Fail(__FILE__, __LINE__, b_);                                                                     \
		}                                                                                                          \
	} while (0)

#endif  // FF_LOGIC_TESTS
