// Fourfold game logic island - inertial-style cross-fades ("offset decay", ARCHITECTURE §8.4: 3-6 frames on every
// switch, from the last output pose). At a switch the difference between the last output pose and the new target
// pose is stored per bone and decays to zero with a smooth curve while the new animation already plays underneath, so
// the motion of the new clip shows from its first frame and nothing pops. A new switch during a running one captures
// the offset relative to the current output, so chains of quick switches stay seamless.
#pragma once

#include "FFGMath.h"

#include <vector>

namespace ffg {

class InertialBlend {
public:
	// Decay curve: 1 at x = 0 -> 0 at x = 1, zero slope at both ends (smootherstep).
	static float Decay(float x) {
		const float u = Saturate(x);
		return 1.0f - u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
	}

	bool Active() const { return active_ && t_ < duration_; }
	float Duration() const { return duration_; }
	float Elapsed() const { return t_; }

	// last_out: the pose shown last frame; target: the new pose this frame (same bone order, n bones).
	void Start(const Xform* last_out, const Xform* target, size_t n, float duration) {
		rot_.resize(n);
		pos_.resize(n);
		for (size_t i = 0; i < n; ++i) {
			Quat d = (last_out[i].q * target[i].q.Inverse()).Normalized();
			if (d.w < 0.0f) d = d.Neg();
			rot_[i] = d;
			pos_[i] = last_out[i].t - target[i].t;
		}
		duration_ = std::max(duration, 1e-3f);
		t_ = 0.0f;
		active_ = n > 0;
	}
	void Start(const std::vector<Xform>& last_out, const std::vector<Xform>& target, float duration) {
		Start(last_out.data(), target.data(), std::min(last_out.size(), target.size()), duration);
	}
	void Stop() { active_ = false; }
	// Advances by dt and applies the remaining offset to `pose` (n bones) in place.
	void Apply(Xform* pose, size_t n, float dt) {
		if (!active_) return;
		t_ += std::max(dt, 0.0f);
		if (t_ >= duration_ || n != rot_.size()) {
			active_ = false;
			return;
		}
		const float w = Decay(t_ / duration_);
		for (size_t i = 0; i < n; ++i) {
			pose[i].q = (Slerp(Quat::Identity(), rot_[i], w) * pose[i].q).Normalized();
			pose[i].t += pos_[i] * w;
		}
	}
	void Apply(std::vector<Xform>& pose, float dt) { Apply(pose.data(), pose.size(), dt); }
	// Weight of the remaining offset (debug).
	float Weight() const { return Active() ? Decay(t_ / duration_) : 0.0f; }

private:
	std::vector<Quat> rot_;
	std::vector<Vec3> pos_;
	float duration_ = 0.1f;
	float t_ = 0.0f;
	bool active_ = false;
};

}  // namespace ffg
