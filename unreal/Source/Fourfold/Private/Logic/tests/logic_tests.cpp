// Fourfold game logic island - unit tests. Built only by Private/Logic/tests/CMakeLists.txt (FF_LOGIC_TESTS); when
// UnrealBuildTool compiles this file as part of the Fourfold module it is empty.
#if defined(FF_LOGIC_TESTS)

#include "FFGAnimDirector.h"
#include "FFGAnimLibrary.h"
#include "FFGAnimTiming.h"
#include "FFGArena.h"
#include "FFGCamera.h"
#include "FFGDesktopInput.h"
#include "FFGFeel.h"
#include "FFGFlick.h"
#include "FFGIK.h"
#include "FFGInertial.h"
#include "FFGLocomotion.h"
#include "FFGSettings.h"
#include "FFGSprings.h"
#include "FFGTouchControls.h"
#include "FFGTouchLayout.h"
#include "FFGUiScale.h"
#include "ff/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace fft {

struct TestCase {
	const char* name;
	void (*fn)();
};
std::vector<TestCase>& Registry() {
	static std::vector<TestCase> r;
	return r;
}
struct Reg {
	Reg(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
int g_failures = 0;
int g_checks = 0;
const char* g_current = "";

void Fail(const char* file, int line, const std::string& msg) {
	++g_failures;
	std::printf("  FAIL %s (%s:%d): %s\n", g_current, file, line, msg.c_str());
}

}  // namespace fft

#define FFT_TEST(name)                                     \
	static void name();                                    \
	static const fft::Reg name##_reg(#name, &name);        \
	static void name()
#define FFT_CHECK(cond)                                                      \
	do {                                                                     \
		++fft::g_checks;                                                     \
		if (!(cond)) fft::Fail(__FILE__, __LINE__, "CHECK(" #cond ")");      \
	} while (0)
#define FFT_NEAR(a, b, eps)                                                                                        \
	do {                                                                                                           \
		++fft::g_checks;                                                                                           \
		const double fa_ = static_cast<double>(a), fb_ = static_cast<double>(b);                                   \
		if (!(std::fabs(fa_ - fb_) <= static_cast<double>(eps))) {                                                 \
			char b_[256];                                                                                          \
			std::snprintf(b_, sizeof(b_), "NEAR(%s, %s): %.6f vs %.6f (eps %.6f)", #a, #b, fa_, fb_, static_cast<double>(eps)); \
			fft::Fail(__FILE__, __LINE__, b_);                                                                     \
		}                                                                                                          \
	} while (0)

using ffg::Vec2;
using ffg::Vec3;

// =================================================================================== math

FFT_TEST(math_quat_basics) {
	const ffg::Quat q = ffg::Quat::AxisAngle(Vec3(0, 0, 1), ffg::kPi * 0.5f);
	const Vec3 v = q.Rotate(Vec3(1, 0, 0));
	FFT_NEAR(v.x, 0.0, 1e-5);
	FFT_NEAR(v.y, 1.0, 1e-5);
	const ffg::Quat f = ffg::Quat::FromTo(Vec3(1, 0, 0), Vec3(0, 0, 1));
	const Vec3 w = f.Rotate(Vec3(1, 0, 0));
	FFT_NEAR(w.z, 1.0, 1e-5);
	const ffg::Quat opp = ffg::Quat::FromTo(Vec3(1, 0, 0), Vec3(-1, 0, 0));
	FFT_NEAR(opp.Rotate(Vec3(1, 0, 0)).x, -1.0, 1e-5);
	// composition order = FQuat: (a * b) applies b first
	const ffg::Quat a = ffg::Quat::AxisAngle(Vec3(0, 0, 1), 0.7f), b = ffg::Quat::AxisAngle(Vec3(1, 0, 0), 0.4f);
	const Vec3 p(0.3f, -0.2f, 0.9f);
	const Vec3 c1 = (a * b).Rotate(p), c2 = a.Rotate(b.Rotate(p));
	FFT_NEAR((c1 - c2).length(), 0.0, 1e-5);
	// slerp end points and half way
	const ffg::Quat h = ffg::Slerp(ffg::Quat::Identity(), q, 0.5f);
	FFT_NEAR(h.Angle(), ffg::kPi * 0.25f, 1e-4);
	FFT_NEAR(ffg::Slerp(ffg::Quat::Identity(), q, 1.0f).Dot(q), 1.0, 1e-5);
	// rotation vector round trip
	const Vec3 rv = ffg::Quat::FromRotVec(Vec3(0.2f, -0.3f, 0.1f)).ToRotVec();
	FFT_NEAR(rv.x, 0.2, 1e-4);
	FFT_NEAR(rv.y, -0.3, 1e-4);
}

FFT_TEST(math_xform_compose) {
	ffg::Xform parent{ffg::Quat::AxisAngle(Vec3(0, 0, 1), 0.5f), Vec3(1, 2, 3)};
	ffg::Xform local{ffg::Quat::AxisAngle(Vec3(1, 0, 0), 0.3f), Vec3(0.4f, 0, 0.2f)};
	const ffg::Xform cs = local * parent;
	const Vec3 p(0.1f, 0.2f, 0.3f);
	FFT_NEAR((cs.Apply(p) - parent.Apply(local.Apply(p))).length(), 0.0, 1e-5);
	const ffg::Xform back = cs.RelativeTo(parent);
	FFT_NEAR((back.t - local.t).length(), 0.0, 1e-5);
	FFT_NEAR(std::fabs(back.q.Dot(local.q)), 1.0, 1e-5);
	const ffg::Xform inv = cs.Inverse();
	FFT_NEAR((inv.Apply(cs.Apply(p)) - p).length(), 0.0, 1e-5);
}

FFT_TEST(math_helpers) {
	FFT_NEAR(ffg::WrapAngle(3.5f * ffg::kPi), -0.5f * ffg::kPi, 1e-4);
	FFT_NEAR(ffg::Fposmod(-0.25f, 1.0f), 0.75, 1e-6);
	FFT_CHECK(ffg::Posmod(-1, 4) == 3);
	FFT_NEAR(ffg::SmoothStep(0.0f, 1.0f, 0.5f), 0.5, 1e-6);
	FFT_NEAR(ffg::MoveToward(0.0f, 1.0f, 0.3f), 0.3, 1e-6);
}

// =================================================================================== flick

FFT_TEST(flick_attack_window_and_release) {
	const float ppm = 10.0f;   // 6 mm = 60 px
	ffg::FlickRecognizer f;
	f.Begin(Vec2(100, 100));
	FFT_CHECK(f.Update(Vec2(100, 70), 0.05f, ppm) == ff::Gesture::None);   // 30 px: below the threshold
	FFT_CHECK(f.hot == ff::Gesture::None);                                 // 30 < 0.55 * 60
	FFT_CHECK(f.Update(Vec2(100, 60), 0.06f, ppm) == ff::Gesture::None);
	FFT_CHECK(f.hot == ff::Gesture::Up);                                   // 40 >= 33 lights the petal
	FFT_CHECK(f.Update(Vec2(100, 35), 0.10f, ppm) == ff::Gesture::Up);    // 65 px up inside the window
	FFT_CHECK(f.Update(Vec2(100, 0), 0.12f, ppm) == ff::Gesture::None);    // fires once
	FFT_CHECK(f.Release(Vec2(100, 0), ppm) == ff::Gesture::None);
	// after the window: no flick on drag, but at release
	f.Begin(Vec2(0, 0));
	FFT_CHECK(f.Update(Vec2(70, 5), 0.4f, ppm) == ff::Gesture::None);
	FFT_CHECK(f.Release(Vec2(70, 5), ppm) == ff::Gesture::Side);
	f.Begin(Vec2(0, 0));
	FFT_CHECK(f.Release(Vec2(0, 61), ppm) == ff::Gesture::Down);
	FFT_CHECK(ffg::FlickRecognizer::Classify(Vec2(-10, 3)) == ff::Gesture::Side);
	FFT_CHECK(ffg::FlickRecognizer::Classify(Vec2()) == ff::Gesture::None);
}

FFT_TEST(flick_guard_rebases) {
	const float ppm = 10.0f;
	ffg::FlickRecognizer g;
	g.Begin(Vec2(0, 0));
	FFT_CHECK(g.Update(Vec2(70, 0), 2.0f, ppm, true) == ff::Gesture::None);   // side is not a guard flick
	FFT_CHECK(g.Update(Vec2(5, -70), 2.0f, ppm, true) == ff::Gesture::Up);    // any time
	FFT_CHECK(g.Update(Vec2(5, -10), 2.1f, ppm, true) == ff::Gesture::Down);  // re-based: 60 px down from -70
	FFT_CHECK(g.Update(Vec2(5, -5), 2.2f, ppm, true) == ff::Gesture::None);
}

// =================================================================================== touch layout

static ffg::TouchLayout PhoneLayout(bool lefty = false, float scale = 1.0f, const char* preset = "default") {
	// iPhone 14 landscape in Slate units at ~2x (1266 x 585), ~460 dpi native = 9.06 px/mm native -> ~4.5 local/mm
	ffg::TouchLayout l;
	ffg::Insets ins;
	ins.left = 47.0f;
	ins.right = 47.0f;
	ins.bottom = 21.0f;
	l.Configure(Vec2(1266.0f, 585.0f), ins, 9.06f, scale, preset, lefty);
	return l;
}

FFT_TEST(layout_buttons_inside_and_right_side) {
	for (const char* preset : {"default", "compact", "wide"}) {
		for (float scale : {0.8f, 1.0f, 1.4f}) {
			const ffg::TouchLayout l = PhoneLayout(false, scale, preset);
			for (int i = 0; i < ffg::TI(ffg::TouchId::Cancel); ++i) {
				const Vec2 c = l.centers[i];
				FFT_CHECK(c.x - l.radii[i] >= l.usable.x - 1.0f);
				FFT_CHECK(c.x + l.radii[i] <= l.usable.Right() + 1.0f);
				FFT_CHECK(c.y - l.radii[i] >= l.usable.y - 1.0f);
				FFT_CHECK(c.y + l.radii[i] <= l.usable.Bottom() + 1.0f);
				if (i != ffg::TI(ffg::TouchId::Pause)) FFT_CHECK(c.x - l.radii[i] >= l.viewport_size.x * 0.5f);   // never cross the midline
			}
		}
	}
}

FFT_TEST(layout_hit_test_and_cancel_zone) {
	const ffg::TouchLayout l = PhoneLayout();
	for (int i = 0; i < ffg::TI(ffg::TouchId::Cancel); ++i) FFT_CHECK(l.HitTest(l.centers[i]) == i);
	FFT_CHECK(l.HitTest(Vec2(100, 300)) == ffg::kTouchNone);
	FFT_CHECK(!l.InCancelZone(l.centers[ffg::TI(ffg::TouchId::Tech)]));
	FFT_CHECK(l.InCancelZone(l.centers[ffg::TI(ffg::TouchId::Cancel)]));
	// aiming at full range can never touch the cancel zone
	const float d = l.centers[ffg::TI(ffg::TouchId::Cancel)].distance_to(l.centers[ffg::TI(ffg::TouchId::Tech)]);
	FFT_CHECK(d >= l.aim_radius + l.radii[ffg::TI(ffg::TouchId::Cancel)] - 0.01f);
	// min hit radius 5 mm
	for (int i = 0; i < ffg::TI(ffg::TouchId::Cancel); ++i) FFT_CHECK(l.hit_radii[i] >= 5.0f * l.ppm - 1e-3f);
	// sizes in mm: ATTACK 17 mm where there is room (iPad); a phone shrinks the cluster uniformly to fit
	ffg::TouchLayout pad;
	pad.Configure(Vec2(1366.0f, 1024.0f), ffg::Insets(), 5.2f, 1.0f, "default", false);
	FFT_NEAR(pad.radii[ffg::TI(ffg::TouchId::Attack)] * 2.0f / pad.ppm, 17.0, 1e-3);
	FFT_NEAR(pad.radii[ffg::TI(ffg::TouchId::Guard)] * 2.0f / pad.ppm, 13.5, 1e-3);
	const float phone_mm = l.radii[ffg::TI(ffg::TouchId::Attack)] * 2.0f / l.ppm;
	FFT_CHECK(phone_mm > 14.0f && phone_mm <= 17.0f);
}

FFT_TEST(layout_left_handed_mirrors) {
	const ffg::TouchLayout r = PhoneLayout(false);
	const ffg::TouchLayout l = PhoneLayout(true);
	for (int i = 0; i < ffg::kTouchCount; ++i) {
		FFT_NEAR(l.centers[i].x, r.viewport_size.x - r.centers[i].x, 1e-3);
		FFT_NEAR(l.centers[i].y, r.centers[i].y, 1e-3);
	}
	FFT_CHECK(l.IsStickSide(Vec2(1000, 300)));
	FFT_CHECK(!r.IsStickSide(Vec2(1000, 300)));
}

FFT_TEST(layout_ring_petals) {
	for (bool lefty : {false, true}) {
		const ffg::TouchLayout l = PhoneLayout(lefty);
		const auto rects = l.RingRects();
		for (size_t i = 0; i < 4; ++i) {
			const ffg::Rect& rc = rects[i];
			FFT_CHECK(rc.x >= l.usable.x - 0.5f && rc.Right() <= l.usable.Right() + 0.5f);
			FFT_CHECK(rc.y >= l.usable.y - 0.5f && rc.Bottom() <= l.usable.Bottom() + 0.5f);
			FFT_CHECK(l.RingHit(rc.Center()) == static_cast<int>(i));
			// never over a chip
			for (int e = 0; e < 4; ++e) {
				const Vec2 c = l.centers[ffg::TI(ffg::TouchId::Elem0) + e];
				FFT_CHECK(!rc.Has(c));
			}
		}
	}
}

// =================================================================================== touch controls

struct TouchRig {
	ffg::TouchControls t;
	ff::InputFrame f;
	TouchRig() {
		ffg::Insets ins;
		t.Relayout(Vec2(1266.0f, 585.0f), ins, 9.06f);
	}
	Vec2 C(ffg::TouchId id) const { return t.layout.centers[ffg::TI(id)]; }
	ff::InputFrame& Tick(float dt = 1.0f / 60.0f) {
		t.Update(dt);
		f = ff::InputFrame();
		t.FillFrame(f);
		return f;
	}
};

FFT_TEST(touch_tap_attack_reports_both_edges) {
	TouchRig r;
	r.t.TouchDown(0, r.C(ffg::TouchId::Attack));
	r.t.TouchUp(0, r.C(ffg::TouchId::Attack), false);
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.attack_pressed && f.attack_released && !f.attack_held);
	const ff::InputFrame& g = r.Tick();
	FFT_CHECK(!g.attack_pressed && !g.attack_released);
}

FFT_TEST(touch_hold_attack_and_ring) {
	TouchRig r;
	r.t.TouchDown(1, r.C(ffg::TouchId::Attack));
	FFT_CHECK(r.Tick().attack_held);
	for (int i = 0; i < 20; ++i) r.Tick();
	FFT_NEAR(r.t.AttackRingFill(), 1.0, 1e-6);   // self-timed: Earth 0.25 s, element 0, 21 ticks = 0.35 s
	// with the game's context the ring follows the sim (buffered press: empty)
	ffg::TouchContext c;
	c.has_charge = true;
	c.attack_charge = 0.0f;
	c.attack_decide = 0.2f;
	r.t.SetContext(c);
	FFT_NEAR(r.t.AttackRingFill(), 0.0, 1e-6);
	c.attack_charge = 0.1f;
	r.t.SetContext(c);
	FFT_NEAR(r.t.AttackRingFill(), 0.5, 1e-4);
	c.attack_charge = 0.2f - 0.0005f;   // reads full from 1 ms before the threshold
	r.t.SetContext(c);
	FFT_NEAR(r.t.AttackRingFill(), 1.0, 1e-6);
	r.t.TouchUp(1, r.C(ffg::TouchId::Attack), false);
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.attack_released && !f.attack_held);
	FFT_NEAR(r.t.AttackRingFill(), 0.0, 1e-6);
}

FFT_TEST(touch_attack_flick_up_then_release_flick) {
	TouchRig r;
	const Vec2 a = r.C(ffg::TouchId::Attack);
	const float px6 = 6.2f * r.t.layout.ppm;
	r.t.TouchDown(0, a);
	r.Tick();
	r.t.TouchMove(0, a + Vec2(0, -px6));
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.attack_gesture == ff::Gesture::Up);
	FFT_CHECK(!r.t.haptics.empty());
	r.t.TouchUp(0, a + Vec2(0, -px6), false);
	FFT_CHECK(r.Tick().attack_gesture == ff::Gesture::None);   // fires once
	// hold past the window, then flick down at release
	r.t.TouchDown(0, a);
	for (int i = 0; i < 30; ++i) r.Tick();
	r.t.TouchMove(0, a + Vec2(0, px6));
	FFT_CHECK(r.Tick().attack_gesture == ff::Gesture::None);
	r.t.TouchUp(0, a + Vec2(0, px6), false);
	const ff::InputFrame& g = r.Tick();
	FFT_CHECK(g.attack_released && g.attack_gesture == ff::Gesture::Down);
	// a cancelled touch never counts as a flick
	r.t.TouchDown(0, a);
	for (int i = 0; i < 30; ++i) r.Tick();
	r.t.TouchMove(0, a + Vec2(px6, 0));
	r.t.TouchUp(0, a + Vec2(px6, 0), true);
	const ff::InputFrame& h = r.Tick();
	FFT_CHECK(h.attack_released && h.attack_gesture == ff::Gesture::None);
}

