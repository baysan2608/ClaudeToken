// Fourfold core - port of game/ui/lab/lab_session.gd.
#include "Lab/LabSession.h"

#include "Lab/SpawnCatalog.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

namespace ff {

bool LabSession::consume_step() {
	if (_steps > 0) {
		--_steps;
		return true;
	}
	return false;
}

void LabSession::set_time_scale(double v) { time_scale = clampf(v, 0.1, 1.0); }

void LabSession::pre_step(CombatWorld& w) const {
	if (!infinite) return;
	for (auto& a : w.actors)
		if (!a->is_dummy) SpawnCatalog::top_up(w, *a);
}

void LabSession::post_step(ActorState* player) const {
	if (god && player != nullptr) {
		player->health = Sim::HEALTH_MAX;
		player->balance = Sim::BALANCE_MAX;
	}
}

std::vector<int> LabSession::ai_elements() const {
	if (ai_kit == "earth") return {0};
	if (ai_kit == "water") return {1};
	if (ai_kit == "fire") return {2};
	if (ai_kit == "air") return {3};
	if (ai_kit == "all") return {0, 1, 2, 3};
	return {};
}

Dict LabSession::ai_config() const {
	Dict cfg = D({{"preset", ai_preset}, {"drill", ai_drill}});
	const std::vector<int> els = ai_elements();
	if (!els.empty()) {
		Array a;
		for (int e : els) a.append(e);
		cfg.set("elements", a);
		if (ai_sub >= 0 && els.size() == 1) cfg.set("subs", D({{itos(els[0]), A({ai_sub})}}));
	}
	return cfg;
}

Dict LabSession::legacy_cfg(const std::string& preset) {
	if (preset == "novice") return D({{"aggression", 0.35}, {"counter", 0.3}, {"reaction", 0.45}});
	if (preset == "master") return D({{"aggression", 0.75}, {"counter", 0.9}, {"reaction", 0.2}});
	return D({{"aggression", 0.55}, {"counter", 0.65}, {"reaction", 0.3}});
}

Dict LabSession::to_dict() const {
	return D({{"time_scale", time_scale}, {"frozen", frozen}, {"infinite", infinite}, {"god", god}, {"overlay", overlay}, {"ai_enabled", ai_enabled},
	          {"ai_preset", ai_preset}, {"ai_kit", ai_kit}, {"ai_sub", ai_sub}, {"ai_drill", ai_drill}});
}

void LabSession::from_dict(const Dict& d) {
	time_scale = clampf(dnum(d, "time_scale", 1.0), 0.1, 1.0);
	frozen = dbool(d, "frozen", false);
	infinite = dbool(d, "infinite", false);
	god = dbool(d, "god", false);
	overlay = dbool(d, "overlay", false);
	ai_enabled = dbool(d, "ai_enabled", true);
	ai_preset = dstr(d, "ai_preset", "adept");
	ai_kit = dstr(d, "ai_kit", "mixed");
	ai_sub = dint(d, "ai_sub", -1);
	ai_drill = dstr(d, "ai_drill", "");
}

LabState LabSession::to_state() const {
	LabState s;
	s.time_scale = static_cast<float>(time_scale);
	s.frozen = frozen;
	s.infinite = infinite;
	s.god = god;
	s.overlay = overlay;
	s.ai_enabled = ai_enabled;
	s.ai_preset = ai_preset;
	s.ai_kit = ai_kit;
	s.ai_sub = ai_sub;
	s.ai_drill = ai_drill;
	return s;
}

void LabSession::from_state(const LabState& s) {
	set_time_scale(s.time_scale);
	frozen = s.frozen;
	infinite = s.infinite;
	god = s.god;
	overlay = s.overlay;
	ai_enabled = s.ai_enabled;
	ai_preset = s.ai_preset;
	ai_kit = s.ai_kit;
	ai_sub = s.ai_sub;
	ai_drill = s.ai_drill;
}

}  // namespace ff
