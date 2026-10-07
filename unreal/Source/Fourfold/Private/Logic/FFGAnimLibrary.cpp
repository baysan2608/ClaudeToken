// Fourfold game logic island - animation clip library (see FFGAnimLibrary.h).
#include "FFGAnimLibrary.h"

#include "ff/Json.h"
#include "ff/Value.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace ffg {

float ClipDef::ContactTimeN(size_t i) const {
	if (i < contacts.size()) return static_cast<float>(contacts[i]) / Fps();
	return duration * 0.4f;
}

float ClipDef::ContactTime() const { return ContactTimeN(0); }

int ClipDef::PlantedAt(int side, float t) const {
	if (!has_plants || side < 0 || side > 1) return -1;
	const float fr = t * Fps();
	for (const auto& r : plants[static_cast<size_t>(side)])
		if (fr >= static_cast<float>(r.first) && fr < static_cast<float>(r.second)) return 1;
	return 0;
}

void MoveClips::OverrideWith(const MoveClips& o) {
	if (!o.startup.empty()) startup = o.startup;
	if (!o.hold.empty()) hold = o.hold;
	if (!o.release.empty()) release = o.release;
	if (!o.perfect.empty()) perfect = o.perfect;
	if (!o.hand_l.empty()) hand_l = o.hand_l;
	if (!o.hand_r.empty()) hand_r = o.hand_r;
}

AnimLibrary::AnimLibrary() { Reset(); }

void AnimLibrary::Reset() {
	clips.clear();
	clip_fallbacks.clear();
	moves.clear();
	warnings.clear();
	hand_poses.clear();
	for (const char* s : {"fist", "palm", "willow", "tiger", "crane", "sword", "oxtongue", "relaxed", "cup", "spread"})
		hand_poses[s] = std::string("hand_") + s;
	reactions = {{"light", "hit_light_front"}, {"light_back", "hit_light_back"}, {"heavy", "hit_heavy"}, {"knockdown", "knockdown"},
	             {"getup", "getup"},           {"guard_break", "guard_break"},   {"bound", "stagger"},    {"stagger", "stagger"},
	             {"block", "block_impact"},    {"perfect", "deflect"}};
	air = {{"jump", "jump"}, {"fall", "fall"}, {"land", "land"}, {"glide", "glide"}};
	modes = {{"flight", "flight"}, {"hover", "hover"}, {"skate", "skate"}, {"surf", "surf"}};
	slot_fallbacks = {{"strike", "f_jab"},   {"thrust", "a_pierce"}, {"ground", "e_ground_slap"}, {"sweep", "e_sweep"},
	                  {"guard", "e_wall"},   {"push", "w_push"},     {"sink", "e_sink"},          {"tech", "w_hold"},
	                  {"evade", "evade_fwd"}, {"evade_hold", "run"}};
	AddBuiltinAnimDefaults(*this);
	// Stand-ins for clips a partial asset set may lack.
	clip_fallbacks.emplace("guard", "e_guard");
	clip_fallbacks.emplace("hit_light_back", "hit_light_front");
	clip_fallbacks.emplace("guard_break", "stagger");
	clip_fallbacks.emplace("hit_heavy", "hit_light_front");
	clip_fallbacks.emplace("e_stance", "idle");
	clip_fallbacks.emplace("w_stance", "idle");
	clip_fallbacks.emplace("f_stance", "idle");
	clip_fallbacks.emplace("a_stance", "idle");
	clip_fallbacks.emplace("strafe_l", "walk");
	clip_fallbacks.emplace("strafe_r", "walk");
	clip_fallbacks.emplace("walk_back", "walk");
	clip_fallbacks.emplace("e_heave", "e_strike");
	clip_fallbacks.emplace("f_tornado_kick", "f_crescent_kick");
	clip_fallbacks.emplace("w_maelstrom", "w_lash");
	clip_fallbacks.emplace("a_rising_guard", "a_guard");
}

void AnimLibrary::AddDefaultClip(const char* name, int frames, bool loop, std::initializer_list<int> contacts, float speed) {
	ClipDef& c = clips[name];
	c.name = name;
	c.asset = std::string("A_") + name;
	c.frames = frames;
	c.duration = frames > 0 ? static_cast<float>(frames) / fps : 0.0f;
	c.loop = loop;
	c.contacts.assign(contacts.begin(), contacts.end());
	c.speed = speed;
}

