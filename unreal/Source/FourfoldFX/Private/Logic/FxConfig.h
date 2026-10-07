// FourfoldFX logic island - tunable look data (palettes, effect styles, quality levels, lights, asset paths).
// Defaults live here (ports of VfxPalette / BurstFX.STYLES / CloudView.STYLES / ShellView.STYLES / ... improved);
// Content/Fourfold/Data/fx_config.json overrides any subset at runtime (parsed with ff::ParseJson), so the owner can
// tune the look without recompiling. `FxConfig::ToJson()` writes the complete default file (the committed
// fx_config.json is generated from it and a test checks they agree).
// Colours in the JSON and in these tables are display (sRGB) values; `Linear()` converts for material parameters.
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"
#include "FxTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ffx {

// sRGB display colour -> linear (material parameters). Alpha unchanged.
Color Linear(const Color& c);

struct BurstStyle {
	Color puff{0.55f, 0.49f, 0.40f, 1.0f};
	Color spark{1.0f, 0.75f, 0.35f, 1.0f};
	bool hasPuff = true;
	bool hasSpark = false;
	float speed = 1.5f;      // puff speed m/s (negative = inflow toward the centre)
	float gravity = 0.2f;    // puff vertical accel (+ up)
	float sparkGravity = -6.0f;
	float spread = 60.0f;    // degrees around the normal
	float dur = 1.0f;        // seconds
	float alpha = 0.55f;     // puff peak alpha
	float sparkSpeed = 4.5f;
	Flipbook flipbook = Flipbook::DustPuff;   // puff atlas
};

enum class CloudStyle : uint8_t {
	SandCloud, Sandstorm, Fog, Mist, Steam, SteamScreen, Geyser, DustLine, Slug, Veil, Smoke, Count
};
inline constexpr int kNumCloudStyles = static_cast<int>(CloudStyle::Count);
inline constexpr std::array<std::string_view, kNumCloudStyles> kCloudStyleNames = {
	"sand_cloud", "sandstorm", "fog", "mist", "steam", "steam_screen", "geyser", "dust_line", "slug", "veil", "smoke"};

struct CloudLook {
	float size = 1.0f, swirl = 0.3f, rise = 0.0f, column = 0.0f, flatten = 0.0f, grow = 0.6f, wander = 0.15f,
	      stretch = 0.0f, opacity = 0.3f, opacityCore = 0.3f;
	Color top{1, 1, 1, 1};
	Color low{0.5f, 0.5f, 0.5f, 1};
};

enum class ShellStyle : uint8_t {
	NullBubble, VacuumWell, Corona, StaticField, WindGuard, SoundBarrier, Aura, Mine, Inrush, Frost, Count
};
inline constexpr int kNumShellStyles = static_cast<int>(ShellStyle::Count);
inline constexpr std::array<std::string_view, kNumShellStyles> kShellStyleNames = {
	"null_bubble", "vacuum_well", "corona", "static_field", "wind_guard", "sound_barrier", "aura", "mine", "inrush",
	"frost"};

struct ShellLook {
	Fam fam = Fam::Wind;     // palette colour when `color.a` == 0
	Color color{0, 0, 0, 0};
	Color coreColor{0, 0, 0, 0};
	float rim = 2.0f, streak = 0.0f, crackle = 0.0f, core = 0.0f, pulse = 0.0f, opacity = 0.5f, glow = 1.0f,
	      absorb = 0.0f, refract = 0.0f;
	bool spiral = false;     // ground spiral (vacuum well / inrush)
	bool arcs = false;       // crawling arcs (static field)
};

enum class Infusion : uint8_t { None, Sand, Fire, Water, Steam, Count };
inline constexpr int kNumInfusions = static_cast<int>(Infusion::Count);
inline constexpr std::array<std::string_view, kNumInfusions> kInfusionNames = {"none", "sand", "fire", "water", "steam"};
struct VortexLook {
	Color a{0.74f, 0.70f, 0.62f, 1}, b{0.96f, 0.97f, 1.0f, 1};
	float opacity = 0.42f, cover = 0.5f, glow = 1.0f;
	bool debris = true;
};

struct CrystalLook {
	Color tint{0.50f, 0.82f, 1.0f, 1};
	float frost = 0.35f, opacity = 0.55f, edge = 0.7f;
};

enum class BeamStyle : uint8_t { Blue, Needle, Flame, Sand, Water, Sound, Vacuum, Count };
inline constexpr int kNumBeamStyles = static_cast<int>(BeamStyle::Count);
inline constexpr std::array<std::string_view, kNumBeamStyles> kBeamStyleNames = {"blue", "needle", "flame", "sand",
                                                                               "water", "sound", "vacuum"};
struct BeamLook {
	Color core{1, 1, 1, 1}, glow{0.3f, 0.55f, 1.0f, 1};
	float width = 0.1f, cover = 0.1f, taper = 0.0f, grain = 0.0f;
	bool light = false;
};

enum class StripStyle : uint8_t { Lava, Water, Sand, Rime, Mud, Count };
inline constexpr int kNumStripStyles = static_cast<int>(StripStyle::Count);
inline constexpr std::array<std::string_view, kNumStripStyles> kStripStyleNames = {"lava", "water", "sand", "rime", "mud"};
struct StripLook {
	float heightFront = 0.40f, heightTail = 0.20f, bulge = 0.28f;
};

