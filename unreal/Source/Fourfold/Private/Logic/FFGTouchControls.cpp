// Fourfold game logic island - touch control state machine (see FFGTouchControls.h).
#include "FFGTouchControls.h"

namespace ffg {

void TouchControls::ApplySettings(const TouchSettings& s) {
	settings = s;
	layout.Configure(layout.viewport_size, layout.insets, layout.ppm, s.control_scale, s.layout_preset, s.left_handed);
}

void TouchControls::Relayout(Vec2 vp_size, const Insets& insets, float ppm) {
	layout.Configure(vp_size, insets, ppm, settings.control_scale, settings.layout_preset, settings.left_handed);
}

void TouchControls::SetContext(const TouchContext& c) {
	const int keep_el = (local_element_ >= 0 && c.element != local_element_ && local_element_t_ < kLocalSelectHoldS) ? local_element_ : -1;
	const int keep_sub = (local_sub_ >= 0 && c.sub != local_sub_ && local_sub_t_ < kLocalSelectHoldS) ? local_sub_ : -1;
	ctx = c;
	ctx.element = Clampi(ctx.element, 0, 3);
	ctx.sub = Clampi(ctx.sub, 0, 3);
	if (keep_el >= 0) ctx.element = keep_el;
	else local_element_ = -1;
	if (keep_sub >= 0) ctx.sub = keep_sub;
	else local_sub_ = -1;
}

float TouchControls::AttackChargeSec(int element) {
	// max(startup, Moves.HOLD_THRESHOLD 0.18 s) of each element's legacy attack, rounded up to whole 60 Hz ticks
	// (docs/CONTROLS.md: Earth 0.25 s, Water / Fire / Air 0.183 s).
	static constexpr float kTimes[4] = {0.25f, 11.0f / 60.0f, 11.0f / 60.0f, 11.0f / 60.0f};
	return kTimes[Clampi(element, 0, 3)];
}

float TouchControls::ChargeTime() const {
	if (charge_hold_sec > 0.0f) return charge_hold_sec;
	if (ctx.attack_decide > 0.0f && ctx.has_charge && ctx.attack_charge >= 0.0f) return ctx.attack_decide;
	return AttackChargeSec(ctx.attack_element >= 0 ? ctx.attack_element : ctx.element);
}

float TouchControls::AttackRingFill() const {
	if (btn_finger_[TI(TouchId::Attack)] == -1) return 0.0f;
	float t = attack_t_;
	if (ctx.has_charge) t = attack_sent_ ? ctx.attack_charge : 0.0f;
	const float ct = std::max(ChargeTime(), 0.01f);
	return t >= ct - kChargeEps ? 1.0f : Clampf(t / ct, 0.0f, 1.0f);
}

void TouchControls::FillFrame(ff::InputFrame& f) {
	f.move = stick_vec_;
	f.cam_delta = cam_accum_;
	cam_accum_ = Vec2();

	f.attack_pressed = l_attack_pressed_;
	if (l_attack_pressed_) attack_sent_ = true;
	f.attack_released = l_attack_released_;
	// A shape tap (ATTACK while a technique is held) is an edge only: it never starts or holds an attack.
	f.attack_held = btn_finger_[TI(TouchId::Attack)] != -1 && !attack_shape_;
	f.guard_pressed = l_guard_pressed_;
	f.guard_released = l_guard_released_;
	f.guard_held = btn_finger_[TI(TouchId::Guard)] != -1;
	f.evade_pressed = l_evade_;
	f.evade_held = btn_finger_[TI(TouchId::Evade)] != -1;
	f.attack_gesture = l_attack_gesture_;
	f.guard_gesture = l_guard_gesture_;
	f.sub_select = l_sub_;

	const bool tech_active = tech_finger_ != -1 && !tech_cancelled_;
	f.tech_pressed = l_tech_pressed_;
	f.tech_released = l_tech_released_;
	f.tech_held = tech_active;
	f.tech_cancel = l_tech_cancel_;
	if (tech_active || l_tech_released_) {
		f.tech_aim = tech_aim_;
		f.tech_aim_active = tech_aim_active_;
	} else {
		f.tech_aim = Vec2();
		f.tech_aim_active = false;
	}
	if (!tech_active) {
		tech_aim_ = Vec2();
		tech_aim_active_ = false;
	}
	f.element_select = l_element_;
	f.target_cycle = l_target_;
	f.pause_pressed = l_pause_;

	l_attack_pressed_ = l_attack_released_ = false;
	l_guard_pressed_ = l_guard_released_ = false;
	l_evade_ = false;
	l_tech_pressed_ = l_tech_released_ = l_tech_cancel_ = false;
	l_element_ = -1;
	l_target_ = l_pause_ = false;
	l_attack_gesture_ = ff::Gesture::None;
	l_guard_gesture_ = ff::Gesture::None;
	l_sub_ = -1;
}

void TouchControls::ReleaseAll(bool cancelled) {
	for (int i = 0; i < kMaxFingers; ++i)
		if (role_[i] != FingerRole::None) ReleaseFinger(i, cancelled, f_pos_[i]);
	CloseRing();
	chip_finger_ = -1;
	attack_shape_ = false;
	stick_finger_ = -1;
	stick_vec_ = Vec2();
	cam_finger_ = -1;
	for (int& b : btn_finger_) b = -1;
	if (tech_finger_ != -1) {
		if (!tech_cancelled_) l_tech_cancel_ = true;
		tech_finger_ = -1;
		tech_cancelled_ = false;
	}
}

int TouchControls::ActiveFingerCount() const {
	int n = 0;
	for (FingerRole r : role_)
		if (r != FingerRole::None) ++n;
	return n;
}

void TouchControls::Update(float dt) {
	const bool reduced = settings.reduced_motion;
	for (int i = 0; i < kTouchCount; ++i) {
		float& p = vis_press_[i];
		if (btn_finger_[i] != -1)
			p = 1.0f;
		else if (p > 0.0f)
			p = reduced ? 0.0f : std::max(0.0f, p - dt * 7.0f);
	}
	for (float& c : chip_flash_) c = std::max(0.0f, c - dt * 4.0f);
	if (stick_finger_ != -1)
		stick_alpha_ = 1.0f;
	else if (stick_alpha_ > 0.0f)
		stick_alpha_ = reduced ? 0.0f : std::max(0.0f, stick_alpha_ - dt * 3.5f);
	const bool tech_live = TechLive();
	const float cancel_target = tech_live ? 1.0f : 0.0f;
	cancel_alpha_ = reduced ? cancel_target : MoveToward(cancel_alpha_, cancel_target, dt * (tech_live ? 6.0f : 5.0f));
	if (tech_live) {
		const float cr = layout.radii[TI(TouchId::Cancel)];
		const float near_r = cr * 1.9f;
		const float dist = tech_pos_.distance_to(layout.centers[TI(TouchId::Cancel)]);
		const float want = Clampf(1.0f - (dist - cr) / std::max(near_r, 1.0f), 0.0f, 1.0f);
		cancel_near_ = Lerpf(cancel_near_, want, std::min(1.0f, dt * 14.0f));
	} else {
		cancel_near_ = 0.0f;
	}
	if (btn_finger_[TI(TouchId::Attack)] != -1)
		attack_t_ += dt;
	else
		attack_t_ = 0.0f;
	if (btn_finger_[TI(TouchId::Guard)] != -1)
		guard_t_ += dt;
	else
		guard_t_ = 0.0f;
	if (chip_finger_ != -1) {
		chip_t_ += dt;
		if (ring_mode_ == RingMode::Closed && chip_t_ >= kRingLongPressS && !chip_moved_ && !TechLive()) OpenRing(RingMode::Slide);
	}
	if (ring_mode_ == RingMode::Tap) {
		ring_t_ += dt;
		if (ring_t_ >= kRingTapTimeoutS || TechLive()) CloseRing();
	}
	local_element_t_ += dt;
	local_sub_t_ += dt;
}

// ---------------------------------------------------------------- touch state machine

void TouchControls::TouchDown(int idx, Vec2 pos) {
	if (idx < 0 || idx >= kMaxFingers) return;
	const int ui = idx;
	if (role_[ui] != FingerRole::None) ReleaseFinger(idx, true, f_pos_[ui]);   // index re-used without an up event
	f_pos_[ui] = pos;
	// Second tap on the cancel zone while a technique is held.
	if (tech_finger_ != -1 && !tech_cancelled_ && layout.InCancelZone(pos)) {
		CloseRing();
		CancelTechnique();
		role_[ui] = FingerRole::Dead;
		return;
	}
	// A tap-opened sub-element ring takes the next touch: a petal selects, anything else closes it.
	if (ring_mode_ == RingMode::Tap) {
		const int ph = layout.RingHit(pos);
		if (ph >= 0) {
			SelectSub(ph);
			CloseRing();
			role_[ui] = FingerRole::Dead;
			return;
		}
		const int chip_hit = layout.HitTest(pos);
		CloseRing();
		if (chip_hit == TI(TouchId::Elem0) + ctx.element) {
			role_[ui] = FingerRole::Dead;   // tapping the active chip again just closes the ring
			return;
		}
	}
	const int hit = layout.HitTest(pos);
	if (hit != kTouchNone) {
		PressButton(idx, hit, pos);
	} else if (layout.IsStickSide(pos)) {
		if (stick_finger_ == -1)
			BeginStick(idx, pos);
		else
			role_[ui] = FingerRole::Dead;
	} else {
		if (cam_finger_ == -1) {
			cam_finger_ = idx;
			role_[ui] = FingerRole::Camera;
		} else {
			role_[ui] = FingerRole::Dead;
		}
	}
}

void TouchControls::TouchMove(int idx, Vec2 pos) {
	if (idx < 0 || idx >= kMaxFingers) return;
	const int ui = idx;
	const Vec2 prev = f_pos_[ui];
	f_pos_[ui] = pos;
	switch (role_[ui]) {
		case FingerRole::Stick: UpdateStick(pos); break;
		case FingerRole::Camera: ApplyCameraDrag(pos - prev); break;
		case FingerRole::Button: {
			const int bid = f_btn_[ui];
			if (bid == TI(TouchId::Tech) && idx == tech_finger_) {
				UpdateTechAim(pos);
			} else if (bid == TI(TouchId::Attack) && idx == btn_finger_[bid] && !attack_shape_) {
				const ff::Gesture g = atk_flick_.Update(pos, attack_t_, layout.ppm);
				if (g != ff::Gesture::None) {
					l_attack_gesture_ = g;
					haptics.push_back("light");
				}
			} else if (bid == TI(TouchId::Guard) && idx == btn_finger_[bid]) {
				const ff::Gesture gg = grd_flick_.Update(pos, guard_t_, layout.ppm, true);
				if (gg != ff::Gesture::None) {
					l_guard_gesture_ = gg;
					haptics.push_back("light");
				}
			} else if (idx == chip_finger_) {
				ChipDrag(pos);
			}
			break;
		}
		default: break;
	}
}

void TouchControls::TouchUp(int idx, Vec2 pos, bool cancelled) {
	if (idx < 0 || idx >= kMaxFingers) return;
	if (role_[idx] == FingerRole::None) return;
	ReleaseFinger(idx, cancelled, pos);
}

void TouchControls::ReleaseFinger(int idx, bool cancelled, Vec2 pos) {
	const int ui = idx;
	const FingerRole role = role_[ui];
	role_[ui] = FingerRole::None;
	switch (role) {
		case FingerRole::Stick:
			stick_finger_ = -1;
			stick_vec_ = Vec2();
			break;
		case FingerRole::Camera: cam_finger_ = -1; break;
		case FingerRole::Button: {
			const int id = f_btn_[ui];
			btn_finger_[id] = -1;
			if (idx == chip_finger_) ChipRelease(cancelled);
			if (id == TI(TouchId::Attack)) {
				if (attack_shape_) {
					attack_shape_ = false;   // a shape tap only ever sends its press edge
				} else {
					l_attack_released_ = true;
					if (!cancelled) {
						const ff::Gesture rg = atk_flick_.Release(pos, layout.ppm);
						if (rg != ff::Gesture::None) l_attack_gesture_ = rg;
					}
				}
				atk_flick_.hot = ff::Gesture::None;
			} else if (id == TI(TouchId::Guard)) {
				l_guard_released_ = true;
				grd_flick_.hot = ff::Gesture::None;
			} else if (id == TI(TouchId::Tech)) {
				if (idx == tech_finger_) {
					if (!tech_cancelled_) {
						if (cancelled)
							l_tech_cancel_ = true;
						else
							l_tech_released_ = true;
					}
					tech_finger_ = -1;
					tech_cancelled_ = false;
				}
			} else if (id == TI(TouchId::Pause)) {
				const float reach = layout.hit_radii[TI(TouchId::Pause)] * kPauseReleaseSlack;
				if (!cancelled && pos.distance_to(layout.centers[TI(TouchId::Pause)]) <= reach) {
					l_pause_ = true;
					++pause_requests;
				}
			}
			break;
		}
		default: break;
	}
}

void TouchControls::PressButton(int idx, int id, Vec2 pos) {
	const int ui = idx;
	if (btn_finger_[id] != -1) {
		role_[ui] = FingerRole::Dead;
		return;
	}
	const bool tech_live = TechLive();
	switch (static_cast<TouchId>(id)) {
		case TouchId::Attack:
			OwnButton(idx, id);
			l_attack_pressed_ = true;
			attack_t_ = 0.0f;
			attack_sent_ = false;
			attack_shape_ = tech_live;   // T+A: ATTACK tapped by a second finger while the technique is held = shape
			atk_flick_.Begin(pos);
			if (tech_live) haptics.push_back("light");
			break;
		case TouchId::Guard:
			OwnButton(idx, id);
			l_guard_pressed_ = true;
			guard_t_ = 0.0f;
			grd_flick_.Begin(pos);
			break;
		case TouchId::Evade:
			OwnButton(idx, id);
			l_evade_ = true;
			break;
		case TouchId::Tech:
			if (tech_finger_ != -1) {
				role_[ui] = FingerRole::Dead;
				return;
			}
			OwnButton(idx, id);
			tech_finger_ = idx;
			tech_origin_ = pos;
			tech_pos_ = pos;
			tech_aim_ = Vec2();
			tech_aim_active_ = false;
			tech_cancelled_ = false;
			l_tech_pressed_ = true;
			break;
		case TouchId::Target:
			OwnButton(idx, id);
			l_target_ = true;
			break;
		case TouchId::Pause: OwnButton(idx, id); break;
		default: {
			const int e = id - TI(TouchId::Elem0);
			if (e < 0 || e >= 4 || tech_live || !IsElementUnlocked(e)) {
				role_[ui] = FingerRole::Dead;
				return;
			}
			OwnButton(idx, id);
			chip_finger_ = idx;
			chip_el_ = e;
			chip_t_ = 0.0f;
			chip_moved_ = false;
			chip_was_active_ = e == ctx.element;
			l_element_ = e;
			ctx.element = e;
			local_element_ = e;
			local_element_t_ = 0.0f;
			chip_flash_[e] = 1.0f;
			haptics.push_back("light");
			break;
		}
	}
}

void TouchControls::OwnButton(int idx, int id) {
	role_[idx] = FingerRole::Button;
	f_btn_[idx] = id;
	btn_finger_[id] = idx;
	vis_press_[id] = 1.0f;
}

// ---------------------------------------------------------------- sub-element ring

void TouchControls::OpenRing(RingMode mode) {
	ring_mode_ = mode;
	ring_t_ = 0.0f;
	ring_hover_ = -1;
	++ui_ring_opened;
}

void TouchControls::CloseRing() {
	if (ring_mode_ == RingMode::Closed) return;
	ring_mode_ = RingMode::Closed;
	ring_hover_ = -1;
}

void TouchControls::SelectSub(int s) {
	if (!IsSubUnlocked(s)) {
		haptics.push_back("light");
		return;
	}
	l_sub_ = s;
	ctx.sub = s;
	local_sub_ = s;
	local_sub_t_ = 0.0f;
	++ui_ring_picked;
	haptics.push_back("light");
}

void TouchControls::ChipDrag(Vec2 pos) {
	if (ring_mode_ == RingMode::Slide) {
		ring_hover_ = layout.RingAim(chip_el_, pos);
		return;
	}
	const int b = TI(TouchId::Elem0) + std::max(chip_el_, 0);
	if (pos.distance_to(layout.centers[b]) > layout.radii[b] * 1.6f) chip_moved_ = true;
	// sliding off the chip toward the petal column opens the ring at once (no long-press): chip + flick picks element
	// and sub-element in one stroke
	const int aim = chip_moved_ && !TechLive() ? layout.RingAim(chip_el_, pos) : -1;
	if (aim >= 0) {
		OpenRing(RingMode::Slide);
		ring_hover_ = aim;
	}
}

void TouchControls::ChipRelease(bool cancelled) {
	const bool was_slide = ring_mode_ == RingMode::Slide;
	const int hover = ring_hover_;
	chip_finger_ = -1;
	if (was_slide) {
		CloseRing();
		if (!cancelled && hover >= 0) SelectSub(hover);
		return;
	}
	// A quick tap on the already-selected chip opens the ring for a second tap.
	if (!cancelled && chip_was_active_ && !chip_moved_ && chip_t_ < kRingLongPressS && !TechLive()) OpenRing(RingMode::Tap);
}

// ---------------------------------------------------------------- stick / camera / technique aim

void TouchControls::BeginStick(int idx, Vec2 pos) {
	stick_finger_ = idx;
	role_[idx] = FingerRole::Stick;
	stick_center_ = pos;
	stick_knob_ = pos;
	stick_vec_ = Vec2();
	stick_alpha_ = 1.0f;
}

void TouchControls::UpdateStick(Vec2 pos) {
	const float r = std::max(layout.stick_radius, 1.0f);
	Vec2 d = pos - stick_center_;
	float len = d.length();
	if (len > r) {
		stick_center_ += d * ((len - r) / len);   // the base follows the thumb so direction changes stay quick
		d = pos - stick_center_;
		len = r;
	}
	stick_knob_ = stick_center_ + d;
	const float mag = len / r;
	if (mag < kDeadzone || len <= 1e-6f) {
		stick_vec_ = Vec2();
	} else {
		const float scaled = std::min((mag - kDeadzone) / (1.0f - kDeadzone), 1.0f);
		stick_vec_ = Vec2(d.x, -d.y) / len * scaled;
	}
}

void TouchControls::ApplyCameraDrag(Vec2 delta_px) {
	const float k = kCamRadPerMm / std::max(layout.ppm, 0.01f) * settings.camera_sensitivity;
	const float inv = settings.invert_y ? -1.0f : 1.0f;
	cam_accum_.x += delta_px.x * k;
	cam_accum_.y += -delta_px.y * k * inv;
}

void TouchControls::UpdateTechAim(Vec2 pos) {
	if (tech_cancelled_) return;
	tech_pos_ = pos;
	const Vec2 d = pos - tech_origin_;
	Vec2 aim = Vec2(d.x, -d.y) / std::max(layout.aim_radius, 1.0f);
	if (aim.length_squared() > 1.0f) aim = aim.normalized();
	tech_aim_ = aim;
	if (!tech_aim_active_ && d.length() > std::max(kAimActivePx, kAimActiveMm * layout.ppm)) tech_aim_active_ = true;
	if (layout.InCancelZone(pos)) CancelTechnique();
}

void TouchControls::CancelTechnique() {
	if (tech_finger_ == -1 || tech_cancelled_) return;
	tech_cancelled_ = true;
	l_tech_cancel_ = true;
	tech_aim_ = Vec2();
	tech_aim_active_ = false;
}

}  // namespace ffg