namespace {

std::string LibLower(std::string s) {
	for (char& ch : s) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
	return s;
}

void LibReadStr(const ff::Value& v, const char* key, std::string& out) {
	const ff::Value& x = v[key];
	if (x.is_string()) out = x.as_string();
}

void LibReadMoveClips(const ff::Value& v, MoveClips& m) {
	LibReadStr(v, "startup", m.startup);
	LibReadStr(v, "hold", m.hold);
	LibReadStr(v, "release", m.release);
	LibReadStr(v, "perfect", m.perfect);
	const ff::Value& h = v["hands"];
	if (h.is_dict()) {
		LibReadStr(h, "l", m.hand_l);
		LibReadStr(h, "r", m.hand_r);
	}
}

void LibReadPlants(const ff::Value& v, std::vector<std::pair<int, int>>& out) {
	out.clear();
	if (!v.is_array()) return;
	for (const ff::Value& r : v.as_array()) {
		if (!r.is_array() || r.as_array().size() < 2) continue;
		out.emplace_back(static_cast<int>(r.as_array()[0].as_int(0)), static_cast<int>(r.as_array()[1].as_int(0)));
	}
}

}  // namespace

bool AnimLibrary::LoadClipsJson(const std::string& text) {
	ff::Value root;
	ff::JsonError err;
	if (!ff::ParseJson(text, root, &err) || !root.is_dict()) {
		warnings.push_back("clips.json does not parse (line " + std::to_string(err.line) + "): " + err.message);
		return false;
	}
	if (root["fps"].is_number()) fps = std::max(1.0f, static_cast<float>(root["fps"].as_float()));
	LibReadStr(root, "asset_root", asset_root);
	const ff::Value& cl = root["clips"];
	if (cl.is_dict()) {
		for (const auto& it : cl.as_dict()) {
			const ff::Value& v = it.second;
			if (!v.is_dict()) continue;
			ClipDef& c = clips[it.first];
			c.name = it.first;
			if (c.asset.empty()) c.asset = "A_" + it.first;
			LibReadStr(v, "asset", c.asset);
			if (v["frames"].is_number()) c.frames = static_cast<int>(v["frames"].as_int(0));
			if (v["duration"].is_number())
				c.duration = static_cast<float>(v["duration"].as_float());
			else if (c.frames > 0)
				c.duration = static_cast<float>(c.frames) / fps;
			if (v["loop"].is_bool()) c.loop = v["loop"].as_bool();
			if (v["contacts"].is_array()) {
				c.contacts.clear();
				for (const ff::Value& x : v["contacts"].as_array())
					if (x.is_number()) c.contacts.push_back(static_cast<int>(x.as_int(0)));
			}
			if (v.has("contact")) {
				if (v["contact"].is_number()) {
					const int ct = static_cast<int>(v["contact"].as_int(0));
					if (c.contacts.empty() || c.contacts[0] != ct) c.contacts.insert(c.contacts.begin(), ct);
				} else if (v["contact"].is_nil() && !v["contacts"].is_array()) {
					c.contacts.clear();
				}
			}
			if (v["speed"].is_number()) c.speed = static_cast<float>(v["speed"].as_float());
			if (v["phase0"].is_number()) c.phase0 = static_cast<float>(v["phase0"].as_float());
			if (v["cycles"].is_number()) c.cycles = std::max(1, static_cast<int>(v["cycles"].as_int(1)));
			const ff::Value& h = v["hands"];
			if (h.is_dict()) {
				LibReadStr(h, "l", c.hand_l);
				LibReadStr(h, "r", c.hand_r);
			}
			const ff::Value& fp = v["foot_plants"];
			if (fp.is_dict()) {
				LibReadPlants(fp["l"], c.plants[0]);
				LibReadPlants(fp["r"], c.plants[1]);
				c.has_plants = true;
			}
		}
	}
	const ff::Value& hp = root["hand_poses"];
	if (hp.is_dict())
		for (const auto& it : hp.as_dict())
			if (it.second.is_string()) hand_poses[it.first] = it.second.as_string();
	return true;
}

