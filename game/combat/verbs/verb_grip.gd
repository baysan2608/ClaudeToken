class_name VerbGrip
extends RefCounted
## Verb "grip" (technique): seize / draw a body with a control contest (CombatWorld.request_grip),
## hold and aim it, shape it with T+A, release = throw (MOVESET §15.7). Legality of a target goes
## through the engine: Interactions.allows(body, ccls).
## Params: ccls (grip class: grip_stone grip_metal grip_sand grip_magma grip_water grip_ice grip_vapor
## grip_vine grip_wind), reach, cone (deg), base (grip strength), grip_mult (x vs its material),
## max_mass, rip_time, rip_source (ground metal_plate waterskin moisture none), rip_mat, rip_mass,
## rip_cost, speed, damage, balance, gravity, shape (split freeze compress cool condense retag),
## pieces, spread, shape_tag, shape_cost, upkeep, mode_label.
## A released body keeps cohesion 0.6 + 0.1·tier against the next grip (Interactions.cohesion).

const PLATE_REACH := 3.0
const PLATE_RIP := 10.0


static func _filter(a: ActorState, d: Dictionary) -> Callable:
	var ccls := StringName(String(d.get("ccls", "grip_stone")))
	return func(b: MatBody) -> bool:
		return b.controller != a.id and b.form != Sim.Form.WALL and b.form != Sim.Form.POOL and b.form != Sim.Form.ZONE \
			and b.captured_by < 0 and Interactions.allows(b, ccls)


static func preview(w: CombatWorld, a: ActorState, d: Dictionary, dir: Vector3) -> Dictionary:
	var label := String(d.get("mode_label", "GRIP"))
	var b := w.find_body(a, dir, float(d.get("reach", 7.5)), float(d.get("cone", 60.0)), _filter(a, d))
	if b != null:
		if b.mass > float(d.get("max_mass", a.max_control_mass)):
			return {"mode": label, "body": b.id, "ok": true, "reason": "mass"}
		return {"mode": label, "body": b.id, "ok": true, "reason": ""}
	if String(d.get("rip_source", "none")) != "none":
		return {"mode": "RIP", "body": -1, "ok": true, "reason": ""}
	return {"mode": label, "body": -1, "ok": false, "reason": "target"}


