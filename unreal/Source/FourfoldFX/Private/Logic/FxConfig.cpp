// FourfoldFX logic island - look data defaults + JSON overrides. Owner: stream `fx`.
#include "FxConfig.h"

#include "ff/Json.h"
#include "ff/Value.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace ffx {

Color Linear(const Color& c) {
	auto lin = [](float v) {
		v = Clamp(v, 0.0f, 64.0f);
		if (v <= 0.04045f) return v / 12.92f;
		if (v <= 1.0f) return std::pow((v + 0.055f) / 1.055f, 2.4f);
		return v * v;   // HDR tints above 1 (rare): keep them monotonic
	};
	return Color(lin(c.r), lin(c.g), lin(c.b), c.a);
}

namespace {

// ------------------------------------------------------------------------------------- field descriptors
enum class FT : uint8_t { F, C, B, Fb, Fam_ };
struct FieldDesc {
	const char* name;
	FT type;
	size_t off;
};

const FieldDesc kBurstFields[] = {
	{"puff", FT::C, offsetof(BurstStyle, puff)},           {"spark", FT::C, offsetof(BurstStyle, spark)},
	{"has_puff", FT::B, offsetof(BurstStyle, hasPuff)},    {"has_spark", FT::B, offsetof(BurstStyle, hasSpark)},
	{"speed", FT::F, offsetof(BurstStyle, speed)},         {"gravity", FT::F, offsetof(BurstStyle, gravity)},
	{"spark_gravity", FT::F, offsetof(BurstStyle, sparkGravity)}, {"spread", FT::F, offsetof(BurstStyle, spread)},
	{"dur", FT::F, offsetof(BurstStyle, dur)},             {"alpha", FT::F, offsetof(BurstStyle, alpha)},
	{"spark_speed", FT::F, offsetof(BurstStyle, sparkSpeed)}, {"flipbook", FT::Fb, offsetof(BurstStyle, flipbook)},
};
const FieldDesc kCloudFields[] = {
	{"size", FT::F, offsetof(CloudLook, size)},       {"swirl", FT::F, offsetof(CloudLook, swirl)},
	{"rise", FT::F, offsetof(CloudLook, rise)},       {"column", FT::F, offsetof(CloudLook, column)},
	{"flatten", FT::F, offsetof(CloudLook, flatten)}, {"grow", FT::F, offsetof(CloudLook, grow)},
	{"wander", FT::F, offsetof(CloudLook, wander)},   {"stretch", FT::F, offsetof(CloudLook, stretch)},
	{"opacity", FT::F, offsetof(CloudLook, opacity)}, {"opacity_core", FT::F, offsetof(CloudLook, opacityCore)},
	{"top", FT::C, offsetof(CloudLook, top)},         {"low", FT::C, offsetof(CloudLook, low)},
};
const FieldDesc kShellFields[] = {
	{"fam", FT::Fam_, offsetof(ShellLook, fam)},          {"color", FT::C, offsetof(ShellLook, color)},
	{"core_color", FT::C, offsetof(ShellLook, coreColor)}, {"rim", FT::F, offsetof(ShellLook, rim)},
	{"streak", FT::F, offsetof(ShellLook, streak)},       {"crackle", FT::F, offsetof(ShellLook, crackle)},
	{"core", FT::F, offsetof(ShellLook, core)},           {"pulse", FT::F, offsetof(ShellLook, pulse)},
	{"opacity", FT::F, offsetof(ShellLook, opacity)},     {"glow", FT::F, offsetof(ShellLook, glow)},
	{"absorb", FT::F, offsetof(ShellLook, absorb)},       {"refract", FT::F, offsetof(ShellLook, refract)},
	{"spiral", FT::B, offsetof(ShellLook, spiral)},       {"arcs", FT::B, offsetof(ShellLook, arcs)},
};
const FieldDesc kVortexFields[] = {
	{"a", FT::C, offsetof(VortexLook, a)},          {"b", FT::C, offsetof(VortexLook, b)},
	{"opacity", FT::F, offsetof(VortexLook, opacity)}, {"cover", FT::F, offsetof(VortexLook, cover)},
	{"glow", FT::F, offsetof(VortexLook, glow)},    {"debris", FT::B, offsetof(VortexLook, debris)},
};
const FieldDesc kCrystalFields[] = {
	{"tint", FT::C, offsetof(CrystalLook, tint)},      {"frost", FT::F, offsetof(CrystalLook, frost)},
	{"opacity", FT::F, offsetof(CrystalLook, opacity)}, {"edge", FT::F, offsetof(CrystalLook, edge)},
};
const FieldDesc kBeamFields[] = {
	{"core", FT::C, offsetof(BeamLook, core)},    {"glow", FT::C, offsetof(BeamLook, glow)},
	{"width", FT::F, offsetof(BeamLook, width)},  {"cover", FT::F, offsetof(BeamLook, cover)},
	{"taper", FT::F, offsetof(BeamLook, taper)},  {"grain", FT::F, offsetof(BeamLook, grain)},
	{"light", FT::B, offsetof(BeamLook, light)},
};
const FieldDesc kStripFields[] = {
	{"height_front", FT::F, offsetof(StripLook, heightFront)},
	{"height_tail", FT::F, offsetof(StripLook, heightTail)},
	{"bulge", FT::F, offsetof(StripLook, bulge)},
};
const FieldDesc kLightFields[] = {
	{"intensity_scale", FT::F, offsetof(LightSettings, intensityScale)},
	{"radius_scale", FT::F, offsetof(LightSettings, radiusScale)},
	{"min_intensity", FT::F, offsetof(LightSettings, minIntensity)},
};
// QualityLevel holds ints: stored as floats in JSON, read back rounded.
enum class QT : uint8_t { F, I, B };
struct QField {
	const char* name;
	QT type;
	size_t off;
};
const QField kQualityFields[] = {
	{"particles", QT::F, offsetof(QualityLevel, particles)},   {"cloud_outer", QT::I, offsetof(QualityLevel, cloudOuter)},
	{"cloud_core", QT::I, offsetof(QualityLevel, cloudCore)},  {"flames_max", QT::I, offsetof(QualityLevel, flamesMax)},
	{"shards_max", QT::I, offsetof(QualityLevel, shardsMax)},  {"debris", QT::I, offsetof(QualityLevel, debris)},
	{"bolt_levels", QT::I, offsetof(QualityLevel, boltLevels)}, {"max_lights", QT::I, offsetof(QualityLevel, maxLights)},
	{"soft_particles", QT::B, offsetof(QualityLevel, softParticles)},
	{"distortion", QT::B, offsetof(QualityLevel, distortion)},
};

ff::Value ColorToValue(const Color& c) {
	auto r3 = [](float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; };
	return ff::Value(ff::Array({ff::Value(r3(c.r)), ff::Value(r3(c.g)), ff::Value(r3(c.b)), ff::Value(r3(c.a))}));
}
bool ValueToColor(const ff::Value& v, Color& out) {
	const ff::Array* a = v.array_ptr();
	if (!a || a->size() < 3) return false;
	out.r = (*a)[0].as_f32(out.r);
	out.g = (*a)[1].as_f32(out.g);
	out.b = (*a)[2].as_f32(out.b);
	out.a = a->size() > 3 ? (*a)[3].as_f32(1.0f) : 1.0f;
	return true;
}
double Round4(float v) { return std::round(static_cast<double>(v) * 10000.0) / 10000.0; }

// Pretty JSON with arrays of scalars kept on one line (colours stay readable); strings via ff::ToJson.
void WriteJson(const ff::Value& v, int depth, std::string& out) {
	auto pad = [&out](int d) { out.append(static_cast<size_t>(d) * 2u, ' '); };
	if (const ff::Dict* d = v.dict_ptr()) {
		if (d->empty()) {
			out += "{}";
			return;
		}
		out += "{\n";
		size_t i = 0;
		for (const auto& kv : d->items()) {
			pad(depth + 1);
			out += ff::ToJson(ff::Value(kv.first)) + ": ";
			WriteJson(kv.second, depth + 1, out);
			out += (++i < d->size()) ? ",\n" : "\n";
		}
		pad(depth);
		out += "}";
		return;
	}
	if (const ff::Array* a = v.array_ptr()) {
		bool scalars = true;
		for (const ff::Value& e : *a) scalars = scalars && !e.is_array() && !e.is_dict();
		if (scalars) {
			out += "[";
			for (size_t i = 0; i < a->size(); ++i) {
				if (i) out += ", ";
				WriteJson((*a)[i], depth, out);
			}
			out += "]";
			return;
		}
		out += "[\n";
		for (size_t i = 0; i < a->size(); ++i) {
			pad(depth + 1);
			WriteJson((*a)[i], depth + 1, out);
			out += (i + 1 < a->size()) ? ",\n" : "\n";
		}
		pad(depth);
		out += "]";
		return;
	}
	if (v.is_float()) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.4g", v.as_float());
		std::string t(buf);
		if (t.find_first_of(".eEn") == std::string::npos) t += ".0";
		out += t;
		return;
	}
	out += ff::ToJson(v);
}