FFT_TEST(touch_camera_finger_never_attacks) {
	TouchRig r;
	const Vec2 start(r.C(ffg::TouchId::Attack).x - 220.0f, 120.0f);   // right half, empty space
	FFT_CHECK(!r.t.layout.IsStickSide(start));
	r.t.TouchDown(3, start);
	FFT_CHECK(r.t.Role(3) == ffg::FingerRole::Camera);
	r.t.TouchMove(3, r.C(ffg::TouchId::Attack));
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(!f.attack_pressed && !f.attack_held);
	FFT_CHECK(f.cam_delta.x > 0.0f);   // finger right = look right
	FFT_CHECK(f.cam_delta.y < 0.0f);   // finger down = look down
	// camera sensitivity: 0.055 rad per mm
	r.t.TouchMove(3, r.C(ffg::TouchId::Attack) + Vec2(r.t.layout.ppm * 10.0f, 0));
	FFT_NEAR(r.Tick().cam_delta.x, 0.55, 1e-3);
}

FFT_TEST(touch_stick_deadzone_and_follow) {
	TouchRig r;
	const Vec2 s(200, 400);
	r.t.TouchDown(2, s);
	FFT_CHECK(r.t.Role(2) == ffg::FingerRole::Stick);
	const float rad = r.t.layout.stick_radius;
	r.t.TouchMove(2, s + Vec2(rad * 0.05f, 0));
	FFT_NEAR(r.Tick().move.length(), 0.0, 1e-6);   // dead zone
	r.t.TouchMove(2, s + Vec2(0, -rad));
	const ff::InputFrame& f = r.Tick();
	FFT_NEAR(f.move.y, 1.0, 1e-4);   // up = forward
	r.t.TouchMove(2, s + Vec2(0, -rad * 3.0f));   // the base follows
	FFT_NEAR(r.Tick().move.length(), 1.0, 1e-4);
	FFT_NEAR(r.t.StickCenter().y, s.y - rad * 2.0f, 1e-2);
	r.t.TouchUp(2, s, false);
	FFT_NEAR(r.Tick().move.length(), 0.0, 1e-6);
}

FFT_TEST(touch_technique_aim_commit_and_cancel) {
	TouchRig r;
	const Vec2 tc = r.C(ffg::TouchId::Tech);
	r.t.TouchDown(4, tc);
	const ff::InputFrame& f0 = r.Tick();
	FFT_CHECK(f0.tech_pressed && f0.tech_held && !f0.tech_aim_active);
	r.t.TouchMove(4, tc + Vec2(-r.t.layout.aim_radius, 0));
	const ff::InputFrame& f1 = r.Tick();
	FFT_CHECK(f1.tech_aim_active);
	FFT_NEAR(f1.tech_aim.x, -1.0, 1e-4);
	r.t.TouchUp(4, tc + Vec2(-r.t.layout.aim_radius, 0), false);
	const ff::InputFrame& f2 = r.Tick();
	FFT_CHECK(f2.tech_released && !f2.tech_cancel && !f2.tech_held);
	FFT_NEAR(f2.tech_aim.x, -1.0, 1e-4);   // the release tick still carries the final aim
	FFT_CHECK(f2.tech_aim_active);
	const ff::InputFrame& f3 = r.Tick();
	FFT_CHECK(!f3.tech_aim_active && f3.tech_aim.length() == 0.0f);
	// drag into the cancel zone: cancel, and the later lift sends nothing
	r.t.TouchDown(4, tc);
	r.Tick();
	r.t.TouchMove(4, r.C(ffg::TouchId::Cancel));
	const ff::InputFrame& g = r.Tick();
	FFT_CHECK(g.tech_cancel && !g.tech_held);
	r.t.TouchUp(4, r.C(ffg::TouchId::Cancel), false);
	const ff::InputFrame& g2 = r.Tick();
	FFT_CHECK(!g2.tech_released && !g2.tech_cancel);
	// second finger tap on the cancel zone
	r.t.TouchDown(4, tc);
	r.Tick();
	r.t.TouchDown(5, r.C(ffg::TouchId::Cancel));
	FFT_CHECK(r.Tick().tech_cancel);
	r.t.TouchUp(5, r.C(ffg::TouchId::Cancel), false);
	r.t.TouchUp(4, tc, false);
	FFT_CHECK(!r.Tick().tech_released);
	// release-all (focus loss) during a held technique: cancelled, never fired
	r.t.TouchDown(4, tc);
	r.Tick();
	r.t.TouchMove(4, tc + Vec2(-30, -30));
	r.t.ReleaseAll(true);
	const ff::InputFrame& h = r.Tick();
	FFT_CHECK(h.tech_cancel && !h.tech_released);
	// press + cancel in the same tick
	r.t.TouchDown(4, tc);
	r.t.ReleaseAll(true);
	const ff::InputFrame& k = r.Tick();
	FFT_CHECK(k.tech_pressed && k.tech_cancel && !k.tech_held && !k.tech_released);
}

FFT_TEST(touch_shape_tap_is_edge_only) {
	TouchRig r;
	r.t.TouchDown(4, r.C(ffg::TouchId::Tech));
	r.Tick();
	r.t.TouchDown(0, r.C(ffg::TouchId::Attack));
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.attack_pressed && !f.attack_held);
	FFT_CHECK(r.t.IsAttackShape());
	r.t.TouchUp(0, r.C(ffg::TouchId::Attack), false);
	FFT_CHECK(!r.Tick().attack_released);
}

FFT_TEST(touch_guard_flicks_repeat) {
	TouchRig r;
	const Vec2 g = r.C(ffg::TouchId::Guard);
	const float px = 6.5f * r.t.layout.ppm;
	r.t.TouchDown(1, g);
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.guard_pressed && f.guard_held);
	for (int i = 0; i < 40; ++i) r.Tick();
	r.t.TouchMove(1, g + Vec2(0, -px));
	FFT_CHECK(r.Tick().guard_gesture == ff::Gesture::Up);
	r.t.TouchMove(1, g + Vec2(0, 0.2f));
	FFT_CHECK(r.Tick().guard_gesture == ff::Gesture::Down);
	r.t.TouchUp(1, g, false);
	FFT_CHECK(r.Tick().guard_released);
}

FFT_TEST(touch_second_finger_on_held_control_is_inert) {
	TouchRig r;
	r.t.TouchDown(0, r.C(ffg::TouchId::Guard));
	r.t.TouchDown(1, r.C(ffg::TouchId::Guard));
	FFT_CHECK(r.t.Role(1) == ffg::FingerRole::Dead);
	r.t.TouchMove(1, r.C(ffg::TouchId::Guard) + Vec2(-200, -100));
	FFT_NEAR(r.Tick().cam_delta.length(), 0.0, 1e-9);
}

FFT_TEST(touch_element_chip_and_sub_ring) {
	TouchRig r;
	ffg::TouchContext c;
	c.element = 0;
	r.t.SetContext(c);
	// tap Fire
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	r.t.TouchUp(0, r.C(ffg::TouchId::Elem2), false);
	FFT_CHECK(r.Tick().element_select == 2);
	FFT_CHECK(!r.t.IsRingOpen());   // it was not the active chip
	// the context lags one frame: the local choice is kept
	r.t.SetContext(c);
	FFT_CHECK(r.t.ShownElement() == 2);
	c.element = 2;
	r.t.SetContext(c);
	// tap the active chip again: ring opens, a petal selects
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	r.t.TouchUp(0, r.C(ffg::TouchId::Elem2), false);
	r.Tick();
	FFT_CHECK(r.t.GetRingMode() == ffg::RingMode::Tap);
	const auto rects = r.t.layout.RingRects();
	r.t.TouchDown(1, rects[3].Center());
	FFT_CHECK(r.t.Role(1) == ffg::FingerRole::Dead);
	FFT_CHECK(r.Tick().sub_select == 3);
	FFT_CHECK(!r.t.IsRingOpen());
	// tap-opened ring closes after 4 s
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	r.t.TouchUp(0, r.C(ffg::TouchId::Elem2), false);
	FFT_CHECK(r.t.IsRingOpen());
	for (int i = 0; i < 250; ++i) r.Tick();
	FFT_CHECK(!r.t.IsRingOpen());
	// long-press any chip -> slide ring; slide onto a petal, lift chooses
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	for (int i = 0; i < 22; ++i) r.Tick();   // > 0.32 s
	FFT_CHECK(r.t.GetRingMode() == ffg::RingMode::Slide);
	r.t.TouchMove(0, rects[1].Center());
	FFT_CHECK(r.t.RingHover() == 1);
	r.t.TouchUp(0, rects[1].Center(), false);
	FFT_CHECK(r.Tick().sub_select == 1);
	// a locked sub-element cannot be chosen
	c.subs_unlocked = {{true, true, false, true}};
	r.t.SetContext(c);
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	for (int i = 0; i < 22; ++i) r.Tick();
	r.t.TouchMove(0, rects[2].Center());
	r.t.TouchUp(0, rects[2].Center(), false);
	FFT_CHECK(r.Tick().sub_select == -1);
	// release_all closes the ring without choosing
	r.t.TouchDown(0, r.C(ffg::TouchId::Elem2));
	for (int i = 0; i < 22; ++i) r.Tick();
	r.t.TouchMove(0, rects[0].Center());
	r.t.ReleaseAll(true);
	FFT_CHECK(r.Tick().sub_select == -1);
	FFT_CHECK(!r.t.IsRingOpen());
	// a locked element chip makes the finger inert
	c.unlocked_elements = {{true, true, true, false}};
	r.t.SetContext(c);
	r.t.TouchDown(2, r.C(ffg::TouchId::Elem3));
	FFT_CHECK(r.t.Role(2) == ffg::FingerRole::Dead);
	FFT_CHECK(r.Tick().element_select == -1);
}

