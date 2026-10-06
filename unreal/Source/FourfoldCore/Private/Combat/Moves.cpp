// Fourfold core - port of game/combat/moves.gd (registry loaded from Data/moves.json).
#include "Combat/Moves.h"

#include "Data/GameData.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Sim.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>

namespace ff {
namespace Moves {
namespace {

struct MovesRegistry {
	bool ensured = false;
	Dict defs;
	Dict bindings;
	std::vector<std::string> registered;
	std::vector<std::string> base_ids;
	Dict techniques;
	Dict legacy;          // "e" -> {slot: id}
	Dict def_defaults;
	Dict orig;            // id -> {key: original}
	Dict over;            // id -> {key: value}
};

MovesRegistry& mreg() {
	static MovesRegistry r;
	return r;
}

std::string mv_key(int element, int sub, const std::string& slot) { return itos(element) + "/" + itos(sub) + "/" + slot; }

}  // namespace

void ensure() {
	MovesRegistry& r = mreg();
	if (r.ensured) return;
	r.ensured = true;
	const Dict& data = GameData::moves();
	r.defs = data.get("defs").as_dict().duplicate(true);
	r.bindings = data.get("bindings").as_dict().duplicate(true);
	for (const Value& v : data.get("registered").as_array()) r.registered.push_back(vstr(v));
	for (const Value& v : data.get("base_ids").as_array()) r.base_ids.push_back(vstr(v));
	r.techniques = data.get("techniques").as_dict().duplicate(true);
	r.legacy = data.get("legacy_bindings").as_dict().duplicate(true);
	r.def_defaults = data.get("def_defaults").as_dict().duplicate(true);
	Interactions::ensure();
	Status::ensure();
	Hooks::ensure();
}

Dict& defs() {
	ensure();
	return mreg().defs;
}
Dict& bindings() {
	ensure();
	return mreg().bindings;
}
const std::vector<std::string>& registered() {
	ensure();
	return mreg().registered;
}
const std::vector<std::string>& base_ids() {
	ensure();
	return mreg().base_ids;
}
bool is_base(const std::string& id) {
	const auto& b = base_ids();
	return std::find(b.begin(), b.end(), id) != b.end();
}
const Dict& techniques() {
	ensure();
	return mreg().techniques;
}

std::string legacy_binding(int element, const std::string& slot) {
	ensure();
	return vstr(mreg().legacy.get(itos(element)).get(slot), "");
}

Dict get_def(const std::string& id) { return defs().get(id).as_dict(); }

void register_def(const std::string& id, const Dict& def) {
	ensure();
	MovesRegistry& r = mreg();
	if (is_base(id)) return;   // Godot: push_error (legacy moves are fixed)
	Dict d = def.duplicate(true);
	for (const auto& it : r.def_defaults)
		if (!d.has(it.first)) d.set(it.first, it.second);
	if (!d.has("name")) d.set("name", id);
	d.set("id", id);
	if (std::find(r.registered.begin(), r.registered.end(), id) == r.registered.end()) r.registered.push_back(id);
	r.defs.set(id, d);
	// Re-apply live overrides that target this id.
	const Dict ov = r.over.get(id).as_dict();
	for (const auto& it : ov) {
		Dict o = r.orig.get(id).as_dict();
		o.set(it.first, d.get(it.first));
		d.set(it.first, it.second);
	}
}

void unregister(const std::string& id) {
	ensure();
	MovesRegistry& r = mreg();
	if (is_base(id) || !r.defs.has(id)) return;
	r.defs.erase(id);
	r.registered.erase(std::remove(r.registered.begin(), r.registered.end(), id), r.registered.end());
	for (const std::string& k : r.bindings.keys())
		if (vstr(r.bindings.get(k)) == id) r.bindings.erase(k);
}

void bind(int element, int sub, const std::string& slot, const std::string& id) {
	if (!Sim::is_slot(slot)) return;   // Godot: push_error
	bindings().set(mv_key(element, sub, slot), id);
}

void unbind(int element, int sub, const std::string& slot) {
	bindings().erase(mv_key(element, sub, slot));
	const std::string leg = legacy_binding(element, slot);
	if (sub == 0 && !leg.empty()) bindings().set(mv_key(element, 0, slot), leg);
}

std::string resolve(int element, int sub, const std::string& slot) {
	const Dict& b = bindings();
	const Dict& d = defs();
	const Value& v = b.get(mv_key(element, sub, slot));
	if (v.is_string() && d.has(v.as_string())) return v.as_string();
	const Value& v0 = b.get(mv_key(element, 0, slot));
	if (v0.is_string() && d.has(v0.as_string())) return v0.as_string();
	return legacy_binding(element, slot);
}

std::vector<std::string> list(int element, int sub) {
	std::vector<std::string> out;
	for (const char* slot : Sim::SLOTS) {
		const std::string id = resolve(element, sub, slot);
		if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
	}
	return out;
}

std::string slot_of(int element, int sub, const std::string& id) {
	for (const char* slot : Sim::SLOTS)
		if (resolve(element, sub, slot) == id) return slot;
	return "";
}

void set_override(const std::string& id, const std::string& key, const Value& value) {
	ensure();
	MovesRegistry& r = mreg();
	if (!r.defs.has(id)) return;
	Dict d = r.defs.get(id).as_dict();
	if (!r.orig.has(id)) {
		r.orig.set(id, Dict());
		r.over.set(id, Dict());
	}
	Dict o = r.orig.get(id).as_dict();
	if (!o.has(key)) o.set(key, d.get(key));
	Dict ov = r.over.get(id).as_dict();
	ov.set(key, value);
	d.set(key, value);
}

void clear_overrides() {
	ensure();
	MovesRegistry& r = mreg();
	for (const auto& it : r.orig) {
		if (!r.defs.has(it.first)) continue;
		Dict d = r.defs.get(it.first).as_dict();
		for (const auto& kv : it.second.as_dict()) {
			if (kv.second.is_nil()) d.erase(kv.first);
			else d.set(kv.first, kv.second);
		}
	}
	r.orig.clear();
	r.over.clear();
}

Dict overrides() {
	ensure();
	return mreg().over.duplicate(true);
}

State save_state() {
	ensure();
	State s;
	s.defs = mreg().defs.duplicate(true);
	s.bindings = mreg().bindings.duplicate();
	s.registered = mreg().registered;
	s.orig = mreg().orig.duplicate(true);
	s.over = mreg().over.duplicate(true);
	return s;
}

void load_state(const State& st) {
	ensure();
	mreg().defs = st.defs.duplicate(true);
	mreg().bindings = st.bindings.duplicate();
	mreg().registered = st.registered;
	mreg().orig = st.orig.duplicate(true);
	mreg().over = st.over.duplicate(true);
}

void legacy_bindings_only() {
	ensure();
	Dict& b = mreg().bindings;
	b.clear();
	for (const auto& e : mreg().legacy)
		for (const auto& s : e.second.as_dict()) b.set(e.first + "/0/" + s.first, s.second);
}

}  // namespace Moves
}  // namespace ff