template <size_t N>
ff::Dict FieldsToDict(const void* obj, const FieldDesc (&fields)[N]) {
	ff::Dict d;
	const char* base = static_cast<const char*>(obj);
	for (const FieldDesc& f : fields) {
		const char* p = base + f.off;
		switch (f.type) {
			case FT::F: d.set(f.name, ff::Value(Round4(*reinterpret_cast<const float*>(p)))); break;
			case FT::C: d.set(f.name, ColorToValue(*reinterpret_cast<const Color*>(p))); break;
			case FT::B: d.set(f.name, ff::Value(*reinterpret_cast<const bool*>(p))); break;
			case FT::Fb:
				d.set(f.name, ff::Value(std::string(kFlipbookNames[static_cast<size_t>(*reinterpret_cast<const Flipbook*>(p))])));
				break;
			case FT::Fam_:
				d.set(f.name, ff::Value(std::string(FamName(*reinterpret_cast<const Fam*>(p)))));
				break;
		}
	}
	return d;
}

template <size_t N>
void DictToFields(const ff::Value& v, void* obj, const FieldDesc (&fields)[N], const std::string& where, std::string* warn) {
	const ff::Dict* d = v.dict_ptr();
	if (!d) return;
	char* base = static_cast<char*>(obj);
	for (const auto& kv : d->items()) {
		const FieldDesc* fd = nullptr;
		for (const FieldDesc& f : fields)
			if (kv.first == f.name) fd = &f;
		if (!fd) {
			if (warn) *warn += "unknown key " + where + "." + kv.first + "\n";
			continue;
		}
		char* p = base + fd->off;
		switch (fd->type) {
			case FT::F: *reinterpret_cast<float*>(p) = kv.second.as_f32(*reinterpret_cast<float*>(p)); break;
			case FT::C: ValueToColor(kv.second, *reinterpret_cast<Color*>(p)); break;
			case FT::B: *reinterpret_cast<bool*>(p) = kv.second.as_bool(*reinterpret_cast<bool*>(p)); break;
			case FT::Fb: {
				const std::string& s = kv.second.as_string();
				for (int i = 0; i < kNumFlipbooks; ++i)
					if (kFlipbookNames[static_cast<size_t>(i)] == s) *reinterpret_cast<Flipbook*>(p) = static_cast<Flipbook>(i);
				break;
			}
			case FT::Fam_: *reinterpret_cast<Fam*>(p) = FamFromName(kv.second.as_string(), *reinterpret_cast<Fam*>(p)); break;
		}
	}
}

