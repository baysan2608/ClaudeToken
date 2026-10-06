// FourfoldFX logic island - which view draws a body (port of BodyViews._kind_for / _zone_kind / _style_of) and the
// body-state helpers views read (Thermal.heat01, crust, zone life fade, wall heat, infusion).
// Owner: stream `fx`.
#pragma once

#include "FxConfig.h"
#include "FxTypes.h"

#include "ff/Snapshot.h"

#include <string>
#include <string_view>

namespace ffx {

enum class ViewKind : uint8_t {
	None, Stone, Wave, Wall, Blob, Ribbon, Puddle, Metal, Cloud, Strip, Crystal, CrystalWall, Spikes, Vine, Flames,
	Fireball, Shell, Vortex, Blade, Crackle, Decal, Rings, LavaPool, Count
};
inline constexpr std::string_view kViewKindNames[] = {
	"none", "stone", "wave", "wall", "blob", "ribbon", "puddle", "metal", "cloud", "strip", "crystal", "crystal_wall",
	"spikes", "vine", "flames", "fireball", "shell", "vortex", "blade", "crackle", "decal", "rings", "lava_pool"};
inline std::string_view ViewKindName(ViewKind k) { return kViewKindNames[static_cast<size_t>(k)]; }

struct ViewSel {
	ViewKind kind = ViewKind::None;
	std::string style;
	bool operator==(const ViewSel& o) const { return kind == o.kind && style == o.style; }
	bool operator!=(const ViewSel& o) const { return !(*this == o); }
};

// Kind + style for a live body (Form::Pool -> None: the world draws the pool).
ViewSel SelectView(const ff::BodyView& b);

// Thermal.heat01: glow from temperature (stone 250 .. 1000 C, metal / sand / glass 250 .. 1200 C).
float Heat01(const ff::BodyView& b);
// BodyViews._crust: crust of a stone body.
float Crust01(const ff::BodyView& b);
// BodyViews._life01: zone fade in over 0.3 s, out over the last 0.45 s of max_life.
float Life01(const ff::BodyView& b);
// BodyViews._wall_heat01 (Molten Lance face heat).
float WallHeat01(const ff::BodyView& b);
// BodyViews._infusion from props.infused.
Infusion InfusionOf(const ff::BodyView& b);
// Material family of a body (BodyView.fx_mat with a mat fallback).
Fam FamOf(const ff::BodyView& b);
// Electric charge above which any body crackles.
inline constexpr float kCrackleCharge = 4.0f;
// Stable per-body seed (BodyViews._seed_of).
inline uint32_t SeedOf(int bodyId) { return static_cast<uint32_t>(bodyId) * 7919u + 13u; }

}  // namespace ffx