FFT_TEST(touch_chip_flick_picks_sub_without_long_press) {
	TouchRig r;
	ffg::TouchContext c;
	c.element = 0;
	r.t.SetContext(c);
	const auto rects = r.t.layout.RingRects();
	const Vec2 fire = r.C(ffg::TouchId::Elem2);
	const float rad = r.t.layout.radii[ffg::TI(ffg::TouchId::Elem2)];
	// touch Fire and slide straight onto petal 3: the ring opens on the slide, lift picks Fire + sub 3 in one stroke
	r.t.TouchDown(0, fire);
	r.t.TouchMove(0, rects[3].Center());
	FFT_CHECK(r.t.GetRingMode() == ffg::RingMode::Slide);
	FFT_CHECK(r.t.RingHover() == 3);
	r.t.TouchUp(0, rects[3].Center(), false);
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.element_select == 2 && f.sub_select == 3);
	FFT_CHECK(!r.t.IsRingOpen());
	// a short flick toward the column that stops short of it aims at the petal nearest its height
	int near = 0;
	for (int i = 1; i < 4; ++i)
		if (std::abs(rects[static_cast<size_t>(i)].Center().y - fire.y) < std::abs(rects[static_cast<size_t>(near)].Center().y - fire.y)) near = i;
	const Vec2 flick(fire.x - rad * 2.0f, rects[static_cast<size_t>(near)].Center().y);
	r.t.TouchDown(0, fire);
	r.t.TouchMove(0, flick);
	FFT_CHECK(r.t.RingHover() == near);
	r.t.TouchUp(0, flick, false);
	FFT_CHECK(r.Tick().sub_select == near);
	// sliding away from the column (or not far enough) opens nothing and picks nothing
	r.t.TouchDown(0, fire);
	r.t.TouchMove(0, fire + Vec2(rad * 2.5f, 0.0f));
	FFT_CHECK(!r.t.IsRingOpen());
	r.t.TouchUp(0, fire + Vec2(rad * 2.5f, 0.0f), false);
	FFT_CHECK(r.Tick().sub_select == -1);
	r.t.TouchDown(0, fire);
	r.t.TouchMove(0, fire - Vec2(rad * 1.2f, 0.0f));
	FFT_CHECK(!r.t.IsRingOpen());
	r.t.TouchUp(0, fire - Vec2(rad * 1.2f, 0.0f), false);
	FFT_CHECK(r.Tick().sub_select == -1);
}

FFT_TEST(touch_pause_on_lift_only) {
	TouchRig r;
	r.t.TouchDown(0, r.C(ffg::TouchId::Pause));
	FFT_CHECK(r.t.pause_requests == 0);
	r.t.TouchUp(0, r.C(ffg::TouchId::Pause), false);
	FFT_CHECK(r.t.pause_requests == 1);
	FFT_CHECK(r.Tick().pause_pressed);
	r.t.TouchDown(0, r.C(ffg::TouchId::Pause));
	r.t.TouchUp(0, r.C(ffg::TouchId::Pause), true);   // cancelled
	FFT_CHECK(r.t.pause_requests == 1);
	r.t.TouchDown(0, r.C(ffg::TouchId::Pause));
	r.t.TouchUp(0, Vec2(10, 500), false);              // slid far away
	FFT_CHECK(r.t.pause_requests == 1);
}

FFT_TEST(touch_index_reuse_cancels_stale_finger) {
	TouchRig r;
	r.t.TouchDown(0, r.C(ffg::TouchId::Guard));
	r.t.TouchDown(0, Vec2(200, 300));   // reused without an up event
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.guard_released);
	FFT_CHECK(r.t.Role(0) == ffg::FingerRole::Stick);
	FFT_CHECK(r.t.ActiveFingerCount() == 1);
}

FFT_TEST(touch_evade_and_target) {
	TouchRig r;
	r.t.TouchDown(0, r.C(ffg::TouchId::Evade));
	const ff::InputFrame& f = r.Tick();
	FFT_CHECK(f.evade_pressed && f.evade_held);
	FFT_CHECK(r.Tick().evade_held);
	r.t.TouchUp(0, r.C(ffg::TouchId::Evade), false);
	FFT_CHECK(!r.Tick().evade_held);
	r.t.TouchDown(1, r.C(ffg::TouchId::Target));
	FFT_CHECK(r.Tick().target_cycle);
}

// =================================================================================== desktop input

struct DeskRig {
	ffg::DesktopInput d;
	ffg::DeviceSample s;
	ff::InputFrame f;
	void Press(ffg::DeskAction a) { s.down[static_cast<size_t>(ffg::DA(a))] = true; }
	void Release(ffg::DeskAction a) { s.down[static_cast<size_t>(ffg::DA(a))] = false; }
	ff::InputFrame& Frame(float dt = 1.0f / 60.0f) {
		s.dt = dt;
		d.Sample(s);
		s.just_pressed = {};
		s.mouse_delta = Vec2();
		f = ff::InputFrame();
		d.FillFrame(f);
		return f;
	}
};

FFT_TEST(desk_attack_tap_hold_and_slot_keys) {
	DeskRig r;
	r.Press(ffg::DeskAction::Attack);
	const ff::InputFrame& a = r.Frame();
	FFT_CHECK(a.attack_pressed && a.attack_held);
	FFT_CHECK(r.Frame().attack_held);
	r.Release(ffg::DeskAction::Attack);
	const ff::InputFrame& b = r.Frame();
	FFT_CHECK(b.attack_released && !b.attack_held);
	r.Press(ffg::DeskAction::Thrust);
	const ff::InputFrame& c = r.Frame();
	FFT_CHECK(c.attack_pressed && c.attack_gesture == ff::Gesture::Up);
	r.Release(ffg::DeskAction::Thrust);
	r.Frame();
	// press and release inside one rendered frame still taps
	r.s.just_pressed[static_cast<size_t>(ffg::DA(ffg::DeskAction::Sweep))] = true;
	const ff::InputFrame& d = r.Frame();
	FFT_CHECK(d.attack_pressed && d.attack_released && d.attack_gesture == ff::Gesture::Side);
}

FFT_TEST(desk_guard_chords_are_not_attacks) {
	DeskRig r;
	r.Press(ffg::DeskAction::Guard);
	FFT_CHECK(r.Frame().guard_pressed);
	r.Press(ffg::DeskAction::Attack);
	const ff::InputFrame& f = r.Frame();
	FFT_CHECK(f.guard_gesture == ff::Gesture::Up && !f.attack_pressed && !f.attack_held);
	r.Release(ffg::DeskAction::Attack);
	FFT_CHECK(!r.Frame().attack_released);
	r.Press(ffg::DeskAction::Ground);
	const ff::InputFrame& g = r.Frame();
	FFT_CHECK(g.guard_gesture == ff::Gesture::Down && !g.attack_pressed);
	// same-frame chord: guard scanned first
	DeskRig q;
	q.Press(ffg::DeskAction::Guard);
	q.Press(ffg::DeskAction::Attack);
	const ff::InputFrame& h = q.Frame();
	FFT_CHECK(h.guard_pressed && h.guard_gesture == ff::Gesture::Up && !h.attack_pressed);
}

FFT_TEST(desk_technique_shape_cancel_pause) {
	DeskRig r;
	r.Press(ffg::DeskAction::Tech);
	FFT_CHECK(r.Frame().tech_pressed);
	r.Press(ffg::DeskAction::Attack);   // J while L held = shape
	const ff::InputFrame& s = r.Frame();
	FFT_CHECK(s.attack_pressed && !s.attack_held);
	r.Release(ffg::DeskAction::Attack);
	FFT_CHECK(!r.Frame().attack_released);
	r.s.mouse_delta = Vec2(200, -50);
	const ff::InputFrame& m = r.Frame();
	FFT_CHECK(m.tech_aim_active && m.tech_aim.x > 0.9f);
	r.Press(ffg::DeskAction::Pause);   // Esc with a held technique cancels it, never pauses
	const ff::InputFrame& c = r.Frame();
	FFT_CHECK(c.tech_cancel && !c.pause_pressed && r.d.pause_requests == 0);
	r.Release(ffg::DeskAction::Pause);
	r.Release(ffg::DeskAction::Tech);
	FFT_CHECK(!r.Frame().tech_released);   // a cancelled hold never commits
	r.Press(ffg::DeskAction::Pause);
	FFT_CHECK(r.Frame().pause_pressed && r.d.pause_requests == 1);
	// focus loss during a held technique
	DeskRig q;
	q.Press(ffg::DeskAction::Tech);
	q.Frame();
	q.d.CancelAll();
	const ff::InputFrame& k = q.Frame();
	FFT_CHECK(k.tech_cancel);
}

FFT_TEST(desk_elements_and_subs) {
	DeskRig r;
	r.d.SetSubContext(0, 0, {{true, true, true, true}});
	r.Press(ffg::DeskAction::Elem2);
	FFT_CHECK(r.Frame().element_select == 2);
	r.Release(ffg::DeskAction::Elem2);
	r.Frame();
	r.d.SetSubContext(2, 0, {{true, false, true, true}});
	r.Press(ffg::DeskAction::Elem2);   // the active element's key again cycles subs (skips locked 1)
	const ff::InputFrame& f = r.Frame();
	FFT_CHECK(f.element_select == -1 && f.sub_select == 2);
	r.Release(ffg::DeskAction::Elem2);
	r.Frame();
	r.d.SetSubContext(2, 2, {{true, false, true, true}});
	r.Press(ffg::DeskAction::SubPrev);
	FFT_CHECK(r.Frame().sub_select == 0);
	r.Release(ffg::DeskAction::SubPrev);
	r.Frame();
	// LB + d-pad = sub-element
	r.Press(ffg::DeskAction::Cancel);
	r.Frame();
	r.Press(ffg::DeskAction::Elem3);
	const ff::InputFrame& g = r.Frame();
	FFT_CHECK(g.sub_select == 3 && g.element_select == -1);
	// locked elements are never selected
	DeskRig q;
	q.d.SetUnlocked({{true, true, false, true}});
	q.d.SetSubContext(0, 0, {{true, true, true, true}});
	q.Press(ffg::DeskAction::Elem2);
	FFT_CHECK(q.Frame().element_select == -1);
}

FFT_TEST(desk_camera_stick_and_mouse) {
	DeskRig r;
	r.s.cam_stick = Vec2(1, 0);
	const ff::InputFrame& f = r.Frame(0.1f);
	FFT_NEAR(f.cam_delta.x, 0.26, 1e-4);   // 2.6 rad/s
	r.d.settings.invert_y = true;
	r.s.cam_stick = Vec2(0, 1);
	FFT_CHECK(r.Frame(0.1f).cam_delta.y < 0.0f);
	r.s.cam_stick = Vec2();
	r.s.mouse_look = true;
	r.s.mouse_delta = Vec2(100, 0);
	FFT_NEAR(r.Frame().cam_delta.x, 0.32, 1e-4);
}

// =================================================================================== settings

FFT_TEST(settings_clamp_and_json) {
	ffg::SettingsData s;
	s.control_scale = 9.0f;
	s.control_opacity = -1.0f;
	s.layout_preset = "giant";
	s.camera_sensitivity = 0.01f;
	s.quality = 7;
	s.frame_rate_cap = 100;
	s.touch_ui_mode = 5;
	const ffg::SettingsData c = ffg::ClampSettings(s);
	FFT_NEAR(c.control_scale, 1.4, 1e-6);
	FFT_NEAR(c.control_opacity, 0.3, 1e-6);
	FFT_CHECK(c.layout_preset == "default");
	FFT_NEAR(c.camera_sensitivity, 0.3, 1e-6);
	FFT_CHECK(c.quality == 2 && c.frame_rate_cap == 120 && c.touch_ui_mode == 2);
	ffg::SettingsData d;
	d.left_handed = true;
	d.flashes = 0.25f;
	d.layout_preset = "wide";
	d.quality = 1;
	d.frame_rate_cap = 30;
	const std::string js = ffg::SettingsToJson(d);
	ffg::SettingsData back;
	FFT_CHECK(ffg::SettingsFromJson(js, back));
	FFT_CHECK(ffg::SettingsEqual(back, d));
	ffg::SettingsData bad;
	FFT_CHECK(!ffg::SettingsFromJson("{not json", bad));
	FFT_CHECK(ffg::SettingsEqual(bad, ffg::SettingsData()));
	ffg::SettingsData part;
	FFT_CHECK(ffg::SettingsFromJson("{\"invert_y\": true, \"control_scale\": \"big\", \"haptics\": 0}", part));
	FFT_CHECK(part.invert_y && !part.haptics);
	FFT_NEAR(part.control_scale, 1.0, 1e-6);
	FFT_CHECK(ffg::AutoQualityFor(false, 8, 16.0) == 2);
	FFT_CHECK(ffg::AutoQualityFor(true, 6, 4.0) == 1);
	FFT_CHECK(ffg::AutoQualityFor(true, 6, 6.0) == 2);
}

// =================================================================================== feel