// Named table <-> dict of dicts.
template <typename T, size_t M, size_t N, size_t K>
ff::Dict TableToDict(const std::array<T, M>& table, const std::array<std::string_view, K>& names,
                     const FieldDesc (&fields)[N]) {
	ff::Dict d;
	for (size_t i = 0; i < M && i < K; ++i) d.set(names[i], ff::Value(FieldsToDict(&table[i], fields)));
	return d;
}
template <typename T, size_t M, size_t N, size_t K>
void DictToTable(const ff::Value& v, std::array<T, M>& table, const std::array<std::string_view, K>& names,
                 const FieldDesc (&fields)[N], const char* where, std::string* warn) {
	const ff::Dict* d = v.dict_ptr();
	if (!d) return;
	for (const auto& kv : d->items()) {
		bool found = false;
		for (size_t i = 0; i < M && i < K; ++i)
			if (names[i] == kv.first) {
				DictToFields(kv.second, &table[i], fields, std::string(where) + "." + kv.first, warn);
				found = true;
			}
		if (!found && warn) *warn += std::string("unknown entry ") + where + "." + kv.first + "\n";
	}
}

std::string MatAssetPath(MatSlot s) {
	// rock -> M_FX_Rock, lava_strip -> M_FX_LavaStrip
	std::string n = "M_FX_";
	bool up = true;
	for (char c : MatSlotName(s)) {
		if (c == '_') {
			up = true;
			continue;
		}
		n += up ? static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c) : c;
		up = false;
	}
	return "/Game/Fourfold/FX/Materials/" + n;
}

}  // namespace

