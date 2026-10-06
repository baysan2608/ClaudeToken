// Fourfold game logic island - game feel policy (see FFGFeel.h).
#include "FFGFeel.h"

namespace ffg {

FeelSpec FeelFor(std::string_view kind) {
	// MOVESET §10.2 as tuned in the Godot build (fx_director.gd FEEL): [hit-stop, shake, kick m, fov deg, haptic]
	if (kind == "t1") return {5, 0.45f, 0.10f, 0.0f, "light"};
	if (kind == "t2") return {7, 0.70f, 0.10f, 0.0f, "heavy"};
	if (kind == "t3") return {9, 1.00f, 0.12f, -3.0f, "heavy"};
	if (kind == "block") return {2, 0.20f, 0.0f, 0.0f, "block"};
	if (kind == "block_heavy") return {4, 0.38f, 0.05f, 0.0f, "block"};
	if (kind == "perfect") return {6, 0.50f, 0.0f, 0.0f, "perfect"};
	if (kind == "clash") return {4, 0.38f, 0.0f, 0.0f, "clash"};
	if (kind == "shatter") return {3, 0.30f, 0.0f, 0.0f, "shatter"};
	if (kind == "transform") return {0, 0.0f, 0.0f, 0.0f, "transform"};
	if (kind == "boom") return {5, 0.90f, 0.0f, 0.0f, "boom"};
	return {3, 0.25f, 0.0f, 0.0f, "light"};   // t0
}

void HitStop::Trim(double now_s) {
	while (!hist_.empty() && now_s - hist_.front() >= 1.0) hist_.pop_front();
}

void HitStop::Request(int frames, double now_s, bool reduced_motion) {
	if (!enabled || frames <= 0) return;
	if (reduced_motion) frames = std::min(frames, 3);
	Trim(now_s);
	const int budget = kCapPerSecond - static_cast<int>(hist_.size());
	pending_ = Clampi(std::max(pending_, frames), 0, std::max(budget, 0));
}

float HitStop::FrameTick(double now_s, float real_dt) {
	Trim(now_s);
	float sc = 1.0f;
	if (pending_ > 0) {
		--pending_;
		hist_.push_back(now_s);
		sc = kScale;
	}
	if (slowmo_ > 0.0f) {
		slowmo_ = std::max(0.0f, slowmo_ - std::max(real_dt, 0.0f));
		sc = std::min(sc, kSlowmoScale);
	}
	return sc;
}

const char* HapticTypeFor(std::string_view kind) {
	if (kind == "heavy" || kind == "boom") return "ImpactHeavy";
	if (kind == "block" || kind == "deflect" || kind == "transform") return "ImpactMedium";
	if (kind == "perfect" || kind == "counter") return "FeedbackSuccess";
	if (kind == "lost_control") return "FeedbackWarning";
	if (kind == "charge") return "SelectionChanged";
	return "ImpactLight";   // light clash shatter zone
}

namespace {

struct FeelCtx {
	const ff::Snapshot& snap;
	const FeelOptions& opt;
	FeelOutput& out;
};

int FeelI(const ff::Value& d, const char* k, int def) {
	const ff::Value& v = d[k];
	return v.is_number() ? static_cast<int>(v.as_int(def)) : def;
}
float FeelF(const ff::Value& d, const char* k, float def) {
	const ff::Value& v = d[k];
	return v.is_number() ? static_cast<float>(v.as_float(def)) : def;
}
const std::string& FeelS(const ff::Value& d, const char* k) { return d[k].as_string(); }
Vec3 FeelV(const ff::Value& d, const char* k, Vec3 def = Vec3()) {
	const ff::Value& v = d[k];
	return v.is_vec3() ? v.as_vec3() : def;
}

Vec3 FeelChest(const FeelCtx& c, int actor) {
	const ff::ActorView* a = c.snap.FindActor(actor);
	return a ? a->pos + Vec3(0.0f, 1.25f, 0.0f) : Vec3();
}
Vec3 FeelBodyPos(const FeelCtx& c, int body) {
	const ff::BodyView* b = c.snap.FindBody(body);
	return b ? b->pos : Vec3();
}

void FeelHaptic(const FeelCtx& c, const char* kind, int actor) {
	if (actor >= 0 && actor == c.opt.player_id) c.out.haptics.push_back(kind);
}

void FeelHitstop(const FeelCtx& c, int frames) { c.out.hitstop = std::max(c.out.hitstop, frames); }

void FeelZoom(const FeelCtx& c, Vec3 at) {
	c.out.zoom = true;
	c.out.zoom_at = at;
}

void FeelFlash(const FeelCtx& c, const char* kind) {
	if (c.opt.flashes > 0.05f) c.out.flash = kind;
}

// One beat of the table: hit-stop, shake with distance falloff, kick along dir, FOV punch, haptic for haptic_actor.
void FeelBeat(const FeelCtx& c, std::string_view kind, Vec3 pos, Vec3 dir, int haptic_actor, float scale = 1.0f) {
	const FeelSpec f = FeelFor(kind);
	FeelHitstop(c, f.hitstop);
	if (f.shake > 0.0f) {
		FeelShake s;
		s.amount = f.shake * Clampf(scale, 0.5f, 2.0f);
		s.pos = pos;
		s.has_pos = true;
		s.decay_s = 0.2f + 0.05f * static_cast<float>(f.hitstop) / 3.0f;
		c.out.shakes.push_back(s);
	}
	if (f.kick > 0.0f && dir.length_squared() > 1e-6f) c.out.kicks.push_back({dir, f.kick});
	if (f.fov != 0.0f) c.out.fov_punch = std::min(c.out.fov_punch, f.fov);
	FeelHaptic(c, f.haptic, haptic_actor);
	if (kind == "transform") FeelZoom(c, pos);
}

void FeelPlainShake(const FeelCtx& c, float amount) {
	if (amount <= 0.0f) return;
	FeelShake s;
	s.amount = amount;
	s.has_pos = false;
	s.decay_s = amount / 3.5f;
	c.out.shakes.push_back(s);
}

void FeelToast(const FeelCtx& c, const std::string& text) {
	if (!text.empty()) c.out.toasts.push_back(text);
}

bool FeelIn(const std::string& s, std::initializer_list<const char*> list) {
	for (const char* x : list)
		if (s == x) return true;
	return false;
}

void FeelInteraction(const FeelCtx& c, const ff::Value& d) {
	const std::string& outcome = FeelS(d, "outcome");
	const Vec3 pos = FeelV(d, "pos");
	const Vec3 dir = FeelV(d, "dir", Vec3(0.0f, 0.0f, -1.0f));
	const bool perfect = d["perfect"].as_bool(false);
	const float tp = FeelF(d, "tp", 0.0f);
	const int ca = FeelI(d, "counter_actor", -1);
	const int ta = FeelI(d, "threat_actor", -1);
	const bool small = FeelS(d, "band") == "partial";
	if (outcome == "block") {
		FeelHitstop(c, tp >= 25.0f ? 4 : 2);
		FeelBeat(c, tp >= 25.0f ? "block_heavy" : "block", pos, dir, ca);
	} else if (FeelIn(outcome, {"deflect", "redirect", "reflect"})) {
		FeelHitstop(c, 3);
		FeelHaptic(c, "deflect", ca);
	} else if (outcome == "reclaim") {
		FeelHaptic(c, "counter", ca);
	} else if (outcome == "transform") {
		if (!small) FeelBeat(c, "transform", pos, Vec3(), ca);
	} else if (outcome == "shatter") {
		FeelHitstop(c, 3);
		FeelBeat(c, "shatter", pos, dir, ca);
	} else if (outcome == "overwhelm") {
		FeelHitstop(c, 4);
	}
	if (perfect) {
		FeelHitstop(c, 6);
		FeelBeat(c, "perfect", pos, dir, ca);
		if (ca == c.opt.player_id || ta == c.opt.player_id) FeelFlash(c, "perfect");
	} else if (!small && ca == c.opt.player_id &&
	           FeelIn(outcome, {"block", "deflect", "redirect", "reflect", "reclaim", "absorb", "capture", "transform", "shatter", "sink",
	                            "ground", "extinguish", "neutralize"})) {
		FeelHaptic(c, outcome != "block" ? "counter" : "block", ca);
	}
}

void FeelFxEvent(const FeelCtx& c, const ff::Value& d) {
	const std::string& fx = FeelS(d, "fx");
	const std::string& mat = FeelS(d, "mat");
	const int actor = FeelI(d, "actor", -1);
	const Vec3 pos = FeelV(d, "pos");
	if (fx == "release" || fx == "cone") {
		FeelHaptic(c, "light", actor);
	} else if (fx == "beam" && mat == "lightning") {
		if (FeelS(d, "shape") == "down") FeelBeat(c, "t3", pos, Vec3(), -1);
		FeelFlash(c, "lightning");
	} else if (fx == "burst") {
		const float radius = FeelF(d, "radius", 1.0f);
		if (mat == "blast" || ((mat == "flame" || mat == "blue") && radius >= 1.2f)) FeelBeat(c, "boom", pos, Vec3(), -1, radius);
	}
}

const char* InsufficientText(const std::string& what) {
	if (what == "focus") return "Not enough Focus";
	if (what == "water") return "No water nearby";
	if (what == "target") return "Nothing to work";
	if (what == "technique") return "Technique not learned";
	if (what == "sight") return "No line of sight";
	if (what == "mass") return "Too heavy";
	return nullptr;
}

}  // namespace

void HandleFeelEvents(const std::vector<ff::Event>& events, const ff::Snapshot& snap, const FeelOptions& opt, FeelOutput& out) {
	const FeelCtx c{snap, opt, out};
	const int pid = opt.player_id;
	for (const ff::Event& e : events) {
		const ff::Value& d = e.data;
		const std::string& t = e.type;
		const int a = FeelI(d, "actor", -1);
		if (t == "hit") {
			const std::string& result = FeelS(d, "result");
			const bool big = result == "knockdown" || FeelF(d, "damage", 0.0f) >= 15.0f;
			FeelHaptic(c, big ? "heavy" : "light", a);
			FeelHaptic(c, "light", FeelI(d, "attacker", -1));
			int tier = FeelI(d, "tier", 0);
			if (result == "knockdown")
				tier = 3;
			else if (big)
				tier = std::max(tier, 1);
			static const char* const kTierKinds[4] = {"t0", "t1", "t2", "t3"};
			FeelBeat(c, kTierKinds[Clampi(tier, 0, 3)], FeelChest(c, a), FeelV(d, "dir"), -1);
		} else if (t == "block") {
			FeelHaptic(c, "block", a);
			const Vec3 p = a >= 0 ? FeelChest(c, a) : FeelBodyPos(c, FeelI(d, "body", -1));
			FeelBeat(c, FeelF(d, "power", 0.0f) >= 25.0f ? "block_heavy" : "block", p, FeelV(d, "dir"), -1);
		} else if (t == "deflect") {
			FeelHaptic(c, "deflect", a);
		} else if (t == "perfect_deflect") {
			FeelHaptic(c, "perfect", a);
			FeelBeat(c, "perfect", FeelChest(c, a), Vec3(), -1);
			if (a == pid) {
				FeelFlash(c, "perfect");
				if (opt.slowmo_assist) out.slowmo = std::max(out.slowmo, 0.22f);
			}
		} else if (t == "evaded") {
			if (a == pid) out.flash = out.flash.empty() ? "evade" : out.flash;
		} else if (t == "intercept") {
			FeelHaptic(c, "light", a);
		} else if (t == "control_fail") {
			if (FeelS(d, "reason") == "mass") {
				FeelHaptic(c, "lost_control", a);
				if (a == pid) FeelToast(c, "Too heavy to control");
			}
		} else if (t == "control_lost") {
			FeelHaptic(c, "lost_control", a);
			if (a == pid) FeelToast(c, "Lost control (" + FeelS(d, "reason") + ")");
		} else if (t == "insufficient") {
			if (a == pid) {
				const char* msg = InsufficientText(FeelS(d, "what"));
				FeelToast(c, msg ? std::string(msg) : FeelS(d, "what"));
			}
		} else if (t == "launch") {
			if (FeelI(d, "tier", 0) >= 3) FeelPlainShake(c, 0.18f);
		} else if (t == "impact") {
			if (FeelF(d, "speed", 0.0f) > 3.0f) FeelPlainShake(c, Clampf(FeelF(d, "mass", 0.0f) / 120.0f, 0.0f, 0.3f));
		} else if (t == "wall") {
			FeelHaptic(c, "light", a);
		} else if (t == "transform") {
			const std::string& to = FeelS(d, "to");
			const Vec3 p = d.has("at") ? FeelV(d, "at") : FeelBodyPos(c, FeelI(d, "body", -1));
			if (to == "molten") {
				const ff::BodyView* b = snap.FindBody(FeelI(d, "body", -1));
				FeelHaptic(c, "transform", b ? b->controller : -1);
				FeelZoom(c, p);
			} else if (to == "rock" || to == "ice") {
				FeelZoom(c, p);
			}
		} else if (t == "shatter") {
			FeelBeat(c, "shatter", FeelBodyPos(c, FeelI(d, "body", -1)), Vec3(), -1);
		} else if (t == "lightning") {
			FeelPlainShake(c, 0.35f);
			FeelFlash(c, "lightning");
		} else if (t == "lightning_redirect") {
			FeelHaptic(c, "perfect", a);
		} else if (t == "reserve_full") {
			if (a == pid) FeelToast(c, "Heat reserve full: vent it");
		} else if (t == "fx") {
			FeelFxEvent(c, d);
		} else if (t == "interaction") {
			FeelInteraction(c, d);
		} else if (t == "charge") {
			if (FeelI(d, "tier", 0) >= 1) FeelHaptic(c, "charge", a);
		} else if (t == "zone") {
			if (FeelS(d, "phase") != "close" && FeelI(d, "owner", -1) == pid) FeelHaptic(c, "zone", pid);
		} else if (t == "clash") {
			FeelHitstop(c, 4);
			FeelBeat(c, "clash", FeelV(d, "pos"), Vec3(), -1);
		} else if (t == "counter_cancel") {
			FeelHitstop(c, 2);
		} else if (t == "slump") {
			FeelBeat(c, "transform", FeelBodyPos(c, FeelI(d, "body", -1)), Vec3(), a);
		} else if (t == "convert") {
			std::string to = FeelS(d, "to");
			for (char& ch : to) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
			if (to == "glass") FeelBeat(c, "transform", FeelBodyPos(c, FeelI(d, "body", -1)), Vec3(), -1);
		} else if (t == "app_toast") {
			FeelToast(c, FeelS(d, "text"));
		}
	}
}

}  // namespace ffg
