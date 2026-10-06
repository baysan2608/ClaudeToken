// Fourfold game logic island - small math helpers (engine-free, header-only).
// Everything under Private/Logic/ includes NO Unreal header: it is compiled by UBT as part of the Fourfold module AND
// unit-tested in the Linux container (Private/Logic/tests). Rules: no UE macro names as identifiers (check, PI, TEXT,
// INDEX_NONE ...), no `using namespace` at file scope (UBT unity builds merge files), warning-clean with -Wshadow
// -Wconversion.
//
// Quaternion convention = Unreal's FQuat: (x, y, z, w), a * b applies b first, then a; Rotate(v) = q v q*.
// That makes Quat <-> FQuat a plain member copy.
#pragma once

#include "ff/Math.h"

#include <algorithm>
#include <cmath>

namespace ffg {

using Vec2 = ff::Vec2;
using Vec3 = ff::Vec3;

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTau = 6.28318530717958647692f;
inline constexpr float kSimDtF = 1.0f / 60.0f;

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float Saturate(float v) { return Clampf(v, 0.0f, 1.0f); }
inline float Sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
// Godot smoothstep(from, to, x).
inline float SmoothStep(float from, float to, float x) {
	if (from == to) return x < from ? 0.0f : 1.0f;
	const float s = Saturate((x - from) / (to - from));
	return s * s * (3.0f - 2.0f * s);
}
inline float MoveToward(float from, float to, float delta) {
	if (std::fabs(to - from) <= delta) return to;
	return from + Sign(to - from) * delta;
}
// Wraps to [-pi, pi).
inline float WrapAngle(float a) {
	float r = std::fmod(a + kPi, kTau);
	if (r < 0.0f) r += kTau;
	return r - kPi;
}
// Godot wrapf(value, min, max).
inline float Wrapf(float v, float lo, float hi) {
	const float range = hi - lo;
	if (range == 0.0f) return lo;
	float r = std::fmod(v - lo, range);
	if (r < 0.0f) r += range;
	return lo + r;
}
// Godot fposmod.
inline float Fposmod(float x, float y) {
	float r = std::fmod(x, y);
	if ((r < 0.0f && y > 0.0f) || (r > 0.0f && y < 0.0f)) r += y;
	return r;
}
inline int Posmod(int x, int y) {
	const int r = x % y;
	return r < 0 ? r + y : r;
}
// Frame-rate independent exponential approach factor (1 - e^(-rate dt)).
inline float ExpK(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }
inline float LerpAngle(float from, float to, float w) { return from + WrapAngle(to - from) * w; }

inline Vec3 V3(float x, float y, float z) { return Vec3(x, y, z); }
inline Vec2 V2(float x, float y) { return Vec2(x, y); }

struct Quat {
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

	constexpr Quat() = default;
	constexpr Quat(float ax, float ay, float az, float aw) : x(ax), y(ay), z(az), w(aw) {}

	static constexpr Quat Identity() { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }

	// Rotation of `angle` radians about `axis` (normalised inside; zero axis = identity).
	static Quat AxisAngle(const Vec3& axis, float angle) {
		const Vec3 n = axis.normalized();
		if (n.length_squared() < 0.5f) return Identity();
		const float h = 0.5f * angle;
		const float s = std::sin(h);
		return Quat(n.x * s, n.y * s, n.z * s, std::cos(h));
	}
	// Rotation vector (axis * angle) -> quaternion; `frac` scales the angle.
	static Quat FromRotVec(const Vec3& v, float frac = 1.0f) {
		const float a = v.length() * frac;
		if (a < 1e-7f) return Identity();
		return AxisAngle(v, a);
	}
	// Shortest-arc rotation taking direction a onto direction b.
	static Quat FromTo(const Vec3& a, const Vec3& b) {
		const Vec3 an = a.normalized();
		const Vec3 bn = b.normalized();
		if (an.length_squared() < 0.5f || bn.length_squared() < 0.5f) return Identity();
		const float d = an.dot(bn);
		if (d > 0.999999f) return Identity();
		if (d < -0.999999f) {
			Vec3 ortho = Vec3(1.0f, 0.0f, 0.0f).cross(an);
			if (ortho.length_squared() < 1e-6f) ortho = Vec3(0.0f, 1.0f, 0.0f).cross(an);
			return AxisAngle(ortho, kPi);
		}
		const Vec3 c = an.cross(bn);
		Quat q(c.x, c.y, c.z, 1.0f + d);
		return q.Normalized();
	}