static func tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var d := Charge.pdef(inst)
	if it.tech_cancel:
		drop(w, a, inst)
		w.emit("cancel", {"actor": a.id, "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["aim"] = w.aim_dir(a, it)
	inst.data["aim_active"] = it.aim_active
	inst.data["face"] = inst.data.aim
	inst.data["aim_point"] = w.aim_point(a, it)
	var upkeep := float(Charge.param(inst, "upkeep", 0.0)) * Sim.DT
	if upkeep > 0.0 and w.held(a) != null and not w.spend_focus(a, upkeep):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		drop(w, a, inst)
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var b := w.held(a)
	if b == null:
		_seek(w, a, inst, d)
		if a.action == inst and inst.phase == ActionInst.P.CHANNEL and not Charge.held(inst, it) and w.held(a) == null:
			w.emit("whiff", {"actor": a.id, "move": inst.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var dir: Vector3 = inst.data.aim
	b.hold_point = a.pos + Vector3(0, 1.2, 0) + a.forward() * (0.3 + b.radius) + dir * 0.15
	if it.attack_pressed and not inst.data.get("shaped", false):
		shape(w, a, inst, b)
	if not Charge.held(inst, it):
		w.set_phase(a, inst, ActionInst.P.ACTIVE)
		throw(w, a, inst)


static func _seek(w: CombatWorld, a: ActorState, inst: ActionInst, d: Dictionary) -> void:
	var reach := float(Charge.param(inst, "reach", 7.5))
	var tb := w.get_body(int(inst.data.get("target", -1)))
	var f := _filter(a, d)
	if tb == null or not tb.alive or not f.call(tb):
		tb = w.find_body(a, inst.data.get("aim", a.forward()), reach, float(Charge.param(inst, "cone", 60.0)), f)
		if tb != null:
			inst.data["target"] = tb.id
			w.emit("target_body", {"actor": a.id, "body": tb.id})
	if tb != null:
		if tb.mass > minf(a.max_control_mass, float(Charge.param(inst, "max_mass", a.max_control_mass))):
			if tb.mass > a.max_control_mass:
				w.request_grip(a, tb, 0.0, "grip")   # emits control_fail (mass)
			else:
				w.emit("control_fail", {"actor": a.id, "body": tb.id, "reason": "mass", "mass": tb.mass})
			w.emit("whiff", {"actor": a.id, "move": inst.id, "body": tb.id})
			w.set_phase(a, inst, ActionInst.P.RECOVERY)
			return
		var s := w.grip_strength(a, tb, float(Charge.param(inst, "base", 0.85)), reach) * float(Charge.param(inst, "grip_mult", 1.0))
		w.request_grip(a, tb, s, String(Charge.param(inst, "verb_name", "grip")))
		return
	var src := String(Charge.param(inst, "rip_source", "none"))
	if src == "none" or inst.data.get("ripped", false) or inst.t < float(Charge.param(inst, "rip_time", 0.28)):
		return
	if src == "metal_plate":
		var mx := clampf(a.pos.x, w.arena.metal_min.x, w.arena.metal_max.x)
		var mz := clampf(a.pos.z, w.arena.metal_min.y, w.arena.metal_max.y)
		if Vector2(mx - a.pos.x, mz - a.pos.z).length() > PLATE_REACH:
			src = "metal"   # away from the plate: the satchel
	if not w.spend_focus(a, float(Charge.param(inst, "rip_cost", 0.0))):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	inst.data["ripped"] = true
	var mat := Verbs.mat_id(Charge.param(inst, "rip_mat", VerbProjectile._source_mat(src if src != "metal_plate" else "metal")))
	var mass := float(Charge.param(inst, "rip_mass", PLATE_RIP if src == "metal_plate" else 20.0))
	var b: MatBody = null
	if src == "metal_plate":
		w.mass_ledger.metal_taken += mass
		b = w.spawn_body(Sim.Mat.METAL, Sim.Form.CHUNK, mass, a.pos + a.forward() * 0.8, "plate")
	else:
		var P := func(k: String, dflt: Variant) -> Variant:
			return Charge.param(inst, k, dflt)
		b = VerbProjectile.spawn(w, a, inst, src, mat, mass, 0.0, P)
	if b == null:
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	b.max_life = Sim.REMNANT_LIFETIME
	w.take_control(a, b, 0.9, "rip")
	w.emit("rip", {"actor": a.id, "body": b.id, "source": src})


## T+A: shapes the held body once (split / freeze / compress / cool / condense / retag).
static func shape(w: CombatWorld, a: ActorState, inst: ActionInst, b: MatBody) -> void:
	var kind := String(Charge.param(inst, "shape", ""))
	if kind == "" or not w.spend_focus(a, float(Charge.param(inst, "shape_cost", 3.0))):
		return
	inst.data["shaped"] = true
	match kind:
		"split":
			inst.data["split"] = int(Charge.param(inst, "pieces", 3))
		"freeze":
			if b.is_water():
				var e0 := b.thermal_energy()
				b.liquid = 0.0
				b.temp = minf(b.temp, -5.0)
				b.phase = Sim.Phase.FROZEN
				w.ledger.freeze_dump += b.thermal_energy() - e0
		"compress":
			if b.mat == Sim.Mat.SAND:
				w.convert_mat(b, Sim.Mat.STONE, "sand_to_sandstone")
				b.tag = &"sandstone"
		"cool":
			var e := maxf(0.0, b.thermal_energy() - b.heat_payload)
			var got := -Thermal.heat(b, -e)
			w.ledger.removed += got   # dumped into the ground (Cool & Set)
		"condense":
			if b.mat == Sim.Mat.STEAM or (b.is_water() and b.form == Sim.Form.CLOUD):
				var e1 := b.thermal_energy()
				b.mat = Sim.Mat.WATER
				b.form = Sim.Form.BLOB
				b.phase = Sim.Phase.LIQUID
				b.liquid = 1.0
				b.temp = Sim.AMBIENT_C
				w.ledger.removed += e1 - b.thermal_energy()
				b.max_life = -1.0
				b.update_radius()
		"retag":
			b.tag = StringName(String(Charge.param(inst, "shape_tag", String(b.tag))))
	w.emit("shape", {"actor": a.id, "body": b.id, "shape": kind})
	Verbs.fx(w, a, inst, "cast", {"body": b.id, "shape": kind})


## Release: the held body (or its split pieces) is thrown at the target as the action's attack.
static func throw(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var b := w.held(a)
	if b == null:
		return
	var n := int(inst.data.get("split", 1))
	var mass := b.mass / float(n)
	var target := Verbs.target_point(w, a, inst)
	var dir: Vector3 = inst.data.get("aim", a.forward())
	var spread := deg_to_rad(float(Charge.param(inst, "spread", 30.0)))
	var speed := float(Charge.param(inst, "speed", 18.0))
	if not inst.data.has("split"):
		speed /= maxf(1.0, sqrt(b.mass / Sim.STONE_SHOT_MASS) * 0.8)
	a.held_body = -1
	b.controller = -1
	var gs := float(Charge.param(inst, "gravity", 1.0))
	for k in n:
		var p := b if k == n - 1 else w.split_body(b, mass, b.pos)
		var ang := 0.0 if n == 1 else lerpf(-spread * 0.5, spread * 0.5, float(k) / float(n - 1))
		var to := a.chest() + (target - a.chest()).rotated(Vector3.UP, ang)
		p.vel = Verbs.launch_vel(p.pos, to, speed, gs)
		p.gravity_scale = gs
		p.on_ground = false
		Verbs.arm(w, a, inst, p, float(Charge.param(inst, "damage", 13.0)) * sqrt(p.mass / Sim.STONE_SHOT_MASS),
			float(Charge.param(inst, "balance", 26.0)) * sqrt(p.mass / Sim.STONE_SHOT_MASS))
		w.emit("launch", {"actor": a.id, "body": p.id, "speed": speed, "kind": "grip"})
		Verbs.fx(w, a, inst, "release", {"body": p.id, "dir": dir})


static func drop(w: CombatWorld, a: ActorState, _inst: ActionInst) -> void:
	if w.held(a) != null:
		w.release_body(a, Vector3(0, -1.0, 0), false)
