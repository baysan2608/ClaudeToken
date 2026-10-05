extends TestCase
## Moveset engine: ledgers stay exact with the new materials (docs/MOVESET.md §15.9; COMBAT_SPEC
## "Engine" §E5): sand <-> glass, metal satchel <-> field <-> plate, water <-> plant, burning plant,
## fire payloads, ambient moisture.

var h: SimHarness
var base := {}


func _snap() -> Dictionary:
	var w := h.w
	return {"energy": w.system_energy() - w.ledger_balance(), "water": w.water_mass(), "earth": w.earth_mass(),
		"metal": w.metal_mass(), "plant": w.plant_mass()}


func _check_ledgers(label: String) -> void:
	var s := _snap()
	near(s.energy, base.energy, 1e-5, "%s: energy ledger" % label)
	near(s.water, base.water, 1e-6, "%s: water mass" % label)
	near(s.earth, base.earth, 1e-6, "%s: earth mass (stone + sand + glass)" % label)
	near(s.metal, base.metal, 1e-6, "%s: metal mass (field + satchels)" % label)
	near(s.plant, base.plant, 1e-6, "%s: plant mass" % label)


func test_scripted_exchange_with_new_materials() -> void:
	h = SimHarness.new(4)
	h.begin_scope()
	Moves.register("t_slug", {"element": 0, "verb": "projectile", "startup": 0.15, "active": 0.05, "recovery": 0.2, "cost": 5.0,
		"source": "ground", "mat": "sand", "mass": 5.0, "speed": 22.0, "on_impact": "shatter"})
	Moves.register("t_disc", {"element": 0, "verb": "projectile", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 5.0,
		"source": "metal", "mat": "metal", "mass": 2.0, "count": 2, "speed": 24.0, "homing": 10.0, "tag": "disc"})
	Moves.register("t_ball", {"element": 2, "verb": "projectile", "startup": 0.12, "active": 0.05, "recovery": 0.2, "heat": 120.0,
		"source": "heat", "mat": "fire", "mass": 1.0, "speed": 18.0, "tag": "fireball", "on_impact": "burst", "impact_radius": 2.0,
		"gravity": 0.3})
	Moves.register("t_icewall", {"element": 1, "verb": "barrier", "barrier": "wall", "mat": "water", "tag": "ice", "source": "moisture",
		"mass": 50.0, "cost": 8.0, "tiers": {"t1": {"mass": 65.0}}})
	Moves.register("t_rip", {"element": 0, "verb": "grip", "startup": 0.1, "active": 0.06, "recovery": 0.2, "ccls": "grip_metal",
		"rip_source": "metal_plate", "rip_time": 0.1, "speed": 16.0})
	Moves.bind(0, 2, "strike", "t_slug")
	Moves.bind(0, 1, "strike", "t_disc")
	Moves.bind(0, 1, "tech", "t_rip")
	Moves.bind(2, 1, "thrust", "t_ball")
	Moves.bind(1, 1, "guard", "t_icewall")
	var e := h.actor("E", Vector3(-8, 0, 1), 0, {}, Sim.Element.EARTH)
	var f := h.actor("F", Vector3(-3, 0, 6), 1, {}, Sim.Element.FIRE)
	var wa := h.actor("W", Vector3(4, 0, -4), 1, {}, Sim.Element.WATER)
	h.step(10)
	base = _snap()
	# Sand slug and metal discs (satchel).
	h.sub(e, 2)
	h.step()
	h.hold(e, "attack", 3)
	h.step(40)
	_check_ledgers("sand slug")
	h.sub(e, 1)
	h.step()
	h.hold(e, "attack", 3)
	h.step(40)
	near(e.metal_carried, 8.0, 1e-9, "two 2 kg discs left the satchel")
	_check_ledgers("discs")
	# Rip scrap from the arena plate (E stands next to it).
	h.press(e, "tech")
	h.step(20)
	check(h.w.mass_ledger.metal_taken == 10.0, "10 kg ripped from the plate")
	h.release(e, "tech")
	h.step(30)
	_check_ledgers("plate scrap")
	# Fireball (heat paid -> payload -> burst / fades).
	h.sub(f, 1)
	h.step()
	h.flick(f, "attack", Sim.Gesture.UP)
	h.step()
	h.release(f, "attack")
	h.step(90)
	_check_ledgers("fireball")
	# Ice wall from ambient moisture, held past T1, released.
	h.sub(wa, 1)
	h.step()
	h.press(wa, "guard")
	h.step(40)
	var iw := h.w.get_body(wa.wall_body)
	check(iw != null and iw.mat == Sim.Mat.WATER and iw.phase == Sim.Phase.FROZEN and iw.mass == 65.0, "65 kg ice wall")
	check(h.w.mass_ledger.moisture_taken == 65.0, "booked as moisture_taken")
	_check_ledgers("ice wall")
	h.release(wa, "guard")
	h.step(40)
	_check_ledgers("ice wall sank")
	# Sand fused by heat sets as glass.
	var sand := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.CHUNK, 10.0, Vector3(2, 0.3, 8), "test")
	h.w.mass_ledger.ground_taken += 10.0
	h.w.ledger.generated += h.w.heat_body(sand, 10.0 * (0.01 * 1180.0 + 10.0) + 20.0)
	h.step()
	check(sand.phase == Sim.Phase.MOLTEN, "sand fused")
	h.w.ledger.ambient += Thermal.heat(sand, -sand.thermal_energy() + 5.0)
	h.step()
	check(sand.mat == Sim.Mat.GLASS and h.w.mass_ledger.sand_to_glass == 10.0, "it set as glass")
	_check_ledgers("glass")
	# Water -> plant, then the vine burns away.
	var vine := h.w.grow_plant(null, 3.0, Vector3(1, 0.3, 8), wa)
	check(vine != null and vine.mass == 3.0 and h.w.mass_ledger.water_to_plant == 3.0, "3 kg of waterskin grew a vine")
	_check_ledgers("plant grown")
	h.w.ledger.generated += h.w.heat_body(vine, 3.0 * 0.04 * 570.0)
	h.step(150)
	check(not vine.alive and h.w.mass_ledger.burned > 2.99, "the vine burned away (%.2f kg)" % h.w.mass_ledger.burned)
	_check_ledgers("plant burned")
	h.step(400)
	_check_ledgers("end")
	h.end_scope()


func test_convert_mat_keeps_energy() -> void:
	h = SimHarness.new(1)
	var b := h.w.spawn_body(Sim.Mat.SAND, Sim.Form.CHUNK, 8.0, Vector3.ZERO, "test", 400.0)
	var e := b.thermal_energy()
	h.w.convert_mat(b, Sim.Mat.STONE, "sand_to_sandstone")
	near(b.thermal_energy(), e, 1e-9, "sand -> sandstone keeps its heat")
	h.w.convert_mat(b, Sim.Mat.METAL)
	near(b.thermal_energy(), e, 1e-9, "any conversion keeps its heat")
	var fb := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, Vector3.ZERO, "test")
	fb.heat_payload = 100.0
	near(fb.thermal_energy(), 100.0, 1e-9, "fire payload is thermal energy")
	var e0 := h.w.system_energy() - h.w.ledger_balance()
	h.step(30)
	check(fb.heat_payload < 100.0, "payload fades")
	near(h.w.system_energy() - h.w.ledger_balance(), e0, 1e-6, "into the ambient ledger")