	float Dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
	float LengthSq() const { return x * x + y * y + z * z + w * w; }
	Quat Normalized() const {
		const float l2 = LengthSq();
		if (l2 < 1e-12f) return Identity();
		const float inv = 1.0f / std::sqrt(l2);
		return Quat(x * inv, y * inv, z * inv, w * inv);
	}
	Quat Inverse() const { return Quat(-x, -y, -z, w); }   // unit quaternions
	Quat Neg() const { return Quat(-x, -y, -z, -w); }

	Quat operator*(const Quat& b) const {
		return Quat(w * b.x + x * b.w + y * b.z - z * b.y,
		            w * b.y - x * b.z + y * b.w + z * b.x,
		            w * b.z + x * b.y - y * b.x + z * b.w,
		            w * b.w - x * b.x - y * b.y - z * b.z);
	}
	Vec3 Rotate(const Vec3& v) const {
		const Vec3 qv(x, y, z);
		const Vec3 t = qv.cross(v) * 2.0f;
		return v + t * w + qv.cross(t);
	}
	// Angle of the rotation (0..pi).
	float Angle() const {
		const float cw = Clampf(std::fabs(w), 0.0f, 1.0f);
		return 2.0f * std::acos(cw);
	}
	// Rotation vector (axis * angle), shortest form.
	Vec3 ToRotVec() const {
		Quat q = w < 0.0f ? Neg() : *this;
		const float s = std::sqrt(std::max(0.0f, 1.0f - q.w * q.w));
		const float a = 2.0f * std::acos(Clampf(q.w, -1.0f, 1.0f));
		if (s < 1e-6f) return Vec3(q.x * 2.0f, q.y * 2.0f, q.z * 2.0f);
		return Vec3(q.x / s * a, q.y / s * a, q.z / s * a);
	}
};

// Normalised lerp along the shortest arc (cheap, good for small blends).
inline Quat Nlerp(const Quat& a, const Quat& b, float t) {
	const Quat bb = a.Dot(b) < 0.0f ? b.Neg() : b;
	return Quat(a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t, a.z + (bb.z - a.z) * t, a.w + (bb.w - a.w) * t).Normalized();
}
// Spherical interpolation along the shortest arc.
inline Quat Slerp(const Quat& a, const Quat& b, float t) {
	float d = a.Dot(b);
	Quat bb = b;
	if (d < 0.0f) {
		d = -d;
		bb = b.Neg();
	}
	if (d > 0.9995f) return Nlerp(a, bb, t);
	const float th = std::acos(Clampf(d, -1.0f, 1.0f));
	const float s = std::sin(th);
	const float wa = std::sin((1.0f - t) * th) / s;
	const float wb = std::sin(t * th) / s;
	return Quat(a.x * wa + bb.x * wb, a.y * wa + bb.y * wb, a.z * wa + bb.z * wb, a.w * wa + bb.w * wb).Normalized();
}

// Rigid transform (rotation + translation), Unreal order: (A * B) applies A first, then B (like FTransform).
struct Xform {
	Quat q;
	Vec3 t;

	Vec3 Apply(const Vec3& p) const { return q.Rotate(p) + t; }
	// this (local, relative to parent) composed with the parent's component-space transform.
	Xform operator*(const Xform& parent) const {
		Xform r;
		r.q = (parent.q * q).Normalized();
		r.t = parent.q.Rotate(t) + parent.t;
		return r;
	}
	Xform Inverse() const {
		Xform r;
		r.q = q.Inverse();
		r.t = r.q.Rotate(-t);
		return r;
	}
	// The transform of `this` relative to `parent` (component -> local).
	Xform RelativeTo(const Xform& parent) const { return (*this) * parent.Inverse(); }
};

}  // namespace ffg