struct QualityLevel {
	float particles = 1.0f;     // multiplier on one-shot particle counts
	int cloudOuter = 12;        // puffs per cloud layer
	int cloudCore = 9;
	int flamesMax = 14;         // flame tongues per field / line
	int shardsMax = 14;
	int debris = 10;            // vortex debris chips
	int boltLevels = 7;         // lightning subdivision depth
	int maxLights = 4;
	bool softParticles = true;
	bool distortion = true;     // vacuum shells / air push screen-space bend (cheap fake on mobile)
};

struct LightSettings {
	float intensityScale = 300.0f;   // candela per unit of the logic's relative intensity (Godot light_energy)
	float radiusScale = 1.0f;
	float minIntensity = 0.03f;      // below this a request is dropped
};

// One Niagara cue slot (fx_config.json "niagara.<cue>"): the system the glue spawns for every SystemReq of the cue.
// `params` binds the system's user parameters (name without "User.") to a source: "color" / "color2" (the request
// colours, linear; skipped when their alpha is 0), "dir" / "-dir" (unit direction in UE axes), "scale" / "intensity"
// (optionally "scale*<k>", "intensity*<k>"), or a number. The glue converts to the parameter's own type.
struct NiagaraSlot {
	std::string path;        // UNiagaraSystem asset path; "" = no system (the procedural one-shot plays alone)
	float scale = 1.0f;      // uniform scale of the spawned system (x the request's scale)
	float life = 0.0f;       // seconds until the system is told to stop (looping systems as one-shots); 0 = its own
	bool replace = false;    // while the system is loaded, the procedural one-shot of the cue is skipped
	int minQuality = 1;      // spawned only at this effect quality or higher (0..2)
	float minIntensity = 0.0f;   // weaker requests (e.g. the periodic status puffs) keep the procedural look only
	std::vector<std::pair<std::string, std::string>> params;

	// The glue spawns this slot's system for a request of this strength at this quality (when the asset loaded).
	bool Wants(float intensity, int quality) const { return !path.empty() && quality >= minQuality && intensity >= minIntensity; }
};

struct FxConfig {
	std::array<Color, kNumFams> mat{};     // VfxPalette.MAT
	std::array<Color, kNumFams> dust{};    // VfxPalette.DUST
	std::array<Color, 4> tier{};           // VfxPalette.TIER
	std::array<Color, 4> flame{};          // deep, mid, hot, white
	std::array<Color, 4> blueFlame{};
	std::array<BurstStyle, kNumBursts> bursts{};
	std::array<CloudLook, kNumCloudStyles> clouds{};
	std::array<ShellLook, kNumShellStyles> shells{};
	std::array<VortexLook, kNumInfusions> vortex{};
	CrystalLook ice{}, glass{};
	std::array<BeamLook, kNumBeamStyles> beams{};
	std::array<StripLook, kNumStripStyles> strips{};
	std::array<QualityLevel, 3> quality{};
	LightSettings lights{};
	float flashScale = 1.0f;               // perfect / lightning in-world flashes (x Settings.Flashes)
	std::array<std::string, kNumMatSlots> materials{};    // MatSlot -> material asset path
	std::array<std::string, kNumMeshAssets> meshes{};     // MeshAsset -> static mesh asset path ("" = procedural)
	std::array<std::string, kNumFlipbooks> flipbooks{};   // Flipbook -> texture asset path
	std::array<NiagaraSlot, kNumNCues> niagara{};         // NCue -> Niagara system + parameter bindings

	FxConfig();   // defaults

	const Color& MatColor(Fam f) const { return mat[static_cast<size_t>(f)]; }
	const Color& DustColor(Fam f) const { return dust[static_cast<size_t>(f)]; }
	const Color& TierColor(int t) const { return tier[static_cast<size_t>(ClampI(t, 0, 3))]; }
	const QualityLevel& Q(int level) const { return quality[static_cast<size_t>(ClampI(level, 0, 2))]; }
	const BurstStyle& Burst(ffx::Burst b) const { return bursts[static_cast<size_t>(b)]; }
	const CloudLook& Cloud(CloudStyle s) const { return clouds[static_cast<size_t>(s)]; }
	const ShellLook& Shell(ShellStyle s) const { return shells[static_cast<size_t>(s)]; }
	const VortexLook& Vortex(Infusion i) const { return vortex[static_cast<size_t>(i)]; }
	const BeamLook& Beam(BeamStyle s) const { return beams[static_cast<size_t>(s)]; }
	const StripLook& Strip(StripStyle s) const { return strips[static_cast<size_t>(s)]; }
	const NiagaraSlot& Niagara(NCue c) const { return niagara[static_cast<size_t>(c)]; }

	// Overrides from JSON text (any subset). Returns false on a parse error (config unchanged then); unknown keys are
	// ignored and listed in `warnings` (one per line) when given.
	bool LoadJson(std::string_view text, std::string* error = nullptr, std::string* warnings = nullptr);
	// Complete config as pretty JSON ("schema": "fourfold.fx_config/1").
	std::string ToJson() const;
};

}  // namespace ffx