FFT_TEST(feel_hitstop_budget) {
	ffg::HitStop h;
	double now = 100.0;
	h.Request(9, now, false);
	int frozen = 0;
	for (int i = 0; i < 30; ++i) {
		if (h.FrameTick(now, 1.0f / 60.0f) < 0.5f) ++frozen;
		now += 1.0 / 60.0;
	}
	FFT_CHECK(frozen == 9);
	h.Request(9, now, false);   // only 3 left in this rolling second
	frozen = 0;
	for (int i = 0; i < 30; ++i) {
		if (h.FrameTick(now, 1.0f / 60.0f) < 0.5f) ++frozen;
		now += 1.0 / 60.0;
	}
	FFT_CHECK(frozen == 3);
	now += 1.2;
	h.Request(9, now, true);   // reduced motion: at most 3
	FFT_CHECK(h.Pending() == 3);
	ffg::HitStop s;
	s.RequestSlowmo(0.22f);
	FFT_NEAR(s.FrameTick(0.0, 0.1f), ffg::HitStop::kSlowmoScale, 1e-6);
	FFT_NEAR(s.FrameTick(0.1, 0.1f), ffg::HitStop::kSlowmoScale, 1e-6);
	FFT_NEAR(s.FrameTick(0.2, 0.1f), ffg::HitStop::kSlowmoScale, 1e-6);   // 0.02 s were still left
	FFT_NEAR(s.FrameTick(0.3, 0.1f), 1.0, 1e-6);
	ffg::HapticGate g;
	FFT_CHECK(g.Allow(1.0, true));
	FFT_CHECK(!g.Allow(1.03, true));
	FFT_CHECK(g.Allow(1.07, true));
	FFT_CHECK(!g.Allow(2.0, false));
}

static ff::Event MakeEvent(const char* type, std::initializer_list<std::pair<const char*, ff::Value>> fields) {
	ff::Event e;
	e.type = type;
	ff::Dict d;
	for (const auto& f : fields) d.set(f.first, f.second);
	e.data = ff::Value(d);
	return e;
}

FFT_TEST(feel_event_mapping) {
	ff::Snapshot snap;
	ff::ActorView p;
	p.id = 1;
	p.pos = Vec3(0, 0, 5);
	ff::ActorView rv;
	rv.id = 2;
	rv.pos = Vec3(0, 0, -5);
	snap.actors = {p, rv};
	ffg::FeelOptions opt;
	opt.player_id = 1;
	opt.slowmo_assist = true;
	std::vector<ff::Event> evs = {
		MakeEvent("hit", {{"actor", 1}, {"attacker", 2}, {"tier", 2}, {"damage", 8.0}, {"dir", Vec3(0, 0, 1)}}),
	};
	ffg::FeelOutput out;
	ffg::HandleFeelEvents(evs, snap, opt, out);
	FFT_CHECK(out.hitstop == 7);
	FFT_CHECK(out.haptics.size() == 1 && out.haptics[0] == "light");   // the victim's haptic (player), T2 visuals
	FFT_CHECK(!out.shakes.empty() && !out.kicks.empty());
	out.Clear();
	evs = {MakeEvent("hit", {{"actor", 2}, {"attacker", 1}, {"result", "knockdown"}})};
	ffg::HandleFeelEvents(evs, snap, opt, out);
	FFT_CHECK(out.hitstop == 9 && out.fov_punch < 0.0f);
	FFT_CHECK(out.haptics.size() == 1 && out.haptics[0] == "light");   // attacker's light pulse
	out.Clear();
	evs = {MakeEvent("perfect_deflect", {{"actor", 1}})};
	ffg::HandleFeelEvents(evs, snap, opt, out);
	FFT_CHECK(out.flash == "perfect" && out.slowmo > 0.2f && out.hitstop == 6);
	out.Clear();
	opt.flashes = 0.0f;
	ffg::HandleFeelEvents(evs, snap, opt, out);
	FFT_CHECK(out.flash.empty());
	out.Clear();
	evs = {MakeEvent("interaction", {{"outcome", "transform"}, {"counter_actor", 1}, {"pos", Vec3(1, 0, 1)}}),
	       MakeEvent("app_toast", {{"text", "Mastered"}}), MakeEvent("clash", {{"pos", Vec3()}})};
	ffg::HandleFeelEvents(evs, snap, opt, out);
	FFT_CHECK(out.zoom && out.toasts.size() == 1 && out.hitstop == 4);
	FFT_CHECK(std::string(ffg::HapticTypeFor("perfect")) == "FeedbackSuccess");
	FFT_CHECK(std::string(ffg::HapticTypeFor("heavy")) == "ImpactHeavy");
}

// =================================================================================== arena + camera

static ff::ArenaView LabArena() {
	ff::ArenaView a;
	const float h = 16.0f;
	auto box = [&](Vec3 mn, Vec3 mx, const char* name) {
		ff::ArenaBox b;
		b.min = mn;
		b.max = mx;
		b.name = name;
		a.solids.push_back(b);
	};
	box(Vec3(-h - 1, 0, -h - 1), Vec3(h + 1, 3.5f, -h), "north_wall");
	box(Vec3(-h - 1, 0, h), Vec3(h + 1, 3.5f, h + 1), "south_wall");
	box(Vec3(-h - 1, 0, -h), Vec3(-h, 3.5f, h), "west_wall");
	box(Vec3(h, 0, -h), Vec3(h + 1, 3.5f, h), "east_wall");
	box(Vec3(-5.0f, 0, -1.25f), Vec3(-2.5f, 1.7f, -0.75f), "cover_wall");
	box(Vec3(-4.0f, 0, 10.0f), Vec3(4.0f, 0.6f, 14.0f), "terrace");
	box(Vec3(9.0f, 0, 9.0f), Vec3(12.0f, 0.35f, 12.0f), "step_block");
	a.pool_min = Vec2(7, -5);
	a.pool_max = Vec2(13, 3);
	a.metal_min = Vec2(-12, -4);
	a.metal_max = Vec2(-6, 2);
	return a;
}

FFT_TEST(arena_ground_and_slab) {
	ffg::ArenaGround g;
	g.Set(LabArena());
	FFT_CHECK(g.solids[0].boundary && !g.solids[4].boundary);
	FFT_NEAR(g.GroundHeight(10.5f, 10.5f, 0.0f), 0.35, 1e-6);   // step block reachable from the floor
	FFT_NEAR(g.GroundHeight(0.0f, 12.0f, 0.0f), 0.0, 1e-6);     // terrace 0.6 is a wall from the floor
	FFT_NEAR(g.GroundHeight(0.0f, 12.0f, 0.3f), 0.6, 1e-6);
	FFT_NEAR(g.GroundHeight(10.0f, 0.0f, 0.0f), -0.3, 1e-6);    // pool floor
	FFT_NEAR(g.GroundHeight(-9.0f, 0.0f, 0.0f), 0.02, 1e-6);    // metal plate
	FFT_NEAR(ffg::ArenaGround::Slab(Vec3(0, 1, 0), Vec3(10, 0, 0), Vec3(5, 0, -1), Vec3(6, 2, 1)), 0.5, 1e-6);
	FFT_CHECK(ffg::ArenaGround::Slab(Vec3(0, 3, 0), Vec3(10, 0, 0), Vec3(5, 0, -1), Vec3(6, 2, 1)) < 0.0f);
}

FFT_TEST(camera_free_follow_and_input) {
	ffg::CameraLogic cam;
	cam.SnapTo(Vec3(0, 0, 7), Vec3(0, 0, -7));
	FFT_NEAR(cam.yaw, ffg::kPi, 1e-5);   // looking along -Z toward the rival
	ffg::CameraOutput o;
	for (int i = 0; i < 120; ++i) o = cam.Update(1.0f / 60.0f, 1.0f / 60.0f, Vec3(0, 0, 7), nullptr, nullptr);
	FFT_NEAR(cam.CurDist(), 5.6, 1e-2);
	FFT_CHECK(o.pos.z > 7.0f);   // behind the player (who looks toward -Z)
	FFT_NEAR(o.fov, 62.0, 1e-4);
	cam.AddInput(Vec2(0.5f, 0.0f));
	FFT_NEAR(cam.yaw, ffg::kPi - 0.5f, 1e-5);
	cam.AddInput(Vec2(0.0f, 5.0f));
	FFT_NEAR(cam.pitch, -0.12, 1e-5);   // clamped
}

FFT_TEST(camera_collision_never_inside_walls) {
	ffg::ArenaGround g;
	g.Set(LabArena());
	ffg::CameraLogic cam;
	cam.arena = &g;
	// player backed against the south wall, looking north: the wall is right behind
	const Vec3 player(0, 0, 15.3f);
	const Vec3 rival(0, 0, 5.0f);
	cam.SnapTo(player, rival);
	ffg::CameraOutput o;
	for (int i = 0; i < 240; ++i) o = cam.Update(1.0f / 60.0f, 1.0f / 60.0f, player, &rival, nullptr);
	FFT_CHECK(o.pos.z < 16.0f);                      // inside the yard
	FFT_CHECK(cam.CurDist() >= ffg::CameraLogic::kMinPull - 1e-3f);
	FFT_CHECK(std::fabs(cam.Orbit()) > 0.05f || cam.Lift() > 0.05f);   // it swung or lifted
	// a pillar-free open yard keeps the full distance
	ffg::CameraLogic open;
	open.arena = &g;
	open.SnapTo(Vec3(0, 0, 3), Vec3(0, 0, -3));
	for (int i = 0; i < 240; ++i) open.Update(1.0f / 60.0f, 1.0f / 60.0f, Vec3(0, 0, 3), nullptr, nullptr);
	FFT_NEAR(open.CurDist(), 5.6, 0.05);
}

FFT_TEST(camera_assist_turns_toward_target_when_idle) {
	ffg::CameraLogic cam;
	cam.SnapTo(Vec3(0, 0, 0), Vec3(0, 0, -5));
	const Vec3 tgt(8, 0, 0);   // off to the side
	const float yaw0 = cam.yaw;
	for (int i = 0; i < 180; ++i) cam.Update(1.0f / 60.0f, 1.0f / 60.0f, Vec3(), &tgt, nullptr);
	const float want = std::atan2(8.0f, 0.0f);
	FFT_CHECK(std::fabs(ffg::WrapAngle(cam.yaw - want)) < std::fabs(ffg::WrapAngle(yaw0 - want)) - 0.5f);
}

FFT_TEST(camera_feel_real_time_and_reduced_motion) {
	ffg::CameraLogic cam;
	cam.SnapTo(Vec3(), Vec3(0, 0, -5));
	cam.ShakeAt(1.0f, Vec3(), 0.2f);
	FFT_NEAR(cam.ShakeAmount(), 1.0, 1e-5);
	cam.Update(0.0f, 0.1f, Vec3(), nullptr, nullptr);   // hit-stop: game dt 0, real dt runs
	FFT_NEAR(cam.ShakeAmount(), 0.5, 1e-4);
	cam.ShakeAt(1.0f, Vec3(16.0f, 0, 0), 0.2f);         // far events shake less: 1 / (1 + 16/8)
	cam.FovPunch(-3.0f, 0.25f);
	ffg::CameraOutput o = cam.Update(0.0f, 0.0f, Vec3(), nullptr, nullptr);
	FFT_NEAR(o.fov, 59.0, 1e-3);
	for (int i = 0; i < 20; ++i) o = cam.Update(0.0f, 1.0f / 60.0f, Vec3(), nullptr, nullptr);
	FFT_NEAR(o.fov, 62.0, 1e-3);
	ffg::CameraLogic rm;
	rm.SnapTo(Vec3(), Vec3(0, 0, -5));
	rm.reduced_motion = true;
	rm.ShakeAt(1.0f, Vec3(), 0.2f);
	FFT_NEAR(rm.ShakeAmount(), 0.3, 1e-5);
	rm.FovPunch();
	rm.ZoomTo(Vec3(1, 1, 1));
	const ffg::CameraOutput r = rm.Update(0.0f, 0.01f, Vec3(), nullptr, nullptr);
	FFT_NEAR(r.fov, 62.0, 1e-4);
}

// =================================================================================== anim timing

FFT_TEST(timing_contact_lands_at_startup_end) {
	struct C {
		float startup, contact;
	};
	for (C c : {C{0.20f, 0.20f}, C{0.25f, 0.30f}, C{0.10f, 0.40f}, C{0.60f, 0.15f}, C{0.30f, 0.05f}}) {
		FFT_NEAR(ffg::AnimTiming::StartupClipTime(c.startup, c.startup, c.contact), c.contact, 1e-5);
		float prev = ffg::AnimTiming::StartupClipTime(0.0f, c.startup, c.contact);
		FFT_CHECK(prev >= 0.0f);
		const int n = 200;
		for (int i = 1; i <= n; ++i) {
			const float t = c.startup * static_cast<float>(i) / n;
			const float x = ffg::AnimTiming::StartupClipTime(t, c.startup, c.contact);
			const float rate = (x - prev) / (c.startup / n);
			FFT_CHECK(rate >= -1e-4f && rate <= ffg::AnimTiming::kRateMax + 1e-3f);
			prev = x;
		}
	}
	// in range: constant rate
	FFT_NEAR(ffg::AnimTiming::StartupClipTime(0.1f, 0.2f, 0.24f), 0.12, 1e-6);
	// slow: anticipation holds at the chamber
	FFT_NEAR(ffg::AnimTiming::StartupClipTime(0.3f, 0.6f, 0.15f), 0.075, 1e-5);
	// recovery reaches the clip end and the weight fades to 0
	FFT_NEAR(ffg::AnimTiming::RecoveryClipTime(0.3f, 0.3f, 0.2f, 0.5f), 0.5, 1e-5);
	FFT_NEAR(ffg::AnimTiming::RecoveryWeight(0.0f, 0.3f), 1.0, 1e-6);
	FFT_NEAR(ffg::AnimTiming::RecoveryWeight(0.3f, 0.3f), 0.0, 1e-6);
	FFT_NEAR(ffg::AnimTiming::LoopTime(2.5f, 1.0f), 0.5, 1e-5);
}

