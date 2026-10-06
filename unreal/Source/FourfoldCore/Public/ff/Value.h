// Fourfold core - dynamic value with GDScript semantics (header-only).
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive changes only; keep the semantics).
//
// Why: the Godot sim is data-driven (move defs, rules, inst.data, body.props and every event are Dictionaries).
// A faithful port keeps that shape. Semantics follow GDScript exactly where it matters:
//  * Array and Dict are REFERENCE types: copying a Value/Array/Dict copies the handle, both see the same storage
//    (like `var d = def.tiers`); use duplicate(deep) for a real copy.
//  * Dict keeps insertion order (Godot Dictionary); keys are strings (int-keyed Godot data is stored with decimal
//    string keys "0", "1", ...).
//  * Numbers: Int (int64) and Float (double); == compares numerically across both (GDScript 1 == 1.0).
//  * Missing keys read as Nil; get(key, def) returns def when missing (GDScript d.get(k, def)).
//  * Vectors are float32 (ff::Vec2 / ff::Vec3) like Godot.
// No exceptions, no RTTI (Unreal builds with both disabled).
#pragma once

#include "ff/Config.h"
#include "ff/Math.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ff {

class Value;

class Array {
public:
	Array();
	explicit Array(std::vector<Value> items);

	size_t size() const;
	bool empty() const;
	Value& operator[](size_t i);
	const Value& operator[](size_t i) const;
	Value get(size_t i) const;              // Nil when out of range
	void append(Value v);                   // GDScript append / push_back
	void push_back(Value v);
	void insert(size_t i, Value v);
	void remove_at(size_t i);
	bool erase(const Value& v);             // first equal element (GDScript erase)
	bool has(const Value& v) const;
	int64_t find(const Value& v) const;     // -1 when absent
	void clear();
	void resize(size_t n);
	Value pop_back();
	Value back() const;
	Value front() const;
	Array duplicate(bool deep = false) const;
	bool same(const Array& o) const { return d_ == o.d_; }   // identity (GDScript is_same)
	std::vector<Value>& items();
	const std::vector<Value>& items() const;
	std::vector<Value>::iterator begin();
	std::vector<Value>::iterator end();
	std::vector<Value>::const_iterator begin() const;
	std::vector<Value>::const_iterator end() const;

private:
	std::shared_ptr<std::vector<Value>> d_;
};

class Dict {
public:
	using Item = std::pair<std::string, Value>;
	Dict();

	size_t size() const;
	bool empty() const;
	bool has(std::string_view key) const;
	const Value& get(std::string_view key) const;                    // Nil when missing
	Value get(std::string_view key, const Value& def) const;         // GDScript d.get(k, def)
	Value& operator[](std::string_view key);                         // inserts Nil when missing (GDScript d[k] = v)
	void set(std::string_view key, Value v);
	bool erase(std::string_view key);
	void clear();
	void merge(const Dict& o, bool overwrite = false);               // GDScript merge (default: keep existing)
	Dict duplicate(bool deep = false) const;
	std::vector<std::string> keys() const;
	const std::vector<Item>& items() const;                          // insertion order
	bool same(const Dict& o) const { return d_ == o.d_; }
	std::vector<Item>::const_iterator begin() const;
	std::vector<Item>::const_iterator end() const;

private:
	struct Data {
		std::vector<Item> items;
		std::unordered_map<std::string, size_t> index;   // built when items.size() > kIndexAt
	};
	static constexpr size_t kIndexAt = 12;
	int64_t slot(std::string_view key) const;
	void reindex() const;
	std::shared_ptr<Data> d_;
};

class Value {
public:
	enum class Type : uint8_t { Null, Bool, Int, Float, String, Vec2, Vec3, Array, Dict };

	Value() noexcept : v_(std::monostate{}) {}
	Value(std::nullptr_t) noexcept : v_(std::monostate{}) {}
	Value(bool b) : v_(b) {}
	Value(int i) : v_(static_cast<int64_t>(i)) {}
	Value(long i) : v_(static_cast<int64_t>(i)) {}
	Value(long long i) : v_(static_cast<int64_t>(i)) {}
	Value(unsigned i) : v_(static_cast<int64_t>(i)) {}
	Value(unsigned long i) : v_(static_cast<int64_t>(i)) {}
	Value(unsigned long long i) : v_(static_cast<int64_t>(i)) {}
	Value(double f) : v_(f) {}
	Value(float f) : v_(static_cast<double>(f)) {}
	Value(const char* s) : v_(std::string(s ? s : "")) {}
	Value(std::string s) : v_(std::move(s)) {}
	Value(std::string_view s) : v_(std::string(s)) {}
	Value(Vec2 v) : v_(v) {}
	Value(Vec3 v) : v_(v) {}
	Value(Array a) : v_(std::move(a)) {}
	Value(Dict d) : v_(std::move(d)) {}

