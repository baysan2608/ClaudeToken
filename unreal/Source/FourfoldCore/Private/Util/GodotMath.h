// Fourfold core - Godot 4 math helpers (ports of core/math/math_funcs.h, vector3.cpp, basis.cpp; MIT).
// GDScript `float` is double, Vector2/3 components are float32: scalar helpers take doubles, vector helpers do their
// arithmetic in float like Godot. Private header (never include from Public/ff).
#pragma once

#include "ff/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ff {

inline constexpr double kPi = 3.1415926535897932384626433833;
inline constexpr double kTau = 6.2831853071795864769252867666;
inline constexpr double kCmpEpsilon = 0.00001;
inline constexpr double kInf = 1.0e300;   // GDScript INF stand-in for "best so far" searches (never compared for NaN)

// ------------------------------------------------------------------ Vec3 / Vec2 with GDScript doubles
// GDScript `vec * x` converts the (double) scalar to float first: these overloads do exactly that. Integer
// scalars are ambiguous on purpose (write 2.0 or double(n)).
inline Vec3 operator*(const Vec3& v, double s) { return v * static_cast<float>(s); }
inline Vec3 operator*(double s, const Vec3& v) { return v * static_cast<float>(s); }
inline Vec3 operator/(const Vec3& v, double s) { return v / static_cast<float>(s); }
inline Vec2 operator*(const Vec2& v, double s) { return v * static_cast<float>(s); }
inline Vec2 operator/(const Vec2& v, double s) { return v / static_cast<float>(s); }
inline Vec3& operator*=(Vec3& v, double s) { return v *= static_cast<float>(s); }
inline Vec3& operator/=(Vec3& v, double s) { return v /= static_cast<float>(s); }

inline float f32(double d) { return static_cast<float>(d); }
inline Vec3 V3(double x, double y, double z) { return Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)); }
inline Vec2 V2(double x, double y) { return Vec2(static_cast<float>(x), static_cast<float>(y)); }
inline Vec3 flat(const Vec3& v) { return Vec3(v.x, 0.0f, v.z); }
// Vector2(a.x - b.x, a.z - b.z).length()
inline float flat_dist(const Vec3& a, const Vec3& b) { return Vec2(a.x - b.x, a.z - b.z).length(); }
inline Vec3 Vec3One() { return Vec3(1.0f, 1.0f, 1.0f); }
inline Vec3 Vec3Right() { return Vec3(1.0f, 0.0f, 0.0f); }