// =================================================================================== locomotion

FFT_TEST(locomotion_weights_and_stride) {
	auto tw = ffg::LocomotionBlender::TargetWeights(Vec2(0, 0));
	FFT_NEAR(tw[0], 1.0, 1e-6);
	tw = ffg::LocomotionBlender::TargetWeights(Vec2(0, 1.4f));
	FFT_NEAR(tw[static_cast<size_t>(ffg::LocoRole::Walk)], 1.0, 1e-5);
	tw = ffg::LocomotionBlender::TargetWeights(Vec2(0, 5.0f));
	FFT_NEAR(tw[static_cast<size_t>(ffg::LocoRole::Run)], 1.0, 1e-5);
	tw = ffg::LocomotionBlender::TargetWeights(Vec2(-1.2f, 0));
	FFT_NEAR(tw[static_cast<size_t>(ffg::LocoRole::StrafeL)], 1.0, 1e-5);
	tw = ffg::LocomotionBlender::TargetWeights(Vec2(1.2f, 0));
	FFT_NEAR(tw[static_cast<size_t>(ffg::LocoRole::StrafeR)], 1.0, 1e-5);
	tw = ffg::LocomotionBlender::TargetWeights(Vec2(0, -1.2f));
	FFT_NEAR(tw[static_cast<size_t>(ffg::LocoRole::Back)], 1.0, 1e-5);
	// stride matching: a steady 1.4 m/s walk cycles at 1.4 / 1.26 per second (feet travel at ground speed)
	ffg::LocomotionBlender l;
	l.Reset();
	for (int i = 0; i < 300; ++i) l.Update(1.0f / 60.0f, Vec2(0, 1.4f));
	FFT_NEAR(l.cycle_rate, 1.4 / 1.26, 1e-3);
	FFT_CHECK(l.Dominant() == ffg::LocoRole::Walk);
	// weights are smoothed: one frame after a direction change nothing pops
	const float w_walk = l.weights[static_cast<size_t>(ffg::LocoRole::Walk)];
	l.Update(1.0f / 60.0f, Vec2(0, -1.0f));
	FFT_CHECK(l.weights[static_cast<size_t>(ffg::LocoRole::Walk)] > w_walk * 0.8f);
}

FFT_TEST(locomotion_multi_cycle_mocap_clip) {
	// a 4 s mocap walk holding 4 cycles, left touchdown at 2.5 % of the clip: stride is one cycle, the clip time runs
	// continuously through all four cycles (no jump when the shared phase wraps) and phase 0 lands on the touchdown
	ffg::LocomotionBlender l;
	l.Reset();
	l.SetGait(ffg::LocoRole::Walk, 2.0f, 4.0f, 0.025f, 4);
	FFT_NEAR(l.gaits[static_cast<size_t>(ffg::LocoRole::Walk)].stride, 2.0, 1e-5);
	FFT_NEAR(l.gaits[static_cast<size_t>(ffg::LocoRole::Walk)].offset, 0.1, 1e-5);
	FFT_NEAR(l.ClipTime(ffg::LocoRole::Walk, 4.0f), 0.1, 1e-5);
	float prev = l.ClipTime(ffg::LocoRole::Walk, 4.0f);
	float max_step = 0.0f;
	int wraps = 0;
	for (int i = 0; i < 60 * 9; ++i) {
		l.Update(1.0f / 60.0f, Vec2(0, 2.0f));
		const float t = l.ClipTime(ffg::LocoRole::Walk, 4.0f);
		float d = t - prev;
		if (d < -2.0f) {
			d += 4.0f;   // the clip itself loops
			++wraps;
		}
		max_step = std::max(max_step, std::fabs(d));
		prev = t;
	}
	FFT_CHECK(max_step < 0.05f);
	FFT_CHECK(wraps >= 1);
}

// =================================================================================== anim library + director

static void MarkAllAvailable(ffg::AnimLibrary& lib, const std::vector<std::string>& except = {}) {
	int h = 0;
	for (auto& it : lib.clips) {
		bool skip = false;
		for (const std::string& e : except)
			if (e == it.first) skip = true;
		it.second.available = !skip;
		it.second.handle = h++;
		if (it.second.duration <= 0.0f) it.second.duration = 1.0f;
	}
}

FFT_TEST(library_defaults_cover_the_catalogue) {
	ffg::AnimLibrary lib;
	FFT_CHECK(lib.clips.size() >= 120);
	FFT_CHECK(lib.moves.size() >= 150);
	const ffg::ClipDef* s = lib.Find("e_strike");
	FFT_CHECK(s && s->frames == 24 && !s->loop);
	FFT_NEAR(s->ContactTime(), 8.0 / 60.0, 1e-6);
	const ffg::ClipDef* w = lib.Find("walk");
	FFT_CHECK(w && w->loop && std::fabs(w->speed - 1.4f) < 1e-6f);
	const ffg::ClipDef* rd = lib.Find("l_redirect");
	FFT_CHECK(rd && rd->contacts.size() == 2 && rd->contacts[1] == 26);
	for (const auto& it : lib.moves) {
		const ffg::MoveClips& c = it.second.base;
		FFT_CHECK(!c.Empty());
	}
}

FFT_TEST(library_resolve_moves) {
	ffg::AnimLibrary lib;
	MarkAllAvailable(lib);
	ffg::MoveClips m = lib.ResolveMove("earth_attack", "", 0, ff::Slot::Strike, 0, "", false);
	FFT_CHECK(m.startup == "e_lift" && m.release == "e_strike" && m.hand_l == "tiger");
	m = lib.ResolveMove("earth_attack", "", 0, ff::Slot::Strike, 2, "", false);
	FFT_CHECK(m.release == "e_heave");
	m = lib.ResolveMove("fire_attack", "", 2, ff::Slot::Strike, 2, "", false);
	FFT_CHECK(m.hold == "f_charge" && m.release == "f_column");
	m = lib.ResolveMove("fire_attack", "", 2, ff::Slot::Strike, 3, "", false);
	FFT_CHECK(m.release == "f_inferno");
	m = lib.ResolveMove("fire_tech", "", 2, ff::Slot::Tech, 0, "DRAW", false);
	FFT_CHECK(m.hold == "f_heat_draw");
	m = lib.ResolveMove("guard", "ice_wall", 1, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.startup == "w_freeze" && m.hold == "w_shield");
	m = lib.ResolveMove("guard", "guard", 0, ff::Slot::Guard, 0, "", true);
	FFT_CHECK(m.startup == "e_wall" && m.hold == "e_guard");
	m = lib.ResolveMove("guard", "guard", 0, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.startup.empty() && m.hold == "e_guard");
	m = lib.ResolveMove("guard", "guard", 1, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.hold == "w_shield");
	m = lib.ResolveMove("static_ward", "", 2, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.perfect == "l_redirect");
	m = lib.ResolveMove("guard", "static_ward", 2, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.perfect == "l_redirect");
	m = lib.ResolveMove("no_such_move", "", 3, ff::Slot::Thrust, 0, "", false);
	FFT_CHECK(m.startup == "a_pierce");
	m = lib.ResolveMove("evade", "", 2, ff::Slot::Evade, 0, "", false);
	FFT_CHECK(m.startup == "evade_*");
	// clip fallbacks for a partial asset set
	MarkAllAvailable(lib, {"e_spatter", "hand_tiger"});
	const ffg::ClipDef* c = lib.Resolve("e_spatter");
	FFT_CHECK(c && c->name == "e_sweep");
	FFT_CHECK(lib.HandPose("tiger") == nullptr);
	FFT_CHECK(lib.HandPose("fist") && lib.HandPose("fist")->name == "hand_fist");
}

// A transition clip with a root travel curve: decelerating (stop) or accelerating (start) over `len` seconds.
static ffg::ClipDef TransitionClip(const char* name, float len, float total, bool stop, float settle = 0.0f) {
	ffg::ClipDef c;
	c.name = name;
	c.duration = len + settle;
	c.frames = static_cast<int>((len + settle) * 60.0f);
	c.available = true;
	c.curve_fps = 30.0f;
	const int n = static_cast<int>((len + settle) * 30.0f) + 1;
	for (int i = 0; i < n; ++i) {
		const float u = std::min(1.0f, (static_cast<float>(i) / 30.0f) / len);
		// stop: v falls linearly to 0 -> d = total * (1 - (1-u)^2); start: v rises -> d = total * u^2
		c.root_dist.push_back(stop ? total * (1.0f - (1.0f - u) * (1.0f - u)) : total * u * u);
	}
	return c;
}

FFT_TEST(loco_transition_distance_curve) {
	const ffg::ClipDef c = TransitionClip("stop", 1.0f, 2.0f, true, 0.5f);
	FFT_NEAR(c.TotalDist(), 2.0, 1e-5);
	FFT_NEAR(c.DistAt(0.0f), 0.0, 1e-6);
	FFT_NEAR(c.DistAt(0.5f), 1.5, 1e-3);
	FFT_NEAR(c.TimeAtDist(1.5f), 0.5, 2e-3);
	FFT_NEAR(c.TimeAtDist(2.0f), 1.0, 1e-3);   // first time the travel is complete (the settle follows)
	FFT_NEAR(c.TimeAtDist(-1.0f), 0.0, 1e-6);
	ffg::AnimLibrary lib;
	FFT_CHECK(lib.LoadClipsJson(R"({"clips": {"st": {"frames": 30, "duration": 0.5, "curve_fps": 10, "root_dist": [0, 0.1, 0.4, 0.9, 1.4, 1.9]}}})"));
	const ffg::ClipDef* st = lib.Find("st");
	FFT_CHECK(st && st->root_dist.size() == 6);
	FFT_NEAR(st->TimeAtDist(0.65f), 0.25, 1e-4);
	FFT_CHECK(lib.LoadAnimMapJson(R"({"locomotion": {"run_start": "st", "run_stop_l": "sl", "run_stop_r": "sr"}})"));
	FFT_CHECK(lib.run_start == "st" && lib.run_stop_l == "sl" && lib.run_stop_r == "sr");
}

FFT_TEST(loco_transition_stop_and_start) {
	const float dt = 1.0f / 60.0f;
	const ffg::ClipDef stop = TransitionClip("stop", 0.8f, 1.6f, true, 0.6f);
	const ffg::ClipDef start = TransitionClip("start", 0.9f, 2.4f, false);
	ffg::LocoTransition tr;
	tr.Reset();
	// a steady run: nothing fires
	for (int i = 0; i < 60; ++i) tr.Update(dt, 5.5f, 5.5f, true, &start, &stop, &stop);
	FFT_CHECK(tr.kind == ffg::LocoTransition::Kind::None);
	// brake like the sim (DECEL 42): the stop fires, its clip time only moves forward, reaches the end of the travel
	// when the fighter stands, then settles in real time and hands back
	float v = 5.5f, prev_t = -1.0f;
	bool fired = false, monotonic = true;
	int frames_after_stop = 0;
	for (int i = 0; i < 120; ++i) {
		v = std::max(0.0f, v - 42.0f * dt);
		tr.Update(dt, v, v, true, &start, &stop, &stop);
		if (tr.kind == ffg::LocoTransition::Kind::Stop) {
			if (!fired) FFT_CHECK(tr.t > 0.4f);   // enters late: the sim only needs the last ~0.3 m of 1.6
			fired = true;
			monotonic = monotonic && tr.t >= prev_t;
			prev_t = tr.t;
			if (v == 0.0f) ++frames_after_stop;
		}
	}
	FFT_CHECK(fired && monotonic);
	FFT_CHECK(tr.kind == ffg::LocoTransition::Kind::None);   // settled and handed back
	FFT_CHECK(frames_after_stop > 20);                        // the settle played (0.6 s tail)
	// from standing: accelerate like the sim (ACCEL 34) - the start fires and follows the travelled distance
	tr.Reset();
	for (int i = 0; i < 30; ++i) tr.Update(dt, 0.0f, 0.0f, true, &start, &stop, &stop);
	v = 0.0f;
	fired = false;
	float travelled = 0.0f;
	for (int i = 0; i < 90; ++i) {
		v = std::min(5.5f, v + 34.0f * dt);
		travelled += v * dt;
		tr.Update(dt, v, v, true, &start, &stop, &stop);
		if (tr.kind == ffg::LocoTransition::Kind::Start) {
			fired = true;
			FFT_CHECK(std::fabs(start.DistAt(tr.t) - travelled) < 0.15f || tr.t >= start.duration * 0.92f);
		}
	}
	FFT_CHECK(fired && tr.kind == ffg::LocoTransition::Kind::None);   // handed over to the run cycle
	// a walk from standing (1.8 m/s) never fires a run start; an action (not allowed) cancels a stop
	tr.Reset();
	for (int i = 0; i < 60; ++i) tr.Update(dt, std::min(1.8f, 34.0f * dt * static_cast<float>(i)), 1.8f, true, &start, &stop, &stop);
	FFT_CHECK(tr.kind == ffg::LocoTransition::Kind::None);
	tr.Reset();
	tr.Update(dt, 5.5f, 5.5f, true, &start, &stop, &stop);
	tr.Update(dt, 4.8f, 4.8f, true, &start, &stop, &stop);
	FFT_CHECK(tr.kind == ffg::LocoTransition::Kind::Stop);
	tr.Update(dt, 4.1f, 4.1f, false, &start, &stop, &stop);
	FFT_CHECK(tr.kind == ffg::LocoTransition::Kind::None && tr.clip == nullptr);
}

