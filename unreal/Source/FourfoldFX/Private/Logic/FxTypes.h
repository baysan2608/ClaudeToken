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

// Scalar material parameters (FName = kParamNames[i]). Every material ignores the ones it does not declare.
enum class P : uint8_t {
	Heat, Melt, Crust, Frost, Damage, Rise, RiseHeight, Seed, Age, Fade, Intensity, Tier, Spin, Glass, Detail, Cover,
	Core, Shape, Phase, Boil, Flow, Wet, Power, Shatter, Style, Burn, Frozen, Foam, Emissive, Count
};
inline constexpr int kNumParams = static_cast<int>(P::Count);
inline constexpr std::array<std::string_view, kNumParams> kParamNames = {
	"Heat", "Melt", "Crust", "Frost", "Damage", "Rise", "RiseHeight", "Seed", "Age", "Fade", "Intensity", "Tier",
	"Spin", "Glass", "Detail", "Cover", "Core", "Shape", "Phase", "Boil", "Flow", "Wet", "Power", "Shatter", "Style",
	"Burn", "Frozen", "Foam", "EmissiveScale"};
// Vector material parameters.
enum class PV : uint8_t { Color, Color2, Tint, Count };
inline constexpr int kNumVParams = static_cast<int>(PV::Count);
inline constexpr std::array<std::string_view, kNumVParams> kVParamNames = {"Color", "Color2", "Tint"};

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
enum class Flipbook : uint8_t { None, SmokePuff, SteamPuff, DustPuff, SandBurst, FireLoop, FireBurst, Explosion, WaterSplash, Count };
inline constexpr int kNumFlipbooks = static_cast<int>(Flipbook::Count);
inline constexpr std::array<std::string_view, kNumFlipbooks> kFlipbookNames = {
	"none", "smoke_puff", "steam_puff", "dust_puff", "sand_burst", "fire_loop", "fire_burst", "explosion",
	"water_splash"};

// Fighter bones the logic asks the scene for (UE5 Manny names in the Unreal glue).
enum class Bone : uint8_t { Pelvis, Spine, Chest, Head, HandL, HandR, FootL, FootR, Count };
inline constexpr std::array<std::string_view, static_cast<size_t>(Bone::Count)> kBoneNames = {
	"pelvis", "spine_03", "spine_05", "head", "hand_l", "hand_r", "foot_l", "foot_r"};

}  // namespace ffx
