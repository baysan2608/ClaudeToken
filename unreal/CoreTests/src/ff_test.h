// Fourfold core tests - a tiny test framework mirroring game/tests/test_case.gd (check / near / note) and
// run_tests.gd (every test of every suite, optional name filter "suite.name" substring). Test code only: never
// compiled by Unreal, so GDScript-like names (check) are fine here.
#pragma once

#include "ff/Value.h"
#include "ff/Math.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fft {

// Text concatenation for messages: S("got ", 3, " want ", 2.5) (numbers formatted like GDScript str()).
inline void s_append(std::string& o, const std::string& v) { o += v; }
inline void s_append(std::string& o, std::string_view v) { o += v; }
inline void s_append(std::string& o, const char* v) { o += v ? v : ""; }
inline void s_append(std::string& o, char v) { o += v; }
inline void s_append(std::string& o, bool v) { o += v ? "true" : "false"; }
inline void s_append(std::string& o, int v) { o += std::to_string(v); }
inline void s_append(std::string& o, long v) { o += std::to_string(v); }
inline void s_append(std::string& o, long long v) { o += std::to_string(v); }
inline void s_append(std::string& o, unsigned v) { o += std::to_string(v); }
inline void s_append(std::string& o, unsigned long v) { o += std::to_string(v); }
inline void s_append(std::string& o, unsigned long long v) { o += std::to_string(v); }
inline void s_append(std::string& o, double v) {
	char b[64];
	std::snprintf(b, sizeof(b), "%.4f", v);
	o += b;
}
inline void s_append(std::string& o, float v) { s_append(o, static_cast<double>(v)); }
inline void s_append(std::string& o, const ff::Value& v) { o += v.to_string(); }
inline void s_append(std::string& o, const ff::Dict& v) { o += ff::Value(v).to_string(); }
inline void s_append(std::string& o, const ff::Array& v) { o += ff::Value(v).to_string(); }
inline void s_append(std::string& o, const ff::Vec3& v) { o += ff::Value(v).to_string(); }
template <typename... A>
std::string S(const A&... a) {
	std::string o;
	(s_append(o, a), ...);
	return o;
}

class TestCase {
public:
	virtual ~TestCase() = default;
	virtual void run() = 0;

	std::vector<std::string> failures;
	std::vector<std::string> notes;
	std::string current;

	bool check(bool cond, const std::string& msg) {
		if (!cond) failures.push_back(current + ": " + msg);
		return cond;
	}
	bool near(double a, double b, double eps, const std::string& msg) {
		const double d = a > b ? a - b : b - a;
		return check(d <= eps, msg + S(" (got ", a, ", want ", b, " +-", eps, ")"));
	}
	void note(const std::string& s) { notes.push_back(current + ": " + s); }
};

using Factory = std::function<std::unique_ptr<TestCase>()>;
struct Entry {
	std::string suite, name;
	Factory make;
};
std::vector<Entry>& registry();
struct Registrar {
	Registrar(const char* suite, const char* name, Factory f) { registry().push_back({suite, name, std::move(f)}); }
};

// Runs every registered test whose "suite.name" contains `filter` (all when empty). Returns the failed count.
int run_all(const std::string& filter, bool verbose);

}  // namespace fft

#define FF_TEST_IMPL(SUITE, NAME, BASE)                                                                          \
	namespace {                                                                                                  \
	struct SUITE##__##NAME final : BASE {                                                                        \
		void run() override;                                                                                     \
	};                                                                                                           \
	const ::fft::Registrar SUITE##__##NAME##__reg(#SUITE, #NAME,                                                 \
	                                              [] { return std::unique_ptr<::fft::TestCase>(new SUITE##__##NAME()); }); \
	}                                                                                                            \
	void SUITE##__##NAME::run()

// FF_TEST(test_core_world, test_zone_effects) { check(...); }
#define FF_TEST(SUITE, NAME) FF_TEST_IMPL(SUITE, NAME, ::fft::TestCase)
// FF_TEST_F(suite, Fixture, name): Fixture derives from fft::TestCase and holds the suite's shared helpers.
#define FF_TEST_F(SUITE, FIXTURE, NAME) FF_TEST_IMPL(SUITE, NAME, FIXTURE)
