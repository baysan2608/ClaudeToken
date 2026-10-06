// Fourfold core - hook registry (see Hooks.h).
#include "Sim/Hooks.h"

#include "Data/GameData.h"
#include "Util/GdUtil.h"

#include <set>

namespace ff {
namespace Hooks {
namespace {

struct HookRuntime {
	bool ready = false;
	std::map<std::string, BodyTickFn> body_ticks;
	std::map<std::string, ZoneEffectFn> zone_effects;
	std::map<std::string, TechPreviewFn> tech_previews;
};

HookRuntime& hrt() {
	static HookRuntime r;
	return r;
}

template <typename M>
bool hk_has(const M& m, const std::string& n) {
	return m.find(n) != m.end();
}

}  // namespace

const HookTable& table() {
	static const HookTable t = [] {
		HookTable h;
		RegisterCoreHooks(h);
		RegisterEarthHooks(h);
		RegisterWaterHooks(h);
		RegisterFireHooks(h);
		RegisterAirHooks(h);
		return h;
	}();
	return t;
}

void ensure() {
	HookRuntime& r = hrt();
	if (r.ready) return;
	r.ready = true;
	const HookTable& t = table();
	const Dict& data = GameData::hooks();
	for (const auto& it : data.get("body_ticks").as_dict()) {
		auto f = t.body_tick.find(GameData::fn_name(it.second));
		if (f != t.body_tick.end()) r.body_ticks[it.first] = f->second;
	}
	for (const auto& it : data.get("zone_effects").as_dict()) {
		auto f = t.zone.find(GameData::fn_name(it.second));
		if (f != t.zone.end()) r.zone_effects[it.first] = f->second;
	}
	for (const auto& it : data.get("tech_previews").as_dict()) {
		auto f = t.preview.find(GameData::fn_name(it.second));
		if (f != t.preview.end()) r.tech_previews[it.first] = f->second;
	}
}

std::map<std::string, BodyTickFn>& body_ticks() {
	ensure();
	return hrt().body_ticks;
}
std::map<std::string, ZoneEffectFn>& zone_effects() {
	ensure();
	return hrt().zone_effects;
}
std::map<std::string, TechPreviewFn>& tech_previews() {
	ensure();
	return hrt().tech_previews;
}

void register_body_tick(const std::string& tag, BodyTickFn cb) { body_ticks()[tag] = std::move(cb); }
void register_zone_effect(const std::string& tag, ZoneEffectFn cb) { zone_effects()[tag] = std::move(cb); }
void register_tech_preview(int element, int sub, TechPreviewFn cb) { tech_previews()[itos(element) + "/" + itos(sub)] = std::move(cb); }

void unregister_hooks(const std::vector<std::string>& tags) {
	for (const std::string& t : tags) {
		body_ticks().erase(t);
		zone_effects().erase(t);
	}
}

ExecHook exec_of(const Value& v) {
	if (!v.is_string()) return nullptr;
	const auto& m = table().exec;
	auto it = m.find(GameData::fn_name(v));
	return it == m.end() ? nullptr : it->second;
}
TickHook tick_of(const Value& v) {
	if (!v.is_string()) return nullptr;
	const auto& m = table().tick;
	auto it = m.find(GameData::fn_name(v));
	return it == m.end() ? nullptr : it->second;
}
ImpactHook impact_of(const Value& v) {
	if (!v.is_string()) return nullptr;
	const auto& m = table().impact;
	auto it = m.find(GameData::fn_name(v));
	return it == m.end() ? nullptr : it->second;
}
CounterScaleHook counter_scale_of(const Value& v) {
	if (!v.is_string()) return nullptr;
	const auto& m = table().counter_scale;
	auto it = m.find(GameData::fn_name(v));
	return it == m.end() ? nullptr : it->second;
}

std::vector<std::pair<std::string, std::string>> referenced() {
	std::vector<std::pair<std::string, std::string>> out;
	std::set<std::pair<std::string, std::string>> seen;
	auto add = [&](const std::string& n, const std::string& kind) {
		if (n.empty()) return;
		if (seen.insert({n, kind}).second) out.emplace_back(n, kind);
	};
	for (const auto& it : GameData::moves().get("defs").as_dict()) {
		const Value& d = it.second;
		add(GameData::fn_name(d.get("hook_execute")), "hook_execute");
		add(GameData::fn_name(d.get("hook_tick")), "hook_tick");
		add(GameData::fn_name(d.get("hook_impact")), "hook_impact");
		add(GameData::fn_name(d.get("counter_scale")), "counter_scale");
	}
	const Dict& rules = GameData::rules();
	for (const auto& it : rules.get("outcome_handlers").as_dict()) add(GameData::fn_name(it.second), "outcome");
	for (const auto& it : rules.get("channel_hooks").as_dict()) add(GameData::fn_name(it.second), "channel");
	const Dict& hooks = GameData::hooks();
	for (const auto& it : hooks.get("body_ticks").as_dict()) add(GameData::fn_name(it.second), "body_tick");
	for (const auto& it : hooks.get("zone_effects").as_dict()) add(GameData::fn_name(it.second), "zone_effect");
	for (const auto& it : hooks.get("tech_previews").as_dict()) add(GameData::fn_name(it.second), "tech_preview");
	return out;
}

std::vector<std::pair<std::string, std::string>> unresolved() {
	const HookTable& t = table();
	std::vector<std::pair<std::string, std::string>> out;
	for (const auto& p : referenced()) {
		const std::string& n = p.first;
		const std::string& k = p.second;
		bool ok = false;
		if (k == "hook_execute") ok = hk_has(t.exec, n);
		else if (k == "hook_tick") ok = hk_has(t.tick, n);
		else if (k == "hook_impact") ok = hk_has(t.impact, n);
		else if (k == "counter_scale") ok = hk_has(t.counter_scale, n);
		else if (k == "outcome") ok = hk_has(t.outcome, n);
		else if (k == "channel") ok = hk_has(t.channel, n);
		else if (k == "body_tick") ok = hk_has(t.body_tick, n);
		else if (k == "zone_effect") ok = hk_has(t.zone, n);
		else if (k == "tech_preview") ok = hk_has(t.preview, n);
		if (!ok) out.push_back(p);
	}
	return out;
}

}  // namespace Hooks
}  // namespace ff
