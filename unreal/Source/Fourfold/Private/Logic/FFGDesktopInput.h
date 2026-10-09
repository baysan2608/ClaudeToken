// Fourfold game logic island - keyboard / mouse / gamepad grammar (port of game/ui/desktop_input.gd).
// The Unreal side samples the bound keys each rendered frame (APlayerController key state) into DeviceSample and calls
// Sample(); FillFrame() then hands one latched ff::InputFrame to each 60 Hz sim tick (edges kept until consumed).
// Chords: K + J = push (guard flick Up), K + N / RB + LT = sink (Down) - never an attack; J / X / LMB while the technique
// is held = shape tap (one press edge, no hold); LB + d-pad = sub-element; the active element's key again cycles subs.
#pragma once

#include "FFGMath.h"
#include "ff/Input.h"

#include <array>

namespace ffg {

enum class DeskAction : int {
	Attack = 0, Guard, Evade, Tech, Cancel, Pause, Elem0, Elem1, Elem2, Elem3, Target, Thrust, Ground, Sweep, SubPrev, SubNext, Count
};
inline constexpr int kDeskCount = static_cast<int>(DeskAction::Count);
inline constexpr int DA(DeskAction a) { return static_cast<int>(a); }

// One rendered frame of device state.
struct DeviceSample {
	std::array<bool, kDeskCount> down{};           // level now (keys OR pad OR mouse buttons)
	std::array<bool, kDeskCount> just_pressed{};   // went down during this frame (catches press + release in one frame)
	Vec2 move;                                     // WASD / left stick, y forward, |v| <= 1
	Vec2 cam_stick;                                // arrows / right stick, x right, y up, |v| <= 1
	Vec2 mouse_delta;                              // pixels this frame (x right, y DOWN)
	bool mouse_look = false;                       // middle-button drag or captured mouse-look (F1)
	float dt = 0.0f;                               // real frame seconds
};

struct DeskSettings {
	float camera_sensitivity = 1.0f;
	bool invert_y = false;
};

class DesktopInput {
public:
	static constexpr float kCamRate = 2.6f;            // rad/s at full stick
	static constexpr float kMouseRadPerPx = 0.0032f;
	static constexpr float kAimActivePx = 12.0f;
	static constexpr float kAimActiveStick = 0.3f;

	DeskSettings settings;
	float aim_radius_px = 108.0f;   // mouse travel that maps to aim length 1 (min(0.15 vh, 140) clamped >= 40)

	void SetUnlocked(const std::array<bool, 4>& elements) { unlocked_ = elements; }
	void SetSubContext(int element, int sub, const std::array<bool, 4>& subs) {
		ctx_element_ = element;
		ctx_sub_ = Clampi(sub, 0, 3);
		ctx_subs_ = subs;
	}
	bool IsTechniqueActive() const { return tech_down_ && !tech_cancelled_; }

	// Per rendered frame: latch edges, accumulate camera / aim.
	void Sample(const DeviceSample& s);
	// Per sim tick.
	void FillFrame(ff::InputFrame& f);
	// Per rendered frame (before the camera updates): the camera turn accumulated since the last call, then cleared.
	// FillFrame also drains it, so a host that only polls per tick still works; the per-frame drain is what keeps the
	// view smooth at 120 Hz and during hit-stop (no sim tick then).
	Vec2 TakeCamDelta() {
		const Vec2 d = cam_accum_;
		cam_accum_ = Vec2();
		return d;
	}
	// Focus loss / pause: everything held is let go; a held technique is cancelled, never committed.
	void CancelAll();
	// After un-pausing: edge-only actions physically down now (the Esc that closed the menu) must not fire again.
	void ResyncEdgeActions(const std::array<bool, kDeskCount>& down_now);

	int pause_requests = 0;   // drained by the host (opens the pause menu)

private:
	void OnPressed(int i);
	void OnReleased(int i);
	void AttackSourcePressed(int i);
	void CycleSub(int step);
	void CancelTechnique();

	std::array<bool, 4> unlocked_{{true, true, true, true}};
	std::array<bool, kDeskCount> prev_{};
	std::array<bool, kDeskCount> p_latch_{};
	std::array<bool, kDeskCount> r_latch_{};
	std::array<bool, kDeskCount> hold_off_{};
	ff::Gesture l_attack_gesture_ = ff::Gesture::None;
	ff::Gesture l_guard_gesture_ = ff::Gesture::None;
	int l_sub_ = -1;
	int ctx_element_ = -1;
	int ctx_sub_ = 0;
	std::array<bool, 4> ctx_subs_{{true, true, true, true}};
	bool tech_down_ = false;
	bool tech_cancelled_ = false;
	bool tech_aim_active_ = false;
	bool tech_cancelled_at_release_ = false;
	Vec2 mouse_aim_;
	bool l_tech_cancel_ = false;
	int l_element_ = -1;
	bool l_pause_ = false;
	Vec2 cam_accum_;
	Vec2 move_;
	Vec2 stick_;
};

}  // namespace ffg
