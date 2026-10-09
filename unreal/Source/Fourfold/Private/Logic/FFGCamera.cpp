// Fourfold game logic island - third-person duel camera (see FFGCamera.h).
#include "FFGCamera.h"

#include <cstdint>
#include <initializer_list>

namespace ffg {

namespace {
constexpr float kDeg = kPi / 180.0f;

float CamAngleBetween(Vec3 a, Vec3 b) {
	const float c = a.cross(b).length();
	const float d = a.dot(b);
	return std::atan2(c, d);
}

// Smooth 1D value noise in [-1, 1] (lattice hash, smoothstep between lattice points).
float CamHash(int i, int seed) {
	std::uint32_t h = static_cast<std::uint32_t>(i) * 0x27d4eb2du ^ static_cast<std::uint32_t>(seed) * 0x165667b1u;
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f;
}
float CamNoise(float t, int seed) {
	const float fl = std::floor(t);
	const int i = static_cast<int>(fl);
	const float f = t - fl;
	const float u = f * f * (3.0f - 2.0f * f);
	return Lerpf(CamHash(i, seed), CamHash(i + 1, seed), u);
}
}  // namespace

float SmoothDamp(float cur, float target, float& vel, float smooth_time, float dt, float max_speed) {
	if (dt <= 0.0f) return cur;
	const float st = std::max(smooth_time, 1e-4f);
	const float omega = 2.0f / st;
	const float x = omega * dt;
	const float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
	float change = cur - target;
	const float orig = target;
	if (max_speed > 0.0f) {
		const float m = max_speed * st;
		change = Clampf(change, -m, m);
	}
	const float tgt = cur - change;
	const float temp = (vel + omega * change) * dt;
	vel = (vel - omega * temp) * e;
	float out = tgt + (change + temp) * e;
	if ((orig - cur > 0.0f) == (out > orig)) {   // never overshoot
		out = orig;
		vel = 0.0f;
	}
	return out;
}

float SmoothDampAngle(float cur, float target, float& vel, float smooth_time, float dt, float max_speed) {
	return SmoothDamp(cur, cur + WrapAngle(target - cur), vel, smooth_time, dt, max_speed);
}

// ---------------------------------------------------------------- FeelEnvelope

float FeelEnvelope::Value() const {
	switch (phase) {
		case 1: return amount * std::sin(0.5f * kPi * Saturate(t / in_s));
		case 2: return amount;
		case 3: return amount * (1.0f - SmoothStep(0.0f, 1.0f, t / out_s));
		default: return 0.0f;
	}
}

void FeelEnvelope::Start(float amt, float ease_in, float ease_out) {
	const float cur = Value();
	// A bigger one rising / holding wins; one easing out yields once the new amount exceeds what is left of it.
	if ((phase == 1 || phase == 2) && std::fabs(amount) >= std::fabs(amt)) return;
	if (phase == 3 && std::fabs(cur) >= std::fabs(amt)) return;
	amount = amt;
	in_s = std::max(ease_in, 1e-3f);
	out_s = std::max(ease_out, 1e-3f);
	// Carry on from the current value (no pop): invert the sine ease-in.
	const float r = std::fabs(amt) > 1e-6f ? Clampf(cur / amt, 0.0f, 1.0f) : 0.0f;
	t = in_s * std::asin(r) / (0.5f * kPi);
	phase = 1;
}

float FeelEnvelope::Step(float rdt, bool slowed) {
	switch (phase) {
		case 1:
			t += rdt;
			if (t >= in_s) {
				phase = slowed ? 2 : 3;
				t = 0.0f;
			}
			break;
		case 2:
			if (!slowed) {
				phase = 3;
				t = 0.0f;
			}
			break;
		case 3:
			t += rdt;
			if (t >= out_s) phase = 0;
			break;
		default: break;
	}
	return Value();
}

// ---------------------------------------------------------------- CameraLogic

void CameraLogic::SnapTo(Vec3 player_pos, Vec3 look_at_pos) {
	Vec3 d = look_at_pos - player_pos;
	d.y = 0.0f;
	if (d.length() > 0.1f) yaw = std::atan2(d.x, d.z);
	pivot_ = player_pos + Vec3(0.0f, height, 0.0f);
	pivot_vel_ = Vec3();
	pivot_y_hold_ = pivot_.y;
	have_tgt_ = false;
	tgt_vel_ = Vec3();
	cur_dist_ = distance;
	coll_d_ = distance + 1.0f;
	orbit_ = 0.0f;
	lift_ = 0.0f;
	frame_w_ = 0.0f;
	idle_ = kReengageS;
	side_ = 1.0f;
	side_hold_ = 0.0f;
	yaw_vel_ = pitch_vel_ = 0.0f;
	focus_ = player_pos;
	move_yaw_ = yaw;
	snap_ = true;
}

void CameraLogic::AddInput(Vec2 delta) {
	if (delta.length_squared() > 1e-8f) {
		idle_ = 0.0f;
		yaw_vel_ = 0.0f;
		pitch_vel_ = 0.0f;
	}
	yaw -= delta.x;
	pitch = Clampf(pitch - delta.y, -0.12f, 0.95f);   // looking up lowers the camera
}

void CameraLogic::Shake(float amount, float decay_s) {
	const float a = amount * shake_scale * (reduced_motion ? kReducedShake : 1.0f);
	if (a > trauma_) {
		trauma_ = std::min(a, 1.0f);
		trauma_decay_ = a / std::max(decay_s, 0.05f);
	}
}

void CameraLogic::ShakeAt(float amount, Vec3 pos, float decay_s, bool player, bool roll) {
	const float d = pos.distance_to(focus_);
	float fall = 1.0f / (1.0f + d / 8.0f);
	if (player) fall = std::max(fall, kShakeFloorPlayer);
	const float a = amount * shake_scale * (reduced_motion ? kReducedShake : 1.0f) * fall;
	if (a > trauma_) {
		trauma_ = std::min(a, 1.0f);
		trauma_decay_ = a / std::max(decay_s, 0.05f);
	}
	if (roll && !reduced_motion) roll_trauma_ = std::max(roll_trauma_, std::min(a, 1.0f));
}

void CameraLogic::Kick(Vec3 dir, float amount) {
	if (reduced_motion || dir.length_squared() < 1e-6f) return;
	// Impulse on a damped spring; peak displacement of v0 / w x exp(-zeta / sqrt(1 - zeta^2) x atan(sqrt(1 - zeta^2) / zeta)).
	const float w = kTau * kKickHz;
	const float s = std::sqrt(1.0f - kKickZeta * kKickZeta);
	const float peak = std::exp(-kKickZeta / s * std::atan2(s, kKickZeta));
	kick_v_ += dir.normalized() * (amount * shake_scale * w / peak);
}

void CameraLogic::FovPunch(float deg, float out_s) {
	if (reduced_motion) return;
	fov_env_.Start(deg, kFovInS, out_s);
}

void CameraLogic::ZoomTo(Vec3 at, float amount, float dur) {
	if (reduced_motion) return;
	zoom_at_ = at;
	zoom_amt_ = amount;
	zoom_t_ = dur;
	zoom_dur_ = dur;
}

void CameraLogic::Cinematic(Vec3 at) {
	if (reduced_motion) return;
	fov_env_.Start(kCineFov, 0.12f, 0.35f);
	dolly_at_ = at;
	dolly_env_.Start(kCineDolly, 0.12f, 0.35f);
}

Vec3 CameraLogic::DirOf(float y, float p) const {
	const Vec3 f(std::sin(y), 0.0f, std::cos(y));
	return (-f * std::cos(p) + Vec3(0.0f, 1.0f, 0.0f) * std::sin(p)).normalized();
}

void CameraLogic::Shift(Vec3 d) {
	pivot_ = pivot_ - d;
	tgt_ = tgt_ - d;
	dolly_at_ = dolly_at_ - d;
	zoom_at_ = zoom_at_ - d;
	focus_ = focus_ - d;
}

float CameraLogic::ClearDist(float y, float p, float want, bool boundary_only) const {
	if (!arena || !arena->valid) return want;
	const Vec3 d = DirOf(y, p) * want;
	float best = -1.0f;
	const Vec3 r(0.3f, 0.3f, 0.3f);
	for (const ArenaSolid& s : arena->solids) {
		if (boundary_only && !s.boundary) continue;
		const float t = ArenaGround::Slab(pivot_, d, s.mn - r, s.mx + r);
		if (t >= 0.0f && (best < 0.0f || t < best)) best = t;
	}
	if (arena->world && want > 0.0f) {
		// Open world: hills behind the boom count like the yard walls.
		constexpr float kStep = 0.5f;
		const Vec3 dir = DirOf(y, p);
		for (float t = kStep; t <= want + 1e-3f; t += kStep) {
			const Vec3 q = pivot_ + dir * t;
			if (q.y < arena->Terrain(q.x, q.z) + 0.3f) {
				const float tt = std::max(0.0f, t - kStep) / want;
				if (best < 0.0f || tt < best) best = tt;
				break;
			}
		}
	}
	return best < 0.0f ? want : std::max(0.0f, want * best - 0.15f);
}

float CameraLogic::HalfHFov() const {
	const float a = aspect > 0.1f ? aspect : 4.0f / 3.0f;
	return std::atan(std::tan(fov_f_ * kDeg * 0.5f) * a);
}

float CameraLogic::CapFov(float vfov) const {
	const float a = aspect > 0.1f ? aspect : 4.0f / 3.0f;
	const float max_v = 2.0f * std::atan(std::tan(kMaxHFov * kDeg * 0.5f) / a) / kDeg;
	return std::min(vfov, max_v);
}

bool CameraLogic::TargetInView(float y, float dist, Vec3 player_pos, Vec3 target_pos, float half_h) const {
	(void)player_pos;
	const Vec3 c = pivot_ + DirOf(y, pitch) * dist;
	Vec3 look = pivot_ - c;
	look.y = 0.0f;
	Vec3 to = target_pos - c;
	to.y = 0.0f;
	if (look.length() < 1e-3f || to.length() < 1e-3f) return true;
	return std::fabs(CamAngleBetween(look, to)) <= half_h * 2.0f;
}

float CameraLogic::SearchOrbit(float want, Vec3 player_pos, const Vec3* target_pos, float half_h, float max_off, float base_d) const {
	float best = 0.0f;
	float best_d = base_d;
	for (int k = 1; k < 14; ++k) {
		const float off = max_off * static_cast<float>(k) / 13.0f;
		for (float sg : {1.0f, -1.0f}) {
			const float o = off * sg;
			if (target_pos && !TargetInView(yaw + o, want, player_pos, *target_pos, half_h)) continue;
			const float d = ClearDist(yaw + o, pitch, want);
			if (d > best_d + 0.25f + 0.4f * std::fabs(o)) {   // a swing has to buy real room
				best_d = d;
				best = o;
			}
		}
		if (best_d >= want * 0.95f) break;
	}
	return best;
}

float CameraLogic::BestOrbit(float want, Vec3 player_pos, const Vec3* target_pos) const {
	const float base_d = ClearDist(yaw, pitch, want);
	if (base_d >= want * 0.95f) return 0.0f;
	const float half_h = HalfHFov() - 8.0f * kDeg;
	float best = SearchOrbit(want, player_pos, target_pos, half_h, 65.0f * kDeg, base_d);
	if (ClearDist(yaw + best, pitch, want, true) < kMinDist) {
		// Flush against a yard wall: run the view along the wall (up to 100 deg; framing keeps the rival in).
		const float wide = SearchOrbit(want, player_pos, target_pos, half_h, 100.0f * kDeg, base_d);
		if (ClearDist(yaw + wide, pitch, want, true) > ClearDist(yaw + best, pitch, want, true) + 0.3f) best = wide;
	}
	// Hysteresis: keep the current swing if it is nearly as good (no flip-flopping between sides).
	if (std::fabs(orbit_) > 0.05f && Sign(orbit_) != Sign(best) && best != 0.0f) {
		const float cur_d = ClearDist(yaw + orbit_, pitch, want);
		if (cur_d >= ClearDist(yaw + best, pitch, want) - 0.5f) return orbit_;
	}
	return best;
}

void CameraLogic::FollowPivot(float dt, Vec3 want) {
	if (snap_ || (want - pivot_).length() > 6.0f) {   // scenario start / teleport: no swoop across the yard
		pivot_ = want;
		pivot_vel_ = Vec3();
		pivot_y_hold_ = want.y;
		return;
	}
	pivot_.x = SmoothDamp(pivot_.x, want.x, pivot_vel_.x, kPivotXzS, dt);
	pivot_.z = SmoothDamp(pivot_.z, want.z, pivot_vel_.z, kPivotXzS, dt);
	if (player_grounded) {
		// Footstep bob and small steps stay inside the band; a real change of level re-centres it.
		if (std::fabs(want.y - pivot_y_hold_) > kPivotYDead)
			pivot_y_hold_ = want.y;
		else
			pivot_y_hold_ = Lerpf(pivot_y_hold_, want.y, ExpK(1.0f, dt));
	} else {
		pivot_y_hold_ = want.y;
	}
	pivot_.y = SmoothDamp(pivot_.y, pivot_y_hold_, pivot_vel_.y, kPivotYS, dt);
}

void CameraLogic::Feel(float rdt, bool slowed, Vec3 cam_pos, Vec3& look, float& fov, float& roll, Vec3& trans, Vec3& dolly) {
	Vec3 fwd = look - cam_pos;
	const float len = std::max(fwd.length(), 1e-3f);
	fwd = fwd / len;
	Vec3 right = fwd.cross(Vec3(0.0f, 1.0f, 0.0f)).normalized();
	if (right.length_squared() < 0.5f) right = Vec3(1.0f, 0.0f, 0.0f);
	const Vec3 up = right.cross(fwd).normalized();
	// Trauma shake: rotation (what reads on screen), a hint of translation; trauma^2 keeps small hits subtle.
	if (trauma_ > 1e-4f || roll_trauma_ > 1e-4f) {
		shake_t_ += rdt * kShakeHz;
		if (shake_t_ > 4096.0f) shake_t_ -= 4096.0f;
		const float t2 = trauma_ * trauma_;
		const float pitch_off = kShakePitchDeg * kDeg * t2 * CamNoise(shake_t_, 11);
		const float yaw_off = kShakeYawDeg * kDeg * t2 * CamNoise(shake_t_, 23);
		roll += kShakeRollDeg * roll_trauma_ * roll_trauma_ * CamNoise(shake_t_ * 0.8f, 37);
		trans += (right * CamNoise(shake_t_, 41) + up * CamNoise(shake_t_, 53)) * (kShakePosM * t2);
		const Quat q = Quat::AxisAngle(Vec3(0.0f, 1.0f, 0.0f), yaw_off) * Quat::AxisAngle(right, pitch_off);
		look = cam_pos + q.Rotate(fwd) * len;
		trauma_ = std::max(0.0f, trauma_ - rdt * trauma_decay_);
		roll_trauma_ = std::max(0.0f, roll_trauma_ - rdt * trauma_decay_);
	}
	// Kick: damped spring, stepped with the exact solution (identical at any frame rate).
	if (kick_x_.length_squared() > 1e-10f || kick_v_.length_squared() > 1e-8f) {
		const float w = kTau * kKickHz;
		const float a = kKickZeta * w;
		const float wd = w * std::sqrt(1.0f - kKickZeta * kKickZeta);
		const float e = std::exp(-a * rdt);
		const float c = std::cos(wd * rdt), sn = std::sin(wd * rdt);
		const Vec3 x0 = kick_x_, v0 = kick_v_;
		kick_x_ = (x0 * c + (v0 + x0 * a) * (sn / wd)) * e;
		kick_v_ = (v0 * c - (v0 * a + x0 * (w * w)) * (sn / wd)) * e;
		if (kick_x_.length_squared() < 1e-10f && kick_v_.length_squared() < 1e-8f) kick_x_ = kick_v_ = Vec3();
		trans += kick_x_;
	}
	fov += fov_env_.Step(rdt, slowed);
	const float dl = dolly_env_.Step(rdt, slowed);
	if (std::fabs(dl) > 1e-5f) dolly += (dolly_at_ - cam_pos) * dl;
	if (zoom_t_ > 0.0f) {
		zoom_t_ = std::max(0.0f, zoom_t_ - rdt);
		const float z = std::sin(kPi * (1.0f - zoom_t_ / std::max(zoom_dur_, 1e-3f)));
		fov *= 1.0f - zoom_amt_ * z;
		trans += (zoom_at_ - cam_pos) * (zoom_amt_ * z);   // drift toward the event so the zoom centres on it
	}
}

CameraOutput CameraLogic::Update(float dt, float real_dt, Vec3 player_pos, const Vec3* target_pos, const Vec3* threat_pos) {
	const float rdt = Clampf(real_dt, 0.0f, 0.1f);
	dt = Clampf(dt, 0.0f, 0.1f);
	const bool slowed = rdt > 1e-5f && dt < 0.5f * rdt;   // hit-stop / cinematic slow motion / pause
	const bool snap = snap_;
	idle_ += rdt;
	side_hold_ = std::max(0.0f, side_hold_ - rdt);
	const bool have_arena = arena && arena->valid;

	// ---- target (smoothed; a lock change or a teleport lands directly)
	const bool has_target = target_pos != nullptr;
	if (has_target) {
		if (snap || !have_tgt_ || !had_target_ || (*target_pos - tgt_).length() > 6.0f) {
			tgt_ = *target_pos;
			tgt_vel_ = Vec3();
			have_tgt_ = true;
		} else {
			tgt_.x = SmoothDamp(tgt_.x, target_pos->x, tgt_vel_.x, kTargetXzS, dt);
			tgt_.z = SmoothDamp(tgt_.z, target_pos->z, tgt_vel_.z, kTargetXzS, dt);
			tgt_.y = SmoothDamp(tgt_.y, target_pos->y, tgt_vel_.y, kPivotYS, dt);
		}
	}
	had_target_ = has_target;
	const bool spec = spectator && has_target;
	locked_ = has_target && !spectator;
	if (snap) {
		lock_w_ = locked_ ? 1.0f : 0.0f;
		lock_w_vel_ = 0.0f;
	} else {
		lock_w_ = SmoothDamp(lock_w_, locked_ ? 1.0f : 0.0f, lock_w_vel_, kFramingS, dt);
	}

	// ---- pivot: the player's chest (free / lock) or the duel midpoint (spectator)
	Vec3 want_pivot;
	if (spec) {
		want_pivot = (player_pos + *target_pos) * 0.5f + Vec3(0.0f, kSpecHeight, 0.0f);
	} else {
		want_pivot = player_pos + Vec3(0.0f, Lerpf(height, kLockPivotH, lock_w_), 0.0f);
		Vec3 lead(player_vel.x * kLookAheadS, 0.0f, player_vel.z * kLookAheadS);
		if (lead.length() > kLookAheadMax) lead = lead.normalized() * kLookAheadMax;
		want_pivot += lead * (1.0f - lock_w_);
	}
	FollowPivot(dt, want_pivot);

	// ---- framing targets
	float want_dist = distance;
	float want_fov = kFreeFov;
	float want_w = w_f_;
	float want_pitch = pitch;
	float want_yaw = yaw;
	float assist_s = kAssistS;
	float frame_s = kFramingS;
	bool assist = false;
	bool assist_pitch = false;
	focus_ = player_pos;
	if (has_target) {
		const Vec3 from = spec ? player_pos : Vec3(pivot_.x, player_pos.y, pivot_.z);
		Vec3 to = tgt_ - from;
		to.y = 0.0f;
		const float sep = to.length();
		focus_ = (player_pos + tgt_) * 0.5f;
		if (sep > 0.3f) axis_ = std::atan2(to.x, to.z);
		if (locked_) {
			const float k = Saturate((sep - kLockSepNear) / kLockSepSpan);
			want_dist = kLockDist0 + kLockDistK * k;
			const float theta = (kLockTheta0 + kLockThetaK * k) * kDeg;
			want_w = kLockW0 + kLockWK * k;
			want_pitch = kLockPitch0 + kLockPitchK * k;
			want_fov = kLockFov0 + kLockFovK * k;
			// Side: while the player steers, the two-shot adopts the side they turned the view to ...
			if (idle_ < kReengageS) {
				const float off = WrapAngle(yaw - axis_);
				if (std::fabs(off) > 5.0f * kDeg) side_ = Sign(off);
			}
			// ... and it flips when a yard wall sits behind this side's boom and the other side is open.
			if (have_arena && side_hold_ <= 0.0f) {
				const float cur_c = ClearDist(axis_ + side_ * theta, want_pitch, want_dist, true);
				const float oth_c = ClearDist(axis_ - side_ * theta, want_pitch, want_dist, true);
				if (cur_c < want_dist * 0.8f && oth_c > cur_c + 1.0f) {
					side_ = -side_;
					side_hold_ = 1.5f;
				}
			}
			want_yaw = axis_ + side_ * theta;
			assist = assist_pitch = idle_ >= kReengageS;
		} else {
			// Spectator: side-on to the fighters' axis, staying on the side of the line the camera is on.
			const float off = WrapAngle(yaw - axis_);
			if (std::fabs(off) > 0.05f) side_ = Sign(off);
			const float ang = kSpecAngle * kDeg;
			want_yaw = axis_ + side_ * ang;
			want_fov = kSpecFov;
			const float a = aspect > 0.1f ? aspect : 4.0f / 3.0f;
			const float half_h = std::atan(std::tan(CapFov(kSpecFov) * kDeg * 0.5f) * a);
			const float lateral = 0.5f * sep * std::sin(ang) + 1.2f;
			want_dist = Clampf(lateral / std::tan(half_h * 0.8f) + 0.5f * sep * std::cos(ang), 4.5f, 16.0f);
			want_pitch = kSpecPitch;
			assist = assist_pitch = true;
			assist_s = frame_s = kSpecS;
		}
	} else if (threat_pos && idle_ > 0.45f) {
		// Free: turn toward an incoming threat (to the edge of an 18 deg dead zone).
		Vec3 to = *threat_pos - player_pos;
		to.y = 0.0f;
		if (to.length() > 1.0f) {
			const float want = std::atan2(to.x, to.z);
			const float diff = WrapAngle(want - yaw);
			const float dead = 18.0f * kDeg;
			if (std::fabs(diff) > dead) {
				want_yaw = want - Sign(diff) * dead;
				assist = true;
				assist_s = 0.5f;
			}
		}
	}
	want_fov = CapFov(want_fov);
	if (snap) {
		dist_f_ = want_dist;
		fov_f_ = want_fov;
		w_f_ = want_w;
		dist_vel_ = fov_vel_ = w_vel_ = 0.0f;
		if (assist) yaw = want_yaw;
		if (assist_pitch) pitch = want_pitch;
		yaw_vel_ = pitch_vel_ = 0.0f;
	} else {
		dist_f_ = SmoothDamp(dist_f_, want_dist, dist_vel_, frame_s, dt);
		fov_f_ = SmoothDamp(fov_f_, want_fov, fov_vel_, frame_s, dt);
		w_f_ = SmoothDamp(w_f_, want_w, w_vel_, frame_s, dt);
		// Assist on: spring onto the framing. Off: the spring's momentum bleeds out (no dead stop).
		yaw = SmoothDampAngle(yaw, assist ? want_yaw : yaw, yaw_vel_, assist_s, dt, kAssistMaxRate);
		if (assist_pitch)
			pitch = SmoothDamp(pitch, want_pitch, pitch_vel_, assist_s, dt);
		else
			pitch_vel_ = 0.0f;
	}
	if (locked_) pitch = std::min(pitch, kMaxPitchTarget);

	// ---- collision: swing, then lift, never closer than kMinDist to a yard wall
	const float want_d = dist_f_;
	float max_p = has_target ? kMaxPitchTarget : 1.2f;
	float want_orbit = 0.0f;
	if (have_arena) {
		const float clear0 = ClearDist(yaw + orbit_, pitch, want_d);
		if (clear0 < want_d * 0.8f || std::fabs(orbit_) > 0.01f) want_orbit = BestOrbit(want_d, player_pos, target_pos);
	}
	orbit_ = snap ? want_orbit : Lerpf(orbit_, want_orbit, ExpK(3.0f, dt));
	const float vy = yaw + orbit_;
	const float clear = have_arena ? ClearDist(vy, pitch, want_d) : want_d;
	const float bclear = have_arena ? ClearDist(vy, pitch, want_d, true) : want_d;
	if (bclear < kMinDist) max_p = 1.1f;   // boxed in by the yard's walls: look down over the fighter
	float want_lift = Clampf(1.0f - clear / std::max(want_d, 0.01f), 0.0f, 1.0f) * 0.75f;
	if (bclear < kMinDist && have_arena) {
		float need = 1.1f;
		for (float pp = pitch; pp <= 1.1f; pp += 0.05f) {
			if (ClearDist(vy, pp, want_d, true) >= kMinDist) {
				need = pp;
				break;
			}
		}
		want_lift = std::max(want_lift, need - pitch);
	}
	want_lift = std::min(want_lift, std::max(0.0f, max_p - pitch));
	lift_ = snap ? want_lift : Lerpf(lift_, want_lift, ExpK(want_lift > lift_ ? 10.0f : 2.5f, dt));
	float limit = want_d;
	if (have_arena) {
		const float pl = std::min(pitch + lift_, 1.2f);
		const float all_c = ClearDist(vy, pl, want_d);
		const float bnd_c = ClearDist(vy, pl, want_d, true);
		limit = std::max(all_c, std::min(std::min(kMinDist, want_d), bnd_c));
		limit = std::max(limit, kMinPull);
	}
	// Pull in fast when something gets between; ease back out once it's gone. Unobstructed, the framing rules.
	const bool obstructed = limit < want_d - 1e-3f;
	const float coll_target = obstructed ? limit : want_d + 1.0f;
	if (obstructed && coll_d_ > want_d) coll_d_ = want_d;
	if (snap) {
		coll_d_ = coll_target;
		coll_vel_ = 0.0f;
	} else if (coll_target < coll_d_) {
		coll_d_ = Lerpf(coll_d_, coll_target, ExpK(kCollisionInK, dt));
		coll_vel_ = 0.0f;
	} else {
		coll_d_ = SmoothDamp(coll_d_, coll_target, coll_vel_, kCollisionOutS, dt);
	}
	cur_dist_ = std::min(want_d, coll_d_);

	// ---- transform
	const float p_eff = std::min(pitch + lift_, 1.2f);
	Vec3 p = pivot_ + DirOf(vy, p_eff) * cur_dist_;
	if (have_arena) p.y = std::max(p.y, arena->GroundHeight(p.x, p.z, p.y) + 0.35f);
	Vec3 look_pt = spec ? pivot_ : pivot_ + Vec3(0.0f, 0.15f, 0.0f);
	if (have_tgt_ && lock_w_ > 1e-3f && !spec) {
		const Vec3 feet = pivot_ - Vec3(0.0f, Lerpf(height, kLockPivotH, lock_w_), 0.0f);
		const Vec3 aim = feet.lerp(tgt_, w_f_) + Vec3(0.0f, kLockAimH, 0.0f);
		look_pt = look_pt.lerp(aim, lock_w_);
	}
	// Safety: a rival outside the frame (a swing along a wall, the player steering) pulls the view toward them, at most
	// to the bisector of the two.
	float want_fw = 0.0f;
	Vec3 u_t;
	const Vec3 u_p = look_pt - p;
	if (have_tgt_ && u_p.length() > 0.1f) {
		u_t = tgt_ + Vec3(0.0f, 1.2f, 0.0f) - p;
		if (locked_) {
			const float a = CamAngleBetween(u_p, u_t);
			const float lim = std::min(fov_f_ * kDeg * 0.5f, HalfHFov()) - 6.0f * kDeg;
			if (a > lim && a > 1e-3f) want_fw = std::min(a - lim, a * 0.5f) / a;
		}
	}
	frame_w_ = snap ? want_fw : Lerpf(frame_w_, want_fw, ExpK(10.0f, dt));
	if (frame_w_ > 1e-3f && have_tgt_ && u_p.length() > 0.1f) {
		const Quat q = Slerp(Quat::Identity(), Quat::FromTo(u_p, u_t), frame_w_);
		look_pt = p + q.Rotate(u_p.normalized()) * u_p.length();
	}
	// Stick basis: while locked and the view is roughly along the lock axis, forward = toward the rival.
	if (locked_) {
		const float off = std::fabs(WrapAngle(vy - axis_));
		const float wgt = SmoothStep(70.0f * kDeg, 40.0f * kDeg, off) * lock_w_;
		move_yaw_ = LerpAngle(vy, axis_, wgt);
	} else {
		move_yaw_ = vy;
	}

	CameraOutput out;
	float fov = fov_f_;
	float roll = 0.0f;
	Vec3 trans, dolly;
	Vec3 look = look_pt;
	Feel(rdt, slowed, p, look, fov, roll, trans, dolly);
	out.pos = p + trans + dolly;
	out.look = look + trans;
	out.fov = fov;
	out.roll = roll;
	out.view_yaw = vy;
	out.move_yaw = move_yaw_;
	// See-through: solids between the pivot and the camera.
	if (have_arena) {
		const Vec3 d = p - pivot_;
		const Vec3 r(0.2f, 0.2f, 0.2f);
		for (const ArenaSolid& s : arena->solids)
			if (ArenaGround::Slab(pivot_, d, s.mn - r, s.mx + r) >= 0.0f) out.see_through.push_back(s.name);
	}
	if (rdt > 0.0f) snap_ = false;   // the scenario-reset call (dt 0) keeps the snap for the first real frame
	return out;
}

}  // namespace ffg