FxConfig::FxConfig() {
	auto C = [](float r, float g, float b) { return Color(r, g, b, 1.0f); };
	// ---- palette (VfxPalette.MAT / DUST / TIER; improved: slightly richer magma, cooler ice, warmer sand)
	const Color kMat[kNumFams] = {C(0.78f, 0.66f, 0.50f), C(0.70f, 0.80f, 0.95f), C(0.90f, 0.74f, 0.46f),
	                              C(0.72f, 0.96f, 0.86f), C(1.00f, 0.42f, 0.10f), C(0.34f, 0.78f, 1.00f),
	                              C(0.72f, 0.93f, 1.00f), C(0.86f, 0.90f, 0.94f), C(0.97f, 0.97f, 0.98f),
	                              C(0.42f, 0.82f, 0.30f), C(1.00f, 0.55f, 0.16f), C(0.45f, 0.72f, 1.00f),
	                              C(0.74f, 0.78f, 1.00f), C(1.00f, 0.70f, 0.32f), C(0.90f, 0.95f, 1.00f),
	                              C(0.80f, 0.92f, 0.98f), C(0.62f, 0.48f, 0.92f), C(1.00f, 0.90f, 0.66f)};
	const Color kDust[kNumFams] = {C(0.50f, 0.45f, 0.38f), C(0.55f, 0.55f, 0.58f), C(0.78f, 0.64f, 0.42f),
	                               C(0.80f, 0.92f, 0.88f), C(0.24f, 0.20f, 0.18f), C(0.80f, 0.90f, 0.95f),
	                               C(0.88f, 0.95f, 1.00f), C(0.82f, 0.86f, 0.90f), C(0.92f, 0.93f, 0.94f),
	                               C(0.36f, 0.45f, 0.22f), C(0.30f, 0.28f, 0.27f), C(0.32f, 0.32f, 0.36f),
	                               C(0.40f, 0.40f, 0.45f), C(0.26f, 0.25f, 0.24f), C(0.62f, 0.58f, 0.52f),
	                               C(0.62f, 0.58f, 0.52f), C(0.50f, 0.47f, 0.52f), C(0.58f, 0.54f, 0.48f)};
	for (int i = 0; i < kNumFams; ++i) {
		mat[static_cast<size_t>(i)] = kMat[i];
		dust[static_cast<size_t>(i)] = kDust[i];
	}
	tier = {C(0.85f, 0.85f, 0.85f), C(1.00f, 0.84f, 0.42f), C(1.00f, 0.56f, 0.20f), C(1.00f, 0.96f, 0.88f)};
	flame = {C(0.62f, 0.07f, 0.01f), C(1.0f, 0.36f, 0.05f), C(1.0f, 0.78f, 0.30f), C(1.0f, 0.95f, 0.75f)};
	blueFlame = {C(0.10f, 0.12f, 0.55f), C(0.25f, 0.45f, 1.0f), C(0.55f, 0.85f, 1.0f), C(0.95f, 0.98f, 1.0f)};

	// ---- bursts (BurstFX.STYLES; dur / alpha as in Godot, flipbooks added)
	auto B = [this](ffx::Burst b) -> BurstStyle& { return bursts[static_cast<size_t>(b)]; };
	{
		BurstStyle& s = B(Burst::Dust);
		s = BurstStyle();
		s.puff = C(0.55f, 0.49f, 0.40f); s.speed = 1.8f; s.gravity = 0.25f; s.spread = 70.0f;
	}
	{
		BurstStyle& s = B(Burst::Sand);
		s.puff = C(0.82f, 0.67f, 0.43f); s.speed = 2.4f; s.gravity = -0.6f; s.spread = 60.0f; s.flipbook = Flipbook::SandBurst;
	}
	{
		BurstStyle& s = B(Burst::Grit);
		s.puff = C(0.74f, 0.60f, 0.40f); s.spark = C(0.95f, 0.80f, 0.55f); s.hasSpark = true; s.speed = 1.2f;
		s.gravity = -1.0f; s.sparkGravity = -6.0f; s.spread = 50.0f; s.flipbook = Flipbook::SandBurst;
	}
	{
		BurstStyle& s = B(Burst::Frost);
		s.puff = C(0.86f, 0.94f, 1.0f); s.spark = C(0.85f, 0.95f, 1.0f); s.hasSpark = true; s.speed = 1.6f;
		s.gravity = -0.2f; s.sparkGravity = -2.0f; s.spread = 65.0f; s.flipbook = Flipbook::SteamPuff;
	}
	{
		BurstStyle& s = B(Burst::Smoke);
		s.puff = C(0.32f, 0.31f, 0.30f); s.speed = 1.0f; s.gravity = 0.9f; s.spread = 35.0f; s.dur = 1.3f;
		s.flipbook = Flipbook::SmokePuff;
	}
	{
		BurstStyle& s = B(Burst::Ash);
		s.puff = C(0.26f, 0.25f, 0.24f); s.spark = C(1.0f, 0.45f, 0.1f); s.hasSpark = true; s.speed = 0.9f;
		s.gravity = 0.8f; s.sparkGravity = 1.5f; s.spread = 40.0f; s.dur = 1.2f; s.flipbook = Flipbook::SmokePuff;
	}
	{
		BurstStyle& s = B(Burst::Mist);
		s.puff = C(0.84f, 0.88f, 0.92f); s.speed = 0.9f; s.gravity = 0.1f; s.spread = 80.0f; s.dur = 1.4f;
		s.alpha = 0.3f; s.flipbook = Flipbook::SteamPuff;
	}
	{
		BurstStyle& s = B(Burst::Steam);
		s.puff = C(0.95f, 0.96f, 0.98f); s.speed = 1.3f; s.gravity = 1.2f; s.spread = 30.0f; s.dur = 1.2f;
		s.flipbook = Flipbook::SteamPuff;
	}
	{
		BurstStyle& s = B(Burst::Water);
		s.puff = C(0.80f, 0.90f, 0.96f); s.spark = C(0.75f, 0.9f, 1.0f); s.hasSpark = true; s.speed = 2.2f;
		s.gravity = -2.0f; s.sparkGravity = -9.0f; s.spread = 55.0f; s.alpha = 0.4f; s.flipbook = Flipbook::WaterSplash;
	}
	{
		BurstStyle& s = B(Burst::Leaves);
		s.puff = C(0.30f, 0.45f, 0.18f); s.spark = C(0.55f, 0.8f, 0.3f); s.hasSpark = true; s.speed = 1.8f;
		s.gravity = -1.2f; s.sparkGravity = -3.0f; s.spread = 70.0f;
	}
	{
		BurstStyle& s = B(Burst::Sparks);
		s.hasPuff = false; s.hasSpark = true; s.spark = C(1.0f, 0.75f, 0.35f); s.sparkGravity = -7.0f; s.spread = 55.0f;
		s.dur = 0.7f;
	}
	{
		BurstStyle& s = B(Burst::Metal);
		s.hasPuff = false; s.hasSpark = true; s.spark = C(1.0f, 0.92f, 0.7f); s.sparkGravity = -9.0f; s.spread = 45.0f;
		s.dur = 0.55f; s.sparkSpeed = 7.0f;
	}
	{
		BurstStyle& s = B(Burst::BlueSparks);
		s.hasPuff = false; s.hasSpark = true; s.spark = C(0.55f, 0.75f, 1.0f); s.sparkGravity = -4.0f; s.spread = 60.0f;
		s.dur = 0.7f;
	}
	{
		BurstStyle& s = B(Burst::Static);
		s.hasPuff = false; s.hasSpark = true; s.spark = C(0.75f, 0.82f, 1.0f); s.sparkGravity = 0.0f; s.spread = 180.0f;
		s.dur = 0.45f; s.sparkSpeed = 3.0f;
	}
	{
		BurstStyle& s = B(Burst::Glass);
		s.puff = C(0.85f, 0.85f, 0.80f); s.spark = C(0.85f, 1.0f, 0.92f); s.hasSpark = true; s.speed = 1.0f;
		s.gravity = 0.0f; s.sparkGravity = -8.0f; s.spread = 60.0f;
	}
	{
		BurstStyle& s = B(Burst::Ember);
		s.puff = C(0.30f, 0.28f, 0.26f); s.spark = C(1.0f, 0.6f, 0.2f); s.hasSpark = true; s.speed = 0.8f;
		s.gravity = 0.6f; s.sparkGravity = -3.0f; s.spread = 50.0f; s.flipbook = Flipbook::SmokePuff;
	}
	{
		BurstStyle& s = B(Burst::Inflow);
		s.puff = C(0.70f, 0.62f, 0.86f); s.speed = -2.6f; s.gravity = 0.0f; s.spread = 180.0f; s.dur = 0.6f;
		s.alpha = 0.35f; s.flipbook = Flipbook::SteamPuff;
	}

	// ---- clouds (CloudView.STYLES)
	auto CL = [this](CloudStyle s) -> CloudLook& { return clouds[static_cast<size_t>(s)]; };
	auto setCloud = [&](CloudStyle st, float size, float swirl, float rise, float column, float flatten, float grow,
	                    float wander, float stretch, float op, float opc, Color top, Color low) {
		CloudLook& c = CL(st);
		c.size = size; c.swirl = swirl; c.rise = rise; c.column = column; c.flatten = flatten; c.grow = grow;
		c.wander = wander; c.stretch = stretch; c.opacity = op; c.opacityCore = opc; c.top = top; c.low = low;
	};
	setCloud(CloudStyle::SandCloud, 1.5f, 0.35f, 0, 0, 0.15f, 0.6f, 0.15f, 0, 0.42f, 0.5f, C(0.66f, 0.50f, 0.30f), C(0.38f, 0.27f, 0.15f));
	setCloud(CloudStyle::Sandstorm, 1.9f, 1.1f, 0, 0, 0, 0.6f, 0.35f, 0, 0.48f, 0.52f, C(0.64f, 0.48f, 0.28f), C(0.36f, 0.25f, 0.14f));
	setCloud(CloudStyle::Fog, 1.8f, 0.06f, 0, 0, 0.85f, 0.6f, 0.15f, 0, 0.15f, 0.17f, C(0.80f, 0.84f, 0.88f), C(0.58f, 0.63f, 0.68f));
	setCloud(CloudStyle::Mist, 1.5f, 0.1f, 0, 0, 0.7f, 0.6f, 0.15f, 0, 0.13f, 0.16f, C(0.82f, 0.87f, 0.91f), C(0.62f, 0.68f, 0.74f));
	setCloud(CloudStyle::Steam, 1.1f, 0.2f, 0.45f, 0.3f, 0, 1.1f, 0.15f, 0, 0.3f, 0.36f, C(0.97f, 0.97f, 0.98f), C(0.80f, 0.82f, 0.85f));
	setCloud(CloudStyle::SteamScreen, 1.4f, 0.05f, 0.18f, 0, 0, 0.6f, 0.15f, 0, 0.3f, 0.34f, C(0.96f, 0.97f, 0.98f), C(0.78f, 0.80f, 0.83f));
	setCloud(CloudStyle::Geyser, 0.9f, 0.5f, 1.1f, 1.0f, 0, 1.5f, 0.15f, 0, 0.42f, 0.55f, C(0.97f, 0.98f, 1.0f), C(0.62f, 0.78f, 0.86f));
	setCloud(CloudStyle::DustLine, 0.9f, 0.2f, 0, 0, 0.35f, 0.6f, 0.15f, 0, 0.36f, 0.4f, C(0.74f, 0.67f, 0.56f), C(0.50f, 0.44f, 0.36f));
	setCloud(CloudStyle::Slug, 0.42f, 2.5f, 0, 0, 0, 0.6f, 0.04f, 0.25f, 0.75f, 0.9f, C(0.70f, 0.53f, 0.31f), C(0.42f, 0.30f, 0.16f));
	setCloud(CloudStyle::Veil, 1.0f, 0.4f, 0.12f, 0, 0, 0.6f, 0.15f, 0, 0.18f, 0.2f, C(0.88f, 0.92f, 0.95f), C(0.72f, 0.76f, 0.80f));
	setCloud(CloudStyle::Smoke, 1.0f, 0.2f, 0.3f, 0.4f, 0, 1.4f, 0.15f, 0, 0.32f, 0.36f, C(0.42f, 0.41f, 0.40f), C(0.20f, 0.19f, 0.19f));

	// ---- shells (ShellView.STYLES)
	auto SH = [this](ShellStyle s) -> ShellLook& { return shells[static_cast<size_t>(s)]; };
	{ ShellLook& s = SH(ShellStyle::NullBubble); s.fam = Fam::Vacuum; s.rim = 2.2f; s.streak = 0.55f; s.opacity = 0.75f; s.refract = 0.045f; }
	{ ShellLook& s = SH(ShellStyle::VacuumWell); s.fam = Fam::Vacuum; s.rim = 1.8f; s.streak = 0.9f; s.opacity = 0.55f; s.refract = 0.03f; s.spiral = true; }
	{ ShellLook& s = SH(ShellStyle::Corona); s.fam = Fam::Blue; s.color = C(0.16f, 0.38f, 1.0f); s.rim = 2.2f; s.crackle = 0.45f; s.opacity = 0.65f; s.glow = 1.0f; s.pulse = 0.2f; s.absorb = 0.95f; }
	{ ShellLook& s = SH(ShellStyle::StaticField); s.fam = Fam::Lightning; s.color = C(0.46f, 0.5f, 1.0f); s.rim = 3.0f; s.crackle = 0.6f; s.opacity = 0.32f; s.arcs = true; s.glow = 1.05f; s.absorb = 0.6f; }
	{ ShellLook& s = SH(ShellStyle::WindGuard); s.fam = Fam::Wind; s.rim = 2.4f; s.streak = -0.35f; s.opacity = 0.32f; }
	{ ShellLook& s = SH(ShellStyle::SoundBarrier); s.fam = Fam::Sound; s.rim = 1.6f; s.streak = -1.2f; s.opacity = 0.42f; }
	{ ShellLook& s = SH(ShellStyle::Aura); s.fam = Fam::Wind; s.rim = 3.0f; s.opacity = 0.22f; s.pulse = 0.15f; s.glow = 1.1f; }
	{ ShellLook& s = SH(ShellStyle::Mine); s.fam = Fam::Blast; s.rim = 1.4f; s.core = 0.85f; s.opacity = 0.6f; s.pulse = 0.55f; s.glow = 1.6f; s.coreColor = C(1.0f, 0.36f, 0.08f); }
	{ ShellLook& s = SH(ShellStyle::Inrush); s.fam = Fam::Vacuum; s.rim = 1.3f; s.streak = 1.5f; s.opacity = 0.34f; s.spiral = true; }
	{ ShellLook& s = SH(ShellStyle::Frost); s.fam = Fam::Ice; s.rim = 1.5f; s.core = 0.12f; s.opacity = 0.6f; s.coreColor = C(0.7f, 0.85f, 0.95f); }

	// ---- vortex infusions (VortexView.INFUSION)
	vortex[static_cast<size_t>(Infusion::None)] = VortexLook();
	vortex[static_cast<size_t>(Infusion::Sand)] = {C(0.55f, 0.38f, 0.19f), C(0.86f, 0.66f, 0.38f), 0.78f, 0.82f, 1.0f, true};
	vortex[static_cast<size_t>(Infusion::Fire)] = {C(0.9f, 0.22f, 0.02f), C(1.0f, 0.62f, 0.18f), 0.85f, 0.62f, 2.0f, false};
	vortex[static_cast<size_t>(Infusion::Water)] = {C(0.10f, 0.40f, 0.62f), C(0.60f, 0.86f, 0.98f), 0.7f, 0.66f, 1.1f, true};
	vortex[static_cast<size_t>(Infusion::Steam)] = {C(0.84f, 0.86f, 0.89f), C(1.0f, 1.0f, 1.0f), 0.5f, 0.55f, 1.1f, true};

	// ---- crystals (CrystalView.STYLE)
	ice = {C(0.50f, 0.82f, 1.0f), 0.35f, 0.55f, 0.7f};
	glass = {C(0.62f, 0.95f, 0.82f), 0.08f, 0.46f, 1.4f};

	// ---- beams (BeamFX.STYLES)
	beams[static_cast<size_t>(BeamStyle::Blue)] = {C(0.92f, 0.98f, 1.0f), C(0.30f, 0.55f, 1.0f), 0.11f, 0.12f, 0.3f, 0.0f, true};
	beams[static_cast<size_t>(BeamStyle::Needle)] = {C(0.95f, 0.99f, 1.0f), C(0.40f, 0.65f, 1.0f), 0.05f, 0.1f, 0.85f, 0.0f, false};
	beams[static_cast<size_t>(BeamStyle::Flame)] = {C(1.0f, 0.85f, 0.5f), C(1.0f, 0.35f, 0.05f), 0.16f, 0.5f, 0.0f, 0.0f, false};
	beams[static_cast<size_t>(BeamStyle::Sand)] = {C(0.90f, 0.78f, 0.55f), C(0.62f, 0.50f, 0.32f), 0.16f, 0.85f, 0.0f, 1.0f, false};
	beams[static_cast<size_t>(BeamStyle::Water)] = {C(0.85f, 0.95f, 1.0f), C(0.30f, 0.65f, 0.85f), 0.09f, 0.7f, 0.2f, 0.0f, false};
	beams[static_cast<size_t>(BeamStyle::Sound)] = {C(1.0f, 0.95f, 0.8f), C(0.85f, 0.75f, 0.5f), 0.2f, 0.2f, 0.0f, 0.6f, false};
	beams[static_cast<size_t>(BeamStyle::Vacuum)] = {C(0.75f, 0.65f, 0.95f), C(0.30f, 0.18f, 0.5f), 0.14f, 0.6f, 0.0f, 0.0f, false};

	// ---- ground strips (LavaWaveView defaults, GroundStripView.PROFILE)
	strips[static_cast<size_t>(StripStyle::Lava)] = {0.40f, 0.20f, 0.28f};
	strips[static_cast<size_t>(StripStyle::Water)] = {0.85f, 0.22f, 0.45f};
	strips[static_cast<size_t>(StripStyle::Sand)] = {0.55f, 0.18f, 0.35f};
	strips[static_cast<size_t>(StripStyle::Rime)] = {0.22f, 0.10f, 0.1f};
	strips[static_cast<size_t>(StripStyle::Mud)] = {0.25f, 0.12f, 0.2f};

	// ---- quality levels (0 low: iPhone 12 under heat, 1 medium, 2 high)
	quality[0] = {0.55f, 7, 5, 8, 8, 5, 5, 2, false, false};
	quality[1] = {0.8f, 10, 7, 11, 11, 8, 6, 3, true, true};
	quality[2] = {1.0f, 12, 9, 14, 14, 10, 7, 4, true, true};

	for (int i = 0; i < kNumMatSlots; ++i) materials[static_cast<size_t>(i)] = MatAssetPath(static_cast<MatSlot>(i));
	for (int i = 1; i < kNumMeshAssets; ++i) {
		std::string n(kMeshAssetNames[static_cast<size_t>(i)]);
		meshes[static_cast<size_t>(i)] = "/Game/Fourfold/FX/Meshes/SM_FX_" + n;
	}
	for (int i = 1; i < kNumFlipbooks; ++i) {
		std::string n(kFlipbookNames[static_cast<size_t>(i)]);
		flipbooks[static_cast<size_t>(i)] = "/Game/Fourfold/FX/Textures/T_FX_FB_" + n;
	}
}