static ffg::ClipDef TurnClip(const char* name, float len, float yaw_deg) {
	ffg::ClipDef c;
	c.name = name;
	c.duration = len;
	c.frames = static_cast<int>(len * 60.0f);
	c.available = true;
	const int n = static_cast<int>(len * 30.0f) + 1;
	for (int i = 0; i < n; ++i) {
		const float u = ffg::SmoothStep(0.15f, 0.75f, static_cast<float>(i) / static_cast<float>(n - 1));
		c.root_yaw.push_back(yaw_deg * u);
	}
	return c;
}

FFT_TEST(turn_in_place_holds_then_steps_round) {
	const float dt = 1.0f / 60.0f;
	const ffg::ClipDef l90 = TurnClip("l90", 1.2f, -90.0f), r90 = TurnClip("r90", 1.2f, 90.0f);
	const ffg::ClipDef l180 = TurnClip("l180", 1.5f, -180.0f), r180 = TurnClip("r180", 1.5f, 180.0f);
	FFT_NEAR(l90.YawFrac(0.0f), 0.0, 1e-6);
	FFT_NEAR(l90.YawFrac(1.2f), 1.0, 1e-6);
	ffg::TurnInPlace tp;
	tp.Reset();
	// the target circles to the fighter's left at 1 rad/s: the body holds (offset grows negative), no clip yet
	for (int i = 0; i < 30; ++i) tp.Update(dt, 1.0f * dt, true, true, &l90, &r90, &l180, &r180);
	FFT_NEAR(tp.offset, -0.5, 0.02);
	FFT_CHECK(tp.clip == nullptr);
	// past ~52 deg the left 90 turn starts and the offset unwinds toward the (still moving) facing
	for (int i = 0; i < 30; ++i) tp.Update(dt, 1.0f * dt, true, true, &l90, &r90, &l180, &r180);
	FFT_CHECK(tp.clip == &l90);
	const float at_start = std::fabs(tp.offset);
	for (int i = 0; i < 80; ++i) tp.Update(dt, 0.0f, true, true, &l90, &r90, &l180, &r180);   // target stops
	FFT_CHECK(tp.clip == nullptr);                       // the clip ran out (1.2 s at 1.35x)
	FFT_CHECK(std::fabs(tp.offset) < 0.05f * at_start + 0.02f);
	// a big swing to the right picks the 180
	tp.Reset();
	tp.Update(dt, -2.3f, true, true, &l90, &r90, &l180, &r180);
	FFT_CHECK(tp.clip == &r180 && tp.offset > 2.0f);
	// moving releases the offset quickly
	for (int i = 0; i < 30; ++i) tp.Update(dt, 0.0f, false, false, &l90, &r90, &l180, &r180);
	FFT_CHECK(tp.clip == nullptr && std::fabs(tp.offset) < 0.03f);
	// held but not allowed to start (a stop clip playing): the offset builds, the turn waits for can_start
	tp.Reset();
	for (int i = 0; i < 10; ++i) tp.Update(dt, 0.15f, true, false, &l90, &r90, &l180, &r180);
	FFT_CHECK(tp.clip == nullptr && tp.offset < -1.4f);
	tp.Update(dt, 0.0f, true, true, &l90, &r90, &l180, &r180);
	FFT_CHECK(tp.clip == &l90);
	// without clips the offset never builds into a turn
	tp.Reset();
	for (int i = 0; i < 120; ++i) tp.Update(dt, 1.0f * dt, true, true, nullptr, nullptr, nullptr, nullptr);
	FFT_CHECK(tp.clip == nullptr);
}

FFT_TEST(library_json_overrides) {
	ffg::AnimLibrary lib;
	const char* clips = R"({"schema": "fourfold.clips/1", "fps": 60, "asset_root": "/Game/X/Anims",
		"clips": {"e_strike": {"asset": "A_e_strike_v2", "frames": 30, "duration": 0.5, "loop": false, "contact": 10,
		                       "hands": {"l": "fist", "r": "tiger"}, "foot_plants": {"l": [[0, 30]], "r": [[0, 4], [12, 30]]}},
		          "new_clip": {"frames": 12, "loop": true, "contact": null}},
		"hand_poses": {"fist": "hand_fist_b"}})";
	FFT_CHECK(lib.LoadClipsJson(clips));
	const ffg::ClipDef* s = lib.Find("e_strike");
	FFT_CHECK(s && s->asset == "A_e_strike_v2" && s->frames == 30);
	FFT_NEAR(s->ContactTime(), 10.0 / 60.0, 1e-6);
	FFT_CHECK(s->PlantedAt(1, 6.0f / 60.0f) == 0 && s->PlantedAt(1, 13.0f / 60.0f) == 1 && s->PlantedAt(0, 0.2f) == 1);
	FFT_CHECK(lib.asset_root == "/Game/X/Anims");
	const ffg::ClipDef* n = lib.Find("new_clip");
	FFT_CHECK(n && n->loop && n->contacts.empty());
	FFT_CHECK(lib.hand_poses["fist"] == "hand_fist_b");
	const char* amap = R"({"schema": "fourfold.anim_map/1",
		"locomotion": {"idle": "idle2", "stance": ["s0", "s1", "s2", "s3"]},
		"reactions": {"light": "hit_x"},
		"guard": {"default": "gd", "by_element": ["g0", "g1", "g2", "g3"]},
		"moves": {"fire_attack": {"startup": "f_jab", "hold": "f_charge", "release": "f_palm_burst",
		                          "tiers": {"2": {"release": "f_column"}}, "hands": {"l": "fist", "r": "fist"},
		                          "modes": {"Heat": {"hold": "f_thermal_hold"}}}},
		"fallbacks": {"slot": {"thrust": "zz"}}})";
	FFT_CHECK(lib.LoadAnimMapJson(amap));
	FFT_CHECK(lib.idle == "idle2" && lib.stance[2] == "s2" && lib.Reaction("light") == "hit_x");
	MarkAllAvailable(lib);
	ffg::MoveClips m = lib.ResolveMove("fire_attack", "", 2, ff::Slot::Strike, 1, "", false);
	FFT_CHECK(m.release == "f_palm_burst");
	m = lib.ResolveMove("fire_attack", "", 2, ff::Slot::Strike, 3, "heat", false);
	FFT_CHECK(m.release == "f_column" && m.hold == "f_thermal_hold");
	m = lib.ResolveMove("guard", "guard", 2, ff::Slot::Guard, 0, "", false);
	FFT_CHECK(m.hold == "g2");
	FFT_CHECK(!lib.LoadClipsJson("{broken"));
	FFT_CHECK(!lib.warnings.empty());
}

// Builds an actor view for director tests.
static ff::ActorView Actor(int element = 0) {
	ff::ActorView a;
	a.id = 1;
	a.element = element;
	a.grounded = true;
	return a;
}

static ff::ActionView Action(const char* id, ff::Slot slot, ff::ActionPhase ph, float t, float total, float su, float act_t, float rec) {
	ff::ActionView v;
	v.active = true;
	v.id = id;
	v.slot = slot;
	v.phase = ph;
	v.t = t;
	v.total = total;
	v.startup = su;
	v.active_time = act_t;
	v.recovery = rec;
	v.data = ff::Value(ff::Dict());
	return v;
}

struct DirRig {
	ffg::AnimLibrary lib;
	ffg::AnimDirector d;
	ffg::DirectorInput in;
	ff::ActorView cur, prev;
	DirRig() {
		MarkAllAvailable(lib);
		d.lib = &lib;
		d.Reset();
		in.dt = 1.0f / 60.0f;
		in.alpha = 1.0f;
	}
	const ffg::AnimRecipe& Step(const ff::ActorView& a) {
		prev = cur;
		cur = a;
		in.cur = &cur;
		in.prev = &prev;
		const ffg::AnimRecipe& r = d.Update(in);
		in.events.clear();
		return r;
	}
	static const ffg::ClipSample* Dominant(const ffg::AnimRecipe& r) {
		const ffg::ClipSample* best = nullptr;
		for (const ffg::ClipSample& s : r.base)
			if (!best || s.weight > best->weight) best = &s;
		return best;
	}
};

FFT_TEST(director_idle_is_element_stance) {
	DirRig r;
	ff::ActorView a = Actor(2);
	const ffg::AnimRecipe& rc = r.Step(a);
	const ffg::ClipSample* dom = DirRig::Dominant(rc);
	FFT_CHECK(dom && dom->clip->name == "f_stance");
	FFT_CHECK(r.d.SourceKey() == "loco");
	a.is_dummy = true;
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "idle");
}

FFT_TEST(director_attack_contact_alignment_and_release) {
	DirRig r;
	ff::ActorView a = Actor(0);
	r.Step(a);
	const uint32_t s0 = r.d.Recipe().transition_serial;
	// earth_attack: startup e_lift (contact 12 f = 0.2 s) over a 0.25 s startup
	a.action = Action("earth_attack", ff::Slot::Strike, ff::ActionPhase::Startup, 0.125f, 0.125f, 0.25f, 0.1f, 0.3f);
	const ffg::AnimRecipe& rc = r.Step(a);
	FFT_CHECK(rc.transition_serial == s0 + 1);   // a cross-fade into the action
	FFT_NEAR(rc.transition_time, 4.0 / 60.0, 1e-6);
	const ffg::ClipSample* dom = DirRig::Dominant(rc);
	FFT_CHECK(dom->clip->name == "e_lift");
	FFT_NEAR(dom->time, 0.1, 1e-5);   // half way to contact
	a.action.t = a.action.total = 0.25f;
	FFT_NEAR(DirRig::Dominant(r.Step(a))->time, 0.2, 1e-5);   // contact on the last startup frame
	// hands: tiger / fist with weight ramping to 1
	for (int i = 0; i < 6; ++i) r.Step(a);
	FFT_CHECK(r.d.Recipe().hand[0] && r.d.Recipe().hand[0]->name == "hand_tiger");
	FFT_NEAR(r.d.Recipe().hand_weight[1], 1.0, 1e-5);
	// ACTIVE: release clip e_strike from just before its contact
	a.action.phase = ff::ActionPhase::Active;
	a.action.t = 0.0f;
	a.action.total = 0.25f;
	const ffg::AnimRecipe& ra = r.Step(a);
	FFT_CHECK(DirRig::Dominant(ra)->clip->name == "e_strike");
	FFT_NEAR(DirRig::Dominant(ra)->time, 7.0 / 60.0, 1e-5);
	FFT_NEAR(ra.transition_time, 3.0 / 60.0, 1e-6);
	// RECOVERY: ends the clip and fades to locomotion
	a.action.phase = ff::ActionPhase::Recovery;
	a.action.t = 0.3f;
	const ffg::AnimRecipe& rr = r.Step(a);
	float act_w = 0.0f;
	for (const ffg::ClipSample& s : rr.base)
		if (s.clip->name == "e_strike") act_w = s.weight;
	FFT_NEAR(act_w, 0.0, 1e-5);
	// action over: locomotion
	a.action = ff::ActionView();
	r.Step(a);
	FFT_CHECK(r.d.SourceKey() == "loco");
}

FFT_TEST(director_charge_hold_and_tier_release) {
	DirRig r;
	ff::ActorView a = Actor(2);
	a.action = Action("fire_attack", ff::Slot::Strike, ff::ActionPhase::Startup, 0.1f, 0.1f, 0.18f, 0.1f, 0.25f);
	r.Step(a);
	FFT_CHECK(DirRig::Dominant(r.d.Recipe())->clip->name == "f_jab");
	a.action.phase = ff::ActionPhase::Charge;
	a.action.t = 1.3f;
	a.action.tier = 2;
	const ffg::AnimRecipe& rc = r.Step(a);
	const ffg::ClipSample* dom = DirRig::Dominant(rc);
	FFT_CHECK(dom->clip->name == "f_charge");
	FFT_NEAR(dom->time, std::fmod(1.3, 48.0 / 60.0), 1e-4);
	FFT_NEAR(rc.transition_time, 6.0 / 60.0, 1e-6);
	a.action.phase = ff::ActionPhase::Active;
	a.action.t = 0.0f;
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "f_column");
}

FFT_TEST(director_reactions) {
	DirRig r;
	ff::ActorView a = Actor(1);
	a.facing = 0.0f;   // forward = +Z
	a.stun = 0.4f;
	a.stun_kind = "light";
	a.last_hit_dir = Vec3(0, 0, -1);   // from the front
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "hit_light_front");
	ff::ActorView b = Actor(1);
	r.Step(b);
	b.stun = 0.4f;
	b.stun_kind = "light";
	b.last_hit_dir = Vec3(0, 0, 1);   // travelling forward: hit from behind
	FFT_CHECK(DirRig::Dominant(r.Step(b))->clip->name == "hit_light_back");
	b.stun_kind = "knockdown";
	b.stun = 2.5f;
	FFT_CHECK(DirRig::Dominant(r.Step(b))->clip->name == "knockdown");
	b.stun_kind = "getup";
	b.stun = 0.4f;   // getup clip 48 f = 0.8 s fitted into 0.4 s -> rate clamped 1.6
	r.Step(b);
	const ffg::AnimRecipe& rg = r.Step(b);
	FFT_CHECK(DirRig::Dominant(rg)->clip->name == "getup");
	FFT_NEAR(DirRig::Dominant(rg)->time, 1.6 / 60.0, 1e-4);
	FFT_CHECK(rg.ik_weight < 1.0f);   // no foot IK while lying / getting up
}