// ------------------------------------------------------------------ scalars (double)
inline double absf(double x) { return std::fabs(x); }
inline double minf(double a, double b) { return a < b ? a : b; }
inline double maxf(double a, double b) { return a > b ? a : b; }
inline int mini(int a, int b) { return a < b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline double clampf(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double lerpf(double a, double b, double w) { return a + (b - a) * w; }
inline double deg_to_rad(double d) { return d * (kPi / 180.0); }
inline double rad_to_deg(double r) { return r * (180.0 / kPi); }
inline double signf(double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); }
inline int signi(int x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

inline bool is_equal_approx(double a, double b) {
	if (a == b) return true;
	double tol = kCmpEpsilon * std::fabs(a);
	if (tol < kCmpEpsilon) tol = kCmpEpsilon;
	return std::fabs(a - b) < tol;
}
inline bool is_equal_approx(double a, double b, double tol) { return a == b || std::fabs(a - b) < tol; }
inline bool is_zero_approx(double s) { return std::fabs(s) < kCmpEpsilon; }

inline double smoothstep(double from, double to, double s) {
	if (is_equal_approx(from, to)) {
		if (from <= to) return s <= from ? 0.0 : 1.0;
		return s <= to ? 1.0 : 0.0;
	}
	const double t = clampf((s - from) / (to - from), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

inline double move_toward(double from, double to, double delta) {
	return std::fabs(to - from) <= delta ? to : from + signf(to - from) * delta;
}

inline double wrapf(double value, double lo, double hi) {
	const double range = hi - lo;
	if (is_zero_approx(range)) return lo;
	const double result = value - (range * std::floor((value - lo) / range));
	if (is_equal_approx(result, hi)) return lo;
	return result;
}

inline int wrapi(int value, int lo, int hi) {
	const int range = hi - lo;
	return range == 0 ? lo : lo + ((((value - lo) % range) + range) % range);
}

inline double fposmod(double x, double y) {
	double v = std::fmod(x, y);
	if ((v < 0.0 && y > 0.0) || (v > 0.0 && y < 0.0)) v += y;
	return v + 0.0;
}

inline int64_t posmod(int64_t x, int64_t y) {
	if (y == 0) return 0;
	int64_t v = x % y;
	if ((v < 0 && y > 0) || (v > 0 && y < 0)) v += y;
	return v;
}

// Godot ease(x, c): c > 1 ease-in, 0 < c < 1 ease-out, c < 0 in-out, 0 constant.
inline double ease(double x, double c) {
	if (x < 0.0) x = 0.0;
	else if (x > 1.0) x = 1.0;
	if (c > 0.0) {
		if (c < 1.0) return 1.0 - std::pow(1.0 - x, 1.0 / c);
		return std::pow(x, c);
	}
	if (c < 0.0) {
		if (x < 0.5) return std::pow(x * 2.0, -c) * 0.5;
		return (1.0 - std::pow(1.0 - (x - 0.5) * 2.0, -c)) * 0.5 + 0.5;
	}
	return 0.0;
}

inline double snappedf(double v, double step) { return step != 0.0 ? std::floor(v / step + 0.5) * step : v; }
inline double lerp_angle(double from, double to, double w) {
	const double diff = std::fmod(to - from, kTau);
	const double dist = std::fmod(2.0 * diff, kTau) - diff;
	return from + dist * w;
}
inline double angle_difference(double from, double to) {
	const double diff = std::fmod(to - from, kTau);
	return std::fmod(2.0 * diff, kTau) - diff;
}

// ------------------------------------------------------------------ vectors (float like Godot)
// Vector3.rotated(axis, angle): Basis(axis, angle).xform(v) (axis must be normalized).
inline Vec3 rotated(const Vec3& v, const Vec3& axis, double angle_d) {
	const float angle = static_cast<float>(angle_d);
	const Vec3 sq(axis.x * axis.x, axis.y * axis.y, axis.z * axis.z);
	const float cosine = std::cos(angle);
	const float sine = std::sin(angle);
	const float t = 1.0f - cosine;
	float r[3][3];
	r[0][0] = sq.x + cosine * (1.0f - sq.x);
	r[1][1] = sq.y + cosine * (1.0f - sq.y);
	r[2][2] = sq.z + cosine * (1.0f - sq.z);
	float xyzt = axis.x * axis.y * t;
	float zyxs = axis.z * sine;
	r[0][1] = xyzt - zyxs;
	r[1][0] = xyzt + zyxs;
	xyzt = axis.x * axis.z * t;
	zyxs = axis.y * sine;
	r[0][2] = xyzt + zyxs;
	r[2][0] = xyzt - zyxs;
	xyzt = axis.y * axis.z * t;
	zyxs = axis.x * sine;
	r[1][2] = xyzt - zyxs;
	r[2][1] = xyzt + zyxs;
	return Vec3(r[0][0] * v.x + r[0][1] * v.y + r[0][2] * v.z, r[1][0] * v.x + r[1][1] * v.y + r[1][2] * v.z,
	            r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z);
}
inline Vec3 rotated_y(const Vec3& v, double angle) { return rotated(v, Vec3::Up(), angle); }

inline Vec3 move_toward(const Vec3& from, const Vec3& to, double delta_d) {
	const float delta = static_cast<float>(delta_d);
	const Vec3 vd = to - from;
	const float len = vd.length();
	return (len <= delta || len < static_cast<float>(kCmpEpsilon)) ? to : from + vd / len * delta;
}

inline float angle_to(const Vec3& a, const Vec3& b) { return std::atan2(a.cross(b).length(), a.dot(b)); }
inline float signed_angle_to(const Vec3& a, const Vec3& b, const Vec3& axis) {
	const Vec3 c = a.cross(b);
	const float un = std::atan2(c.length(), a.dot(b));
	return c.dot(axis) < 0.0f ? -un : un;
}

inline Vec3 slerp(const Vec3& a, const Vec3& b, double w_d) {
	const float w = static_cast<float>(w_d);
	const float s2 = a.length_squared();
	const float e2 = b.length_squared();
	if (s2 == 0.0f || e2 == 0.0f) return a.lerp(b, w);
	Vec3 axis = a.cross(b);
	const float al2 = axis.length_squared();
	if (al2 == 0.0f) return a.lerp(b, w);
	axis /= std::sqrt(al2);
	const float sl = std::sqrt(s2);
	const float rl = sl + (std::sqrt(e2) - sl) * w;
	const float ang = angle_to(a, b);
	return rotated(a, axis, static_cast<double>(ang * w)) * (rl / sl);
}

inline Vec3 reflect(const Vec3& v, const Vec3& n) { return 2.0f * n * v.dot(n) - v; }
inline Vec3 bounce(const Vec3& v, const Vec3& n) { return -reflect(v, n); }
inline Vec3 vabs(const Vec3& v) { return Vec3(std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)); }
inline bool vec_equal_approx(const Vec3& a, const Vec3& b) {
	return is_equal_approx(a.x, b.x) && is_equal_approx(a.y, b.y) && is_equal_approx(a.z, b.z);
}

inline float angle2(const Vec2& v) { return std::atan2(v.y, v.x); }
inline Vec2 rotated2(const Vec2& v, double by) {
	const float s = std::sin(static_cast<float>(by));
	const float c = std::cos(static_cast<float>(by));
	return Vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}
inline float cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

}  // namespace ff
