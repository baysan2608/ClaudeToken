// FourfoldFX logic island - body -> view mapping. Owner: stream `fx`.
#include "FxMapping.h"

#include <initializer_list>

namespace ffx {

namespace {

using ff::Form;
using ff::Mat;
using ff::Phase;

bool MapIn(std::string_view s, std::initializer_list<std::string_view> l) {
	for (std::string_view x : l)
		if (s == x) return true;
	return false;
}

ViewKind ZoneKind(const ff::BodyView& b, std::string_view tag) {
	if (MapIn(tag, {"fog", "mist", "steam", "sand_cloud", "sandstorm", "steam_screen", "geyser"})) return ViewKind::Cloud;
	if (MapIn(tag, {"quicksand", "ice_floor", "mud", "melt_pit"})) return ViewKind::Decal;
	if (tag == "fire_field") return ViewKind::Flames;
	if (MapIn(tag, {"tornado", "eddy", "vortex_wall"})) return ViewKind::Vortex;
	if (MapIn(tag, {"vacuum_well", "null_bubble", "mine", "corona", "wind_guard", "sound_barrier", "fuse", "static_field",
	             "inrush"}))
		return ViewKind::Shell;
	if (MapIn(tag, {"caltrops", "rod"})) return ViewKind::Metal;
	if (MapIn(tag, {"briar", "snare"})) return ViewKind::Vine;
	if (tag == "flight_field") return ViewKind::Rings;
	if (tag == "lava_pool") return ViewKind::LavaPool;
	if (b.mat == Mat::Water || b.mat == Mat::Steam) return ViewKind::Cloud;
	return ViewKind::None;
}

bool PropTrue(const ff::BodyView& b, std::string_view key) { return b.props.get(key, ff::Value(false)).truthy(); }

ViewKind KindFor(const ff::BodyView& b) {
	const std::string_view tag = b.tag;
	if (b.form == Form::Zone) return ZoneKind(b, tag);
	if (b.mat == Mat::Stone) {
		if (tag == "spikes" || tag == "spike_line") return ViewKind::Spikes;
		if (tag == "tremor") return ViewKind::Rings;
		if (b.form == Form::Wall) return ViewKind::Wall;
		if (b.form == Form::Wave) return ViewKind::Wave;
		// a settled / cooled wave keeps its ridge shape until someone lifts it
		if (b.wave_path.size() >= 2 && b.controller < 0) return ViewKind::Wave;
		return ViewKind::Stone;
	}
	if (b.mat == Mat::Water) {
		if (tag == "spikes") return ViewKind::Spikes;
		switch (b.form) {
			case Form::Puddle: return ViewKind::Puddle;
			case Form::Cloud: return ViewKind::Cloud;
			case Form::Wall: return ViewKind::CrystalWall;
			case Form::Wave: return ViewKind::Strip;
			case Form::Stream:
			case Form::Shard:
				if (tag == "needle") return ViewKind::Crystal;
				return b.controller < 0 ? ViewKind::Ribbon : ViewKind::Blob;
			default:
				if (tag == "needle") return ViewKind::Crystal;
				return ViewKind::Blob;
		}
	}
	switch (b.mat) {
		case Mat::Steam: return ViewKind::Cloud;
		case Mat::Metal:
			if (tag == "spikes" || tag == "spike_line") return ViewKind::Spikes;
			return ViewKind::Metal;
		case Mat::Sand:
			if (b.form == Form::Wave) return ViewKind::Strip;
			if (b.form == Form::Wall) return ViewKind::Wall;
			return ViewKind::Cloud;
		case Mat::Glass:
			if (tag == "spikes") return ViewKind::Spikes;
			return b.form == Form::Wall ? ViewKind::CrystalWall : ViewKind::Crystal;
		case Mat::Plant: return ViewKind::Vine;
		case Mat::Fire:
			if (b.form == Form::Wave) return ViewKind::Flames;
			if (PropTrue(b, "mine") || tag == "bomb") return ViewKind::Shell;
			return ViewKind::Fireball;
		case Mat::Air:
			if (tag == "crescent" || tag == "wind_wall") return ViewKind::Blade;
			if (MapIn(tag, {"twister", "funnel", "spiral", "tornado", "eddy", "vortex_wall"})) return ViewKind::Vortex;
			if (tag == "dust_line") return ViewKind::Cloud;
			if (tag == "tremor" || tag == "sound_barrier") return b.form == Form::Wave ? ViewKind::Rings : ViewKind::Shell;
			if (tag == "ground_current") return ViewKind::Crackle;
			break;
		default: break;
	}
	if (tag == "ground_current") return ViewKind::Crackle;
	if (tag == "fire_line") return ViewKind::Flames;
	return ViewKind::None;
}

std::string StyleOf(const ff::BodyView& b, ViewKind kind) {
	const std::string& tag = b.tag;
	switch (kind) {
		case ViewKind::Cloud:
			if (b.form == Form::Zone) {
				for (std::string_view s : kCloudStyleNames)
					if (tag == s) return tag;
				return b.mat == Mat::Steam ? "steam" : "mist";
			}
			if (b.mat == Mat::Sand) return "slug";
			if (tag == "dust_line") return "dust_line";
			return b.mat == Mat::Steam ? "steam" : "mist";
		case ViewKind::Strip:
			if (b.mat == Mat::Sand) return "sand";
			return (tag == "rime" || (b.phase == Phase::Frozen && tag != "water_wave")) ? "rime" : "water";
		case ViewKind::Decal: return tag;
		case ViewKind::Shell:
			if (tag.empty() && b.mat == Mat::Fire) return "mine";
			if (tag == "fuse" || tag == "bomb" || PropTrue(b, "mine")) return "mine";
			return tag;
		case ViewKind::Metal:
			if (b.form == Form::Zone) return tag == "rod" ? "planted" : "caltrops";
			if (b.form == Form::Wall || tag == "plate") return "plate";
			if (tag == "disc" || tag == "lance" || tag == "rod") return tag;
			return "orb";
		case ViewKind::Vine:
			if (b.form == Form::Wall) return "lattice";
			if (b.form == Form::Wave) return "roots";
			if (b.form == Form::Zone) return "briar";
			return "lash";
		case ViewKind::Crystal:
		case ViewKind::CrystalWall: return b.mat == Mat::Glass ? "glass" : "ice";
		case ViewKind::Spikes:
			if (b.mat == Mat::Metal) return "metal";
			return b.mat == Mat::Glass ? "glass" : (b.mat == Mat::Water ? "ice" : "stone");
		case ViewKind::Flames: return b.form == Form::Zone ? "field" : "line";
		case ViewKind::Blade: return tag == "wind_wall" ? "wall" : "crescent";
		case ViewKind::Vortex: return tag.empty() ? "twister" : tag;
		case ViewKind::Wall:
			if (tag == "obsidian" || b.mat == Mat::Sand || tag == "mud" || tag == "sand")
				return (b.mat == Mat::Sand || tag == "sand") ? "sand" : tag;
			return "";
		default: return "";
	}
}

}  // namespace

ViewSel SelectView(const ff::BodyView& b) {
	ViewSel s;
	if (b.form == Form::Pool) return s;
	s.kind = KindFor(b);
	s.style = StyleOf(b, s.kind);
	return s;
}

float Heat01(const ff::BodyView& b) {
	switch (b.mat) {
		case Mat::Stone: return Sat((b.temp - 250.0f) / (1000.0f - 250.0f));
		case Mat::Metal:
		case Mat::Sand:
		case Mat::Glass: return Sat((b.temp - 250.0f) / (1200.0f - 250.0f));
		default: return 0.0f;
	}
}

float Crust01(const ff::BodyView& b) {
	if (b.mat != Mat::Stone) return 0.0f;
	if (b.liquid > 0.0f) return Sat(1.0f - b.liquid) * 0.8f;
	return b.temp > 200.0f ? 1.0f : 0.0f;
}

float Life01(const ff::BodyView& b) {
	float f = Sat(b.age / 0.3f);
	if (b.max_life > 0.0f) f = MinF(f, Sat((b.max_life - b.age) / 0.45f));
	return f;
}

float WallHeat01(const ff::BodyView& b) {
	float h = Heat01(b);
	const float fh = b.props.get("face_hu", ff::Value(0.0)).as_f32(0.0f);
	if (fh > 0.0f && b.mat == Mat::Stone) {
		const float ft = 20.0f + fh / MaxF(b.mass * 0.25f * 0.01f, 1e-3f);
		h = MaxF(h, Sat((ft - 250.0f) / (1000.0f - 250.0f)));
	}
	return h;
}

Infusion InfusionOf(const ff::BodyView& b) {
	const std::string inf = b.props.get("infused", ff::Value("")).as_string();
	if (inf.find("fire") != std::string::npos) return Infusion::Fire;
	if (inf.find("sand") != std::string::npos) return Infusion::Sand;
	if (inf.find("steam") != std::string::npos) return Infusion::Steam;
	if (inf.find("water") != std::string::npos) return Infusion::Water;
	if (inf.find("magma") != std::string::npos) return Infusion::Fire;
	return Infusion::None;
}

Fam FamOf(const ff::BodyView& b) {
	if (!b.fx_mat.empty()) return FamFromName(b.fx_mat, Fam::Stone);
	switch (b.mat) {
		case Mat::Stone: return b.phase == Phase::Molten ? Fam::Magma : Fam::Stone;
		case Mat::Water: return b.phase == Phase::Frozen ? Fam::Ice : Fam::Water;
		case Mat::Steam: return Fam::Steam;
		case Mat::Metal: return Fam::Metal;
		case Mat::Sand: return Fam::Sand;
		case Mat::Glass: return Fam::Glass;
		case Mat::Plant: return Fam::Plant;
		case Mat::Fire: return Fam::Flame;
		case Mat::Air: return Fam::Wind;
	}
	return Fam::Stone;
}

}  // namespace ffx