FFT_TEST(director_guard_spec_and_perfect_overlay) {
	DirRig r;
	ff::ActorView a = Actor(1);
	a.guarding = true;
	a.action = Action("guard", ff::Slot::Guard, ff::ActionPhase::Active, 0.05f, 0.05f, 0.0f, 9.0f, 0.1f);
	ff::Dict d;
	d.set("spec", "ice_wall");
	a.action.data = ff::Value(d);
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "w_freeze");
	a.action.total = 2.0f;
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "w_shield");
	ffg::ReactionEvent ev;
	ev.kind = ffg::ReactionEvent::Perfect;
	r.in.events.push_back(ev);
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "deflect");
	for (int i = 0; i < 30; ++i) r.Step(a);   // 22 frames later the guard loop is back
	FFT_CHECK(DirRig::Dominant(r.d.Recipe())->clip->name == "w_shield");
	// block event: additive block impact
	ffg::ReactionEvent bl;
	bl.kind = ffg::ReactionEvent::Block;
	r.in.events.push_back(bl);
	const ffg::AnimRecipe& rb = r.Step(a);
	FFT_CHECK(rb.additive.clip && rb.additive.clip->name == "block_impact" && rb.additive_weight > 0.0f);
	// walking while guarding: the legs follow the gait
	r.in.local_vel = Vec2(0, 1.2f);
	for (int i = 0; i < 60; ++i) r.Step(a);
	FFT_CHECK(r.d.Recipe().legs_weight > 0.5f && !r.d.Recipe().legs.empty());
}

FFT_TEST(director_evade_direction_and_air) {
	DirRig r;
	ff::ActorView a = Actor(0);
	a.facing = 0.0f;
	a.action = Action("evade", ff::Slot::Evade, ff::ActionPhase::Startup, 0.02f, 0.02f, 0.1f, 0.2f, 0.2f);
	ff::Dict d;
	d.set("dir", Vec3(1, 0, 0));    // sim +X = the character's left at facing 0
	d.set("face", Vec3(0, 0, 1));
	a.action.data = ff::Value(d);
	FFT_CHECK(DirRig::Dominant(r.Step(a))->clip->name == "evade_l");
	FFT_CHECK(r.d.Recipe().ik_weight < 1.0f);
	ff::ActorView j = Actor(0);
	r.Step(j);
	j.grounded = false;
	j.vel = Vec3(0, 5, 0);
	FFT_CHECK(DirRig::Dominant(r.Step(j))->clip->name == "jump");
	for (int i = 0; i < 30; ++i) r.Step(j);
	FFT_CHECK(DirRig::Dominant(r.d.Recipe())->clip->name == "fall");
	j.gliding = true;
	FFT_CHECK(DirRig::Dominant(r.Step(j))->clip->name == "glide");
}

FFT_TEST(director_missing_assets_never_crash) {
	ffg::AnimLibrary lib;   // nothing available
	ffg::AnimDirector d;
	d.lib = &lib;
	d.Reset();
	ffg::DirectorInput in;
	ff::ActorView a = Actor(0);
	a.action = Action("earth_attack", ff::Slot::Strike, ff::ActionPhase::Startup, 0.1f, 0.1f, 0.25f, 0.1f, 0.3f);
	in.cur = &a;
	in.dt = 1.0f / 60.0f;
	const ffg::AnimRecipe& rc = d.Update(in);
	FFT_CHECK(rc.base.empty());   // reference pose
	a.stun = 0.5f;
	a.stun_kind = "heavy";
	FFT_CHECK(d.Update(in).base.empty());
	// only the stance exists: everything degrades to it
	ffg::AnimLibrary lib2;
	lib2.clips["idle"].available = true;
	lib2.clips["idle"].duration = 2.0f;
	d.lib = &lib2;
	d.Reset();
	ff::ActorView b = Actor(0);
	in.cur = &b;
	const ffg::AnimRecipe& r2 = d.Update(in);
	FFT_CHECK(!r2.base.empty() && r2.base[0].clip->name == "idle");
}

FFT_TEST(director_smooth_visual_time_interpolation) {
	DirRig r;
	ff::ActorView a = Actor(3);
	a.action = Action("air_attack", ff::Slot::Strike, ff::ActionPhase::Startup, 2.0f / 60.0f, 2.0f / 60.0f, 0.2f, 0.1f, 0.2f);
	r.in.alpha = 0.5f;   // rendering half way between the two last ticks
	const ffg::ClipSample* s = DirRig::Dominant(r.Step(a));
	FFT_CHECK(s->clip->name == "a_palm");
	// a_palm contact 8 f; rate (8/60) / 0.2 = 0.667; visual time = 1.5 ticks
	FFT_NEAR(s->time, 1.5 / 60.0 * (8.0 / 60.0 / 0.2), 1e-5);
}

// =================================================================================== springs / IK / inertial

FFT_TEST(springs_hit_reactor_rings_out) {
	ffg::HitReactor h;
	h.Hit(Vec3(0, -1, 0), 1.0f);   // blow travelling backward (model fwd = +Y): torso tips back
	h.Step(0.1f);
	const ffg::Quat q = ffg::Quat::FromRotVec(h.torso);
	FFT_CHECK(q.Rotate(Vec3(0, 0, 1)).y < -0.05f);
	float maxa = 0.0f;
	for (int i = 0; i < 120; ++i) {
		h.Step(1.0f / 60.0f);
		maxa = std::max(maxa, h.torso.length());
	}
	FFT_CHECK(maxa <= ffg::HitReactor::kMaxAngle + 1e-5f);
	FFT_CHECK(h.torso.length() < 0.01f && h.head.length() < 0.02f);
	ffg::LandingSpring l;
	l.Kick(3.0f);
	l.Step(0.05f);
	FFT_CHECK(l.y < -0.05f);
	for (int i = 0; i < 120; ++i) l.Step(1.0f / 60.0f);
	FFT_NEAR(l.y, 0.0, 2e-3);
}

