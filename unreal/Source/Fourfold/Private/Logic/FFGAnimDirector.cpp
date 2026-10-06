// Fourfold game logic island - per-fighter animation director (see FFGAnimDirector.h).
#include "FFGAnimDirector.h"

#include <cstdio>

namespace ffg {

namespace {

Vec3 DirSimForward(float facing) { return Vec3(std::sin(facing), 0.0f, std::cos(facing)); }

const std::string& DirDataStr(const ff::Value& data, const char* key) { return data[key].as_string(); }

bool DirIsDown(const ff::ActorView& a) { return a.stun > 0.0f && (a.stun_kind == "knockdown" || a.stun_kind == "getup"); }

}  // namespace

void AnimDirector::Reset() {
	r_ = AnimRecipe();
	key_.clear();
	act_id_.clear();
	act_phase_ = ff::ActionPhase::Done;
	act_total_ = 0.0f;
	stun_kind_.clear();
	stun_t_ = 0.0f;
	overlay_ = nullptr;
	additive_ = nullptr;
	hits.Reset();
	landing.Reset();
	loco.Reset();
	ik_w_ = 1.0f;   // a (re)spawned fighter stands: IK starts engaged
	look_w_ = aim_w_ = legs_w_ = breathe_w_ = 0.0f;
	lean_ = Vec2();
	hand_w_ = 0.0f;
	was_grounded_ = true;
	jumped_ = false;
	first_ = true;
}

void AnimDirector::SetKey(const std::string& key, float fade_frames) {
	if (key == key_) return;
	const bool had = !key_.empty();
	key_ = key;
	if (!had) return;   // the very first pose needs no fade
	++serial_;
	r_.transition_serial = serial_;
	r_.transition_time = fade_frames / 60.0f;
}

std::string AnimDirector::EvadeClip(const ff::ActorView& a) const {
	// Evade clip from the evade direction relative to the facing it started with, so the clip always travels the way
	// the sim moves the fighter.
	const Vec3 dir = a.action.data["dir"].as_vec3();
	const Vec3 face = a.action.data["face"].is_vec3() ? a.action.data["face"].as_vec3() : DirSimForward(a.facing);
	const float fd = dir.dot(face);
	const float rd = dir.dot(face.cross(Vec3(0.0f, 1.0f, 0.0f)));   // > 0: toward the character's right
	if (dir.length_squared() < 1e-6f || std::fabs(fd) >= std::fabs(rd)) return fd > 0.0f || dir.length_squared() < 1e-6f ? "evade_fwd" : "evade_back";
	return rd > 0.0f ? "evade_r" : "evade_l";
}

void AnimDirector::AddLocomotion(const ff::ActorView& a, float weight, std::vector<ClipSample>& out, int max_clips, bool gaits_only) {
	if (weight <= 1e-4f || !lib) return;
	const std::string* names[kLocoRoles] = {nullptr, &lib->walk, &lib->run, &lib->strafe_l, &lib->strafe_r, &lib->back};
	const std::string stance = lib->StanceFor(a.element, a.is_dummy);
	struct Cand {
		const ClipDef* c;
		LocoRole role;
		float w;
	};
	Cand cands[kLocoRoles];
	int n = 0;
	for (int i = gaits_only ? 1 : 0; i < kLocoRoles; ++i) {
		const float w = loco.weights[static_cast<size_t>(i)];
		if (w < 0.003f) continue;
		const ClipDef* c = Clip(i == 0 ? stance : *names[i]);
		if (!c && i == 0) c = Clip(lib->idle);
		if (!c) continue;
		cands[n++] = {c, static_cast<LocoRole>(i), w};
	}
	// heaviest first, keep max_clips
	for (int i = 0; i < n; ++i)
		for (int j = i + 1; j < n; ++j)
			if (cands[j].w > cands[i].w) std::swap(cands[i], cands[j]);
	n = std::min(n, std::max(max_clips, 1));
	float tot = 0.0f;
	for (int i = 0; i < n; ++i) tot += cands[i].w;
	if (tot <= 1e-5f) {
		if (gaits_only) return;
		const ClipDef* c = Clip(stance);
		if (!c) c = Clip(lib->idle);
		if (c) out.push_back({c, loco.ClipTime(LocoRole::Stance, c->duration), weight});
		return;
	}
	for (int i = 0; i < n; ++i)
		out.push_back({cands[i].c, loco.ClipTime(cands[i].role, cands[i].c->duration), weight * cands[i].w / tot});
}

void AnimDirector::ReactionPose(const DirectorInput& in, const ff::ActorView& a) {
	const float dt = r_.dt;
	std::string kind = a.stun_kind.empty() ? "light" : a.stun_kind;
	const bool restarted = in.prev && in.prev->stun > 0.0f && a.stun > in.prev->stun + 0.02f && kind != "getup";
	if (kind != stun_kind_ || restarted) {
		stun_kind_ = kind;
		stun_t_ = 0.0f;
		stun_rate_ = 1.0f;
		++stun_serial_;
		if (kind == "getup") {
			const ClipDef* g = Clip(lib->Reaction("getup"));
			if (g) stun_rate_ = AnimTiming::FitRate(g->duration, a.stun);
		}
	} else {
		stun_t_ += dt * stun_rate_;
	}
	std::string key = kind;
	if (kind == "light") {
		const bool back = a.last_hit_dir.dot(DirSimForward(a.facing)) > 0.3f;
		key = back ? "light_back" : "light";
	} else if (kind != "heavy" && kind != "knockdown" && kind != "getup" && kind != "guard_break" && kind != "bound") {
		key = "stagger";
	}
	const ClipDef* c = Clip(lib->Reaction(key));
	if (!c && key == "light_back") c = Clip(lib->Reaction("light"));
	if (!c) {
		SetKey("react:none", 3.0f);
		AddLocomotion(a, 1.0f, r_.base, 2);
		return;
	}
	r_.base.push_back({c, AnimTiming::OnceTime(stun_t_, c->duration), 1.0f});
	SetKey("react:" + std::to_string(stun_serial_) + ":" + c->name, (kind == "knockdown" || kind == "getup") ? 4.0f : 3.0f);
}

bool AnimDirector::ActionPose(const DirectorInput& in, const ff::ActorView& a, float& w_act) {
	const ff::ActionView& act = a.action;
	w_act = 0.0f;
	if (!act.active) {
		act_id_.clear();
		act_phase_ = ff::ActionPhase::Done;
		return false;
	}
	const bool fresh = act.id != act_id_ ||
	                   (act.phase == ff::ActionPhase::Startup && act_phase_ != ff::ActionPhase::Startup && act_phase_ != ff::ActionPhase::Done) ||
	                   act.total + 0.05f < act_total_;
	if (fresh) {
		++act_serial_;
		act_id_ = act.id;
		hand_w_ = 0.0f;
	}
	act_phase_ = act.phase;
	act_total_ = act.total;

	const float lag = (1.0f - Saturate(in.alpha)) * kSimDt;
	const float t = std::max(0.0f, act.t - lag);
	const float total = std::max(0.0f, act.total - lag);
	const std::string& mode = DirDataStr(act.data, "mode");
	const std::string& spec = DirDataStr(act.data, "spec");
	const MoveClips mc = lib->ResolveMove(act.id, spec, act.element, act.slot, act.tier, mode, a.wall_body >= 0);
	const std::string su_name = mc.startup == "evade_*" ? EvadeClip(a) : mc.startup;
	const ClipDef* csu = Clip(su_name);
	const ClipDef* chold = Clip(mc.hold);
	const ClipDef* crel = Clip(mc.release);
	const bool hold_is_gait = !mc.hold.empty() && (mc.hold == lib->walk || mc.hold == lib->run);
	if (hold_is_gait) chold = nullptr;   // a wading / running mode is plain locomotion

	const ClipDef* clip = nullptr;
	float time = 0.0f;
	w_act = 1.0f;
	if (act.id == "guard") {
		const float su_t = std::max(act.startup, 0.0f);
		if (csu) {
			const float c = csu->ContactTime();
			const float played = su_t > 0.03f ? (total < su_t ? AnimTiming::StartupClipTime(total, su_t, c) : c + (total - su_t)) : total;
			if (played < csu->duration || !chold) {
				clip = csu;
				time = AnimTiming::OnceTime(played, csu->duration);
			} else {
				clip = chold;
				const float loop_from = su_t > 0.03f ? su_t + (csu->duration - c) : csu->duration;
				time = AnimTiming::LoopTime(total - loop_from, chold->duration);
			}
		} else if (chold) {
			clip = chold;
			time = AnimTiming::LoopTime(total, chold->duration);
		}
	} else {
		switch (act.phase) {
			case ff::ActionPhase::Startup:
				if (csu) {
					clip = csu;
					time = AnimTiming::StartupClipTime(t, act.startup, csu->ContactTime());
				} else if (chold) {
					clip = chold;
					time = AnimTiming::LoopTime(t, chold->duration);
				} else if (crel) {
					clip = crel;
					time = AnimTiming::StartupClipTime(t, act.startup, crel->ContactTime());
				}
				break;
			case ff::ActionPhase::Charge:
			case ff::ActionPhase::Channel: {
				const ClipDef* h = chold;
				if (!h && !hold_is_gait) h = Clip(lib->hold_by_element[static_cast<size_t>(Clampi(act.element, 0, 3))]);
				if (h) {
					clip = h;
					time = AnimTiming::LoopTime(t, h->duration);
				} else if (csu) {
					clip = csu;
					time = csu->ContactTime();
				}
				break;
			}
			case ff::ActionPhase::Active:
			case ff::ActionPhase::Recovery: {
				const ClipDef* c = (crel && crel != csu) ? crel : csu;
				float from = 0.0f;
				if (c) {
					from = c == crel && crel != csu ? std::max(c->ContactTime() - 1.0f / 60.0f, 0.0f) : c->ContactTime();
				} else if (chold) {
					c = chold;   // a channel-type move: the hold keeps looping through active
				}
				if (c) {
					clip = c;
					if (c->loop) {
						time = AnimTiming::LoopTime(total, c->duration);
					} else if (act.phase == ff::ActionPhase::Active) {
						time = AnimTiming::OnceTime(AnimTiming::ActiveClipTime(t, from), c->duration);
					} else {
						const float t0 = std::min(from + std::max(act.active_time, 0.0f), c->duration);
						time = AnimTiming::RecoveryClipTime(t, act.recovery, t0, c->duration);
					}
				}
				if (act.phase == ff::ActionPhase::Recovery) w_act = AnimTiming::RecoveryWeight(t, act.recovery);
				break;
			}
			default: break;
		}
	}
	if (!clip) {
		w_act = 0.0f;
		return false;
	}
	r_.base.push_back({clip, time, w_act});
	SetKey("act:" + std::to_string(act_serial_) + ":" + clip->name,
	       (act.phase == ff::ActionPhase::Active || act.phase == ff::ActionPhase::Recovery) ? 3.0f : (fresh ? 4.0f : 6.0f));
	// Hand shapes: weight 1 from startup to the end of recovery.
	std::string hl = mc.hand_l, hr = mc.hand_r;
	if (hl.empty()) hl = clip->hand_l;
	if (hr.empty()) hr = clip->hand_r;
	const float target = act.phase == ff::ActionPhase::Recovery ? w_act : 1.0f;
	hand_w_ = MoveToward(hand_w_, target, r_.dt / (4.0f / 60.0f));
	r_.hand[0] = lib->HandPose(hl);
	r_.hand[1] = lib->HandPose(hr);
	r_.hand_weight[0] = r_.hand[0] ? hand_w_ : 0.0f;
	r_.hand_weight[1] = r_.hand[1] ? hand_w_ : 0.0f;
	return true;
}

const AnimRecipe& AnimDirector::Update(const DirectorInput& in) {
	r_.base.clear();
	r_.legs.clear();
	r_.legs_weight = 0.0f;
	r_.additive = ClipSample();
	r_.additive_weight = 0.0f;
	r_.hand = {{nullptr, nullptr}};
	r_.hand_weight = {{0.0f, 0.0f}};
	r_.plant_hint = {{-1, -1}};
	r_.has_look = false;
	if (!in.cur || !lib) return r_;
	const ff::ActorView& a = *in.cur;
	const float dt = Clampf(in.dt, 0.0f, 0.1f);
	r_.dt = dt;
	r_.lod = in.lod;
	r_.chains = in.lod == 0;
	hits.axes = axes;

	// Gait design speeds from the library (stride-matched playback).
	{
		const std::string* g[kLocoRoles] = {nullptr, &lib->walk, &lib->run, &lib->strafe_l, &lib->strafe_r, &lib->back};
		for (int i = 1; i < kLocoRoles; ++i) {
			const ClipDef* c = lib->Find(*g[i]);
			if (c && c->speed > 0.0f && c->duration > 0.0f) loco.SetGait(static_cast<LocoRole>(i), c->speed, c->duration);
		}
	}

	// Reactions from this frame's events.
	for (const ReactionEvent& ev : in.events) {
		switch (ev.kind) {
			case ReactionEvent::Hit:
				hits.Hit(ev.dir, ev.knockdown ? ev.strength * 0.45f : ev.strength);
				if (ev.heavy) landing.Kick(0.9f);
				break;
			case ReactionEvent::Block: {
				hits.Block(-axes.fwd, Clampf(ev.strength, 0.0f, 1.0f));
				const ClipDef* b = Clip(lib->Reaction("block"));
				if (b) {
					additive_ = b;
					additive_t_ = 0.0f;
				}
				break;
			}
			case ReactionEvent::Perfect: {
				hits.Block(-axes.fwd, 0.22f);
				if (a.guarding || (a.action.active && a.action.id == "guard")) {
					const MoveClips mc = lib->ResolveMove("guard", DirDataStr(a.action.data, "spec"), a.element, ff::Slot::Guard,
					                                      a.action.tier, "", a.wall_body >= 0);
					const ClipDef* p = Clip(mc.perfect.empty() ? lib->Reaction("perfect") : mc.perfect);
					if (!p) p = Clip(lib->Reaction("perfect"));
					if (p) {
						overlay_ = p;
						overlay_t_ = 0.0f;
						++overlay_serial_;
					}
				}
				break;
			}
			case ReactionEvent::Land: {
				landing.Kick(ev.down_speed * 0.34f);
				const ClipDef* l = Clip(lib->air.count("land") ? lib->air.at("land") : std::string("land"));
				if (l && ev.down_speed > 4.0f && !a.action.active && a.stun <= 0.0f) {
					overlay_ = l;
					overlay_t_ = 0.0f;
					++overlay_serial_;
				}
				break;
			}
			case ReactionEvent::Burn: hits.Hit(-axes.fwd, 0.3f); break;
		}
	}
	hits.Step(dt);
	landing.Step(dt);

	// Turning on the spot steps the feet round: the yaw rate becomes a small sideways gait input.
	Vec2 lv = in.local_vel;
	const float spd = lv.length();
	const float turn_k = SmoothStep(1.2f, 3.0f, std::fabs(in.yaw_rate)) * (1.0f - SmoothStep(0.4f, 1.0f, spd));
	if (turn_k > 0.0f) lv.x += Clampf(-in.yaw_rate * 0.22f, -1.0f, 1.0f) * turn_k;
	loco.Update(dt, lv);

	const int max_loco = in.lod == 0 ? 4 : 2;
	bool free_loco = false;
	float legs = 0.0f;
	const bool stunned = a.stun > 0.0f;
	if (!stunned) stun_kind_.clear();

	// Overlays end when their clip ends, or a stun / a different action takes over.
	if (overlay_) {
		overlay_t_ += dt;
		if (overlay_t_ >= overlay_->duration || stunned) overlay_ = nullptr;
	}
	if (additive_) {
		additive_t_ += dt;
		if (additive_t_ >= additive_->duration) additive_ = nullptr;
	}

	float w_act = 0.0f;
	if (stunned) {
		ReactionPose(in, a);
		act_id_.clear();
	} else if (overlay_ && (!a.action.active || a.action.id == "guard")) {
		// A perfect deflect plays over the running guard (the guard's instance tracking stays as it is).
		r_.base.push_back({overlay_, AnimTiming::OnceTime(overlay_t_, overlay_->duration), 1.0f});
		SetKey("ovl:" + std::to_string(overlay_serial_) + ":" + overlay_->name, 3.0f);
	} else if (ActionPose(in, a, w_act)) {
		AddLocomotion(a, 1.0f - w_act, r_.base, max_loco);
		const ff::ActionView& act = a.action;
		if (a.grounded) {
			if (act.id == "guard" && a.wall_body < 0)
				legs = SmoothStep(0.15f, 0.6f, spd);
			else if (act.phase == ff::ActionPhase::Charge || act.phase == ff::ActionPhase::Channel)
				legs = SmoothStep(0.2f, 0.7f, spd);
		}
	} else if (!a.grounded) {
		const std::string& glide = lib->air.count("glide") ? lib->air.at("glide") : lib->idle;
		const std::string& fall = lib->air.count("fall") ? lib->air.at("fall") : lib->idle;
		const std::string& jump = lib->air.count("jump") ? lib->air.at("jump") : lib->idle;
		if (was_grounded_ && a.vel.y > 1.5f) {
			jumped_ = true;
			air_t_ = 0.0f;
		} else {
			air_t_ += dt;
		}
		const ClipDef* mode_clip = nullptr;
		if (!a.stance.empty() && lib->modes.count(a.stance)) mode_clip = Clip(lib->modes.at(a.stance));
		if (a.flying && !mode_clip && lib->modes.count("flight")) mode_clip = Clip(lib->modes.at("flight"));
		const ClipDef* gl = a.gliding ? Clip(glide) : nullptr;
		const ClipDef* jc = Clip(jump);
		if (gl) {
			r_.base.push_back({gl, AnimTiming::LoopTime(air_t_, gl->duration), 1.0f});
			SetKey("air:glide", 6.0f);
		} else if (mode_clip) {
			r_.base.push_back({mode_clip, AnimTiming::LoopTime(air_t_, mode_clip->duration), 1.0f});
			SetKey("mode:" + mode_clip->name, 6.0f);
		} else if (jumped_ && jc && air_t_ < jc->duration) {
			r_.base.push_back({jc, air_t_, 1.0f});
			SetKey("air:jump", 4.0f);
		} else if (const ClipDef* fc = Clip(fall)) {
			r_.base.push_back({fc, AnimTiming::LoopTime(air_t_, fc->duration), 1.0f});
			SetKey("air:fall", 6.0f);
		} else {
			AddLocomotion(a, 1.0f, r_.base, max_loco);
			SetKey("loco", 6.0f);
		}
	} else if (overlay_) {
		r_.base.push_back({overlay_, AnimTiming::OnceTime(overlay_t_, overlay_->duration), 1.0f});
		SetKey("ovl:" + std::to_string(overlay_serial_) + ":" + overlay_->name, 3.0f);
	} else {
		const ClipDef* mode_clip = (!a.stance.empty() && lib->modes.count(a.stance)) ? Clip(lib->modes.at(a.stance)) : nullptr;
		if (mode_clip) {
			r_.base.push_back({mode_clip, loco.ClipTime(LocoRole::Stance, mode_clip->duration), 1.0f});
			SetKey("mode:" + mode_clip->name, 6.0f);
		} else {
			free_loco = true;
			AddLocomotion(a, 1.0f, r_.base, max_loco);
			SetKey("loco", 6.0f);
		}
	}
	if (a.grounded) {
		jumped_ = false;
		air_t_ = 0.0f;
	}
	was_grounded_ = a.grounded;
	prev_vy_ = a.vel.y;

	// Legs-only gait layer under an upper-body action.
	legs_w_ += (legs - legs_w_) * ExpK(8.0f, dt);
	if (legs_w_ > 0.01f) {
		std::vector<ClipSample> gait;
		AddLocomotion(a, 1.0f, gait, max_loco, true);
		if (!gait.empty()) {
			r_.legs = gait;
			r_.legs_weight = legs_w_;
		}
	}
	if (additive_) {
		r_.additive = {additive_, additive_t_, 1.0f};
		r_.additive_weight = 1.0f;
	}
	// Plant hints from the dominant clip.
	const ClipSample* dom = nullptr;
	for (const ClipSample& s : r_.base)
		if (!dom || s.weight > dom->weight) dom = &s;
	if (dom && dom->clip) {
		r_.plant_hint[0] = dom->clip->PlantedAt(0, dom->time);
		r_.plant_hint[1] = dom->clip->PlantedAt(1, dom->time);
	}
	Procedural(in, a, free_loco, legs);
	if (dom && dom->clip) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%s %s t=%.2f w=%.2f", key_.c_str(), dom->clip->name.c_str(), static_cast<double>(dom->time),
		              static_cast<double>(dom->weight));
		r_.debug = buf;
	} else {
		r_.debug = key_ + " (ref pose)";
	}
	first_ = false;
	return r_;
}

