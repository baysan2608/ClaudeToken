// Port of game/tests/sim/test_core_registry.gd: move registry, bindings, live overrides, kit stubs (COMBAT_SPEC E1).
#include "ff_test.h"
#include "sim_harness.h"

#include "App/PlayerController.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct RegFx : HarnessCase {};
std::vector<std::string> sv(std::initializer_list<const char*> l) {
	std::vector<std::string> v;
	for (const char* s : l) v.push_back(s);
	return v;
}
}  // namespace

FF_TEST_F(test_core_registry, RegFx, test_base_defs_are_unchanged_and_live_table_matches) {
	Moves::ensure_ready();
	for (const std::string& id : Moves::base_ids()) check(Moves::defs().has(id), id + " is in the live table");
	near(dnum(Moves::defs().get("earth_attack").as_dict(), "mass"), 20.0, 0.0, "Moves.DEFS.x access keeps working");
	near(dnum(Moves::defs().get("pour").as_dict(), "wave_speed"), 7.5, 0.0, "pour wave speed");
	check(Moves::HOLD_THRESHOLD == 0.18, "HOLD_THRESHOLD kept");
	check(Moves::overrides().empty(), "no live override by default");
}

FF_TEST_F(test_core_registry, RegFx, test_legacy_bindings_and_fallbacks) {
	SimHarness& h = H(1);
	h.begin_scope();
	h.legacy_bindings_only();
	const char* want[4][3] = {{"earth_attack", "earth_tech", "evade"}, {"water_attack", "water_tech", "evade"},
	                          {"fire_attack", "fire_tech", "evade"}, {"air_attack", "air_tech", "air_dash"}};
	for (int e = 0; e < 4; ++e) {
		check(Moves::resolve(e, 0, "strike") == want[e][0], S("element ", e, " strike"));
		check(Moves::resolve(e, 0, "tech") == want[e][1], S("element ", e, " tech"));
		check(Moves::resolve(e, 0, "evade") == want[e][2], S("element ", e, " evade"));
		check(Moves::resolve(e, 0, "guard") == "guard", S("element ", e, " guard keeps the id 'guard'"));
		for (int s : {1, 2, 3}) check(Moves::resolve(e, s, "strike") == want[e][0], S("element ", e, " sub ", s, " falls back to the sub-0 strike"));
		check(Moves::resolve(e, 0, "thrust").empty(), "nothing bound to thrust in the legacy kit");
	}
	check(Moves::list(0, 0) == sv({"earth_attack", "guard", "earth_tech", "evade"}), "list(0,0) in slot order");
	h.end_scope();
	Moves::ensure_ready();
	for (int e = 0; e < 4; ++e) {
		check(Moves::resolve(e, 0, "strike") == want[e][0], S("element ", e, " sub-0 strike is still the legacy id"));
		for (int s = 0; s < 4; ++s)
			for (const char* slot : Sim::SLOTS) check(!Moves::resolve(e, s, slot).empty(), S("element ", e, " sub ", s, " slot ", slot, " bound"));
	}
}

FF_TEST_F(test_core_registry, RegFx, test_register_bind_resolve_list_unregister) {
	SimHarness& h = H(1);
	h.begin_scope();
	h.legacy_bindings_only();
	Moves::register_def("t_spear", D({{"element", 0}, {"sub", 1}, {"verb", "projectile"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3}}));
	check(Moves::defs().has("t_spear"), "registered");
	check(dstr(Moves::defs().get("t_spear").as_dict(), "module") == "verbs", "module defaults to verbs");
	check(dstr(Moves::defs().get("t_spear").as_dict(), "name") == "t_spear", "name defaults to the id");
	Moves::bind(0, 1, "thrust", "t_spear");
	check(Moves::resolve(0, 1, "thrust") == "t_spear", "bound");
	check(Moves::resolve(0, 2, "thrust").empty(), "other subs unaffected (no sub-0 thrust)");
	check(Moves::slot_of(0, 1, "t_spear") == "thrust", "slot_of");
	const auto l = Moves::list(0, 1);
	check(std::find(l.begin(), l.end(), "t_spear") != l.end() && std::find(l.begin(), l.end(), "earth_attack") != l.end(),
	      "list includes the binding and sub-0 fallbacks");
	Moves::bind(0, 0, "thrust", "t_spear");
	check(Moves::resolve(0, 3, "thrust") == "t_spear", "a sub-0 binding is the fallback of every sub");
	Moves::unregister("t_spear");
	check(!Moves::defs().has("t_spear") && Moves::resolve(0, 1, "thrust").empty(), "unregister removes def and bindings");
	h.end_scope();
	check(!Moves::defs().has("t_spear"), "scope restored");
}

