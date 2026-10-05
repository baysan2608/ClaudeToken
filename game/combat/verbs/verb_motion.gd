class_name VerbMotion
extends RefCounted
## Movement verbs (MOVESET §15.7):
##   dash   (evade): distance, iframes, dir (stick aim toward back), up (vertical launch m/s), hidden
##          (concealed while dashing), burrow (i-frames for the whole dash + concealed), trail (zone tag)
##   mode   (evade_hold / tech, sustained while held): kind glide surf skate flight hover burrow run,
##          speed_mult, height (flight/hover), glide_fall, glide_speed, upkeep (Focus/s), status,
##          ground_immune (flight/hover/levitating: ground lines pass under)
##   stance (held or timed): stance, armor (0..1 vs kinetic), anchored (+ anchor_cp: counter power of
##          the anchor cell vs pressure), status, upkeep, held (default true)


static func dash_start(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var mode := String(Charge.param(inst, "dir", "stick"))
	var dir := it.move
	dir.y = 0.0
	match mode:
		"aim":
			dir = inst.data.get("aim", a.forward())
		"toward":
			var t := w.get_actor(a.lock_target)
			dir = (t.pos - a.pos) if t != null else a.forward()
		"back":
			dir = -a.forward()
	dir.y = 0.0
	if dir.length() < 0.2:
		dir = -a.forward() if mode == "stick" else a.forward()
	dir = dir.normalized()
	inst.data["dir"] = dir
	inst.data["controls_motion"] = true
	inst.data["face"] = a.forward()
	var dur := float(Charge.param(inst, "active", inst.def.active))
	a.iframes = maxf(a.iframes, float(Charge.param(inst, "iframes", 0.12)))
	if bool(Charge.param(inst, "burrow", false)):
		a.iframes = maxf(a.iframes, dur)
		Status.apply(w, a, "concealed", dur, 1.0, a.id)
	elif bool(Charge.param(inst, "hidden", false)):
		Status.apply(w, a, "concealed", dur, 1.0, a.id)
	var up := float(Charge.param(inst, "up", 0.0))
	if up > 0.0:
		a.vel.y = up
		a.grounded = false
	var trail := String(Charge.param(inst, "trail", ""))
	if trail != "":
		var z := w.spawn_zone(StringName(trail), a.pos, float(Charge.param(inst, "trail_radius", 0.8)), a.id,
			float(Charge.param(inst, "power", 0.0)), Sim.Mat.AIR, 0.0, float(Charge.param(inst, "trail_life", 1.0)))
		z.props["spare_owner"] = true
	w.emit("evade", {"actor": a.id, "dir": dir, "side": "fwd", "dash": true, "move": inst.id})
	Verbs.fx(w, a, inst, "trail", {"dir": dir, "length": float(Charge.param(inst, "distance", 4.0))})


static func dash_tick(_w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var dur := float(Charge.param(inst, "active", inst.def.active))
	var dist := float(Charge.param(inst, "distance", 4.0))
	var x := clampf(inst.t / maxf(dur, 1e-3), 0.0, 1.0)
	var spd := 2.0 * dist / maxf(dur, 1e-3) * (1.0 - x)
	var dir: Vector3 = inst.data.dir
	a.vel.x = dir.x * spd
	a.vel.z = dir.z * spd
	if inst.t + Sim.DT >= dur:
		inst.data["controls_motion"] = false


static func mode_start(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	var kind := String(Charge.param(inst, "kind", "run"))
	a.stance = kind
	inst.data["move_scale"] = float(Charge.param(inst, "speed_mult", 1.0))
	match kind:
		"flight", "hover":
			a.flying = true
			inst.data["hover_height"] = float(Charge.param(inst, "height", 2.2 if kind == "flight" else 1.5))
			Status.apply(w, a, "levitating", -1.0, 1.0, a.id)
		"glide":
			inst.data["glide_fall"] = float(Charge.param(inst, "glide_fall", 1.6))
			inst.data["glide_speed"] = float(Charge.param(inst, "glide_speed", 6.0))
		"burrow":
			Status.apply(w, a, "concealed", -1.0, 1.0, a.id)
	var st := String(Charge.param(inst, "status", ""))
	if st != "":
		Status.apply(w, a, st, -1.0, float(Charge.param(inst, "status_mag", 1.0)), a.id)
	w.emit("mode", {"actor": a.id, "kind": kind, "on": true, "move": inst.id})
	Verbs.fx(w, a, inst, "aura", {"on": true, "shape": kind})


static func mode_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if it.tech_cancel or not Charge.held(inst, it):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var upkeep := float(Charge.param(inst, "upkeep", 0.0)) * Sim.DT
	if upkeep > 0.0 and not w.spend_focus(a, upkeep):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	if a.stance == "glide" and not a.grounded and a.vel.y < 0.0 and not a.gliding:
		a.gliding = true
		w.emit("glide", {"actor": a.id})


static func mode_end(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if inst.data.get("mode_ended", false):
		return
	inst.data["mode_ended"] = true
	var kind := a.stance
	a.stance = ""
	a.flying = false
	a.gliding = false
	inst.data.erase("hover_height")
	for st in ["levitating", "concealed", String(Charge.param(inst, "status", ""))]:
		if st != "" and a.status.has(st) and int(a.status[st].get("src", -1)) == a.id:
			Status.remove(w, a, st)
	w.emit("mode", {"actor": a.id, "kind": kind, "on": false, "move": inst.id})
	Verbs.fx(w, a, inst, "aura", {"on": false})


static func stance_start(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	a.stance = String(Charge.param(inst, "stance", "stance"))
	a.armor = float(Charge.param(inst, "armor", 0.0))
	if bool(Charge.param(inst, "anchored", false)):
		a.anchored = true
		Status.apply(w, a, "anchored", -1.0, float(Charge.param(inst, "anchor_cp", 30.0)), a.id)
	var st := String(Charge.param(inst, "status", ""))
	if st != "":
		Status.apply(w, a, st, -1.0, float(Charge.param(inst, "status_mag", 1.0)), a.id)
	inst.data["move_scale"] = float(Charge.param(inst, "speed_mult", 0.4))
	w.emit("stance", {"actor": a.id, "stance": a.stance, "on": true})
	Verbs.fx(w, a, inst, "aura", {"on": true, "shape": a.stance})


static func stance_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	if inst.phase == ActionInst.P.CHANNEL and (not Charge.held(inst, it) or it.tech_cancel):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
		return
	var upkeep := float(Charge.param(inst, "upkeep", 0.0)) * Sim.DT
	if upkeep > 0.0 and not w.spend_focus(a, upkeep):
		w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id})
		w.set_phase(a, inst, ActionInst.P.RECOVERY)


static func stance_end(w: CombatWorld, a: ActorState, inst: ActionInst) -> void:
	if inst.data.get("stance_ended", false):
		return
	inst.data["stance_ended"] = true
	var nm := a.stance
	a.stance = ""
	a.armor = 0.0
	a.anchored = false
	for st in ["anchored", String(Charge.param(inst, "status", ""))]:
		if st != "" and a.status.has(st) and int(a.status[st].get("src", -1)) == a.id:
			Status.remove(w, a, st)
	w.emit("stance", {"actor": a.id, "stance": nm, "on": false})
	Verbs.fx(w, a, inst, "aura", {"on": false})
