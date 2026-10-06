// FourfoldFX - a tiny HLSL-in-C++ shim so the shared shader functions (unreal/Shaders/Common, unreal/Shaders/FX) compile
// as C++ and their math can be unit-tested here (no shader compiler in the container). Covers exactly the subset the
// portability rules of FFCommon.ush allow. Test-only (FF_LOGIC_TESTS).
#pragma once
#if defined(FF_LOGIC_TESTS)

#include <cmath>
#include <cstdint>

namespace hlsl {

using uint = uint32_t;

struct float2 {
	float x = 0.0f, y = 0.0f;
	float2() = default;
	explicit float2(float s) : x(s), y(s) {}
	float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float3() = default;
	explicit float3(float s) : x(s), y(s), z(s) {}
	float3(float a, float b, float c) : x(a), y(b), z(c) {}
	float3(float2 a, float c) : x(a.x), y(a.y), z(c) {}
};
struct float4 {
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
	float4() = default;
	explicit float4(float s) : x(s), y(s), z(s), w(s) {}
	float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
	float4(float3 a, float d) : x(a.x), y(a.y), z(a.z), w(d) {}
};

#define FFS_OP2(T, OP)                                                                       \
	inline T operator OP(const T& a, const T& b) { return Map2(a, b, [](float p, float q) { return p OP q; }); } \
	inline T operator OP(const T& a, float b) { return Map2(a, T(b), [](float p, float q) { return p OP q; }); } \
	inline T operator OP(float a, const T& b) { return Map2(T(a), b, [](float p, float q) { return p OP q; }); } \
	inline T& operator OP##=(T& a, const T& b) { a = a OP b; return a; }                  \
	inline T& operator OP##=(T& a, float b) { a = a OP b; return a; }

template <typename F> inline float2 Map1(const float2& a, F f) { return float2(f(a.x), f(a.y)); }
template <typename F> inline float3 Map1(const float3& a, F f) { return float3(f(a.x), f(a.y), f(a.z)); }
template <typename F> inline float4 Map1(const float4& a, F f) { return float4(f(a.x), f(a.y), f(a.z), f(a.w)); }
template <typename F> inline float2 Map2(const float2& a, const float2& b, F f) { return float2(f(a.x, b.x), f(a.y, b.y)); }
template <typename F> inline float3 Map2(const float3& a, const float3& b, F f) {
	return float3(f(a.x, b.x), f(a.y, b.y), f(a.z, b.z));
}
template <typename F> inline float4 Map2(const float4& a, const float4& b, F f) {
	return float4(f(a.x, b.x), f(a.y, b.y), f(a.z, b.z), f(a.w, b.w));
}

FFS_OP2(float2, +) FFS_OP2(float2, -) FFS_OP2(float2, *) FFS_OP2(float2, /)
FFS_OP2(float3, +) FFS_OP2(float3, -) FFS_OP2(float3, *) FFS_OP2(float3, /)
FFS_OP2(float4, +) FFS_OP2(float4, -) FFS_OP2(float4, *) FFS_OP2(float4, /)
inline float2 operator-(const float2& a) { return float2(-a.x, -a.y); }
inline float3 operator-(const float3& a) { return float3(-a.x, -a.y, -a.z); }
inline float4 operator-(const float4& a) { return float4(-a.x, -a.y, -a.z, -a.w); }

// scalar intrinsics
inline float frac(float x) { return x - std::floor(x); }
inline float saturate(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float smoothstep(float a, float b, float x) {
	const float t = clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
inline float step(float e, float x) { return x >= e ? 1.0f : 0.0f; }
inline float sign(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
inline float rsqrt(float x) { return 1.0f / std::sqrt(x); }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
using std::abs;
using std::atan2;
using std::cos;
using std::exp;
using std::floor;
using std::fmod;
using std::log;
using std::pow;
using std::sin;
using std::sqrt;

#define FFS_V1(NAME, EXPR)                                                                  \
	inline float2 NAME(const float2& a) { return Map1(a, [](float v) { return EXPR; }); }      \
	inline float3 NAME(const float3& a) { return Map1(a, [](float v) { return EXPR; }); }      \
	inline float4 NAME(const float4& a) { return Map1(a, [](float v) { return EXPR; }); }
FFS_V1(frac, frac(v)) FFS_V1(floor, std::floor(v)) FFS_V1(abs, std::fabs(v)) FFS_V1(sqrt, std::sqrt(v))
FFS_V1(saturate, saturate(v)) FFS_V1(sin, std::sin(v)) FFS_V1(cos, std::cos(v)) FFS_V1(exp, std::exp(v)) FFS_V1(sign, sign(v))

#define FFS_V2(NAME, EXPR)                                                                                       \
	inline float2 NAME(const float2& a, const float2& b) { return Map2(a, b, [](float p, float q) { return EXPR; }); } \
	inline float3 NAME(const float3& a, const float3& b) { return Map2(a, b, [](float p, float q) { return EXPR; }); } \
	inline float4 NAME(const float4& a, const float4& b) { return Map2(a, b, [](float p, float q) { return EXPR; }); }
FFS_V2(min, min(p, q)) FFS_V2(max, max(p, q)) FFS_V2(pow, std::pow(p, q)) FFS_V2(step, step(p, q))

template <typename T> inline T lerp(const T& a, const T& b, float t) { return a + (b - a) * t; }
template <typename T> inline T lerp(const T& a, const T& b, const T& t) { return a + (b - a) * t; }
template <typename T> inline T clamp(const T& x, float a, float b) { return Map1(x, [a, b](float v) { return clamp(v, a, b); }); }
inline float2 smoothstep(float a, float b, const float2& x) { return Map1(x, [a, b](float v) { return smoothstep(a, b, v); }); }
inline float3 smoothstep(float a, float b, const float3& x) { return Map1(x, [a, b](float v) { return smoothstep(a, b, v); }); }

inline float dot(const float2& a, const float2& b) { return a.x * b.x + a.y * b.y; }
inline float dot(const float3& a, const float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(const float4& a, const float4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float3 cross(const float3& a, const float3& b) {
	return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
template <typename T> inline float length(const T& v) { return std::sqrt(dot(v, v)); }
template <typename T> inline T normalize(const T& v) { return v * (1.0f / std::sqrt(dot(v, v))); }

// Texture stand-in: procedural tileable noise in each channel (the real T_FX_Noise is generated by
// Tools/vfx/noise_textures.py); only Sample / SampleLevel are used by the shared code.
struct SamplerState {};
struct Texture2D {
	float4 Sample(const SamplerState&, float2 uv) const;
	float4 SampleLevel(const SamplerState&, float2 uv, float) const { return Sample(SamplerState(), uv); }
};

}  // namespace hlsl

#define FF_OUT(T) T&
#define FF_INOUT(T) T&
#define FF_UNROLL
#define FF_TEX2D const Texture2D&
#define FF_SAMPLER const SamplerState&

#endif  // FF_LOGIC_TESTS
