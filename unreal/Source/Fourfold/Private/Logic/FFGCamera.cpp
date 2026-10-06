// Fourfold game logic island - third-person orbit camera (see FFGCamera.h).
#include "FFGCamera.h"

#include <initializer_list>

namespace ffg {

namespace {
float CamAngleBetween(Vec3 a, Vec3 b) {
	const float c = a.cross(b).length();
	const float d = a.dot(b);
	return std::atan2(c, d);
}
}  // namespace

void CameraLogic::SnapTo(Vec3 player_pos, Vec3 look_at_pos) {
	Vec3 d = look_at_pos - player_pos;
	d.y = 0.0f;
	if (d.length() > 0.1f) yaw = std::atan2(d.x, d.z);
	pivot_ = player_pos + Vec3(0.0f, height, 0.0f);
	cur_dist_ = distance;
	orbit_ = 0.0f;
	lift_ = 0.0f;
	frame_w_ = 0.0f;
	idle_ = 0.0f;
}

void CameraLogic::AddInput(Vec2 delta) {
	if (delta.length_squared() > 1e-8f) idle_ = 0.0f;
	yaw -= delta.x;
	pitch = Clampf(pitch - delta.y, -0.12f, 0.95f);   // looking up lowers the camera
}

void CameraLogic::Shake(float amount) {
	shake_ = std::max(shake_, amount * shake_scale * (reduced_motion ? kReducedShake : 1.0f));
	shake_decay_ = 3.5f;
}

void CameraLogic::ShakeAt(float amount, Vec3 pos, float decay_s) {
	const float d = pos.distance_to(pivot_ - Vec3(0.0f, height, 0.0f));
	const float a = amount * shake_scale * (reduced_motion ? kReducedShake : 1.0f) / (1.0f + d / 8.0f);
	if (a > shake_) {
		shake_ = a;
		shake_decay_ = a / std::max(decay_s, 0.05f);
	}
}

void CameraLogic::Kick(Vec3 dir, float amount) {
	if (reduced_motion || dir.length_squared() < 1e-6f) return;
	kick_ = dir.normalized() * (amount * shake_scale);
	kick_t_ = 0.2f;
}

void CameraLogic::FovPunch(float deg, float dur) {
	if (reduced_motion) return;
	fov_punch_ = deg;
	fov_t_ = dur;
	fov_dur_ = dur;
}

void CameraLogic::ZoomTo(Vec3 at, float amount, float dur) {
	if (reduced_motion) return;
	zoom_at_ = at;
	zoom_amt_ = amount;
	zoom_t_ = dur;
	zoom_dur_ = dur;
}

Vec3 CameraLogic::DirOf(float y, float p) const {
	const Vec3 f(std::sin(y), 0.0f, std::cos(y));
	return (-f * std::cos(p) + Vec3(0.0f, 1.0f, 0.0f) * std::sin(p)).normalized();
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
	return best < 0.0f ? want : std::max(0.0f, want * best - 0.15f);
}

float CameraLogic::HalfHFov() const {
	const float a = aspect > 0.1f ? aspect : 4.0f / 3.0f;
	return std::atan(std::tan(kBaseFov * kPi / 180.0f * 0.5f) * a);
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
	const float half_h = HalfHFov() - 8.0f * kPi / 180.0f;
	float best = SearchOrbit(want, player_pos, target_pos, half_h, 65.0f * kPi / 180.0f, base_d);
	if (ClearDist(yaw + best, pitch, want, true) < kMinDist) {
		// Flush against a yard wall: run the view along the wall (up to 100 deg; framing keeps the rival in).
		const float wide = SearchOrbit(want, player_pos, target_pos, half_h, 100.0f * kPi / 180.0f, base_d);
		if (ClearDist(yaw + wide, pitch, want, true) > ClearDist(yaw + best, pitch, want, true) + 0.3f) best = wide;
	}
	// Hysteresis: keep the current swing if it is nearly as good (no flip-flopping between sides).
	if (std::fabs(orbit_) > 0.05f && Sign(orbit_) != Sign(best) && best != 0.0f) {
		const float cur_d = ClearDist(yaw + orbit_, pitch, want);
		if (cur_d >= ClearDist(yaw + best, pitch, want) - 0.5f) return orbit_;
	}
	return best;
}

Vec3 CameraLogic::Feel(float rdt, Vec3 cam_pos, Vec3 right, Vec3 up, float& fov_out) {
	Vec3 off;
	if (shake_ > 0.001f) {
		shake_t_ += rdt * 40.0f;
		const float sx = std::sin(shake_t_ * 1.3f) * shake_ * 0.06f;
		const float sy = std::sin(shake_t_ * 1.7f + 1.0f) * shake_ * 0.06f;
		off += right * sx + up * sy;
		shake_ = std::max(0.0f, shake_ - rdt * shake_decay_);
	}
	if (kick_t_ > 0.0f) {
		kick_t_ = std::max(0.0f, kick_t_ - rdt);
		const float k = kick_t_ / 0.2f;
		off += kick_ * (k * k);
	}
	float fov = kBaseFov;
	if (fov_t_ > 0.0f) {
		fov_t_ = std::max(0.0f, fov_t_ - rdt);
		const float f = fov_t_ / std::max(fov_dur_, 1e-3f);
		fov += fov_punch_ * f * f;
	}
	if (zoom_t_ > 0.0f) {
		zoom_t_ = std::max(0.0f, zoom_t_ - rdt);
		const float z = std::sin(kPi * (1.0f - zoom_t_ / std::max(zoom_dur_, 1e-3f)));
		fov *= 1.0f - zoom_amt_ * z;
		off += (zoom_at_ - cam_pos) * (zoom_amt_ * z);   // drift toward the event so the zoom centres on it
	}
	fov_out = fov;
	return off;
}

CameraOutput CameraLogic::Update(float dt, float real_dt, Vec3 player_pos, const Vec3* target_pos, const Vec3* threat_pos) {
	const float rdt = Clampf(real_dt, 0.0f, 0.1f);
	dt = Clampf(dt, 0.0f, 0.1f);
	idle_ += dt;
	// Follow: fast but not rigid (smooths sim steps and knockbacks).
	const Vec3 want_pivot = player_pos + Vec3(0.0f, height, 0.0f);
	pivot_ = pivot_.lerp(want_pivot, ExpK(14.0f, dt));
	// Assist: keep the target (or an incoming threat) in frame when the player isn't steering.
	const Vec3* focus = threat_pos ? threat_pos : target_pos;
	if (focus && idle_ > 0.45f) {
		Vec3 to = *focus - player_pos;
		to.y = 0.0f;
		if (to.length() > 1.0f) {
			const float want = std::atan2(to.x, to.z);
			const float diff = WrapAngle(want - yaw);
			const float dead = 18.0f * kPi / 180.0f;
			if (std::fabs(diff) > dead) {
				const float rate = (threat_pos ? 2.6f : 1.4f) * dt;
				yaw += Clampf(diff - Sign(diff) * dead, -rate, rate);
			}
		}
	}
	float want_dist = distance;
	if (target_pos) {
		const float sep = target_pos->distance_to(player_pos);
		want_dist = Clampf(distance + (sep - 8.0f) * 0.12f, distance - 0.6f, distance + 1.6f);
	}
	const bool have_arena = arena && arena->valid;
	float max_p = target_pos ? kMaxPitchTarget : 1.2f;
	float want_orbit = 0.0f;
	if (have_arena) {
		const float clear0 = ClearDist(yaw + orbit_, pitch, want_dist);
		if (clear0 < want_dist * 0.8f || std::fabs(orbit_) > 0.01f) want_orbit = BestOrbit(want_dist, player_pos, target_pos);
	}
	orbit_ = Lerpf(orbit_, want_orbit, ExpK(3.0f, dt));
	const float vy = yaw + orbit_;
	const float clear = have_arena ? ClearDist(vy, pitch, want_dist) : want_dist;
	const float bclear = have_arena ? ClearDist(vy, pitch, want_dist, true) : want_dist;
	if (bclear < kMinDist) max_p = 1.1f;   // boxed in by the yard's walls: look down over the fighter
	float want_lift = Clampf(1.0f - clear / std::max(want_dist, 0.01f), 0.0f, 1.0f) * 0.75f;
	if (bclear < kMinDist && have_arena) {
		float need = 1.1f;
		for (float pp = pitch; pp <= 1.1f; pp += 0.05f) {
			if (ClearDist(vy, pp, want_dist, true) >= kMinDist) {
				need = pp;
				break;
			}
		}
		want_lift = std::max(want_lift, need - pitch);
	}
	want_lift = std::min(want_lift, std::max(0.0f, max_p - pitch));
	lift_ = Lerpf(lift_, want_lift, ExpK(want_lift > lift_ ? 10.0f : 2.5f, dt));
	float limit = want_dist;
	if (have_arena) {
		const float pl = std::min(pitch + lift_, 1.2f);
		const float all_c = ClearDist(vy, pl, want_dist);
		const float bnd_c = ClearDist(vy, pl, want_dist, true);
		limit = std::max(all_c, std::min(std::min(kMinDist, want_dist), bnd_c));
		limit = std::max(limit, kMinPull);
	}
	cur_dist_ = Lerpf(cur_dist_, limit, ExpK(limit < cur_dist_ ? 18.0f : 3.0f, dt));

	// Transform.
	const float p_eff = std::min(pitch + lift_, 1.2f);
	const Vec3 f(std::sin(vy), 0.0f, std::cos(vy));
	const Vec3 offset_dir = (-f * std::cos(p_eff) + Vec3(0.0f, 1.0f, 0.0f) * std::sin(p_eff)).normalized();
	Vec3 p = pivot_ + offset_dir * cur_dist_;
	if (have_arena) p.y = std::max(p.y, arena->GroundHeight(p.x, p.z, p.y) + 0.35f);
	Vec3 look_pt = pivot_ + Vec3(0.0f, 0.15f, 0.0f);
	const Vec3 u_p = look_pt - p;
	if (target_pos && u_p.length() > 0.1f) {
		// Keep the rival in frame: turn the view toward them just enough (at most to the bisector of the two).
		const Vec3 u_t = *target_pos + Vec3(0.0f, 1.2f, 0.0f) - p;
		const float a = CamAngleBetween(u_p, u_t);
		const float lim = (kBaseFov * 0.5f - 6.0f) * kPi / 180.0f;
		float want_w = 0.0f;
		if (a > lim && a > 1e-3f) want_w = std::min(a - lim, a * 0.5f) / a;
		frame_w_ = Lerpf(frame_w_, want_w, 0.25f);
		if (frame_w_ > 1e-3f) {
			const Quat q = Slerp(Quat::Identity(), Quat::FromTo(u_p, u_t), frame_w_);
			look_pt = p + q.Rotate(u_p.normalized()) * u_p.length();
		}
	} else {
		frame_w_ = 0.0f;
	}
	const Vec3 fwd = (look_pt - p).normalized();
	Vec3 right = fwd.cross(Vec3(0.0f, 1.0f, 0.0f)).normalized();
	if (right.length_squared() < 0.5f) right = Vec3(1.0f, 0.0f, 0.0f);
	const Vec3 up = right.cross(fwd).normalized();
	CameraOutput out;
	float fov = kBaseFov;
	const Vec3 feel = Feel(rdt, p, right, up, fov);
	out.pos = p + feel;
	out.look = look_pt + feel;
	out.fov = fov;
	out.view_yaw = vy;
	// See-through: solids between the pivot and the camera.
	if (have_arena) {
		const Vec3 d = p - pivot_;
		const Vec3 r(0.2f, 0.2f, 0.2f);
		for (const ArenaSolid& s : arena->solids)
			if (ArenaGround::Slab(pivot_, d, s.mn - r, s.mx + r) >= 0.0f) out.see_through.push_back(s.name);
	}
	return out;
}

}  // namespace ffg
