// Fourfold game logic island - keyboard / mouse / gamepad grammar (see FFGDesktopInput.h).
#include "FFGDesktopInput.h"

namespace ffg {

namespace {
// Scan order of one edge pass: GUARD first, so a chord (K + J) pressed in the same frame is seen as a chord.
constexpr DeskAction kDeskScanOrder[kDeskCount] = {
	DeskAction::Guard, DeskAction::Attack, DeskAction::Evade, DeskAction::Tech, DeskAction::Cancel, DeskAction::Pause,
	DeskAction::Elem0, DeskAction::Elem1, DeskAction::Elem2, DeskAction::Elem3, DeskAction::Target, DeskAction::Thrust,
	DeskAction::Ground, DeskAction::Sweep, DeskAction::SubPrev, DeskAction::SubNext};
}  // namespace

void DesktopInput::Sample(const DeviceSample& s) {
	for (DeskAction a : kDeskScanOrder) {
		const int i = DA(a);
		const size_t u = static_cast<size_t>(i);
		const bool now = s.down[u];
		const bool was = prev_[u];
		if (!was && (now || s.just_pressed[u])) {
			prev_[u] = true;
			OnPressed(i);
			if (!now) {   // pressed and released inside one frame: still a tap
				prev_[u] = false;
				OnReleased(i);
			}
		} else if (was && !now) {
			prev_[u] = false;
			OnReleased(i);
		}
	}
	move_ = s.move.limit_length(1.0f);
	stick_ = s.cam_stick.limit_length(1.0f);
	const bool tech_active = IsTechniqueActive();
	if (tech_down_) {
		if (!tech_cancelled_) {
			mouse_aim_.x += s.mouse_delta.x;
			mouse_aim_.y -= s.mouse_delta.y;
		}
	} else if (s.mouse_look) {
		const float k = kMouseRadPerPx * settings.camera_sensitivity;
		const float inv = settings.invert_y ? -1.0f : 1.0f;
		cam_accum_ += Vec2(s.mouse_delta.x * k, -s.mouse_delta.y * k * inv);
	}
	if (!tech_active) {
		// The right stick turns the camera unless it is aiming a technique.
		const float inv = settings.invert_y ? -1.0f : 1.0f;
		const float k = kCamRate * settings.camera_sensitivity * Clampf(s.dt, 0.0f, 0.1f);
		cam_accum_ += Vec2(stick_.x * k, stick_.y * k * inv);
	}
}

void DesktopInput::FillFrame(ff::InputFrame& f) {
	f.move = move_;
	const bool tech_active = IsTechniqueActive();
	f.attack_pressed = f.attack_released = f.attack_held = false;
	constexpr DeskAction kSources[4] = {DeskAction::Attack, DeskAction::Thrust, DeskAction::Ground, DeskAction::Sweep};
	for (DeskAction a : kSources) {
		const size_t u = static_cast<size_t>(DA(a));
		f.attack_pressed = f.attack_pressed || p_latch_[u];
		f.attack_released = f.attack_released || r_latch_[u];
		f.attack_held = f.attack_held || (prev_[u] && !hold_off_[u]);
	}
	f.attack_gesture = l_attack_gesture_;
	f.guard_gesture = l_guard_gesture_;
	f.sub_select = l_sub_;
	f.guard_pressed = p_latch_[DA(DeskAction::Guard)];
	f.guard_released = r_latch_[DA(DeskAction::Guard)];
	f.guard_held = prev_[DA(DeskAction::Guard)];
	f.evade_pressed = p_latch_[DA(DeskAction::Evade)];
	f.evade_held = prev_[DA(DeskAction::Evade)];
	f.tech_pressed = p_latch_[DA(DeskAction::Tech)];
	f.tech_released = r_latch_[DA(DeskAction::Tech)] && !tech_cancelled_at_release_;
	f.tech_held = tech_active;
	f.tech_cancel = l_tech_cancel_;
	if (tech_active || f.tech_released) {
		Vec2 aim = mouse_aim_ / std::max(aim_radius_px, 1.0f) + stick_;
		if (aim.length_squared() > 1.0f) aim = aim.normalized();
		f.tech_aim = aim;
		if (!tech_aim_active_ && (mouse_aim_.length() > kAimActivePx || stick_.length() > kAimActiveStick)) tech_aim_active_ = true;
		f.tech_aim_active = tech_aim_active_;
	} else {
		f.tech_aim = Vec2();
		f.tech_aim_active = false;
	}
	f.cam_delta = cam_accum_;
	cam_accum_ = Vec2();
	if (!tech_active) {
		tech_aim_active_ = false;
		mouse_aim_ = Vec2();
	}
	f.element_select = l_element_;
	f.target_cycle = p_latch_[DA(DeskAction::Target)];
	f.pause_pressed = l_pause_;

	p_latch_.fill(false);
	r_latch_.fill(false);
	l_attack_gesture_ = ff::Gesture::None;
	l_guard_gesture_ = ff::Gesture::None;
	l_sub_ = -1;
	l_tech_cancel_ = false;
	l_element_ = -1;
	l_pause_ = false;
	tech_cancelled_at_release_ = false;
}

void DesktopInput::CancelAll() {
	if (IsTechniqueActive()) CancelTechnique();
	for (int i = 0; i < kDeskCount; ++i) {
		const size_t u = static_cast<size_t>(i);
		if (prev_[u]) {
			prev_[u] = false;
			OnReleased(i);
		}
	}
	hold_off_.fill(false);
	move_ = Vec2();
	stick_ = Vec2();
}

void DesktopInput::ResyncEdgeActions(const std::array<bool, kDeskCount>& down_now) {
	constexpr DeskAction kEdgeOnly[] = {DeskAction::Evade, DeskAction::Cancel, DeskAction::Pause, DeskAction::Elem0, DeskAction::Elem1,
	                                    DeskAction::Elem2, DeskAction::Elem3, DeskAction::Target, DeskAction::SubPrev, DeskAction::SubNext};
	for (DeskAction a : kEdgeOnly) {
		const size_t u = static_cast<size_t>(DA(a));
		prev_[u] = down_now[u];
		p_latch_[u] = false;
	}
}

void DesktopInput::OnPressed(int i) {
	const size_t u = static_cast<size_t>(i);
	switch (static_cast<DeskAction>(i)) {
		case DeskAction::Attack:
		case DeskAction::Thrust:
		case DeskAction::Ground:
		case DeskAction::Sweep: AttackSourcePressed(i); break;
		case DeskAction::Guard:
		case DeskAction::Evade:
		case DeskAction::Target: p_latch_[u] = true; break;
		case DeskAction::SubPrev: CycleSub(-1); break;
		case DeskAction::SubNext: CycleSub(1); break;
		case DeskAction::Tech:
			p_latch_[u] = true;
			tech_down_ = true;
			tech_cancelled_ = false;
			tech_aim_active_ = false;
			mouse_aim_ = Vec2();
			break;
		case DeskAction::Cancel:
			if (IsTechniqueActive()) CancelTechnique();
			break;
		case DeskAction::Pause:
			if (IsTechniqueActive()) {
				CancelTechnique();
			} else {
				l_pause_ = true;
				++pause_requests;
			}
			break;
		default: {
			const int e = i - DA(DeskAction::Elem0);
			if (e < 0 || e >= 4) return;
			const size_t ue = static_cast<size_t>(e);
			if (prev_[DA(DeskAction::Cancel)]) {
				if (ctx_subs_[ue]) l_sub_ = e;   // gamepad: LB + d-pad picks the sub-element
			} else if (e == ctx_element_ && unlocked_[ue]) {
				CycleSub(1);   // the active element's key again cycles its sub-elements
			} else if (unlocked_[ue]) {
				l_element_ = e;
			}
			break;
		}
	}
}

void DesktopInput::AttackSourcePressed(int i) {
	const size_t u = static_cast<size_t>(i);
	const DeskAction a = static_cast<DeskAction>(i);
	if (prev_[DA(DeskAction::Guard)] && (a == DeskAction::Attack || a == DeskAction::Ground)) {
		// K + J = push, K + N / RB + LT = sink: a guard flick, never an attack.
		hold_off_[u] = true;
		l_guard_gesture_ = a == DeskAction::Attack ? ff::Gesture::Up : ff::Gesture::Down;
		return;
	}
	if (IsTechniqueActive()) {
		// J / X / LMB while the technique is held = shape: one press edge, no attack.
		hold_off_[u] = true;
		p_latch_[u] = true;
		return;
	}
	hold_off_[u] = false;
	p_latch_[u] = true;
	if (a == DeskAction::Thrust) l_attack_gesture_ = ff::Gesture::Up;
	else if (a == DeskAction::Ground) l_attack_gesture_ = ff::Gesture::Down;
	else if (a == DeskAction::Sweep) l_attack_gesture_ = ff::Gesture::Side;
}

void DesktopInput::CycleSub(int step) {
	for (int k = 1; k < 4; ++k) {
		const int cand = Posmod(ctx_sub_ + step * k, 4);
		if (ctx_subs_[static_cast<size_t>(cand)]) {
			l_sub_ = cand;
			ctx_sub_ = cand;
			return;
		}
	}
}

void DesktopInput::OnReleased(int i) {
	const size_t u = static_cast<size_t>(i);
	switch (static_cast<DeskAction>(i)) {
		case DeskAction::Attack:
		case DeskAction::Thrust:
		case DeskAction::Ground:
		case DeskAction::Sweep:
			if (hold_off_[u])
				hold_off_[u] = false;
			else
				r_latch_[u] = true;
			break;
		case DeskAction::Guard: r_latch_[u] = true; break;
		case DeskAction::Tech:
			r_latch_[u] = true;
			tech_cancelled_at_release_ = tech_cancelled_;
			tech_down_ = false;
			tech_cancelled_ = false;
			break;
		default: break;
	}
}

void DesktopInput::CancelTechnique() {
	tech_cancelled_ = true;
	l_tech_cancel_ = true;
	tech_aim_active_ = false;
	mouse_aim_ = Vec2();
}

}  // namespace ffg