FFT_TEST(springs_chain_lags_and_settles) {
	ffg::SpringChain c;
	c.params.gravity = 0.0f;
	std::vector<Vec3> anim = {Vec3(0, 0, 1.0f), Vec3(0, 0, 0.8f), Vec3(0, 0, 0.6f)};   // hanging down
	std::vector<Vec3> dirs;
	c.Step(1.0f / 60.0f, anim, {}, dirs);
	FFT_CHECK(dirs.size() == 2);
	FFT_NEAR(dirs[0].z, -1.0, 1e-4);
	// the root jumps sideways: the tails lag, then settle back onto the animated direction
	for (Vec3& p : anim) p.x += 0.3f;
	c.Step(1.0f / 60.0f, anim, {}, dirs);
	FFT_CHECK(dirs[0].x < -0.2f);
	for (int i = 0; i < 180; ++i) c.Step(1.0f / 60.0f, anim, {}, dirs);
	FFT_NEAR(dirs[0].z, -1.0, 2e-2);
	FFT_NEAR(dirs[1].z, -1.0, 2e-2);
	// collision: a sash hanging beside the thigh; the hips swing toward it and the tails slide around the capsule
	ffg::SpringChain k;
	k.params.gravity = 0.9f;
	k.params.radius = 0.015f;
	std::vector<Vec3> sash = {Vec3(0.12f, -0.08f, 1.0f), Vec3(0.14f, -0.085f, 0.86f), Vec3(0.15f, -0.085f, 0.72f)};
	ffg::CapsuleCollider cap;
	cap.a = Vec3(0.095f, 0, 0.92f);
	cap.b = Vec3(0.1f, -0.01f, 0.5f);
	cap.radius = 0.065f;
	float worst = 0.0f;
	for (int i = 0; i < 240; ++i) {
		const float sw = 0.08f * std::sin(static_cast<float>(i) * 0.15f);
		std::vector<Vec3> moved = sash;
		for (Vec3& p : moved) p.y += sw;   // the root swings forward / back
		k.Step(1.0f / 60.0f, moved, {cap}, dirs);
		for (const Vec3& t : k.Tails()) {
			const float pen = cap.radius + k.params.radius - (t - ffg::ClosestOnSegment(t, cap.a, cap.b)).length();
			worst = std::max(worst, pen);
			FFT_CHECK(std::isfinite(t.x) && std::isfinite(t.z));
		}
	}
	FFT_CHECK(worst < 0.01f);   // at most 1 cm of residual penetration
	// degenerate input never produces NaN
	ffg::SpringChain z;
	std::vector<Vec3> same = {Vec3(), Vec3(), Vec3()};
	z.Step(1.0f / 60.0f, same, {}, dirs);
	for (const Vec3& v : dirs) FFT_CHECK(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
}

static Vec3 FK(Vec3 a, Vec3 b, Vec3 c, const ffg::Quat& qa0, const ffg::Quat& qb0, const ffg::TwoBoneResult& r, Vec3& knee) {
	// new chain from the rotation deltas: knee = a + dA (b - a); ankle = knee + dB (c - b)
	const ffg::Quat dA = r.thigh * qa0.Inverse();
	const ffg::Quat dB = r.calf * qb0.Inverse();
	knee = a + dA.Rotate(b - a);
	return knee + dB.Rotate(c - b);
}

FFT_TEST(ik_two_bone) {
	const Vec3 a(0.1f, 0, 0.92f), b(0.1f, 0.03f, 0.5f), c(0.1f, 0, 0.085f);   // knee slightly forward (+Y)
	const ffg::Quat qa = ffg::Quat::AxisAngle(Vec3(1, 0, 0), 0.1f), qb = ffg::Quat::AxisAngle(Vec3(1, 0, 0), -0.2f);
	const ffg::Quat qc = ffg::Quat::Identity();
	const Vec3 hinge(1, 0, 0);
	// target = animated ankle -> unchanged
	ffg::TwoBoneResult r = ffg::SolveTwoBone(a, b, c, qa, qb, qc, c, qc, 1.0f, hinge);
	FFT_NEAR(std::fabs(r.thigh.Dot(qa)), 1.0, 1e-5);
	FFT_NEAR(std::fabs(r.calf.Dot(qb)), 1.0, 1e-5);
	// a reachable lower target is reached, the knee bends in the authored plane (x stays)
	const Vec3 t(0.1f, 0.05f, 0.2f);
	r = ffg::SolveTwoBone(a, b, c, qa, qb, qc, t, qc, 1.0f, hinge);
	Vec3 knee;
	const Vec3 ank = FK(a, b, c, qa, qb, r, knee);
	FFT_NEAR((ank - t).length(), 0.0, 1e-3);
	FFT_NEAR(knee.x, 0.1, 1e-3);
	FFT_CHECK(knee.y > 0.0f);   // the knee still points forward
	FFT_NEAR(r.reach_error, 0.0, 1e-6);
	// out of reach: straight leg, reports the error
	r = ffg::SolveTwoBone(a, b, c, qa, qb, qc, Vec3(0.1f, 0.0f, -0.5f), qc, 1.0f, hinge);
	FFT_CHECK(r.reach_error > 0.5f);
	const Vec3 ank2 = FK(a, b, c, qa, qb, r, knee);
	FFT_CHECK(std::isfinite(ank2.z));
	// half weight moves half way (no pop when fading)
	r = ffg::SolveTwoBone(a, b, c, qa, qb, qc, t, qc, 0.5f, hinge);
	const Vec3 ank3 = FK(a, b, c, qa, qb, r, knee);
	FFT_NEAR((ank3 - c.lerp(t, 0.5f)).length(), 0.0, 1e-3);
}

class FlatGround : public ffg::GroundSampler {
public:
	float h = 0.0f;
	float step_x = 1e9f;   // ground rises to step_h for world x > step_x
	float step_h = 0.0f;
	float GroundUp(Vec3 p, float from_up) const override {
		const float g = p.x > step_x ? step_h : h;
		return g <= from_up + 0.4f ? g : h;
	}
};

static ffg::FootFrameInput StandInput(Vec3 origin) {
	ffg::FootFrameInput in;
	in.dt = 1.0f / 60.0f;
	in.ik_w = 1.0f;
	in.lock_target_w = 1.0f;
	in.world_from_model.t = origin;
	for (int i = 0; i < 2; ++i) {
		const float x = i == 0 ? 0.105f : -0.105f;
		in.feet[static_cast<size_t>(i)].ankle = Vec3(x, 0.03f, 0.085f);
		in.feet[static_cast<size_t>(i)].ball = Vec3(x, 0.15f, 0.025f);
		in.thigh_heads[static_cast<size_t>(i)] = Vec3(x * 0.9f, 0.0f, 0.86f);   // soft knees: hip 0.78 m above the ankle
		in.leg_len[static_cast<size_t>(i)] = 0.835f;
	}
	return in;
}

FFT_TEST(ik_foot_planter_ground_and_locks) {
	ffg::FootPlanter fp;
	FlatGround g;
	ffg::FootFrameInput in = StandInput(Vec3(5, 5, 0));
	ffg::FootFrameOutput o;
	for (int i = 0; i < 30; ++i) o = fp.Update(in, &g);
	FFT_NEAR((o.goal[0] - in.feet[0].ankle).length(), 0.0, 1e-4);   // flat floor: the clip's own footprints
	FFT_NEAR(o.pelvis_shift, 0.0, 1e-4);
	FFT_CHECK(fp.locked[0] && fp.locked[1]);
	// the body drifts 5 cm: locked feet stay where they are in the world
	in.world_from_model.t = Vec3(5.05f, 5, 0);
	o = fp.Update(in, &g);
	FFT_NEAR(o.goal[0].x, in.feet[0].ankle.x - 0.05f, 1e-3);
	// a long slide (knockback in a stance, 0.6 m/s): the lock lets go past 20 cm and eases back, no re-lock while
	// the foot is still dragged
	float worst = 0.0f;
	for (int i = 0; i < 60; ++i) {
		in.world_from_model.t = Vec3(5.05f + 0.01f * static_cast<float>(i + 1), 5, 0);
		o = fp.Update(in, &g);
		worst = std::max(worst, (o.goal[0] - in.feet[0].ankle).length());
	}
	FFT_CHECK(worst <= 0.21f);
	FFT_NEAR((o.goal[0] - in.feet[0].ankle).length(), 0.0, 2e-2);
	// one foot over a 20 cm step (left foot at +x): it rises, the pelvis comes part of the way down
	ffg::FootPlanter fp2;
	FlatGround st;
	st.step_x = 0.05f;
	st.step_h = 0.2f;
	ffg::FootFrameInput in2 = StandInput(Vec3(0, 0, 0.2f));   // the sim stands the body on the higher ground
	for (int i = 0; i < 90; ++i) o = fp2.Update(in2, &st);
	FFT_NEAR(o.goal[0].z, in2.feet[0].ankle.z, 1e-2);            // on the step (the origin's height)
	FFT_NEAR(o.goal[1].z, in2.feet[1].ankle.z - 0.2f, 1e-2);     // reaching down to the floor
	FFT_CHECK(o.pelvis_shift < -0.05f);
}

FFT_TEST(ik_look_at_turns_toward_target_and_clamps) {
	ffg::LookAt l;
	const Vec3 head(0, 0, 1.6f), fwd(0, 1, 0);
	const Vec3 target(2.0f, 2.0f, 1.6f);   // 45 deg to the character's left (+X)
	for (int i = 0; i < 200; ++i) l.Update(1.0f / 60.0f, head, fwd, target, 1.0f);
	const Vec3 f2 = l.Rotation(1.0f, 1.0f, 1.0f).Rotate(fwd);
	FFT_NEAR(f2.x, std::sqrt(0.5), 1e-2);
	FFT_NEAR(f2.y, std::sqrt(0.5), 1e-2);
	ffg::LookAt b;
	for (int i = 0; i < 200; ++i) b.Update(1.0f / 60.0f, head, fwd, Vec3(0, -3, 1.6f), 1.0f);   // behind
	FFT_CHECK(std::fabs(b.yaw) <= ffg::LookAt::kYawMax + 1e-4f);
	ffg::LookAt up;
	for (int i = 0; i < 200; ++i) up.Update(1.0f / 60.0f, head, fwd, Vec3(0, 1, 5), 1.0f);
	FFT_NEAR(up.pitch, ffg::LookAt::kPitchMax, 1e-3);
	FFT_CHECK(up.Rotation(0.0f, 1.0f, 0.0f).Rotate(fwd).z > 0.3f);   // positive pitch raises the face
}

FFT_TEST(inertial_blend_starts_at_last_pose_and_ends_at_target) {
	std::vector<ffg::Xform> last(2), target(2), pose;
	last[0].q = ffg::Quat::AxisAngle(Vec3(0, 0, 1), 0.8f);
	last[1].t = Vec3(1, 0, 0);
	ffg::InertialBlend b;
	b.Start(last, target, 0.1f);
	pose = target;
	b.Apply(pose, 0.0f);
	FFT_NEAR(std::fabs(pose[0].q.Dot(last[0].q)), 1.0, 1e-5);
	FFT_NEAR(pose[1].t.x, 1.0, 1e-5);
	float prev = 1.0f;
	for (int i = 0; i < 6; ++i) {
		pose = target;
		b.Apply(pose, 1.0f / 60.0f);
		FFT_CHECK(pose[1].t.x <= prev + 1e-6f);
		prev = pose[1].t.x;
	}
	FFT_CHECK(!b.Active());
	FFT_NEAR(ffg::InertialBlend::Decay(0.0f), 1.0, 1e-6);
	FFT_NEAR(ffg::InertialBlend::Decay(1.0f), 0.0, 1e-6);
}

// =================================================================================== ui scale / hud bridge

FFT_TEST(ui_scale_and_insets) {
	ffg::UiScaleInput in;
	in.dpi = 460.0f;
	in.dpi_is_real = true;
	in.mobile = true;
	in.local_per_pixel = 0.5f;   // Slate at 2x
	in.viewport_h_local = 585.0f;
	FFT_NEAR(ffg::PxPerMm(in), 460.0 / 25.4 * 0.5, 1e-3);
	in.mobile = false;
	in.dpi = 96.0f;
	in.dpi_is_real = false;
	in.local_per_pixel = 1.0f;
	in.viewport_h_local = 1080.0f;
	FFT_NEAR(ffg::PxPerMm(in), 0.0108 * 1080.0, 1e-3);   // desktop floor
	in.dpi = 100000.0f;
	FFT_NEAR(ffg::PxPerMm(in), 0.0225 * 1080.0, 1e-2);   // absurd reports clamped
	ffg::Insets i;
	i.left = 400.0f;
	const ffg::Insets s = ffg::SanitizeInsets(i, Vec2(1000, 500));
	FFT_NEAR(s.left, 0.0, 1e-6);
	ff::HudModel h;
	h.valid = true;
	h.element = 3;
	h.sub = 2;
	h.charge.active = true;
	h.charge.button = "guard";
	h.charge.max_tier = 3;
	h.charge.tier = 1;
	const ffg::TouchContext c = ffg::TouchContextFromHud(h);
	FFT_CHECK(c.element == 3 && c.sub == 2 && c.charge_slot == "guard" && c.sub_names[2] == "Vacuum" && c.has_charge);
}

// =================================================================================== real data files (when present)

static bool ReadFile(const std::string& path, std::string& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::stringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

FFT_TEST(data_files_parse_when_present) {
	const std::string root = FFG_UNREAL_DIR;
	std::string text;
	ffg::AnimLibrary lib;
	if (ReadFile(root + "/Content/Fourfold/Data/clips.json", text)) {
		FFT_CHECK(lib.LoadClipsJson(text));
		std::printf("    clips.json: %zu clips\n", lib.clips.size());
	} else {
		std::printf("    (clips.json not present yet: built-in catalogue only)\n");
	}
	if (ReadFile(root + "/Content/Fourfold/Data/anim_map.json", text)) {
		FFT_CHECK(lib.LoadAnimMapJson(text));
		std::printf("    anim_map.json: %zu moves\n", lib.moves.size());
	} else {
		std::printf("    (anim_map.json not present yet: built-in move map only)\n");
	}
	for (const std::string& w : lib.warnings) std::printf("    warning: %s\n", w.c_str());
	// every move of the core's move index resolves to something
	if (ReadFile(root + "/Source/FourfoldCore/Data/move_index.json", text)) {
		ff::Value v;
		FFT_CHECK(ff::ParseJson(text, v));
		MarkAllAvailable(lib);
		int moves = 0, unresolved = 0;
		std::function<void(const ff::Value&)> walk = [&](const ff::Value& x) {
			if (x.is_dict()) {
				const ff::Value& id = x["id"];
				const ff::Value& slot = x["slot"];
				if (id.is_string() && slot.is_string()) {
					++moves;
					const ffg::MoveClips m = lib.ResolveMove(id.as_string(), id.as_string(), static_cast<int>(x["element"].as_int(0)),
					                                         ff::SlotFromName(slot.as_string()), 0, "", true);
					if (m.Empty()) ++unresolved;
				}
				for (const auto& it : x.as_dict()) walk(it.second);
			} else if (x.is_array()) {
				for (const ff::Value& e : x.as_array()) walk(e);
			}
		};
		walk(v);
		std::printf("    move_index.json: %d moves, %d without clips\n", moves, unresolved);
		FFT_CHECK(unresolved == 0);
	}
}

// Actions the sim switches to mid-move (start_action / morph_action with a literal id in FourfoldCore: the lightning
// release, the lava pour, the wind grip ...) are not slot moves of move_index.json, so the test above misses them.
// Each must resolve to clips (with the JSON data and with the built-in defaults alone) and never to another element's.
static int ClipElement(const std::string& clip) {
	if (clip.size() < 2 || clip[1] != '_') return -1;   // shared clip (idle, guard, evade_fwd, hover ...)
	switch (clip[0]) {
		case 'e': return 0;
		case 'w': return 1;
		case 'f': case 'l': case 'c': return 2;
		case 'a': return 3;
		default: return -1;
	}
}

static void ScanChainedIds(const std::string& dir, std::vector<std::string>& ids, int& files) {
	std::error_code ec;
	std::filesystem::recursive_directory_iterator it(dir, ec), end;
	for (; !ec && it != end; it.increment(ec)) {
		const std::string path = it->path().string();
		if (path.size() < 4 || path.compare(path.size() - 4, 4, ".cpp") != 0) continue;
		std::string text;
		if (!ReadFile(path, text)) continue;
		++files;
		for (const char* fn : {"start_action(", "morph_action("}) {
			for (size_t p = text.find(fn); p != std::string::npos; p = text.find(fn, p + 1)) {
				const size_t comma = text.find(',', p);
				const size_t close = text.find(')', p);
				if (comma == std::string::npos || (close != std::string::npos && close < comma)) continue;
				size_t q = comma + 1;
				while (q < text.size() && text[q] == ' ') ++q;
				if (q >= text.size() || text[q] != '"') continue;   // a variable id: a slot move, covered above
				const size_t qe = text.find('"', q + 1);
				if (qe == std::string::npos) continue;
				const std::string id = text.substr(q + 1, qe - q - 1);
				bool seen = false;
				for (const std::string& x : ids) seen = seen || x == id;
				if (!seen) ids.push_back(id);
			}
		}
	}
}

FFT_TEST(chained_actions_resolve_to_own_element_clips) {
	const std::string root = FFG_UNREAL_DIR;
	std::string defs_text;
	if (!ReadFile(root + "/Source/FourfoldCore/Data/moves.json", defs_text)) {
		std::printf("    (moves.json not present: skipped)\n");
		return;
	}
	ff::Value defs_doc;
	FFT_CHECK(ff::ParseJson(defs_text, defs_doc));
	const ff::Value& defs = defs_doc["defs"];
	std::vector<std::string> ids;
	int files = 0;
	ScanChainedIds(root + "/Source/FourfoldCore/Private", ids, files);
	FFT_CHECK(files > 20);
	// "guard" is a slot move (handled by its own rule); the registered dash is reached through the evade slot binding
	// (a variable id), so it is listed by hand.
	ids.erase(std::remove(ids.begin(), ids.end(), std::string("guard")), ids.end());
	for (const char* extra : {"flare_dash"})
		if (std::find(ids.begin(), ids.end(), std::string(extra)) == ids.end()) ids.push_back(extra);
	for (const char* must : {"lightning", "pour", "gust_grip"})
		FFT_CHECK(std::find(ids.begin(), ids.end(), std::string(must)) != ids.end());

	ffg::AnimLibrary with_json, defaults_only;
	std::string text;
	if (ReadFile(root + "/Content/Fourfold/Data/clips.json", text)) FFT_CHECK(with_json.LoadClipsJson(text));
	if (ReadFile(root + "/Content/Fourfold/Data/anim_map.json", text)) FFT_CHECK(with_json.LoadAnimMapJson(text));
	MarkAllAvailable(with_json);
	MarkAllAvailable(defaults_only);
	int bad = 0;
	for (const std::string& id : ids) {
		const ff::Value& d = defs[id];
		FFT_CHECK(d.is_dict());
		if (!d.is_dict()) continue;
		const int el = static_cast<int>(d["element"].as_int(-1));
		const ff::Slot slot = d["slot"].is_string() ? ff::SlotFromName(d["slot"].as_string()) : ff::Slot::None;
		for (const ffg::AnimLibrary* lib : {&with_json, &defaults_only}) {
			for (int tier = 0; tier <= 3; ++tier) {
				const ffg::MoveClips m = lib->ResolveMove(id, "", el < 0 ? 0 : el, slot, tier, "", false);
				bool ok = !m.Empty();
				for (const std::string* c : {&m.startup, &m.hold, &m.release}) {
					if (c->empty() || *c == "evade_*") continue;
					const int ce = ClipElement(*c);
					if (ce >= 0 && el >= 0 && ce != el) ok = false;
					if (!lib->Find(*c)) ok = false;
				}
				if (!ok) {
					++bad;
					std::printf("    %s (%s) T%d: startup '%s' hold '%s' release '%s'\n", id.c_str(),
					            lib == &with_json ? "json" : "defaults", tier, m.startup.c_str(), m.hold.c_str(), m.release.c_str());
				}
			}
		}
	}
	std::printf("    chained actions (%d source files): %zu ids, %d bad resolutions\n", files, ids.size(), bad);
	FFT_CHECK(bad == 0);
}

// =================================================================================== runner

int main(int argc, char** argv) {
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int ran = 0;
	for (const fft::TestCase& t : fft::Registry()) {
		if (filter && !std::strstr(t.name, filter)) continue;
		fft::g_current = t.name;
		const int before = fft::g_failures;
		t.fn();
		++ran;
		std::printf("%s %s\n", fft::g_failures == before ? "ok  " : "FAIL", t.name);
	}
	std::printf("\n%d tests, %d checks, %d failures\n", ran, fft::g_checks, fft::g_failures);
	return fft::g_failures == 0 ? 0 : 1;
}

#endif  // FF_LOGIC_TESTS
