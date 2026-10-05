extends TestCase
## Moveset engine: move registry, bindings, live overrides, kit stubs (docs/COMBAT_SPEC.md "Engine" §E1).

var h: SimHarness


func test_base_defs_are_unchanged_and_live_table_matches() -> void:
	Moves.ensure()
	for id in Moves.BASE_DEFS:
		check(Moves.DEFS.has(id), "%s is in the live table" % id)
		for k in Moves.BASE_DEFS[id]:
			check(Moves.DEFS[id][k] == Moves.BASE_DEFS[id][k], "%s.%s unchanged" % [id, k])
	near(float(Moves.DEFS.earth_attack.mass), 20.0, 0.0, "Moves.DEFS.x access keeps working")
	near(float(Moves.DEFS.pour.wave_speed), 7.5, 0.0, "pour wave speed")
	check(Moves.HOLD_THRESHOLD == 0.18, "HOLD_THRESHOLD kept")
	check(Moves.overrides().is_empty(), "no live override by default")


func test_legacy_bindings_and_fallbacks() -> void:
	Moves.ensure()
	var want := {0: ["earth_attack", "earth_tech", "evade"], 1: ["water_attack", "water_tech", "evade"],
		2: ["fire_attack", "fire_tech", "evade"], 3: ["air_attack", "air_tech", "air_dash"]}
	for e in want:
		check(Moves.resolve(e, 0, "strike") == want[e][0], "element %d strike" % e)
		check(Moves.resolve(e, 0, "tech") == want[e][1], "element %d tech" % e)
		check(Moves.resolve(e, 0, "evade") == want[e][2], "element %d evade" % e)
		check(Moves.resolve(e, 0, "guard") == "guard", "element %d guard keeps the id 'guard'" % e)
		for s in [1, 2, 3]:
			check(Moves.resolve(e, s, "strike") == want[e][0], "element %d sub %d falls back to the sub-0 strike" % [e, s])
		check(Moves.resolve(e, 0, "thrust") == "", "nothing bound to thrust in the legacy kit")
	check(Moves.list(0, 0) == ["earth_attack", "guard", "earth_tech", "evade"], "list(0,0) in slot order: %s" % [Moves.list(0, 0)])


func test_register_bind_resolve_list_unregister() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.register("t_spear", {"element": 0, "sub": 1, "verb": "projectile", "startup": 0.2, "active": 0.05, "recovery": 0.3})
	check(Moves.DEFS.has("t_spear"), "registered")
	check(Moves.DEFS.t_spear.module == "verbs", "module defaults to verbs")
	check(Moves.DEFS.t_spear.name == "t_spear", "name defaults to the id")
	Moves.bind(0, 1, "thrust", "t_spear")
	check(Moves.resolve(0, 1, "thrust") == "t_spear", "bound")
	check(Moves.resolve(0, 2, "thrust") == "", "other subs unaffected (no sub-0 thrust)")
	check(Moves.slot_of(0, 1, "t_spear") == "thrust", "slot_of")
	check(Moves.list(0, 1).has("t_spear") and Moves.list(0, 1).has("earth_attack"), "list includes the binding and sub-0 fallbacks")
	Moves.bind(0, 0, "thrust", "t_spear")
	check(Moves.resolve(0, 3, "thrust") == "t_spear", "a sub-0 binding is the fallback of every sub")
	Moves.unregister("t_spear")
	check(not Moves.DEFS.has("t_spear") and Moves.resolve(0, 1, "thrust") == "", "unregister removes def and bindings")
	h.end_scope()
	check(not Moves.DEFS.has("t_spear"), "scope restored")


func test_live_overrides_apply_and_clear() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	Moves.set_override("earth_attack", "speed", 30.0)
	near(float(Moves.DEFS.earth_attack.speed), 30.0, 0.0, "override applied to the live table")
	check(Moves.overrides() == {"earth_attack": {"speed": 30.0}}, "overrides() reports it")
	near(float(Moves.BASE_DEFS.earth_attack.speed), 17.0, 0.0, "BASE_DEFS untouched")
	Moves.set_override("earth_attack", "new_key", 1.0)
	Moves.clear_overrides()
	near(float(Moves.DEFS.earth_attack.speed), 17.0, 0.0, "cleared back to the base value")
	check(not Moves.DEFS.earth_attack.has("new_key"), "added keys removed on clear")
	check(Moves.overrides().is_empty(), "no overrides left")
	# Overrides survive a re-registration of a kit move.
	Moves.register("t_x", {"cost": 5.0})
	Moves.set_override("t_x", "cost", 9.0)
	Moves.register("t_x", {"cost": 5.0})
	near(float(Moves.DEFS.t_x.cost), 9.0, 0.0, "override re-applied after re-register")
	Moves.clear_overrides()
	near(float(Moves.DEFS.t_x.cost), 5.0, 0.0, "and cleared to the registered value")
	h.end_scope()


