// Fourfold game logic island - physical secondary motion (see FFGSprings.h).
#include "FFGSprings.h"

namespace ffg {

// ---------------------------------------------------------------- HitReactor

Vec3 HitReactor::TipAxis(Vec3 dir) const {
	Vec3 d = dir - axes.up * dir.dot(axes.up);
	if (d.length_squared() < 1e-6f) d = -axes.fwd;
	// Rotating `up` about (up x d) by a positive angle moves it toward d (pure algebra, any handedness).
	return axes.up.cross(d.normalized());
}

void HitReactor::Hit(Vec3 dir, float strength) {
	const float s = Clampf(strength, 0.0f, 1.4f);
	const Vec3 ax = TipAxis(dir);
	// Lateral part of the blow twists the torso toward where the blow travels.
	Vec3 lat = dir - axes.up * dir.dot(axes.up) - axes.fwd * dir.dot(axes.fwd);
	const float lat_len = Clampf(lat.length(), 0.0f, 1.0f);
	Vec3 twist;
	if (lat_len > 1e-4f) twist = axes.fwd.cross(lat.normalized()) * lat_len;   // rotates fwd toward the blow's side
	torso_v += ax * (6.5f * s) + twist * (1.6f * s);
	pending_head_ += ax * (7.5f * s) + twist * (3.0f * s);
	pending_t_ = kHeadDelay;
	// Arms are thrown opposite to the torso's motion (they lag), plus a little outward.
	const Vec3 down = -axes.up;
	const Vec3 out_l = down.cross(axes.left);     // raises the left arm sideways
	const Vec3 out_r = down.cross(-axes.left);    // raises the right arm sideways
	arm_l_v += -ax * (5.0f * s) + out_l * (2.0f * s);
	arm_r_v += -ax * (5.0f * s) + out_r * (2.0f * s);
}

void HitReactor::Block(Vec3 dir, float strength) {
	const float s = Clampf(strength, 0.0f, 1.0f);
	const Vec3 ax = TipAxis(dir);
	torso_v += ax * (3.2f * s);
	head_v += ax * (1.6f * s);
	arm_l_v += ax * (4.0f * s);
	arm_r_v += ax * (4.0f * s);
}

void HitReactor::Reset() {
	torso = torso_v = head = head_v = arm_l = arm_l_v = arm_r = arm_r_v = Vec3();
	pending_head_ = Vec3();
	pending_t_ = -1.0f;
}

bool HitReactor::Active() const {
	const float x = torso.length_squared() + head.length_squared() + arm_l.length_squared() + arm_r.length_squared();
	const float v = torso_v.length_squared() + head_v.length_squared() + arm_l_v.length_squared() + arm_r_v.length_squared();
	return x + v * 0.01f > 1e-6f || pending_t_ >= 0.0f;
}

void HitReactor::Integrate(Vec3& x, Vec3& v, const SpringParams& p, float h) {
	const float w = kTau * p.freq;
	const Vec3 acc = -x * (w * w) - v * (2.0f * p.zeta * w);
	v += acc * h;
	x += v * h;
	if (x.length() > kMaxAngle) {
		x = x.normalized() * kMaxAngle;
		v *= 0.5f;
	}
}

void HitReactor::Step(float dt) {
	float left = Clampf(dt, 0.0f, 0.1f);
	if (pending_t_ >= 0.0f) {
		pending_t_ -= left;
		if (pending_t_ < 0.0f) {
			head_v += pending_head_;
			pending_head_ = Vec3();
		}
	}
	while (left > 1e-6f) {
		const float h = std::min(left, 1.0f / 120.0f);
		left -= h;
		Integrate(torso, torso_v, torso_p, h);
		Integrate(head, head_v, head_p, h);
		Integrate(arm_l, arm_l_v, arm_p, h);
		Integrate(arm_r, arm_r_v, arm_p, h);
	}
}

// ---------------------------------------------------------------- LandingSpring

void LandingSpring::Step(float dt) {
	const float w = kTau * 2.4f;
	float h = Clampf(dt, 0.0f, 0.1f);
	while (h > 1e-6f) {
		const float s = std::min(h, 1.0f / 120.0f);
		h -= s;
		v += (-y * w * w - v * 2.0f * 0.55f * w) * s;
		y = Clampf(y + v * s, -0.22f, 0.05f);
	}
}

// ---------------------------------------------------------------- SpringChain

Vec3 ClosestOnSegment(Vec3 p, Vec3 a, Vec3 b) {
	const Vec3 ab = b - a;
	const float l2 = ab.length_squared();
	if (l2 < 1e-10f) return a;
	const float t = Clampf((p - a).dot(ab) / l2, 0.0f, 1.0f);
	return a + ab * t;
}

void SpringChain::Step(float dt, const std::vector<Vec3>& animated, const std::vector<CapsuleCollider>& colliders,
                       std::vector<Vec3>& out_dirs) {
	const size_t n = animated.size() >= 2 ? animated.size() - 1 : 0;
	out_dirs.assign(n, Vec3());
	if (n == 0) return;
	bool reset = !initialized_ || cur_.size() != n;
	// A teleport (respawn, round reset, hitch): start again from the animated pose.
	if (!reset && (cur_[0] - animated[1]).length() > 1.5f) reset = true;
	if (reset) {
		cur_.assign(animated.begin() + 1, animated.end());
		prev_ = cur_;
		len_.assign(n, 0.0f);
		for (size_t i = 0; i < n; ++i) len_[i] = std::max((animated[i + 1] - animated[i]).length(), 1e-3f);
		initialized_ = true;
		acc_ = 0.0f;
	}
	// Fixed steps (Verlet assumes equal steps: a variable last sub-step jittered the tails at uneven frame times). Each
	// step sees the animated pose at its own time (lerp from the last frame's pose), not this frame's pose held.
	const float fdt = Clampf(dt, 0.0f, 0.1f);
	const float since = acc_;   // time since the last step, at the start of this frame
	acc_ = std::min(acc_ + fdt, 0.1f);
	const float h = kStep;
	const float g = 9.81f * params.gravity;
	const float keep = std::pow(Clampf(1.0f - params.drag, 0.0f, 1.0f), h * 60.0f);
	const float pull = 1.0f - std::exp(-params.stiffness * 8.0f * h);
	if (reset || anim_prev_.size() != animated.size()) anim_prev_ = animated;
	pose_.resize(animated.size());
	int step = 0;
	while (acc_ >= h - 1e-6f) {
		acc_ = std::max(0.0f, acc_ - h);
		const float at = h - since + static_cast<float>(step) * h;
		++step;
		const float f = fdt > 1e-6f ? Saturate(at / fdt) : 1.0f;
		for (size_t k = 0; k < animated.size(); ++k) pose_[k] = anim_prev_[k].lerp(animated[k], f);
		for (size_t i = 0; i < n; ++i) {
			const Vec3 head = i == 0 ? pose_[0] : cur_[i - 1];
			const Vec3 rest_dir = (pose_[i + 1] - pose_[i]).normalized();
			const Vec3 target = head + rest_dir * len_[i];
			Vec3 next = cur_[i] + (cur_[i] - prev_[i]) * keep;
			next += (target - next) * pull;
			next += gravity_dir * (g * h * h);
			// length constraint
			Vec3 d = next - head;
			if (d.length_squared() < 1e-10f) d = rest_dir;
			d = d.normalized();
			// angle limit from the animated direction
			const float cosang = Clampf(d.dot(rest_dir), -1.0f, 1.0f);
			const float ang = std::acos(cosang);
			if (ang > params.max_angle && rest_dir.length_squared() > 0.5f) {
				const Quat q = Slerp(Quat::Identity(), Quat::FromTo(rest_dir, d), params.max_angle / ang);
				d = q.Rotate(rest_dir);
			}
			next = head + d * len_[i];
			// collision (two passes of push-out + length constraint: the tail slides around the capsule)
			for (int pass = 0; pass < 2; ++pass) {
				for (const CapsuleCollider& c : colliders) {
					const Vec3 cp = ClosestOnSegment(next, c.a, c.b);
					const Vec3 off = next - cp;
					const float min_d = c.radius + params.radius;
					const float dist = off.length();
					if (dist < min_d) {
						const Vec3 nrm = dist > 1e-6f ? off / dist : rest_dir;
						next = cp + nrm * min_d;
						const Vec3 dd = next - head;
						if (dd.length_squared() > 1e-10f) next = head + dd.normalized() * len_[i];
					}
				}
			}
			prev_[i] = cur_[i];
			cur_[i] = next;
		}
	}
	anim_prev_ = animated;
	// Render between the last two steps (alpha = the unsimulated remainder), so the tails move evenly at any frame rate.
	const float alpha = Clampf(acc_ / h, 0.0f, 1.0f);
	Vec3 prev_tail;
	for (size_t i = 0; i < n; ++i) {
		const Vec3 tail = prev_[i].lerp(cur_[i], alpha);
		const Vec3 head = i == 0 ? animated[0] : prev_tail;
		Vec3 d = (tail - head).normalized();
		if (d.length_squared() < 0.5f) d = (animated[i + 1] - animated[i]).normalized();
		out_dirs[i] = d;
		prev_tail = tail;
	}
}

}  // namespace ffg