FF_TEST_F(test_core_registry, RegFx, test_live_overrides_apply_and_clear) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::set_override("earth_attack", "speed", 30.0);
	near(dnum(Moves::defs().get("earth_attack").as_dict(), "speed"), 30.0, 0.0, "override applied to the live table");
	check(Value(Moves::overrides()) == Value(D({{"earth_attack", D({{"speed", 30.0}})}})), "overrides() reports it");
	Moves::set_override("earth_attack", "new_key", 1.0);
	Moves::clear_overrides();
	near(dnum(Moves::defs().get("earth_attack").as_dict(), "speed"), 17.0, 0.0, "cleared back to the base value");
	check(!Moves::defs().get("earth_attack").as_dict().has("new_key"), "added keys removed on clear");
	check(Moves::overrides().empty(), "no overrides left");
	Moves::register_def("t_x", D({{"cost", 5.0}}));
	Moves::set_override("t_x", "cost", 9.0);
	Moves::register_def("t_x", D({{"cost", 5.0}}));
	near(dnum(Moves::defs().get("t_x").as_dict(), "cost"), 9.0, 0.0, "override re-applied after re-register");
	Moves::clear_overrides();
	near(dnum(Moves::defs().get("t_x").as_dict(), "cost"), 5.0, 0.0, "and cleared to the registered value");
	h.end_scope();
}

FF_TEST_F(test_core_registry, RegFx, test_ensure_is_idempotent_and_kit_stubs_exist) {
	Moves::ensure_ready();
	const size_t n = Interactions::all_rules().size();
	const size_t nd = Moves::defs().size();
	Moves::ensure_ready();
	Moves::ensure_ready();
	check(Interactions::all_rules().size() == n && Moves::defs().size() == nd, "ensure() twice changes nothing");
	CombatWorld w(3);
	ActorState* a = w.add_actor("A", Vec3(), 0);
	ActionInst inst;
	inst.def = D({{"module", "kit_earth"}});
	const ActorIntent it;
	check(w.module_after_startup("kit_earth", *a, inst, it) == ActionPhase::Active, "kit stub after_startup -> ACTIVE");
	w.module_start("kit_water", *a, inst, it);
	w.module_tick("kit_fire", *a, inst, it);
	w.module_phase("kit_air", *a, inst, ActionPhase::Active);
	w.module_interrupt("kit_air", *a, inst, "hit");
	check(true, "kit stub hooks are no-ops");
}

FF_TEST_F(test_core_registry, RegFx, test_sim_enums_appended_without_renumbering) {
	check(static_cast<int>(Mat::Stone) == 0 && static_cast<int>(Mat::Water) == 1 && static_cast<int>(Mat::Steam) == 2, "legacy materials keep their values");
	check(static_cast<int>(Mat::Metal) == 3 && static_cast<int>(Mat::Air) == 8, "new materials appended");
	check(static_cast<int>(Form::Cloud) == 8 && static_cast<int>(Form::Zone) == 9 && std::string(kFormNames[9]) == "zone", "ZONE appended");
	check(SubName(2, 2) == "Lightning" && SubName(3, 3) == "Sound", "sub-element names");
	check(static_cast<int>(Gesture::None) == 0 && static_cast<int>(Gesture::Side) == 3, "gestures");
	check(sizeof(Sim::SLOTS) / sizeof(Sim::SLOTS[0]) == 10, "ten slots");
	ActorState a;
	check(a.subs == std::array<int, 4>{{0, 0, 0, 0}} && a.sub() == 0 && a.metal_carried == 12.0 && a.status.empty(), "actor defaults");
	MatBody b;
	check(b.tag.empty() && b.owner == -1 && b.captured.empty() && b.heat_payload == 0.0, "body defaults");
}

FF_TEST_F(test_core_registry, RegFx, test_input_frame_carries_the_new_fields) {
	InputFrame f;
	f.sub_select = 2;
	f.attack_gesture = Gesture::Up;
	f.guard_gesture = Gesture::Down;
	f.evade_held = true;
	const InputFrame g = f;
	check(g.sub_select == 2 && g.attack_gesture == Gesture::Up && g.guard_gesture == Gesture::Down && g.evade_held, "copy_from");
	InputFrame m;
	m.MergeFrom(f);
	check(m.sub_select == 2 && m.attack_gesture == Gesture::Up && m.evade_held, "merge_from");
	f.ClearEdges();
	check(f.sub_select == -1 && f.attack_gesture == Gesture::None && f.guard_gesture == Gesture::None && f.evade_held, "clear_edges keeps the evade level");
	PlayerController pc;
	ActorIntent it = pc.build(g, 0.0);
	check(it.sub_select == 2 && it.attack_gesture == 1 && it.guard_gesture == 2 && it.evade_held, "PlayerController maps them");
	it.clear();
	check(it.sub_select == -1 && it.attack_gesture == 0 && !it.evade_held, "ActorIntent.clear");
}
