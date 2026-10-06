// Fourfold core - GDScript-style helpers on ff::Value / Dict / Array (private header).
// d.get(k, def) with GDScript conversions: dnum = float(d.get(k, def)), dint = int(...), dbool = bool(...),
// dstr = String(...), dvec = Vector3, dtruthy = `if d.get(k):`. Missing keys and Nil values give the default.
#pragma once

#include "ff/Value.h"
#include "Util/GodotMath.h"

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ff {

using KV = std::pair<std::string_view, Value>;

// GDScript dictionary literal {"a": 1, "b": x}.
inline Dict D(std::initializer_list<KV> kv) {
	Dict d;
	for (const KV& p : kv) d.set(p.first, p.second);
	return d;
}
// GDScript array literal [a, b, c].
inline Array A(std::initializer_list<Value> v) { return Array(std::vector<Value>(v)); }

// ---- Value conversions
inline double vnum(const Value& v, double def = 0.0) { return v.is_nil() ? def : v.as_float(def); }
inline int vint(const Value& v, int def = 0) { return v.is_nil() ? def : static_cast<int>(v.as_int(def)); }
inline bool vbool(const Value& v, bool def = false) { return v.is_nil() ? def : v.as_bool(v.truthy()); }
inline std::string vstr(const Value& v, std::string_view def = "") {
	if (v.is_string()) return v.as_string();
	if (v.is_nil()) return std::string(def);
	if (v.is_int()) return std::to_string(v.as_int());
	return v.to_string();
}
inline Vec3 vvec(const Value& v, Vec3 def = Vec3()) { return v.is_vec3() ? v.as_vec3() : def; }

// ---- Dict getters (GDScript d.get(k, def) + conversion)
inline double dnum(const Dict& d, std::string_view k, double def = 0.0) { return vnum(d.get(k), def); }
inline int dint(const Dict& d, std::string_view k, int def = 0) { return vint(d.get(k), def); }
inline bool dbool(const Dict& d, std::string_view k, bool def = false) { return vbool(d.get(k), def); }
inline std::string dstr(const Dict& d, std::string_view k, std::string_view def = "") { return vstr(d.get(k), def); }
inline Vec3 dvec(const Dict& d, std::string_view k, Vec3 def = Vec3()) { return vvec(d.get(k), def); }
inline bool dtruthy(const Dict& d, std::string_view k) { return d.get(k).truthy(); }
// Shared handle of a nested dict / array (a NEW empty one when missing: like d.get(k, {})).
inline Dict ddict(const Dict& d, std::string_view k) { return d.get(k).as_dict(); }
inline Array darr(const Dict& d, std::string_view k) { return d.get(k).as_array(); }

inline double dnum(const Value& d, std::string_view k, double def = 0.0) { return vnum(d.get(k), def); }
inline int dint(const Value& d, std::string_view k, int def = 0) { return vint(d.get(k), def); }
inline bool dbool(const Value& d, std::string_view k, bool def = false) { return vbool(d.get(k), def); }
inline std::string dstr(const Value& d, std::string_view k, std::string_view def = "") { return vstr(d.get(k), def); }

// Array of strings / numbers contains (GDScript `[..].has(x)`).
inline bool arr_has_str(const Array& a, std::string_view s) {
	for (const Value& v : a)
		if (v.is_string() && v.as_string() == s) return true;
	return false;
}
inline bool arr_has_int(const Array& a, int64_t i) {
	for (const Value& v : a)
		if (v.is_number() && v == Value(i)) return true;
	return false;
}
template <size_t N>
inline bool in_list(std::string_view s, const char* const (&list)[N]) {
	for (const char* x : list)
		if (s == x) return true;
	return false;
}
inline bool in_list(std::string_view s, std::initializer_list<std::string_view> list) {
	for (std::string_view x : list)
		if (s == x) return true;
	return false;
}

// ---- text (locale independent)
inline std::string itos(int64_t i) { return std::to_string(i); }
// "%.Nf"
inline std::string ftos(double v, int decimals = 1) {
	char buf[64];
	const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, decimals);
	return std::string(buf, r.ptr);
}
inline bool begins_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
inline bool ends_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }
inline int to_int(std::string_view s) {
	int v = 0;
	const char* b = s.data();
	const char* e = s.data() + s.size();
	if (b != e && *b == '+') ++b;
	std::from_chars(b, e, v);
	return v;
}

}  // namespace ff
