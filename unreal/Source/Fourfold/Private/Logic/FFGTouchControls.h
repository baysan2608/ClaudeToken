// Fourfold game logic island - the touch control state machine (port of game/ui/touch_controls.gd, minus drawing).
// Floating movement stick, camera drag, ATTACK / GUARD / EVADE / TECHNIQUE buttons, element chips + sub-element ring,
// pause and target buttons. Finger ownership is decided at touch-down and kept until release / cancel (docs/CONTROLS.md
// "Touch ownership rules"). State is latched here and handed out once per 60 Hz sim tick by FillFrame().
// The Slate overlay (SFourfoldTouchOverlay) forwards OnTouchStarted / Moved / Ended and draws from the public state.
#pragma once

#include "FFGFlick.h"
#include "FFGTouchLayout.h"
#include "ff/Input.h"

#include <array>
#include <string>
#include <vector>

namespace ffg {

struct TouchSettings {
	float control_scale = 1.0f;
	float control_opacity = 0.8f;
	std::string layout_preset = "default";
	bool left_handed = false;
	bool strong_labels = false;
	float camera_sensitivity = 1.0f;
	bool invert_y = false;
	bool reduced_motion = false;
	bool show_debug = false;
};

// HUD hints from the game (built from ff::HudModel every frame).
struct TouchContext {
	int element = 0;
	std::array<bool, 4> unlocked_elements{{true, true, true, true}};
	std::string tech_label;
	bool tech_available = true;
	bool holding = false;
	bool has_charge = false;          // the game reports the sim's attack progress (else the ring self-times)
	float attack_charge = -1.0f;      // s since the attack action started (0 = buffered, -1 = none)
	int attack_element = -1;
	float attack_decide = 0.0f;       // running registry attack's tap / hold decision time (0 = per-element default)
	int sub = 0;
	std::array<bool, 4> subs_unlocked{{true, true, true, true}};
	std::array<std::string, 4> sub_names{{"", "", "", ""}};
	std::string petal_up, petal_down, petal_side;   // ATTACK flick move names
	std::string guard_petal_up, guard_petal_down;   // GUARD push / sink names
	// Context counters: a threat is coming; what push / sink do to it ("" = no answer) and their bands
	// (0 none, 1 fail, 2 partial, 3 full) - the petals show these, lit, without the button held.
	bool threat = false;
	bool guard_now = false;                         // the threat is inside the perfect-guard window: press GUARD now
	std::string counter_up, counter_down;
	int counter_band_up = 0, counter_band_down = 0;
	// the attack slot that also answers it best ("thrust" | "ground" | "sweep" | "strike", "" = none): its ATTACK petal
	std::string counter_attack_slot, counter_attack;
	int counter_band_attack = 0;
	// chain window open: which ATTACK inputs would chain now (flick up / down / side, plain tap)
	bool chain_up = false, chain_down = false, chain_side = false, chain_tap = false;
	std::string shape_label;                        // what T+A does now ("" = no technique running)
	std::string charge_slot;                        // "attack" | "guard" | "tech" | "evade" | "" (charge ring owner)
	int charge_tier = 0;
	float charge_frac = 0.0f;
	int charge_max = 0;
};

enum class FingerRole : int { None = 0, Stick = 1, Camera = 2, Button = 3, Dead = 4 };
enum class RingMode : int { Closed = 0, Tap = 1, Slide = 2 };

class TouchControls {
public:
	static constexpr int kMaxFingers = 16;
	static constexpr int kMouseFinger = 15;
	static constexpr float kRingLongPressS = 0.32f;
	static constexpr float kRingTapTimeoutS = 4.0f;
	static constexpr float kDeadzone = 0.08f;
	static constexpr float kChargeEps = 0.001f;
	static constexpr float kAimActivePx = 12.0f;
	static constexpr float kAimActiveMm = 2.0f;
	static constexpr float kCamRadPerMm = 0.055f;
	static constexpr float kPauseReleaseSlack = 1.8f;
	static constexpr float kLocalSelectHoldS = 0.25f;   // a chip / petal choice wins over a stale context this long

	TouchLayout layout;
	TouchSettings settings;
	TouchContext ctx;
	float charge_hold_sec = 0.0f;   // ring fill override (0 = the rule below)

	// ---- configuration
	void ApplySettings(const TouchSettings& s);
	void Relayout(Vec2 vp_size, const Insets& insets, float ppm);
	void SetContext(const TouchContext& c);

	// ---- events (viewport units, y down)
	void TouchDown(int idx, Vec2 pos);
	void TouchMove(int idx, Vec2 pos);
	void TouchUp(int idx, Vec2 pos, bool cancelled);
	// Release every finger safely: stick -> 0, held buttons send released, a held technique sends tech_cancel.
	void ReleaseAll(bool cancelled = true);
	// Per rendered frame (real seconds): timers, long-press ring, fades.
	void Update(float dt);
	// Once per 60 Hz sim tick: writes every touch-owned field of `f` and clears the latched edges.
	void FillFrame(ff::InputFrame& f);
	// Per rendered frame (before the camera updates): the camera drag accumulated since the last call, then cleared
	// (FillFrame drains it too; see DesktopInput::TakeCamDelta).
	Vec2 TakeCamDelta() {
		const Vec2 d = cam_accum_;
		cam_accum_ = Vec2();
		return d;
	}

