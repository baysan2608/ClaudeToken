// Fourfold game logic island - procedural pose math (see FFGIK.h).
#include "FFGIK.h"

namespace ffg {

namespace {
constexpr float kIkSoft = 0.05f;

float IkSoft(float d, float l, float d_anim) {
	const float knee = std::min(l - kIkSoft, d_anim);
	if (d <= knee) return d;
	const float span = std::max(l - knee, 1e-3f);
	return knee + span * (1.0f - std::exp(-(d - knee) / span));
}

// Signed angle (about `up`) turning a onto b, both projected onto the plane normal to up.
float IkSignedYaw(Vec3 a, Vec3 b, Vec3 up) {
	const Vec3 ah = a - up * a.dot(up);
	const Vec3 bh = b - up * b.dot(up);
	return std::atan2(ah.cross(bh).dot(up), ah.dot(bh));
}
}  // namespace

TwoBoneResult SolveTwoBone(Vec3 a, Vec3 b, Vec3 c, Quat qa, Quat qb, Quat qc, Vec3 target, Quat foot_goal, float w, Vec3 hinge_fallback) {
	TwoBoneResult r{qa, qb, qc, 0.0f};
	if (w <= 1e-4f) return r;
	const float wc = Saturate(w);
	Vec3 t = c.lerp(target, wc);
	const Vec3 t_raw = t;
	const float l1 = (b - a).length();
	const float l2 = (c - b).length();
	if (l1 < 1e-4f || l2 < 1e-4f) return r;
	if ((t - c).length_squared() > 1e-10f) {
		// Soft IK: near full extension the knee angle is singular, so the reach is eased into the last kIkSoft metres.
		// The eased zone starts at the animated ankle's own distance when that is already in it, so a target equal
		// to it still reproduces the clip exactly.
		const float dt = (t - a).length();
		const float soft_d = IkSoft(dt, l1 + l2, (c - a).length());
		if (dt > 1e-5f && std::fabs(soft_d - dt) > 1e-6f) t = a + (t - a) * (soft_d / dt);
		const float lat = Clampf((t - a).length(), std::fabs(l1 - l2) + 1e-3f, l1 + l2 - 1e-3f);
		const Vec3 ba = (a - b) / l1;
		const Vec3 bc = (c - b) / l2;
		const float theta0 = std::acos(Clampf(ba.dot(bc), -1.0f, 1.0f));
		const float theta1 = std::acos(Clampf((l1 * l1 + l2 * l2 - lat * lat) / (2.0f * l1 * l2), -1.0f, 1.0f));
		Vec3 n = bc.cross(ba);
		if (n.length_squared() < 1e-10f) n = hinge_fallback;
		n = n.normalized();
		// A positive rotation about n = bc x ba turns bc toward ba (closes the knee).
		const Quat r1 = Quat::AxisAngle(n, theta0 - theta1);
		const Vec3 c1 = b + r1.Rotate(c - b);
		const Quat r2 = Quat::FromTo(c1 - a, t - a);
		r.thigh = (r2 * qa).Normalized();
		r.calf = (r2 * r1 * qb).Normalized();
	}
	r.foot = wc < 0.999f ? Slerp(qc, foot_goal, wc) : foot_goal;
	r.reach_error = std::max(0.0f, (t_raw - a).length() - (l1 + l2));   // measured on the goal before softening
	return r;
}

// ---------------------------------------------------------------- FootPlanter

void FootPlanter::Reset() {
	for (size_t i = 0; i < 2; ++i) {
		locked[i] = false;
		lock_w[i] = 0.0f;
		relock_wait_[i] = false;
		foot_off[i] = 0.0f;
		ground_valid_[i] = false;
		catch_lift_[i] = 0.0f;
	}
	pelvis_shift = 0.0f;
	hip_tilt = 0.0f;
	have_origin_ = false;
}

float FootPlanter::PlantOf(const FootInput& f) const {
	const float h = f.ankle.dot(axes.up);
	const float hp = 1.0f - SmoothStep(kPlantFrom, kPlantTo, h);
	if (f.plant_hint < 0) return hp;
	return 0.5f * hp + 0.5f * static_cast<float>(f.plant_hint);
}

float FootPlanter::YawOf(const FootFrameInput& in, const FootInput& f) const {
	// Absolute heading of the foot (ankle -> ball) in the world horizontal plane, against a fixed world reference.
	const Vec3 d = in.world_from_model.q.Rotate(f.ball - f.ankle);
	return IkSignedYaw(in.world_ref, d, in.world_up);
}

FootFrameOutput FootPlanter::Update(const FootFrameInput& in, const GroundSampler* ground) {
	FootFrameOutput out;
	for (size_t i = 0; i < 2; ++i) {
		out.goal[i] = in.feet[i].ankle;
		out.goal_rot[i] = in.feet[i].foot_rot;
	}
	const float dt = Clampf(in.dt, 0.0f, 0.1f);
	out.use_ik = in.ik_w > 0.002f && ground != nullptr;
	if (out.use_ik) {
		LockFeet(in, out.goal, out.goal_rot);
		GroundGoals(in, ground, out.goal);
		// Pelvis: both feet lower -> all the way down; one foot on lower ground -> part of the way (the lower leg
		// straightens, the upper one bends), then further only if a goal is out of reach.
		const float lo = std::min(foot_off[0], foot_off[1]);
		const float hi = std::min(std::max(foot_off[0], foot_off[1]), 0.0f);
		float shift_t = lo >= 0.0f ? lo : hi + (lo - hi) * kPelvisShare;
		shift_t = Clampf(shift_t, -kMaxDrop, 0.4f);
		shift_t -= ReachDrop(in, out.goal, shift_t);
		shift_t = std::max(shift_t, -kMaxDrop);
		pelvis_shift = Lerpf(pelvis_shift, shift_t, ExpK(12.0f, dt));
		const float tilt_t = Clampf((foot_off[0] - foot_off[1]) * 1.1f, -0.2f, 0.2f);
		hip_tilt = Lerpf(hip_tilt, tilt_t, ExpK(10.0f, dt));
	} else {
		pelvis_shift = Lerpf(pelvis_shift, 0.0f, ExpK(10.0f, dt));
		hip_tilt = Lerpf(hip_tilt, 0.0f, ExpK(10.0f, dt));
		for (size_t i = 0; i < 2; ++i) {
			foot_off[i] = 0.0f;
			ground_valid_[i] = false;
			locked[i] = false;
			lock_w[i] = 0.0f;
		}
	}
	out.pelvis_shift = pelvis_shift;
	out.hip_tilt = hip_tilt;
	return out;
}

void FootPlanter::LockFeet(const FootFrameInput& in, std::array<Vec3, 2>& goal, std::array<Quat, 2>& grot) {
	const Xform& W = in.world_from_model;
	const Xform inv = W.Inverse();
	const float dt = std::max(Clampf(in.dt, 0.0f, 0.1f), 1e-4f);
	// A teleport (respawn, reset) or a long hitch: nothing stays locked.
	const bool jumped = have_origin_ && (W.t - last_origin_).length() > 0.6f;
	last_origin_ = W.t;
	const bool first = !have_origin_;
	have_origin_ = true;
	const Vec3 wup = in.world_up;
	for (size_t i = 0; i < 2; ++i) {
		const FootInput& f = in.feet[i];
		const float plant = PlantOf(f);
		const Vec3 anim_w = W.Apply(f.ball);   // the ball of the foot is what stays put
		const Vec3 pv = first ? anim_w : prev_anim_w_[i];
		Vec3 dv = anim_w - pv;
		dv -= wup * dv.dot(wup);
		const float vel = dv.length() / dt;
		prev_anim_w_[i] = anim_w;
		const bool pivot = in.ground_speed < 0.35f;
		const bool can = in.lock_target_w > 0.5f && enable_lock && in.ik_w > 0.95f && !jumped;
		const float yaw_now = YawOf(in, f);
		if (locked[i]) {
			Vec3 dd = lock_pos_[i] - anim_w;
			dd -= wup * dd.dot(wup);
			const float d = dd.length();
			const float yaw_d = std::fabs(WrapAngle(yaw_now - lock_yaw_[i]));
			if (!can || plant < 0.4f || d > kLockMaxDist || yaw_d > kLockMaxYaw) {
				locked[i] = false;
				// Torn loose while still planted: no new lock until this foot has stepped; the catch-up is a quick
				// step (the foot lifts on the way) rather than a slide.
				relock_wait_[i] = plant >= 0.4f;
				catch_lift_[i] = plant >= 0.4f ? Clampf(d * 0.45f, 0.0f, 0.09f) : 0.0f;
			}
		} else if (can && plant > 0.9f && lock_w[i] < 0.35f &&
		           (pivot || vel < kLockMaxFootSpeed + 0.8f * in.local_speed) && !relock_wait_[i]) {
			// Lock where the foot is drawn now (mid-release that is not the animated spot).
			float e0 = lock_w[i];
			e0 = e0 * e0 * (3.0f - 2.0f * e0);
			Vec3 lp0 = lock_pos_[i];
			lp0 = lp0 - wup * lp0.dot(wup) + wup * anim_w.dot(wup);
			const Vec3 cur = anim_w.lerp(lp0, e0);
			const float dyaw0 = WrapAngle(lock_yaw_[i] - yaw_now) * e0;
			locked[i] = true;
			lock_pos_[i] = cur;
			lock_yaw_[i] = yaw_now + dyaw0;
			lock_w[i] = 1.0f;
		}
		if (plant < 0.5f || vel > 1.4f) relock_wait_[i] = false;
		if (!locked[i]) lock_w[i] = MoveToward(lock_w[i], 0.0f, dt / kLockRelease);
		const float w = lock_w[i];
		if (w <= 1e-3f) continue;
		// Hold the ball horizontally (the ankle goal moves by the same amount); heights stay animated.
		const float e = w * w * (3.0f - 2.0f * w);
		Vec3 shift_w = lock_pos_[i] - anim_w;
		shift_w -= wup * shift_w.dot(wup);
		shift_w *= e;
		goal[i] = goal[i] + inv.q.Rotate(shift_w);
		if (catch_lift_[i] > 0.0f && !locked[i]) goal[i] = goal[i] + axes.up * (4.0f * catch_lift_[i] * w * (1.0f - w));
		// Keep the foot's world heading: undo the body's yaw change since touchdown.
		const float dyaw = WrapAngle(lock_yaw_[i] - yaw_now) * e;
		grot[i] = (Quat::AxisAngle(axes.up, dyaw) * f.foot_rot).Normalized();
	}
}

void FootPlanter::GroundGoals(const FootFrameInput& in, const GroundSampler* ground, std::array<Vec3, 2>& goal) {
	const Xform& W = in.world_from_model;
	const Vec3 wup = in.world_up;
	const float origin_up = W.t.dot(wup);
	const float floor_up = origin_up - in.model_lift;
	const float k = ExpK(22.0f, in.dt);
	for (size_t i = 0; i < 2; ++i) {
		const FootInput& f = in.feet[i];
		Vec3 hshift = W.q.Rotate(goal[i] - f.ankle);
		hshift -= wup * hshift.dot(wup);
		const Vec3 aw = W.Apply(f.ankle) + hshift;
		const Vec3 bw = W.Apply(f.ball) + hshift;
		Vec3 back = aw - bw;
		back -= wup * back.dot(wup);
		const Vec3 hw = aw + back.normalized() * 0.09f;   // the heel, behind the ankle
		const float top = floor_up + kStepHeight + 0.02f;
		const float g = std::max(ground->GroundUp(hw, top - kStepHeight), ground->GroundUp(bw, top - kStepHeight));
		float off = g - origin_up;
		if (g - floor_up < -kMaxDrop) off = floor_up - origin_up;   // past a ledge edge: stay level with the edge
		// Smoothed in world space: the model origin itself moves (visual height smoothing), and a planted foot must not
		// ride along with it.
		const float gy = origin_up + off;
		if (!ground_valid_[i] || std::fabs(gy - ground_w_[i]) > 1.2f) {
			ground_w_[i] = gy;
			ground_valid_[i] = true;
		} else {
			ground_w_[i] = Lerpf(ground_w_[i], gy, k);
		}
		off = Clampf(ground_w_[i] - origin_up, -kMaxDrop - 0.1f, kMaxLift);
		const float plant = PlantOf(f);
		foot_off[i] = off > 0.0f ? off : off * plant;
		goal[i] = goal[i] + axes.up * foot_off[i];
	}
}

float FootPlanter::ReachDrop(const FootFrameInput& in, const std::array<Vec3, 2>& goal, float shift) const {
	float need = 0.0f;
	for (size_t i = 0; i < 2; ++i) {
		const Vec3 hip = in.thigh_heads[i] + axes.up * shift;
		const float l = in.leg_len[i] * kReachFrac;
		const Vec3 d = goal[i] - hip;
		const float v = -d.dot(axes.up);   // hip above the goal
		const Vec3 hd = d + axes.up * v;
		const float h2 = hd.length_squared();
		if (h2 >= l * l) continue;
		const float extra = v - std::sqrt(l * l - h2);
		need = std::max(need, extra);
	}
	return Clampf(need, 0.0f, kMaxDrop);
}

// ---------------------------------------------------------------- LookAt

void LookAt::Update(float dt, Vec3 head_pos, Vec3 head_fwd, Vec3 target, float weight) {
	float want_yaw = 0.0f, want_pitch = 0.0f;
	if (weight > 0.002f) {
		const Vec3 to = target - head_pos;
		const Vec3 to_h = to - axes.up * to.dot(axes.up);
		const float hl = to_h.length();
		if (hl > 0.3f) {
			want_yaw = Clampf(IkSignedYaw(head_fwd, to, axes.up), -kYawMax, kYawMax) * weight;
			const float fwd_up = Clampf(head_fwd.normalized().dot(axes.up), -1.0f, 1.0f);
			want_pitch = Clampf(std::atan2(to.dot(axes.up), hl) - std::asin(fwd_up), -kPitchMax, kPitchMax) * weight;
		}
	}
	const float k = ExpK(7.0f, Clampf(dt, 0.0f, 0.1f));
	yaw = Lerpf(yaw, want_yaw, k);
	pitch = Lerpf(pitch, want_pitch, k);
}

Quat LookAt::Rotation(float yaw_share, float pitch_share, float pitch_axis_yaw_share) const {
	const Quat qy = Quat::AxisAngle(axes.up, yaw * yaw_share);
	if (std::fabs(pitch * pitch_share) < 1e-6f) return qy;
	// Positive pitch raises the face: rotate forward toward up, about (fwd x up) turned with the look yaw.
	const Vec3 raise_axis = Quat::AxisAngle(axes.up, yaw * pitch_axis_yaw_share).Rotate(axes.fwd.cross(axes.up));
	return (Quat::AxisAngle(raise_axis, pitch * pitch_share) * qy).Normalized();
}

}  // namespace ffg