std::string FxConfig::ToJson() const {
	ff::Dict root;
	root.set("schema", ff::Value("fourfold.fx_config/1"));
	root.set("note", ff::Value("Generated from FxConfig defaults (ffx_logic_tests --write-config). Colours are sRGB "
	                           "display values [r, g, b, a]. Any subset may be removed: missing keys keep the defaults."));
	{
		ff::Dict pal;
		ff::Dict m, d;
		for (int i = 0; i < kNumFams; ++i) {
			m.set(kFamNames[static_cast<size_t>(i)], ColorToValue(mat[static_cast<size_t>(i)]));
			d.set(kFamNames[static_cast<size_t>(i)], ColorToValue(dust[static_cast<size_t>(i)]));
		}
		pal.set("mat", ff::Value(m));
		pal.set("dust", ff::Value(d));
		auto arr4 = [](const std::array<Color, 4>& a) {
			ff::Array out;
			for (const Color& c : a) out.append(ColorToValue(c));
			return ff::Value(out);
		};
		pal.set("tier", arr4(tier));
		pal.set("flame", arr4(flame));
		pal.set("blue_flame", arr4(blueFlame));
		root.set("palette", ff::Value(pal));
	}
	root.set("bursts", ff::Value(TableToDict(bursts, kBurstNames, kBurstFields)));
	root.set("clouds", ff::Value(TableToDict(clouds, kCloudStyleNames, kCloudFields)));
	root.set("shells", ff::Value(TableToDict(shells, kShellStyleNames, kShellFields)));
	root.set("vortex", ff::Value(TableToDict(vortex, kInfusionNames, kVortexFields)));
	{
		ff::Dict cr;
		cr.set("ice", ff::Value(FieldsToDict(&ice, kCrystalFields)));
		cr.set("glass", ff::Value(FieldsToDict(&glass, kCrystalFields)));
		root.set("crystals", ff::Value(cr));
	}
	root.set("beams", ff::Value(TableToDict(beams, kBeamStyleNames, kBeamFields)));
	root.set("strips", ff::Value(TableToDict(strips, kStripStyleNames, kStripFields)));
	{
		ff::Array q;
		for (const QualityLevel& l : quality) {
			ff::Dict d;
			const char* base = reinterpret_cast<const char*>(&l);
			for (const QField& f : kQualityFields) {
				const char* p = base + f.off;
				if (f.type == QT::F) d.set(f.name, ff::Value(Round4(*reinterpret_cast<const float*>(p))));
				else if (f.type == QT::I) d.set(f.name, ff::Value(*reinterpret_cast<const int*>(p)));
				else d.set(f.name, ff::Value(*reinterpret_cast<const bool*>(p)));
			}
			q.append(ff::Value(d));
		}
		root.set("quality", ff::Value(q));
	}
	root.set("lights", ff::Value(FieldsToDict(&lights, kLightFields)));
	root.set("flash_scale", ff::Value(Round4(flashScale)));
	{
		ff::Dict m;
		for (int i = 0; i < kNumMatSlots; ++i) m.set(kMatSlotNames[static_cast<size_t>(i)], ff::Value(materials[static_cast<size_t>(i)]));
		root.set("materials", ff::Value(m));
		ff::Dict me;
		for (int i = 1; i < kNumMeshAssets; ++i) me.set(kMeshAssetNames[static_cast<size_t>(i)], ff::Value(meshes[static_cast<size_t>(i)]));
		root.set("meshes", ff::Value(me));
		ff::Dict fb;
		for (int i = 1; i < kNumFlipbooks; ++i) fb.set(kFlipbookNames[static_cast<size_t>(i)], ff::Value(flipbooks[static_cast<size_t>(i)]));
		root.set("flipbooks", ff::Value(fb));
	}
	std::string out;
	WriteJson(ff::Value(root), 0, out);
	return out + "\n";
}