	// ---- queries (drawing, tests)
	static float AttackChargeSec(int element);
	float ChargeTime() const;
	float AttackRingFill() const;
	bool TechLive() const { return tech_finger_ != -1 && !tech_cancelled_; }
	bool IsRingOpen() const { return ring_mode_ != RingMode::Closed; }
	RingMode GetRingMode() const { return ring_mode_; }
	int RingHover() const { return ring_hover_; }
	ff::Gesture AttackGestureHot() const { return btn_finger_[TI(TouchId::Attack)] != -1 ? atk_flick_.hot : ff::Gesture::None; }
	ff::Gesture GuardGestureHot() const { return btn_finger_[TI(TouchId::Guard)] != -1 ? grd_flick_.hot : ff::Gesture::None; }
	const FlickRecognizer& AttackFlick() const { return atk_flick_; }
	bool AttackHeld() const { return btn_finger_[TI(TouchId::Attack)] != -1 && !attack_shape_; }
	bool ButtonHeld(TouchId id) const { return btn_finger_[TI(id)] != -1; }
	bool IsAttackShape() const { return attack_shape_; }
	bool IsElementUnlocked(int e) const { return e >= 0 && e < 4 && ctx.unlocked_elements[static_cast<size_t>(e)]; }
	bool IsSubUnlocked(int s) const { return s >= 0 && s < 4 && ctx.subs_unlocked[static_cast<size_t>(s)]; }
	int ActiveFingerCount() const;
	FingerRole Role(int idx) const { return (idx >= 0 && idx < kMaxFingers) ? role_[idx] : FingerRole::None; }
	Vec2 FingerPos(int idx) const { return (idx >= 0 && idx < kMaxFingers) ? f_pos_[idx] : Vec2(); }
	bool IsTechniqueCancelled() const { return tech_cancelled_; }
	int StickFinger() const { return stick_finger_; }
	int CameraFinger() const { return cam_finger_; }
	Vec2 StickCenter() const { return stick_center_; }
	Vec2 StickKnob() const { return stick_knob_; }
	Vec2 StickVec() const { return stick_vec_; }
	float StickAlpha() const { return stick_alpha_; }
	Vec2 TechOrigin() const { return tech_origin_; }
	Vec2 TechPos() const { return tech_pos_; }
	Vec2 TechAim() const { return tech_aim_; }
	bool TechAimActive() const { return tech_aim_active_; }
	float VisPress(int id) const { return (id >= 0 && id < kTouchCount) ? vis_press_[id] : 0.0f; }
	float ChipFlash(int e) const { return (e >= 0 && e < 4) ? chip_flash_[e] : 0.0f; }
	float CancelAlpha() const { return cancel_alpha_; }
	float CancelNear() const { return cancel_near_; }
	int ShownElement() const { return ctx.element; }
	int ShownSub() const { return ctx.sub; }

	// ---- outputs for the host, drained once per frame
	std::vector<std::string> haptics;   // semantic haptic kinds ("light")
	int pause_requests = 0;             // pause button lifted (the host opens the pause menu)
	int ui_ring_opened = 0;             // sub ring opened (UI sound cue)
	int ui_ring_picked = 0;             // sub picked from the ring

private:
	void ReleaseFinger(int idx, bool cancelled, Vec2 pos);
	void PressButton(int idx, int id, Vec2 pos);
	void OwnButton(int idx, int id);
	void OpenRing(RingMode mode);
	void CloseRing();
	void SelectSub(int s);
	void ChipDrag(Vec2 pos);
	void ChipRelease(bool cancelled);
	void BeginStick(int idx, Vec2 pos);
	void UpdateStick(Vec2 pos);
	void ApplyCameraDrag(Vec2 delta_px);
	void UpdateTechAim(Vec2 pos);
	void CancelTechnique();

	FingerRole role_[kMaxFingers] = {};
	int f_btn_[kMaxFingers] = {};
	Vec2 f_pos_[kMaxFingers] = {};
	int btn_finger_[kTouchCount] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

	int stick_finger_ = -1;
	Vec2 stick_center_, stick_knob_, stick_vec_;
	float stick_alpha_ = 0.0f;

	int cam_finger_ = -1;
	Vec2 cam_accum_;

	int tech_finger_ = -1;
	Vec2 tech_origin_, tech_pos_, tech_aim_;
	bool tech_aim_active_ = false;
	bool tech_cancelled_ = false;

	bool l_attack_pressed_ = false, l_attack_released_ = false;
	bool l_guard_pressed_ = false, l_guard_released_ = false;
	bool l_evade_ = false;
	bool l_tech_pressed_ = false, l_tech_released_ = false, l_tech_cancel_ = false;
	int l_element_ = -1;
	bool l_target_ = false, l_pause_ = false;
	ff::Gesture l_attack_gesture_ = ff::Gesture::None;
	ff::Gesture l_guard_gesture_ = ff::Gesture::None;
	int l_sub_ = -1;

	FlickRecognizer atk_flick_, grd_flick_;
	bool attack_shape_ = false;
	bool attack_sent_ = false;
	float attack_t_ = 0.0f;
	float guard_t_ = 0.0f;
	RingMode ring_mode_ = RingMode::Closed;
	float ring_t_ = 0.0f;
	int ring_hover_ = -1;
	int chip_finger_ = -1;
	int chip_el_ = -1;
	float chip_t_ = 0.0f;
	bool chip_was_active_ = false;
	bool chip_moved_ = false;
	// A local choice (chip / petal) shown until the game's context catches up.
	int local_element_ = -1;
	float local_element_t_ = 0.0f;
	int local_sub_ = -1;
	float local_sub_t_ = 0.0f;

	float vis_press_[kTouchCount] = {};
	float chip_flash_[4] = {};
	float cancel_alpha_ = 0.0f;
	float cancel_near_ = 0.0f;
};

}  // namespace ffg
