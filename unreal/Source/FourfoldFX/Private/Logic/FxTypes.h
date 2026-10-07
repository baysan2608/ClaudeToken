// FourfoldFX logic island - shared enums: material families, material slots, material parameters, static-mesh
// assets, draw items. Engine-free. The Unreal glue maps MatSlot -> material asset path (fx_config.json "materials"),
// ParamId -> FName (kParamNames: the parameter names the Python material builder creates), MeshAsset -> static mesh.
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ffx {

// Material family of a body / cue (docs/MOVESET.md §11.1; FxEvents.mat_of + VfxPalette keys).
enum class Fam : uint8_t {
	Stone, Metal, Sand, Glass, Magma, Water, Ice, Mist, Steam, Plant, Flame, Blue, Lightning, Blast, Wind, Vortex,
	Vacuum, Sound, Count
};
inline constexpr int kNumFams = static_cast<int>(Fam::Count);
inline constexpr std::array<std::string_view, kNumFams> kFamNames = {
	"stone", "metal", "sand", "glass", "magma", "water", "ice", "mist", "steam", "plant", "flame", "blue",
	"lightning", "blast", "wind", "vortex", "vacuum", "sound"};
inline Fam FamFromName(std::string_view n, Fam fallback = Fam::Stone) {
	for (int i = 0; i < kNumFams; ++i)
		if (kFamNames[static_cast<size_t>(i)] == n) return static_cast<Fam>(i);
	return fallback;
}
inline std::string_view FamName(Fam f) { return kFamNames[static_cast<size_t>(f)]; }
// Sim (element, sub) -> family (VfxPalette.SUB_MAT).
inline Fam FamOfSub(int element, int sub) {
	static constexpr Fam kSub[4][4] = {{Fam::Stone, Fam::Metal, Fam::Sand, Fam::Magma},
	                                   {Fam::Water, Fam::Ice, Fam::Mist, Fam::Plant},
	                                   {Fam::Flame, Fam::Blue, Fam::Lightning, Fam::Blast},
	                                   {Fam::Wind, Fam::Vortex, Fam::Vacuum, Fam::Sound}};
	if (element < 0 || element > 3) return Fam::Wind;
	return kSub[element][ClampI(sub, 0, 3)];
}

// Materials the logic draws with (one master material each, built by Content/Python/fourfold/fx).
enum class MatSlot : uint8_t {
	Rock,        // stone / lava blob / cooled rock / walls / spikes (opaque lit, WPO melt from blob UVs)
	LavaStrip,   // lava waves, lava pools (opaque lit + emissive)
	Metal,       // discs, lances, rods, plates, caltrops (opaque lit, red-hot)
	Crystal,     // ice / glass walls, needles, shards (translucent lit)
	Water,       // ribbons, blobs, puddles, water waves (translucent lit, fake refraction)
	GroundStrip, // sand surge / rime / mud strips (opaque lit)
	Vine,        // vines, roots, briars (opaque lit)
	Flame,       // mesh flames: cones, tongues, fields, fireball tongues (alpha composite)
	FireSprite,  // fire / explosion flipbook quads (alpha composite, black-body)
	Smoke,       // smoke / steam / dust / sand / mist flipbook quads and cloud puffs (translucent)
	Splash,      // water splash / spray flipbook quads (translucent)
	Spark,       // additive streaks, motes, embers, glints (additive)
	Lightning,   // bolts, arcs, crackle ribbons (additive)
	Beam,        // needles / beams (additive)
	Ring,        // shock / cast / deflect rings, blur rings (additive or translucent by param)
	Shell,       // fresnel shells: auras, bubbles, wells, corona, static field, frost shell (translucent)
	Vortex,      // funnel layers (translucent)
	Wind,        // crescents, wind walls, air push bands, streaks (translucent)
	Ground,      // ground quads: scorch, wet, quicksand, ice floor, mud, melt pit (translucent)
	Count
};
inline constexpr int kNumMatSlots = static_cast<int>(MatSlot::Count);
inline constexpr std::array<std::string_view, kNumMatSlots> kMatSlotNames = {
	"rock", "lava_strip", "metal", "crystal", "water", "ground_strip", "vine", "flame", "fire_sprite", "smoke",
	"splash", "spark", "lightning", "beam", "ring", "shell", "vortex", "wind", "ground"};
inline std::string_view MatSlotName(MatSlot s) { return kMatSlotNames[static_cast<size_t>(s)]; }

// Scalar material parameters (FName = kParamNames[i]). Every material ignores the ones it does not declare; the
// Python material builder (Content/Python/fourfold/fx) creates exactly these names. Append only.
enum class P : uint8_t {
	Heat, Melt, Crust, Frost, Damage, Rise, RiseHeight, Seed, Age, Fade, Intensity, Tier, Spin, Glass, Detail, Cover,
	Core, Shape, Phase, Boil, Flow, Wet, Power, Shatter, Style, Burn, Frozen, Foam, EmissiveScale, Opacity, Glow,
	Width, Rim, Streak, Crackle, Pulse, Absorb, Erosion, Taper, Grain, Scroll, Dusty, Height, Radius, Billboard, Count
};
inline constexpr int kNumParams = static_cast<int>(P::Count);
inline constexpr std::array<std::string_view, kNumParams> kParamNames = {
	"Heat", "Melt", "Crust", "Frost", "Damage", "Rise", "RiseHeight", "Seed", "Age", "Fade", "Intensity", "Tier",
	"Spin", "Glass", "Detail", "Cover", "Core", "Shape", "Phase", "Boil", "Flow", "Wet", "Power", "Shatter", "Style",
	"Burn", "Frozen", "Foam", "EmissiveScale", "Opacity", "Glow", "Width", "Rim", "Streak", "Crackle", "Pulse",
	"Absorb", "Erosion", "Taper", "Grain", "Scroll", "Dusty", "Height", "Radius", "Billboard"};
static_assert(kNumParams <= 64, "ParamBlock uses a 64-bit mask");
// Vector material parameters (linear colours). Flames use Color..Color4 as the deep / mid / hot / white ramp.
enum class PV : uint8_t { Color, Color2, Color3, Color4, Tint, Count };
inline constexpr int kNumVParams = static_cast<int>(PV::Count);
inline constexpr std::array<std::string_view, kNumVParams> kVParamNames = {"Color", "Color2", "Color3", "Color4", "Tint"};
// The one texture parameter the logic switches per draw item (flipbook atlas); FName "Flipbook".
inline constexpr std::string_view kFlipbookParamName = "Flipbook";

// Static meshes made in Blender (Tools/vfx/meshes.py -> SourceArt/VFX/Meshes/SM_FX_*.fbx).
enum class MeshAsset : uint8_t {
	None, Rock0, Rock1, Rock2, Rock3, Rock4, Rock5, Rock6, Rock7, Spike, Crystal0, Crystal1, IceShard, Disc, Lance,
	Plate, Caltrop, Chip, Count
};
inline constexpr int kNumMeshAssets = static_cast<int>(MeshAsset::Count);
inline constexpr std::array<std::string_view, kNumMeshAssets> kMeshAssetNames = {
	"none", "rock_0", "rock_1", "rock_2", "rock_3", "rock_4", "rock_5", "rock_6", "rock_7", "spike", "crystal_0",
	"crystal_1", "ice_shard", "disc", "lance", "plate", "caltrop", "chip"};
inline constexpr int kNumRocks = 8;
inline MeshAsset RockAsset(uint32_t seed) { return static_cast<MeshAsset>(1 + static_cast<int>(seed % kNumRocks)); }

// Particle / burst looks (fx_cues MAT_BURST styles + a few more).
enum class Burst : uint8_t {
	Dust, Metal, Sand, Glass, Ember, Water, Frost, Mist, Steam, Leaves, BlueSparks, Static, Ash, Inflow, Sparks,
	Smoke, Grit, Count
};
inline constexpr int kNumBursts = static_cast<int>(Burst::Count);
inline constexpr std::array<std::string_view, kNumBursts> kBurstNames = {
	"dust", "metal", "sand", "glass", "ember", "water", "frost", "mist", "steam", "leaves", "blue_sparks", "static",
	"ash", "inflow", "sparks", "smoke", "grit"};
inline Burst BurstFromName(std::string_view n, Burst fallback = Burst::Dust) {
	for (int i = 0; i < kNumBursts; ++i)
		if (kBurstNames[static_cast<size_t>(i)] == n) return static_cast<Burst>(i);
	return fallback;
}
// fx_cues.MAT_BURST
inline Burst BurstOfFam(Fam f) {
	static constexpr Burst kMap[kNumFams] = {Burst::Dust, Burst::Metal, Burst::Sand, Burst::Glass, Burst::Ember,
	                                         Burst::Water, Burst::Frost, Burst::Mist, Burst::Steam, Burst::Leaves,
	                                         Burst::Ember, Burst::BlueSparks, Burst::Static, Burst::Ash, Burst::Dust,
	                                         Burst::Dust, Burst::Inflow, Burst::Dust};
	return kMap[static_cast<int>(f)];
}

// Flipbooks (Tools/vfx/flipbooks.py -> SourceArt/VFX/Flipbooks/T_FX_FB_*.png).
// Flipbook frames per atlas: 8 x 8 = 64 (row-major from the top-left); PuffAtlas is a static 2 x 2 atlas of soft
// puff shapes (persistent clouds pick one quadrant per puff).
enum class Flipbook : uint8_t { None, SmokePuff, SteamPuff, DustPuff, SandBurst, FireLoop, FireBurst, Explosion, WaterSplash, PuffAtlas, Count };
inline constexpr int kNumFlipbooks = static_cast<int>(Flipbook::Count);
inline constexpr std::array<std::string_view, kNumFlipbooks> kFlipbookNames = {
	"none", "smoke_puff", "steam_puff", "dust_puff", "sand_burst", "fire_loop", "fire_burst", "explosion",
	"water_splash", "puff_atlas"};
inline int FlipbookFrames(Flipbook f) { return f == Flipbook::None ? 1 : (f == Flipbook::PuffAtlas ? 4 : 64); }
inline int FlipbookGrid(Flipbook f) { return f == Flipbook::None ? 1 : (f == Flipbook::PuffAtlas ? 2 : 8); }

// Fighter bones the logic asks the scene for (UE5 Manny names in the Unreal glue).
enum class Bone : uint8_t { Pelvis, Spine, Chest, Head, HandL, HandR, FootL, FootR, Count };
inline constexpr std::array<std::string_view, static_cast<size_t>(Bone::Count)> kBoneNames = {
	"pelvis", "spine_03", "spine_05", "head", "hand_l", "hand_r", "foot_l", "foot_r"};

// Niagara cue slots (fx_config.json "niagara"): every one-shot play also emits a SystemReq for its slot, and the
// Unreal glue spawns the Niagara system configured for the slot (Epic Niagara Examples Pack by default) when it is
// loaded. Burst slots follow the Burst order, shard slots the ShardMat order. Append only.
enum class NCue : uint8_t {
	Blast, BlastBlue,
	BurstDust, BurstMetal, BurstSand, BurstGlass, BurstEmber, BurstWater, BurstFrost, BurstMist, BurstSteam,
	BurstLeaves, BurstBlueSparks, BurstStatic, BurstAsh, BurstInflow, BurstSparks, BurstSmoke, BurstGrit,
	ShardsStone, ShardsIce, ShardsGlass, ShardsMetal, ShardsPlant,
	FireBurst, FireBurstBlue, Splash, Steam, Dust, Ember, BoltHit, AirPush, BoltArc, Count
};
inline constexpr int kNumNCues = static_cast<int>(NCue::Count);
static_assert(static_cast<int>(NCue::BurstGrit) - static_cast<int>(NCue::BurstDust) == kNumBursts - 1,
              "burst cue slots follow the Burst order");
static_assert(kNumNCues <= 64, "FxFrameIn::niagaraLoaded is a 64-bit mask");
inline constexpr std::array<std::string_view, kNumNCues> kNCueNames = {
	"blast", "blast_blue",
	"burst_dust", "burst_metal", "burst_sand", "burst_glass", "burst_ember", "burst_water", "burst_frost", "burst_mist",
	"burst_steam", "burst_leaves", "burst_blue_sparks", "burst_static", "burst_ash", "burst_inflow", "burst_sparks",
	"burst_smoke", "burst_grit",
	"shards_stone", "shards_ice", "shards_glass", "shards_metal", "shards_plant",
	"fire_burst", "fire_burst_blue", "splash", "steam", "dust", "ember", "bolt_hit", "air_push", "bolt_arc"};
inline std::string_view NCueName(NCue c) { return kNCueNames[static_cast<size_t>(c)]; }

// Persistent Niagara slots (fx_config.json "niagara_loops"): systems a view keeps alive while its body lives (one
// component per LoopReq key, moved and re-bound every frame, stopped gently when the key disappears). Append only.
enum class LCue : uint8_t { Fire, FireBlue, Steam, Smoke, TrailFire, TrailBlue, Wind, Count };
inline constexpr int kNumLCues = static_cast<int>(LCue::Count);
static_assert(kNumLCues <= 64, "FxFrameIn::niagaraLoopsLoaded is a 64-bit mask");
inline constexpr std::array<std::string_view, kNumLCues> kLCueNames = {"fire", "fire_blue", "steam", "smoke",
                                                                       "trail_fire", "trail_blue", "wind"};
inline std::string_view LCueName(LCue c) { return kLCueNames[static_cast<size_t>(c)]; }
inline NCue BurstCue(Burst b) { return static_cast<NCue>(static_cast<int>(NCue::BurstDust) + static_cast<int>(b)); }

}  // namespace ffx
