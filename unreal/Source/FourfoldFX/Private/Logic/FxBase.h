// FourfoldFX logic island - basic math, colour, transforms, deterministic RNG (engine-free; no Unreal headers).
// Everything in the logic island works in SIM space (metres, right-handed, +Y up, like ff::Snapshot); the Unreal glue
// converts the finished draw list once (FxUeConvert.h). Owner: stream `fx`.
#pragma once

#include "ff/Math.h"

#include <cmath>
#include <cstdint>

namespace ffx {

using Vec2 = ff::Vec2;
using Vec3 = ff::Vec3;

inline constexpr float kFxPi = 3.14159265358979f;
inline constexpr float kFxTau = 6.28318530717959f;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float Sat(float x) { return Clamp(x, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float InvLerp(float a, float b, float x) { return (b - a) != 0.0f ? (x - a) / (b - a) : 0.0f; }
inline float Smooth(float e0, float e1, float x) {
	const float t = Sat(InvLerp(e0, e1, x));
	return t * t * (3.0f - 2.0f * t);
}
inline float MaxF(float a, float b) { return a > b ? a : b; }
inline float MinF(float a, float b) { return a < b ? a : b; }
inline int MaxI(int a, int b) { return a > b ? a : b; }
inline int MinI(int a, int b) { return a < b ? a : b; }
inline int ClampI(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float EaseOutCubic(float t) {
	const float u = 1.0f - Sat(t);
	return 1.0f - u * u * u;
}

inline Vec3 V3(float x, float y, float z) { return Vec3(x, y, z); }
inline Vec3 Norm(const Vec3& v, const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f)) {
	const float l2 = v.length_squared();
	if (l2 < 1e-12f) return fallback;
	return v / std::sqrt(l2);
}
inline Vec3 Flat(const Vec3& v) { return Vec3(v.x, 0.0f, v.z); }
inline Vec3 LerpV(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
// Any unit vector perpendicular to n.
inline Vec3 Perp(const Vec3& n) {
	const Vec3 a = std::fabs(n.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
	return Norm(a.cross(n));
}

struct Color {
	float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
	constexpr Color() = default;
	constexpr Color(float ar, float ag, float ab, float aa = 1.0f) : r(ar), g(ag), b(ab), a(aa) {}
	constexpr Color operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
	constexpr Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
	constexpr Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
	constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	constexpr bool operator!=(const Color& o) const { return !(*this == o); }
	constexpr Color WithA(float na) const { return {r, g, b, na}; }
	constexpr Color Rgb(float s) const { return {r * s, g * s, b * s, a}; }
};
inline Color LerpC(const Color& a, const Color& b, float t) {
	return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

// Orthonormal (or scaled) frame: columns = where the local x / y / z axes point in sim space.
struct Basis {
	Vec3 x{1.0f, 0.0f, 0.0f};
	Vec3 y{0.0f, 1.0f, 0.0f};
	Vec3 z{0.0f, 0.0f, 1.0f};

	Vec3 Apply(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
	Basis Scaled(float sx, float sy, float sz) const { return {x * sx, y * sy, z * sz}; }
	Basis Scaled(float s) const { return Scaled(s, s, s); }
	Basis Mul(const Basis& o) const { return {Apply(o.x), Apply(o.y), Apply(o.z)}; }   // this * o
	float Det() const { return x.dot(y.cross(z)); }

	static Basis Identity() { return {}; }
	// Rotation by `angle` radians about the unit `axis` (right-handed).
	static Basis AxisAngle(const Vec3& axis, float angle) {
		const Vec3 a = Norm(axis);
		const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
		return {Vec3(t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y),
		        Vec3(t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x),
		        Vec3(t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c)};
	}
	static Basis YawY(float yaw) { return AxisAngle(Vec3(0.0f, 1.0f, 0.0f), yaw); }
	// Local +Y along `up`, local +Z as close as possible to `fwd`.
	static Basis FromUpFwd(const Vec3& up, const Vec3& fwd) {
		const Vec3 y = Norm(up);
		Vec3 x = y.cross(fwd);
		if (x.length_squared() < 1e-10f) x = Perp(y);
		x = Norm(x);
		return {x, y, x.cross(y)};
	}
	// Local +Z along `fwd`, local +Y as close as possible to `up`.
	static Basis FromFwdUp(const Vec3& fwd, const Vec3& up) {
		const Vec3 z = Norm(fwd, Vec3(0.0f, 0.0f, 1.0f));
		Vec3 x = up.cross(z);
		if (x.length_squared() < 1e-10f) x = Perp(z);
		x = Norm(x);
		return {x, z.cross(x), z};
	}
	// Re-orthonormalise (Gram-Schmidt on y then z), dropping scale.
	Basis Orthonormal() const {
		const Vec3 ny = Norm(y);
		Vec3 nz = z - ny * z.dot(ny);
		nz = Norm(nz, Perp(ny));
		return {ny.cross(nz), ny, nz};
	}
};

// Component transform: world = pos + basis.Apply(local).
struct Xform {
	Vec3 pos;
	Basis basis;
	Vec3 Apply(const Vec3& p) const { return pos + basis.Apply(p); }
	static Xform At(const Vec3& p) { return {p, Basis::Identity()}; }
	static Xform At(const Vec3& p, float scale) { return {p, Basis::Identity().Scaled(scale)}; }
};

// PCG32 (O'Neill), deterministic on every platform.
class Rng {
public:
	explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL) { Seed(seed); }
	void Seed(uint64_t seed) {
		state_ = 0u;
		Next();
		state_ += seed;
		Next();
	}
	uint32_t Next() {
		const uint64_t old = state_;
		state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
		const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = static_cast<uint32_t>(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
	}
	float F01() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
	float Range(float lo, float hi) { return lo + (hi - lo) * F01(); }
	int RangeI(int lo, int hiInclusive) {
		const uint32_t span = static_cast<uint32_t>(hiInclusive - lo + 1);
		return lo + static_cast<int>(Next() % (span == 0u ? 1u : span));
	}
	float Signed() { return F01() * 2.0f - 1.0f; }
	Vec3 InSphere() {
		for (int i = 0; i < 16; ++i) {
			const Vec3 v(Signed(), Signed(), Signed());
			if (v.length_squared() <= 1.0f) return v;
		}
		return Vec3(0.0f, 0.0f, 0.0f);
	}
	Vec3 OnSphere() { return Norm(InSphere(), Vec3(0.0f, 1.0f, 0.0f)); }
	// Unit vector within `spread` radians of `dir`.
	Vec3 InCone(const Vec3& dir, float spread) {
		const Vec3 d = Norm(dir);
		const Vec3 a = Perp(d);
		const Vec3 b = d.cross(a);
		const float phi = F01() * kFxTau;
		const float cosT = Lerp(1.0f, std::cos(spread), F01());
		const float sinT = std::sqrt(MaxF(0.0f, 1.0f - cosT * cosT));
		return Norm(d * cosT + (a * std::cos(phi) + b * std::sin(phi)) * sinT);
	}

private:
	uint64_t state_ = 0;
};

inline uint32_t HashU32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}
inline uint32_t HashCombine(uint32_t a, uint32_t b) { return HashU32(a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2))); }
inline float Hash01(uint32_t x) { return static_cast<float>(HashU32(x) >> 8) * (1.0f / 16777216.0f); }

}  // namespace ffx