bool FxConfig::LoadJson(std::string_view text, std::string* error, std::string* warnings) {
	ff::Value v;
	ff::JsonError je;
	if (!ff::ParseJson(text, v, &je)) {
		if (error) *error = "fx_config.json:" + std::to_string(je.line) + ":" + std::to_string(je.column) + ": " + je.message;
		return false;
	}
	if (!v.is_dict()) {
		if (error) *error = "fx_config.json: root is not an object";
		return false;
	}
	FxConfig& c = *this;
	const ff::Value& pal = v["palette"];
	if (pal.is_dict()) {
		auto readFam = [&](const ff::Value& d, std::array<Color, kNumFams>& out, const char* where) {
			const ff::Dict* dd = d.dict_ptr();
			if (!dd) return;
			for (const auto& kv : dd->items()) {
				bool ok = false;
				for (int i = 0; i < kNumFams; ++i)
					if (kFamNames[static_cast<size_t>(i)] == kv.first) ok = ValueToColor(kv.second, out[static_cast<size_t>(i)]);
				if (!ok && warnings) *warnings += std::string("bad entry palette.") + where + "." + kv.first + "\n";
			}
		};
		readFam(pal["mat"], c.mat, "mat");
		readFam(pal["dust"], c.dust, "dust");
		auto read4 = [](const ff::Value& a, std::array<Color, 4>& out) {
			const ff::Array* arr = a.array_ptr();
			if (!arr) return;
			for (size_t i = 0; i < arr->size() && i < 4; ++i) ValueToColor((*arr)[i], out[i]);
		};
		read4(pal["tier"], c.tier);
		read4(pal["flame"], c.flame);
		read4(pal["blue_flame"], c.blueFlame);
	}
	DictToTable(v["bursts"], c.bursts, kBurstNames, kBurstFields, "bursts", warnings);
	DictToTable(v["clouds"], c.clouds, kCloudStyleNames, kCloudFields, "clouds", warnings);
	DictToTable(v["shells"], c.shells, kShellStyleNames, kShellFields, "shells", warnings);
	DictToTable(v["vortex"], c.vortex, kInfusionNames, kVortexFields, "vortex", warnings);
	if (v["crystals"].is_dict()) {
		DictToFields(v["crystals"]["ice"], &c.ice, kCrystalFields, "crystals.ice", warnings);
		DictToFields(v["crystals"]["glass"], &c.glass, kCrystalFields, "crystals.glass", warnings);
	}
	DictToTable(v["beams"], c.beams, kBeamStyleNames, kBeamFields, "beams", warnings);
	DictToTable(v["strips"], c.strips, kStripStyleNames, kStripFields, "strips", warnings);
	if (const ff::Array* q = v["quality"].array_ptr()) {
		for (size_t i = 0; i < q->size() && i < 3; ++i) {
			const ff::Dict* d = (*q)[i].dict_ptr();
			if (!d) continue;
			char* base = reinterpret_cast<char*>(&c.quality[i]);
			for (const auto& kv : d->items()) {
				const QField* fd = nullptr;
				for (const QField& f : kQualityFields)
					if (kv.first == f.name) fd = &f;
				if (!fd) {
					if (warnings) *warnings += "unknown key quality." + kv.first + "\n";
					continue;
				}
				char* p = base + fd->off;
				if (fd->type == QT::F) *reinterpret_cast<float*>(p) = kv.second.as_f32(*reinterpret_cast<float*>(p));
				else if (fd->type == QT::I) *reinterpret_cast<int*>(p) = static_cast<int>(kv.second.as_int(*reinterpret_cast<int*>(p)));
				else *reinterpret_cast<bool*>(p) = kv.second.as_bool(*reinterpret_cast<bool*>(p));
			}
		}
	}
	DictToFields(v["lights"], &c.lights, kLightFields, "lights", warnings);
	c.flashScale = v["flash_scale"].as_f32(c.flashScale);
	auto readPaths = [&](const ff::Value& d, auto& out, const auto& names, int first, const char* where) {
		const ff::Dict* dd = d.dict_ptr();
		if (!dd) return;
		for (const auto& kv : dd->items()) {
			bool ok = false;
			for (size_t i = static_cast<size_t>(first); i < names.size(); ++i)
				if (names[i] == kv.first && kv.second.is_string()) {
					out[i] = kv.second.as_string();
					ok = true;
				}
			if (!ok && warnings) *warnings += std::string("bad entry ") + where + "." + kv.first + "\n";
		}
	};
	readPaths(v["materials"], c.materials, kMatSlotNames, 0, "materials");
	readPaths(v["meshes"], c.meshes, kMeshAssetNames, 1, "meshes");
	readPaths(v["flipbooks"], c.flipbooks, kFlipbookNames, 1, "flipbooks");
	return true;
}

}  // namespace ffx