	Type type() const { return static_cast<Type>(v_.index()); }
	bool is_nil() const { return type() == Type::Null; }
	bool is_bool() const { return type() == Type::Bool; }
	bool is_int() const { return type() == Type::Int; }
	bool is_float() const { return type() == Type::Float; }
	bool is_number() const { return type() == Type::Int || type() == Type::Float; }
	bool is_string() const { return type() == Type::String; }
	bool is_vec2() const { return type() == Type::Vec2; }
	bool is_vec3() const { return type() == Type::Vec3; }
	bool is_array() const { return type() == Type::Array; }
	bool is_dict() const { return type() == Type::Dict; }

	// GDScript truthiness (if x:): Nil false, numbers != 0, strings / arrays / dicts non-empty, vectors non-zero.
	bool truthy() const;
	// Conversions (GDScript bool()/int()/float()): numbers convert, bool -> 0/1, anything else -> def.
	bool as_bool(bool def = false) const;
	int64_t as_int(int64_t def = 0) const;
	double as_float(double def = 0.0) const;
	float as_f32(float def = 0.0f) const { return static_cast<float>(as_float(def)); }
	const std::string& as_string() const;          // "" when not a string
	Vec2 as_vec2(Vec2 def = {}) const;
	Vec3 as_vec3(Vec3 def = {}) const;
	Array as_array() const;                        // shared handle; a new empty Array when not an array
	Dict as_dict() const;                          // shared handle; a new empty Dict when not a dict
	Array* array_ptr();                            // nullptr when not an array
	Dict* dict_ptr();
	const Array* array_ptr() const;
	const Dict* dict_ptr() const;

	// Dict convenience (Nil / def when this is not a Dict or the key is missing).
	const Value& operator[](std::string_view key) const;
	Value get(std::string_view key, const Value& def = Value()) const;
	bool has(std::string_view key) const;

	bool operator==(const Value& o) const;
	bool operator!=(const Value& o) const { return !(*this == o); }

	Value duplicate(bool deep = false) const;      // arrays / dicts copied, scalars returned as is
	std::string to_string() const;                 // debug text (close to GDScript str())

	static const Value& null_value();

private:
	std::variant<std::monostate, bool, int64_t, double, std::string, Vec2, Vec3, Array, Dict> v_;
};

// ============================================================================ inline implementation

inline const Value& Value::null_value() {
	static const Value n;
	return n;
}

// ---- Array
inline Array::Array() : d_(std::make_shared<std::vector<Value>>()) {}
inline Array::Array(std::vector<Value> items) : d_(std::make_shared<std::vector<Value>>(std::move(items))) {}
inline size_t Array::size() const { return d_->size(); }
inline bool Array::empty() const { return d_->empty(); }
inline Value& Array::operator[](size_t i) { return (*d_)[i]; }
inline const Value& Array::operator[](size_t i) const { return (*d_)[i]; }
inline Value Array::get(size_t i) const { return i < d_->size() ? (*d_)[i] : Value(); }
inline void Array::append(Value v) { d_->push_back(std::move(v)); }
inline void Array::push_back(Value v) { d_->push_back(std::move(v)); }
inline void Array::insert(size_t i, Value v) {
	if (i > d_->size()) i = d_->size();
	d_->insert(d_->begin() + static_cast<std::ptrdiff_t>(i), std::move(v));
}
inline void Array::remove_at(size_t i) {
	if (i < d_->size()) d_->erase(d_->begin() + static_cast<std::ptrdiff_t>(i));
}
inline int64_t Array::find(const Value& v) const {
	for (size_t i = 0; i < d_->size(); ++i)
		if ((*d_)[i] == v) return static_cast<int64_t>(i);
	return -1;
}
inline bool Array::has(const Value& v) const { return find(v) >= 0; }
inline bool Array::erase(const Value& v) {
	const int64_t i = find(v);
	if (i < 0) return false;
	remove_at(static_cast<size_t>(i));
	return true;
}
inline void Array::clear() { d_->clear(); }
inline void Array::resize(size_t n) { d_->resize(n); }
inline Value Array::pop_back() {
	if (d_->empty()) return Value();
	Value v = std::move(d_->back());
	d_->pop_back();
	return v;
}
inline Value Array::back() const { return d_->empty() ? Value() : d_->back(); }
inline Value Array::front() const { return d_->empty() ? Value() : d_->front(); }
inline Array Array::duplicate(bool deep) const {
	std::vector<Value> out;
	out.reserve(d_->size());
	for (const Value& v : *d_) out.push_back(deep ? v.duplicate(true) : v);
	return Array(std::move(out));
}
inline std::vector<Value>& Array::items() { return *d_; }
inline const std::vector<Value>& Array::items() const { return *d_; }
inline std::vector<Value>::iterator Array::begin() { return d_->begin(); }
inline std::vector<Value>::iterator Array::end() { return d_->end(); }
inline std::vector<Value>::const_iterator Array::begin() const { return d_->cbegin(); }
inline std::vector<Value>::const_iterator Array::end() const { return d_->cend(); }

