// Fourfold core - JSON reader / writer (see ff/Json.h). Engine-free, no exceptions, locale-independent.
#include "ff/Json.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ff {
namespace {

struct JsonReader {
	std::string_view s;
	size_t i = 0;
	JsonError* err = nullptr;
	bool failed = false;
	int depth = 0;

	void Fail(const char* msg) {
		if (failed) return;
		failed = true;
		if (err) {
			err->offset = i;
			err->message = msg;
			int line = 1, col = 1;
			for (size_t k = 0; k < i && k < s.size(); ++k) {
				if (s[k] == '\n') { ++line; col = 1; } else { ++col; }
			}
			err->line = line;
			err->column = col;
		}
	}
	void Ws() {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
	}
	bool Lit(const char* w) {
		size_t n = 0;
		while (w[n]) ++n;
		if (s.substr(i, n) == std::string_view(w, n)) { i += n; return true; }
		return false;
	}
	static void PutUtf8(std::string& o, uint32_t cp) {
		if (cp < 0x80) o.push_back(static_cast<char>(cp));
		else if (cp < 0x800) { o.push_back(static_cast<char>(0xC0 | (cp >> 6))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		else if (cp < 0x10000) {
			o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}
	bool Hex4(uint32_t& out) {
		if (i + 4 > s.size()) return false;
		uint32_t v = 0;
		for (int k = 0; k < 4; ++k) {
			const char c = s[i + static_cast<size_t>(k)];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
			else return false;
		}
		i += 4;
		out = v;
		return true;
	}
	bool Str(std::string& o) {
		if (i >= s.size() || s[i] != '"') { Fail("expected string"); return false; }
		++i;
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') return true;
			if (c == '\\') {
				if (i >= s.size()) break;
				const char e = s[i++];
				switch (e) {
					case '"': o.push_back('"'); break;
					case '\\': o.push_back('\\'); break;
					case '/': o.push_back('/'); break;
					case 'b': o.push_back('\b'); break;
					case 'f': o.push_back('\f'); break;
					case 'n': o.push_back('\n'); break;
					case 'r': o.push_back('\r'); break;
					case 't': o.push_back('\t'); break;
					case 'u': {
						uint32_t cp = 0;
						if (!Hex4(cp)) { Fail("bad \\u escape"); return false; }
						if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
							i += 2;
							uint32_t lo = 0;
							if (!Hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { Fail("bad surrogate pair"); return false; }
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						PutUtf8(o, cp);
						break;
					}
					default: Fail("bad escape"); return false;
				}
			} else {
				o.push_back(c);
			}
		}
		Fail("unterminated string");
		return false;
	}
	// Locale-independent decimal -> double: exact fast path (Clinger) for <= 15 digits and |exp10| <= 22.
	static double Pow10(int e) {
		static const double kExact[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
		                                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
		if (e >= 0 && e <= 22) return kExact[e];
		if (e < 0 && e >= -22) return 1.0 / kExact[-e];
		return std::pow(10.0, static_cast<double>(e));
	}
	bool Num(Value& out) {
		const size_t start = i;
		bool neg = false;
		if (s[i] == '-') { neg = true; ++i; }
		uint64_t mant = 0;
		int digits = 0, dropped = 0, frac = 0;
		bool isFloat = false, any = false;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
			any = true;
			if (digits < 19) { mant = mant * 10 + static_cast<uint64_t>(s[i] - '0'); if (mant) ++digits; }
			else ++dropped;
			++i;
		}
		if (i < s.size() && s[i] == '.') {
			isFloat = true;
			++i;
			while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
				any = true;
				if (digits < 19) { mant = mant * 10 + static_cast<uint64_t>(s[i] - '0'); if (mant) ++digits; ++frac; }
				++i;
			}
		}
		int exp10 = 0;
		if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
			isFloat = true;
			++i;
			bool eneg = false;
			if (i < s.size() && (s[i] == '+' || s[i] == '-')) { eneg = s[i] == '-'; ++i; }
			int ev = 0;
			bool edig = false;
			while (i < s.size() && s[i] >= '0' && s[i] <= '9') { if (ev < 10000) ev = ev * 10 + (s[i] - '0'); ++i; edig = true; }
			if (!edig) { Fail("bad exponent"); return false; }
			exp10 = eneg ? -ev : ev;
		}
		if (!any) { Fail("bad number"); return false; }
		if (!isFloat) {
			int64_t iv = 0;
			const auto r = std::from_chars(s.data() + start, s.data() + i, iv);
			if (r.ec == std::errc()) { out = Value(iv); return true; }
			isFloat = true;   // out of int64 range: fall through as a float
		}
		const int e = exp10 + dropped - frac;
		double v;
		if (mant <= (uint64_t(1) << 53) && e >= -22 && e <= 22) {
			v = static_cast<double>(mant);
			v = e >= 0 ? v * Pow10(e) : v / Pow10(-e);
		} else {
			long double lv = static_cast<long double>(mant);
			lv *= std::pow(10.0L, static_cast<long double>(e));
			v = static_cast<double>(lv);
		}
		out = Value(neg ? -v : v);
		return true;
	}
	static Value Decode(Dict d) {
		if (d.size() != 1) return Value(std::move(d));
		const auto& item = d.items().front();
		const std::string& k = item.first;
		const Value& v = item.second;
		if (k.empty() || k[0] != '$') return Value(std::move(d));
		if (k == "$v3" && v.is_array() && v.as_array().size() == 3) {
			const Array a = v.as_array();
			return Value(Vec3(a[0].as_f32(), a[1].as_f32(), a[2].as_f32()));
		}
		if (k == "$v2" && v.is_array() && v.as_array().size() == 2) {
			const Array a = v.as_array();
			return Value(Vec2(a[0].as_f32(), a[1].as_f32()));
		}
		if (k == "$v3a" && v.is_array()) {
			Array out;
			for (const Value& p : v.as_array()) {
				const Array a = p.as_array();
				if (a.size() == 3) out.append(Value(Vec3(a[0].as_f32(), a[1].as_f32(), a[2].as_f32())));
			}
			return Value(out);
		}
		if (k == "$color" && v.is_array()) {
			Array out;
			for (const Value& c : v.as_array()) out.append(Value(c.as_float()));
			return Value(out);
		}
		if (k == "$map" && v.is_array()) {
			Dict out;
			for (const Value& pr : v.as_array()) {
				const Array a = pr.as_array();
				if (a.size() != 2) continue;
				std::string key = a[0].is_string() ? a[0].as_string() : (a[0].is_int() ? std::to_string(a[0].as_int()) : a[0].to_string());
				out.set(key, a[1]);
			}
			return Value(out);
		}
		if (k == "$f" && v.is_string()) {
			const std::string& t = v.as_string();
			if (t == "inf") return Value(std::numeric_limits<double>::infinity());
			if (t == "-inf") return Value(-std::numeric_limits<double>::infinity());
			return Value(std::numeric_limits<double>::quiet_NaN());
		}
		if (k == "$fn" && v.is_string()) return Value("fn:" + v.as_string());
		return Value(std::move(d));
	}
	bool Val(Value& out) {
		if (++depth > 256) { Fail("nesting too deep"); return false; }
		Ws();
		if (i >= s.size()) { Fail("unexpected end"); return false; }
		const char c = s[i];
		bool ok = true;
		if (c == '{') {
			++i;
			Dict d;
			Ws();
			if (i < s.size() && s[i] == '}') { ++i; }
			else {
				while (true) {
					Ws();
					std::string key;
					if (!Str(key)) { ok = false; break; }
					Ws();
					if (i >= s.size() || s[i] != ':') { Fail("expected ':'"); ok = false; break; }
					++i;
					Value v;
					if (!Val(v)) { ok = false; break; }
					d.set(key, std::move(v));
					Ws();
					if (i < s.size() && s[i] == ',') { ++i; continue; }
					if (i < s.size() && s[i] == '}') { ++i; break; }
					Fail("expected ',' or '}'");
					ok = false;
					break;
				}
			}
			if (ok) out = Decode(std::move(d));
		} else if (c == '[') {
			++i;
			Array a;
			Ws();
			if (i < s.size() && s[i] == ']') { ++i; }
			else {
				while (true) {
					Value v;
					if (!Val(v)) { ok = false; break; }
					a.append(std::move(v));
					Ws();
					if (i < s.size() && s[i] == ',') { ++i; continue; }
					if (i < s.size() && s[i] == ']') { ++i; break; }
					Fail("expected ',' or ']'");
					ok = false;
					break;
				}
			}
			if (ok) out = Value(a);
		} else if (c == '"') {
			std::string str;
			ok = Str(str);
			if (ok) out = Value(std::move(str));
		} else if (c == 't') {
			ok = Lit("true");
			if (ok) out = Value(true); else Fail("bad literal");
		} else if (c == 'f') {
			ok = Lit("false");
			if (ok) out = Value(false); else Fail("bad literal");
		} else if (c == 'n') {
			ok = Lit("null");
			if (ok) out = Value(); else Fail("bad literal");
		} else if (c == '-' || (c >= '0' && c <= '9')) {
			ok = Num(out);
		} else {
			Fail("unexpected character");
			ok = false;
		}
		--depth;
		return ok && !failed;
	}
};

void WriteString(std::string& o, const std::string& s) {
	o.push_back('"');
	for (const char ch : s) {
		const unsigned char c = static_cast<unsigned char>(ch);
		switch (c) {
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break;
			case '\r': o += "\\r"; break;
			case '\t': o += "\\t"; break;
			case '\b': o += "\\b"; break;
			case '\f': o += "\\f"; break;
			default:
				if (c < 0x20) {
					static const char* hex = "0123456789abcdef";
					o += "\\u00";
					o.push_back(hex[c >> 4]);
					o.push_back(hex[c & 15]);
				} else {
					o.push_back(static_cast<char>(c));
				}
		}
	}
	o.push_back('"');
}

void WriteDouble(std::string& o, double d) {
	if (!(d == d)) { o += "{\"$f\":\"nan\"}"; return; }
	if (d == std::numeric_limits<double>::infinity()) { o += "{\"$f\":\"inf\"}"; return; }
	if (d == -std::numeric_limits<double>::infinity()) { o += "{\"$f\":\"-inf\"}"; return; }
	char buf[64];
	const auto r = std::to_chars(buf, buf + sizeof(buf), d);
	std::string t(buf, r.ptr);
	if (t.find_first_of(".eE") == std::string::npos) t += ".0";
	o += t;
}

void Indent(std::string& o, int indent, int level) {
	if (indent < 0) return;
	o.push_back('\n');
	o.append(static_cast<size_t>(indent * level), ' ');
}

void Write(std::string& o, const Value& v, int indent, int level) {
	switch (v.type()) {
		case Value::Type::Null: o += "null"; break;
		case Value::Type::Bool: o += v.as_bool() ? "true" : "false"; break;
		case Value::Type::Int: o += std::to_string(v.as_int()); break;
		case Value::Type::Float: WriteDouble(o, v.as_float()); break;
		case Value::Type::String: WriteString(o, v.as_string()); break;
		case Value::Type::Vec2: {
			const Vec2 p = v.as_vec2();
			o += "{\"$v2\":[";
			WriteDouble(o, p.x); o += ","; WriteDouble(o, p.y);
			o += "]}";
			break;
		}
		case Value::Type::Vec3: {
			const Vec3 p = v.as_vec3();
			o += "{\"$v3\":[";
			WriteDouble(o, p.x); o += ","; WriteDouble(o, p.y); o += ","; WriteDouble(o, p.z);
			o += "]}";
			break;
		}
		case Value::Type::Array: {
			const Array* a = v.array_ptr();
			if (a->empty()) { o += "[]"; break; }
			o.push_back('[');
			for (size_t k = 0; k < a->size(); ++k) {
				if (k) o.push_back(',');
				Indent(o, indent, level + 1);
				Write(o, (*a)[k], indent, level + 1);
			}
			Indent(o, indent, level);
			o.push_back(']');
			break;
		}
		case Value::Type::Dict: {
			const Dict* d = v.dict_ptr();
			if (d->empty()) { o += "{}"; break; }
			o.push_back('{');
			bool first = true;
			for (const auto& it : d->items()) {
				if (!first) o.push_back(',');
				first = false;
				Indent(o, indent, level + 1);
				WriteString(o, it.first);
				o += indent < 0 ? ":" : ": ";
				Write(o, it.second, indent, level + 1);
			}
			Indent(o, indent, level);
			o.push_back('}');
			break;
		}
	}
}

}  // namespace

bool ParseJson(std::string_view text, Value& out, JsonError* err) {
	JsonReader r;
	r.s = text;
	r.err = err;
	Value v;
	if (!r.Val(v)) {
		out = Value();
		return false;
	}
	r.Ws();
	if (r.i != text.size()) {
		r.Fail("trailing characters");
		out = Value();
		return false;
	}
	out = std::move(v);
	return true;
}

std::string ToJson(const Value& v, int indent) {
	std::string o;
	Write(o, v, indent, 0);
	return o;
}

}  // namespace ff