void AnimDirector::Procedural(const DirectorInput& in, const ff::ActorView& a, bool free_loco, float legs) {
	const float dt = r_.dt;
	const ff::ActionView& act = a.action;
	const bool down = DirIsDown(a);
	const bool lifts = act.active && act.slot == ff::Slot::Evade &&
	                   (act.phase == ff::ActionPhase::Startup || act.phase == ff::ActionPhase::Active);
	// Foot IK only while standing in poses that keep the feet down.
	const float ik_target = (a.grounded && !lifts && !down) ? 1.0f : 0.0f;
	ik_w_ = MoveToward(ik_w_, ik_target, dt / (ik_target > ik_w_ ? 0.08f : 0.12f));
	r_.ik_weight = in.lod >= 2 ? 0.0f : ik_w_;
	// Planted feet lock while the legs belong to a gait or a stance (not in stuns, where the knockback drags them).
	r_.lock_weight = (a.grounded && a.stun <= 0.0f && (free_loco || legs > 0.0f || !act.active || act.id == "guard")) ? 1.0f : 0.0f;
	// Lean into acceleration and turns (centripetal: speed x yaw rate), only on the ground in free locomotion.
	Vec2 lt;
	if (free_loco) {
		const float fwd_spd = in.local_vel.y;
		lt.x = Clampf(in.local_acc.y * 0.010f, -0.09f, 0.11f);
		lt.y = Clampf(-fwd_spd * in.yaw_rate * 0.016f + in.local_acc.x * 0.008f, -0.17f, 0.17f);
	}
	lean_ = lean_ + (lt - lean_) * ExpK(6.0f, dt);
	r_.lean_pitch = lean_.x;
	r_.lean_roll = lean_.y;
	r_.land_y = landing.y;
	r_.spring_torso = hits.torso;
	r_.spring_head = hits.head;
	r_.spring_arm_l = hits.arm_l;
	r_.spring_arm_r = hits.arm_r;
	// Look at the incoming threat first, else the lock target.
	float lw = 0.0f;
	if (in.lod == 0 && in.has_look_target && !down && !lifts) {
		if (a.stun > 0.0f)
			lw = 0.25f;
		else if (!act.active)
			lw = 0.85f;
		else if (act.id == "guard")
			lw = 0.7f;
		else
			lw = 0.4f;
		r_.look_target = in.look_target;
		r_.has_look = true;
	}
	look_w_ += (lw - look_w_) * ExpK(5.0f, dt);
	// Without a target this frame the weight fades out toward the last one (r_.look_target keeps it).
	if (!r_.has_look && look_w_ > 0.01f && !first_) r_.has_look = true;
	r_.look_weight = r_.has_look ? look_w_ : 0.0f;
	// Chest aims along the attack.
	float aw = 0.0f;
	if (act.active && act.slot != ff::Slot::Evade &&
	    (act.phase == ff::ActionPhase::Startup || act.phase == ff::ActionPhase::Charge || act.phase == ff::ActionPhase::Channel ||
	     act.phase == ff::ActionPhase::Active)) {
		const ff::Value& fv = act.data["face"];
		if (fv.is_vec3()) {
			const Vec3 fd = fv.as_vec3();
			if (Vec2(fd.x, fd.z).length() > 0.01f) {
				r_.aim_yaw = Clampf(WrapAngle(std::atan2(fd.x, fd.z) - a.facing), -0.6f, 0.6f);
				aw = 1.0f;
			}
		}
	}
	aim_w_ += (aw - aim_w_) * ExpK(12.0f, dt);
	r_.aim_weight = aim_w_;
	// Breathing in stances (subtle: the stance clips breathe too).
	const float bt = (free_loco && in.local_vel.length() < 0.3f) ? 1.0f : 0.0f;
	breathe_w_ += (bt - breathe_w_) * ExpK(2.0f, dt);
	breathe_phase_ = Fposmod(breathe_phase_ + dt * kTau / 2.2f, kTau);
	r_.breathe = breathe_w_;
	r_.breathe_phase = breathe_phase_;
}

}  // namespace ffg