bool AnimLibrary::LoadAnimMapJson(const std::string& text) {
	ff::Value root;
	ff::JsonError err;
	if (!ff::ParseJson(text, root, &err) || !root.is_dict()) {
		warnings.push_back("anim_map.json does not parse (line " + std::to_string(err.line) + "): " + err.message);
		return false;
	}
	const ff::Value& lo = root["locomotion"];
	if (lo.is_dict()) {
		LibReadStr(lo, "idle", idle);
		LibReadStr(lo, "walk", walk);
		LibReadStr(lo, "run", run);
		LibReadStr(lo, "strafe_l", strafe_l);
		LibReadStr(lo, "strafe_r", strafe_r);
		LibReadStr(lo, "back", back);
		const ff::Value& st = lo["stance"];
		if (st.is_array())
			for (size_t i = 0; i < 4 && i < st.as_array().size(); ++i)
				if (st.as_array()[i].is_string()) stance[i] = st.as_array()[i].as_string();
	}
	auto read_map = [](const ff::Value& v, std::map<std::string, std::string>& out) {
		if (!v.is_dict()) return;
		for (const auto& it : v.as_dict())
			if (it.second.is_string()) out[it.first] = it.second.as_string();
	};
	read_map(root["reactions"], reactions);
	read_map(root["air"], air);
	read_map(root["modes"], modes);
	const ff::Value& gd = root["guard"];
	if (gd.is_dict()) {
		LibReadStr(gd, "default", guard_default);
		const ff::Value& be = gd["by_element"];
		if (be.is_array())
			for (size_t i = 0; i < 4 && i < be.as_array().size(); ++i)
				if (be.as_array()[i].is_string()) guard_by_element[i] = be.as_array()[i].as_string();
	}
	const ff::Value& fb = root["fallbacks"];
	if (fb.is_dict()) {
		read_map(fb["slot"], slot_fallbacks);
		read_map(fb["clips"], clip_fallbacks);
	}
	const ff::Value& mv = root["moves"];
	if (mv.is_dict()) {
		for (const auto& it : mv.as_dict()) {
			if (!it.second.is_dict()) continue;
			MoveAnimEntry e;   // a JSON entry replaces the built-in one entirely
			LibReadMoveClips(it.second, e.base);
			const ff::Value& tiers = it.second["tiers"];
			if (tiers.is_dict())
				for (const auto& t : tiers.as_dict()) {
					const int tier = std::atoi(t.first.c_str());
					if (tier >= 0 && tier <= 3 && t.second.is_dict()) LibReadMoveClips(t.second, e.tiers[tier]);
				}
			const ff::Value& md = it.second["modes"];
			if (md.is_dict())
				for (const auto& m : md.as_dict())
					if (m.second.is_dict()) LibReadMoveClips(m.second, e.modes[LibLower(m.first)]);
			moves[it.first] = e;
		}
	}
	return true;
}

const ClipDef* AnimLibrary::Find(const std::string& name) const {
	const auto it = clips.find(name);
	return it == clips.end() ? nullptr : &it->second;
}

const ClipDef* AnimLibrary::Resolve(const std::string& name) const {
	std::string n = name;
	for (int guard = 0; guard < 6 && !n.empty(); ++guard) {
		const ClipDef* c = Find(n);
		if (c && c->available) return c;
		const auto it = clip_fallbacks.find(n);
		if (it == clip_fallbacks.end()) return nullptr;
		n = it->second;
	}
	return nullptr;
}

const ClipDef* AnimLibrary::HandPose(const std::string& shape) const {
	if (shape.empty()) return nullptr;
	const auto it = hand_poses.find(shape);
	return Resolve(it != hand_poses.end() ? it->second : "hand_" + shape);
}

std::string AnimLibrary::StanceFor(int element, bool dummy) const {
	if (dummy) return idle;
	return stance[static_cast<size_t>(Clampi(element, 0, 3))];
}

std::string AnimLibrary::Reaction(const std::string& key) const {
	const auto it = reactions.find(key);
	return it == reactions.end() ? std::string() : it->second;
}

MoveClips AnimLibrary::ResolveMove(const std::string& id, const std::string& guard_spec, int element, ff::Slot slot, int tier,
                                   const std::string& mode, bool earth_wall_up) const {
	const int el = Clampi(element, 0, 3);
	const MoveAnimEntry* e = nullptr;
	bool legacy_earth_guard = false;
	if (id == "guard") {
		auto it = guard_spec.empty() || guard_spec == "guard" ? moves.end() : moves.find(guard_spec);
		if (it == moves.end()) {
			it = moves.find("guard@" + std::to_string(el));
			legacy_earth_guard = el == 0;
		}
		if (it == moves.end()) it = moves.find("guard");
		if (it != moves.end()) e = &it->second;
	} else if (id == "evade") {
		auto it = moves.find("evade");
		if (it == moves.end()) it = moves.find("evade@" + std::to_string(el));
		if (it != moves.end()) e = &it->second;
	} else {
		const auto it = moves.find(id);
		if (it != moves.end()) e = &it->second;
	}
	MoveClips c;
	if (e) {
		c = e->base;
		for (const auto& t : e->tiers)
			if (t.first <= tier) c.OverrideWith(t.second);
		if (!mode.empty()) {
			const auto mt = e->modes.find(LibLower(mode));
			if (mt != e->modes.end()) c.OverrideWith(mt->second);
		}
	}
	if (id == "guard") {
		if (c.hold.empty()) c.hold = guard_by_element[static_cast<size_t>(el)].empty() ? guard_default : guard_by_element[static_cast<size_t>(el)];
		if (legacy_earth_guard && !earth_wall_up && c.startup == "e_wall") c.startup.clear();
		return c;
	}
	if (c.Empty()) {
		const auto it = slot_fallbacks.find(std::string(ff::SlotName(slot)));
		if (it != slot_fallbacks.end()) {
			const ClipDef* fc = Find(it->second);
			if (fc && fc->loop)
				c.hold = it->second;
			else
				c.startup = it->second;
		}
	}
	return c;
}

}  // namespace ffg
