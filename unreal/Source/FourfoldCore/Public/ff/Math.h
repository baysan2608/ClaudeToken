// Fourfold core - vectors with Godot 4 semantics (float32 components, like Godot's single-precision Vector2/3).
// FROZEN CONTRACT (architect): field names / existing functions are fixed; the `core` stream may ADD Godot-compatible
// methods (rotated, slerp, move_toward, signed_angle_to, ...). Port them from Godot's MIT-licensed core/math sources.
// Sim space: metres, +Y up, right-handed (Godot). Unreal conversion lives in Fourfold/Public/FourfoldCoords.h.
#pragma once

#include <cmath>

namespace ff {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	constexpr Vec2() = default;
	constexpr Vec2(float ax, float ay) : x(ax), y(ay) {}

	constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
	constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
	constexpr Vec2 operator-() const { return {-x, -y}; }
	constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
	constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
	Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
	Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
	Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
	constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
	constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }

	constexpr float dot(const Vec2& o) const { return x * o.x + y * o.y; }
	constexpr float length_squared() const { return x * x + y * y; }
	float length() const { return std::sqrt(length_squared()); }
	Vec2 normalized() const {
		const float l2 = length_squared();
		if (l2 == 0.0f) return {};
		const float l = std::sqrt(l2);
		return {x / l, y / l};
	}
	// Godot Vector2.limit_length
	Vec2 limit_length(float len = 1.0f) const {
		const float l = length();
		if (l > 0.0f && len < l) return Vec2{x / l * len, y / l * len};
		return *this;
	}
	float distance_to(const Vec2& o) const { return (*this - o).length(); }
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Vec3() = default;
	constexpr Vec3(float ax, float ay, float az) : x(ax), y(ay), z(az) {}

	constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
	constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
	constexpr Vec3 operator-() const { return {-x, -y, -z}; }
	constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
	constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
	constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
	Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
	Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
	Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
	constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
	constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
	float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
	float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

	constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
	constexpr Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
	constexpr float length_squared() const { return x * x + y * y + z * z; }
	float length() const { return std::sqrt(length_squared()); }
	// Godot Vector3.normalized(): zero vector stays zero.
	Vec3 normalized() const {
		const float l2 = length_squared();
		if (l2 == 0.0f) return {};
		const float l = std::sqrt(l2);
		return {x / l, y / l, z / l};
	}
	Vec3 limit_length(float len = 1.0f) const {
		const float l = length();
		if (l > 0.0f && len < l) return Vec3{x / l * len, y / l * len, z / l * len};
		return *this;
	}
	float distance_to(const Vec3& o) const { return (*this - o).length(); }
	float distance_squared_to(const Vec3& o) const { return (*this - o).length_squared(); }
	Vec3 lerp(const Vec3& to, float w) const { return {x + w * (to.x - x), y + w * (to.y - y), z + w * (to.z - z)}; }
	Vec3 direction_to(const Vec3& to) const { return (to - *this).normalized(); }
	bool is_zero_approx() const { return std::fabs(x) < 1e-5f && std::fabs(y) < 1e-5f && std::fabs(z) < 1e-5f; }

	static constexpr Vec3 Zero() { return {0.0f, 0.0f, 0.0f}; }
	static constexpr Vec3 Up() { return {0.0f, 1.0f, 0.0f}; }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline constexpr Vec2 operator*(float s, const Vec2& v) { return v * s; }

}  // namespace ff