// ---- Dict
inline Dict::Dict() : d_(std::make_shared<Data>()) {}
inline size_t Dict::size() const { return d_->items.size(); }
inline bool Dict::empty() const { return d_->items.empty(); }
inline void Dict::reindex() const {
	d_->index.clear();
	if (d_->items.size() > kIndexAt)
		for (size_t i = 0; i < d_->items.size(); ++i) d_->index.emplace(d_->items[i].first, i);
}
inline int64_t Dict::slot(std::string_view key) const {
	if (d_->items.size() > kIndexAt) {
		if (d_->index.size() != d_->items.size()) reindex();
		auto it = d_->index.find(std::string(key));
		return it == d_->index.end() ? -1 : static_cast<int64_t>(it->second);
	}
	for (size_t i = 0; i < d_->items.size(); ++i)
		if (d_->items[i].first == key) return static_cast<int64_t>(i);
	return -1;
}
inline bool Dict::has(std::string_view key) const { return slot(key) >= 0; }
inline const Value& Dict::get(std::string_view key) const {
	const int64_t i = slot(key);
	return i < 0 ? Value::null_value() : d_->items[static_cast<size_t>(i)].second;
}
inline Value Dict::get(std::string_view key, const Value& def) const {
	const int64_t i = slot(key);
	return i < 0 ? def : d_->items[static_cast<size_t>(i)].second;
}
inline Value& Dict::operator[](std::string_view key) {
	const int64_t i = slot(key);
	if (i >= 0) return d_->items[static_cast<size_t>(i)].second;
	d_->items.emplace_back(std::string(key), Value());
	if (d_->items.size() > kIndexAt) {
		if (d_->index.size() + 1 == d_->items.size())
			d_->index.emplace(d_->items.back().first, d_->items.size() - 1);
		else
			reindex();
	}
	return d_->items.back().second;
}
inline void Dict::set(std::string_view key, Value v) { (*this)[key] = std::move(v); }
inline bool Dict::erase(std::string_view key) {
	const int64_t i = slot(key);
	if (i < 0) return false;
	d_->items.erase(d_->items.begin() + i);
	reindex();
	return true;
}
inline void Dict::clear() {
	d_->items.clear();
	d_->index.clear();
}
inline void Dict::merge(const Dict& o, bool overwrite) {
	if (o.d_ == d_) return;
	for (const Item& it : o.d_->items)
		if (overwrite || !has(it.first)) (*this)[it.first] = it.second;
}
inline Dict Dict::duplicate(bool deep) const {
	Dict out;
	out.d_->items.reserve(d_->items.size());
	for (const Item& it : d_->items) out.d_->items.emplace_back(it.first, deep ? it.second.duplicate(true) : it.second);
	out.reindex();
	return out;
}
inline std::vector<std::string> Dict::keys() const {
	std::vector<std::string> k;
	k.reserve(d_->items.size());
	for (const Item& it : d_->items) k.push_back(it.first);
	return k;
}
inline const std::vector<Dict::Item>& Dict::items() const { return d_->items; }
inline std::vector<Dict::Item>::const_iterator Dict::begin() const { return d_->items.cbegin(); }
inline std::vector<Dict::Item>::const_iterator Dict::end() const { return d_->items.cend(); }

