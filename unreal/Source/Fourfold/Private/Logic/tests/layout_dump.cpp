// Fourfold game logic island - touch layout dump for visual review (built by tests/CMakeLists.txt as ffg_layout_dump;
// empty when UnrealBuildTool compiles it). Prints the touch HUD geometry of several iPhone / iPad / Mac screens as
// JSON; tests/draw_layouts.py turns that into one PNG per screen (physical sizes, safe area, hit radii, ring, petals).
#if defined(FF_LOGIC_TESTS)

#include "FFGTouchLayout.h"
#include "FFGUiScale.h"

#include <array>
#include <cstddef>
#include <cstdio>

namespace {

struct Screen {
	const char* name;
	float w, h, ppi;              // landscape pixels, physical pixels per inch
	float il, it, ir, ib;         // safe-area insets in pixels
	bool mobile, lefty;
	float scale;
	const char* preset;
};

// Landscape sizes and safe areas of common devices (points x scale factor).
constexpr Screen kScreens[] = {
	{"iphone_15_pro", 2556, 1179, 460, 177, 0, 177, 63, true, false, 1.0f, "default"},
	{"iphone_15_pro_left_handed", 2556, 1179, 460, 177, 0, 177, 63, true, true, 1.0f, "default"},
	{"iphone_se", 1334, 750, 326, 0, 0, 0, 0, true, false, 1.0f, "default"},
	{"iphone_15_pro_max_compact", 2796, 1290, 460, 177, 0, 177, 63, true, false, 1.0f, "compact"},
	{"ipad_air_11", 2360, 1640, 264, 0, 48, 0, 40, true, false, 1.0f, "default"},
	{"ipad_pro_13_wide_scale_1_2", 2752, 2064, 264, 0, 48, 0, 40, true, false, 1.2f, "wide"},
	{"mac_window_1600x900", 1600, 900, 110, 0, 0, 0, 0, false, false, 1.0f, "default"},
};

}  // namespace

int main() {
	std::printf("[\n");
	bool first = true;
	for (const Screen& s : kScreens) {
		ffg::UiScaleInput in;
		in.dpi = s.ppi;
		in.dpi_is_real = s.mobile;
		in.local_per_pixel = 1.0f;
		in.viewport_h_local = s.h;
		in.mobile = s.mobile;
		const float ppm = ffg::PxPerMm(in);
		ffg::Insets ins;
		ins.left = s.il;
		ins.top = s.it;
		ins.right = s.ir;
		ins.bottom = s.ib;
		ins = ffg::SanitizeInsets(ins, ffg::Vec2(s.w, s.h));
		ffg::TouchLayout lay;
		lay.Configure(ffg::Vec2(s.w, s.h), ins, ppm, s.scale, s.preset, s.lefty);
		std::printf("%s{\"name\":\"%s\",\"w\":%g,\"h\":%g,\"ppm\":%g,\"usable\":[%g,%g,%g,%g],\"split\":%g,", first ? "" : ",",
		            s.name, double(s.w), double(s.h), double(ppm), double(lay.usable.x), double(lay.usable.y),
		            double(lay.usable.w), double(lay.usable.h), double(lay.split_x));
		std::printf("\"stick\":[%g,%g,%g],\"aim\":%g,\"controls\":[", double(lay.stick_ghost.x), double(lay.stick_ghost.y),
		            double(lay.stick_radius), double(lay.aim_radius));
		first = false;
		for (int i = 0; i < ffg::kTouchCount; ++i)
			std::printf("%s[%g,%g,%g,%g]", i ? "," : "", double(lay.centers[i].x), double(lay.centers[i].y), double(lay.radii[i]),
			            double(lay.hit_radii[i]));
		std::printf("],\"ring\":[");
		const std::array<ffg::Rect, 4> ring = lay.RingRects();
		for (std::size_t i = 0; i < ring.size(); ++i)
			std::printf("%s[%g,%g,%g,%g]", i ? "," : "", double(ring[i].x), double(ring[i].y), double(ring[i].w), double(ring[i].h));
		std::printf("],\"petals\":[");
		const ff::Gesture attack[3] = {ff::Gesture::Up, ff::Gesture::Down, ff::Gesture::Side};
		for (int i = 0; i < 3; ++i) {
			const ffg::PetalAnchor a = lay.AttackPetalAnchor(attack[i]);
			std::printf("%s[%g,%g,%d,0]", i ? "," : "", double(a.pos.x), double(a.pos.y), a.align);
		}
		const ff::Gesture guard[2] = {ff::Gesture::Up, ff::Gesture::Down};
		for (int i = 0; i < 2; ++i) {
			const ffg::PetalAnchor a = lay.GuardPetalAnchor(guard[i]);
			std::printf(",[%g,%g,%d,1]", double(a.pos.x), double(a.pos.y), a.align);
		}
		std::printf("]}\n");
	}
	std::printf("]\n");
	return 0;
}

#endif  // FF_LOGIC_TESTS