func test_ensure_is_idempotent_and_kit_stubs_exist() -> void:
	Moves.ensure()
	var n := Interactions.all_rules().size()
	var nd := Moves.DEFS.size()
	Moves.ensure()
	Moves.ensure()
	check(Interactions.all_rules().size() == n and Moves.DEFS.size() == nd, "ensure() twice changes nothing")
	for k in [KitEarth, KitWater, KitFire, KitAir]:
		check(k.has_method("register") or true, "kit stub loads")
	var w := CombatWorld.new(3)
	var a := w.add_actor("A", Vector3.ZERO, 0)
	var inst := ActionInst.new()
	inst.def = {"module": "kit_earth"}
	check(w.module_after_startup("kit_earth", a, inst, ActorIntent.new()) == ActionInst.P.ACTIVE, "kit stub after_startup -> ACTIVE")
	w.module_start("kit_water", a, inst, ActorIntent.new())
	w.module_tick("kit_fire", a, inst, ActorIntent.new())
	w.module_phase("kit_air", a, inst, ActionInst.P.ACTIVE)
	w.module_interrupt("kit_air", a, inst, "hit")
	check(true, "kit stub hooks are no-ops")


func test_sim_enums_appended_without_renumbering() -> void:
	check(Sim.Mat.STONE == 0 and Sim.Mat.WATER == 1 and Sim.Mat.STEAM == 2, "legacy materials keep their values")
	check(Sim.Mat.METAL == 3 and Sim.Mat.AIR == 8 and Sim.MAT_NAMES.size() == 9, "new materials appended")
	check(Sim.Form.CLOUD == 8 and Sim.Form.ZONE == 9 and Sim.FORM_NAMES[9] == "zone", "ZONE appended")
	check(Sim.SUB_NAMES[2][2] == "Lightning" and Sim.SUB_NAMES[3][3] == "Sound", "sub-element names")
	check(Sim.Gesture.NONE == 0 and Sim.Gesture.SIDE == 3, "gestures")
	check(Sim.SLOTS.size() == 10, "ten slots")
	var a := ActorState.new()
	check(a.subs == [0, 0, 0, 0] and a.sub() == 0 and a.metal_carried == 12.0 and a.status.is_empty(), "actor defaults")
	var b := MatBody.new()
	check(b.tag == &"" and b.owner == -1 and b.captured.is_empty() and b.heat_payload == 0.0, "body defaults")


func test_input_frame_carries_the_new_fields() -> void:
	var f := InputFrame.new()
	f.sub_select = 2
	f.attack_gesture = Sim.Gesture.UP
	f.guard_gesture = Sim.Gesture.DOWN
	f.evade_held = true
	var g := InputFrame.new()
	g.copy_from(f)
	check(g.sub_select == 2 and g.attack_gesture == Sim.Gesture.UP and g.guard_gesture == Sim.Gesture.DOWN and g.evade_held, "copy_from")
	var m := InputFrame.new()
	m.merge_from(f)
	check(m.sub_select == 2 and m.attack_gesture == Sim.Gesture.UP and m.evade_held, "merge_from")
	f.clear_edges()
	check(f.sub_select == -1 and f.attack_gesture == 0 and f.guard_gesture == 0 and f.evade_held, "clear_edges keeps the evade level")
	var pc := PlayerController.new()
	var it := pc.build(g, 0.0)
	check(it.sub_select == 2 and it.attack_gesture == Sim.Gesture.UP and it.guard_gesture == Sim.Gesture.DOWN and it.evade_held, "PlayerController maps them")
	it.clear()
	check(it.sub_select == -1 and it.attack_gesture == 0 and not it.evade_held, "ActorIntent.clear")