// ---- Value
inline bool Value::truthy() const {
	switch (type()) {
		case Type::Null: return false;
		case Type::Bool: return std::get<bool>(v_);
		case Type::Int: return std::get<int64_t>(v_) != 0;
		case Type::Float: return std::get<double>(v_) != 0.0;
		case Type::String: return !std::get<std::string>(v_).empty();
		case Type::Vec2: return std::get<Vec2>(v_) != Vec2{};
		case Type::Vec3: return std::get<Vec3>(v_) != Vec3{};
		case Type::Array: return !std::get<Array>(v_).empty();
		case Type::Dict: return !std::get<Dict>(v_).empty();
	}
	return false;
}
inline bool Value::as_bool(bool def) const {
	switch (type()) {
		case Type::Bool: return std::get<bool>(v_);
		case Type::Int: return std::get<int64_t>(v_) != 0;
		case Type::Float: return std::get<double>(v_) != 0.0;
		default: return def;
	}
}
inline int64_t Value::as_int(int64_t def) const {
	switch (type()) {
		case Type::Int: return std::get<int64_t>(v_);
		case Type::Float: {
			const double f = std::get<double>(v_);
			if (!(f == f)) return def;   // NaN guard without isnan (fast-math safe enough)
			return static_cast<int64_t>(f);
		}
		case Type::Bool: return std::get<bool>(v_) ? 1 : 0;
		default: return def;
	}
}
inline double Value::as_float(double def) const {
	switch (type()) {
		case Type::Float: return std::get<double>(v_);
		case Type::Int: return static_cast<double>(std::get<int64_t>(v_));
		case Type::Bool: return std::get<bool>(v_) ? 1.0 : 0.0;
		default: return def;
	}
}
inline const std::string& Value::as_string() const {
	static const std::string empty;
	return type() == Type::String ? std::get<std::string>(v_) : empty;
}
inline Vec2 Value::as_vec2(Vec2 def) const { return type() == Type::Vec2 ? std::get<Vec2>(v_) : def; }
inline Vec3 Value::as_vec3(Vec3 def) const { return type() == Type::Vec3 ? std::get<Vec3>(v_) : def; }
inline Array Value::as_array() const { return type() == Type::Array ? std::get<Array>(v_) : Array(); }
inline Dict Value::as_dict() const { return type() == Type::Dict ? std::get<Dict>(v_) : Dict(); }
inline Array* Value::array_ptr() { return type() == Type::Array ? &std::get<Array>(v_) : nullptr; }
inline Dict* Value::dict_ptr() { return type() == Type::Dict ? &std::get<Dict>(v_) : nullptr; }
inline const Array* Value::array_ptr() const { return type() == Type::Array ? &std::get<Array>(v_) : nullptr; }
inline const Dict* Value::dict_ptr() const { return type() == Type::Dict ? &std::get<Dict>(v_) : nullptr; }
inline const Value& Value::operator[](std::string_view key) const {
	const Dict* d = dict_ptr();
	return d ? d->get(key) : null_value();
}
inline Value Value::get(std::string_view key, const Value& def) const {
	const Dict* d = dict_ptr();
	return d ? d->get(key, def) : def;
}
inline bool Value::has(std::string_view key) const {
	const Dict* d = dict_ptr();
	return d && d->has(key);
}
inline bool Value::operator==(const Value& o) const {
	if (is_number() && o.is_number()) {
		if (is_int() && o.is_int()) return std::get<int64_t>(v_) == std::get<int64_t>(o.v_);
		return as_float() == o.as_float();
	}
	if (type() != o.type()) return false;
	switch (type()) {
		case Type::Null: return true;
		case Type::Bool: return std::get<bool>(v_) == std::get<bool>(o.v_);
		case Type::String: return std::get<std::string>(v_) == std::get<std::string>(o.v_);
		case Type::Vec2: return std::get<Vec2>(v_) == std::get<Vec2>(o.v_);
		case Type::Vec3: return std::get<Vec3>(v_) == std::get<Vec3>(o.v_);
		case Type::Array: {
			const Array& a = std::get<Array>(v_);
			const Array& b = std::get<Array>(o.v_);
			if (a.same(b)) return true;
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i] != b[i]) return false;
			return true;
		}
		case Type::Dict: {
			const Dict& a = std::get<Dict>(v_);
			const Dict& b = std::get<Dict>(o.v_);
			if (a.same(b)) return true;
			if (a.size() != b.size()) return false;
			for (const auto& it : a.items()) {
				if (!b.has(it.first) || b.get(it.first) != it.second) return false;
			}
			return true;
		}
		default: return false;
	}
}
inline Value Value::duplicate(bool deep) const {
	if (type() == Type::Array) return Value(std::get<Array>(v_).duplicate(deep));
	if (type() == Type::Dict) return Value(std::get<Dict>(v_).duplicate(deep));
	return *this;
}
inline std::string Value::to_string() const {
	switch (type()) {
		case Type::Null: return "<null>";
		case Type::Bool: return std::get<bool>(v_) ? "true" : "false";
		case Type::Int: return std::to_string(std::get<int64_t>(v_));
		case Type::Float: {
			std::string s = std::to_string(std::get<double>(v_));
			while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
			return s;
		}
		case Type::String: return std::get<std::string>(v_);
		case Type::Vec2: {
			const Vec2 v = std::get<Vec2>(v_);
			return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
		}
		case Type::Vec3: {
			const Vec3 v = std::get<Vec3>(v_);
			return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
		}
		case Type::Array: {
			std::string s = "[";
			const Array& a = std::get<Array>(v_);
			for (size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + a[i].to_string();
			return s + "]";
		}
		case Type::Dict: {
			std::string s = "{";
			bool first = true;
			for (const auto& it : std::get<Dict>(v_).items()) {
				s += (first ? "" : ", ") + std::string("\"") + it.first + "\": " + it.second.to_string();
				first = false;
			}
			return s + "}";
		}
	}
	return "";
}

}  // namespace ff
