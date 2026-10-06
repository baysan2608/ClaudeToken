class_name Outcomes
extends RefCounted
## Outcome handlers of the counter rule (docs/MOVESET.md §15.3; COMBAT_SPEC "Engine" §E4.3).
## Every handler moves heat and mass only through the CombatWorld ledger helpers (heat_body,
## boil_water, split_body, merge_bodies, decay_body, convert_mat). Signature:
##   handler(w, threat: Agent, counter: Agent, res: Dictionary, rule: Dictionary, ctx: Dictionary) -> bool
## false = declined (the rule's `fallback` outcome runs instead). res fields a site reads back:
##   result (actor guard: "block" / "guard_break" / "perfect" / "deflect" / "redirected" / ""),
##   stopped, pass_scale (fraction of the threat that continues), knock_scale, counter_broken,
##   heat_used (HU a volume spent), absorbed (HU into a reserve), conduct, shielded (actor id).


static func apply(w: CombatWorld, nm: String, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var custom := Interactions.handler(nm)
	if custom.is_valid():
		return bool(custom.call(w, t, c, res, r, ctx))
	match nm:
		"pass": return _pass(w, t, c, res, r, ctx)
		"block": return block(w, t, c, res, r, ctx)
		"deflect": return deflect(w, t, c, res, r, ctx)
		"redirect": return redirect(w, t, c, res, r, ctx)
		"reflect": return reflect(w, t, c, res, r, ctx)
		"reclaim": return reclaim(w, t, c, res, r, ctx)
		"capture": return capture(w, t, c, res, r, ctx)
		"absorb": return absorb(w, t, c, res, r, ctx)
		"transform": return transform(w, t, c, res, r, ctx)
		"shatter": return shatter(w, t, c, res, r, ctx)
		"sink": return sink(w, t, c, res, r, ctx)
		"conduct": return conduct(w, t, c, res, r, ctx)
		"ground": return ground(w, t, c, res, r, ctx)
		"amplify": return amplify(w, t, c, res, r, ctx)
		"extinguish": return extinguish(w, t, c, res, r, ctx)
		"weaken": return weaken(w, t, c, res, r, ctx)
		"bend": return bend(w, t, c, res, r, ctx)
		"slow": return slow(w, t, c, res, r, ctx)
		"overwhelm": return overwhelm(w, t, c, res, r, ctx)
		"clash": return clash(w, t, c, res, r, ctx)
		"disrupt": return disrupt(w, t, c, res, r, ctx)
		"neutralize": return neutralize(w, t, c, res, r, ctx)
		"heat": return heat(w, t, c, res, r, ctx)
		"push": return push(w, t, c, res, r, ctx)
		"disperse": return disperse(w, t, c, res, r, ctx)
	push_warning("Outcomes: unknown outcome '%s'" % nm)
	return false


static func _id(x: Agent) -> int:
	return x.actor.id if x != null and x.actor != null else -1


## Fraction of the threat that continues after `absorb` x CP_eff was taken from it.
static func _left(res: Dictionary, absorb: float) -> float:
	var tp := float(res.tp)
	if tp <= 1e-6:
		return 0.0
	return clampf((tp - absorb * float(res.cp_eff)) / tp, 0.0, 1.0)


## Takes up to `hu` of heat from a source agent and returns what it gave: a body source loses it from its
## real thermal energy (Thermal), a volume from its heat budget. Callers must place or book the result.
static func draw_heat(src: Agent, hu: float) -> float:
	if src == null or hu <= 0.0:
		return 0.0
	if src.body != null and src.body.alive:
		var take := minf(hu, maxf(0.0, src.body.thermal_energy()))
		var got := -Thermal.heat(src.body, -take)
		src.heat = maxf(0.0, src.heat - got)
		return got
	var g := minf(hu, maxf(0.0, src.heat))
	src.heat -= g
	return g


## Moves up to `hu` of heat from `src` into body `dst` through the ledgers; whatever `dst` cannot take
## goes back to the source (or is booked as spent). Returns HU that reached `dst`.
static func move_heat(w: CombatWorld, src: Agent, dst: MatBody, hu: float) -> float:
	var got := draw_heat(src, hu)
	if got <= 0.0:
		return 0.0
	var used := w.heat_body(dst, got)
	var left := got - used
	if left > 1e-9:
		if src.body != null and src.body.alive:
			left -= Thermal.heat(src.body, left)
		else:
			src.heat += left
			left = 0.0
		if left > 1e-9:
			w.ledger.spent += left
	return used


static func _pass(_w: CombatWorld, _t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.pass_scale = 1.0
	return true


# ------------------------------------------------------------------ block

static func block(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if c.kind == "guard":
		var info: Dictionary = ctx.get("info", {})
		res.result = w.guard_chip(c.actor, info, r, t, float(res.ratio))
		return true
	if c.kind == "stance":
		res.stopped = false
		res.pass_scale = 1.0
		res.knock_scale = 0.0
		return true
	if t.kind == "body" and t.body != null:
		var b := t.body
		if c.kind == "env":
			# Arena solids (legacy impact): the site already placed the body at the contact point.
			w._body_impact(b, "wall")
			b.vel = Vector3(-b.vel.x * 0.15, maxf(b.vel.y, 0.0) * 0.2, -b.vel.z * 0.15)
			return true
		if c.body != null and c.body.form == Sim.Form.WALL:
			var wall := c.body
			if b.form == Sim.Form.WAVE:
				w.emit("block", {"actor": wall.last_actor, "body": b.id, "kind": "wave_wall", "wall": wall.id,
					"power": res.tp, "mat": FxEvents.mat_of(b), "tier": b.tier, "dir": b.wave_dir})
				w.emit("wave_blocked", {"body": b.id, "at": b.pos})
				w._settle_wave(b, "blocked")
				return true
			var owner := w.get_actor(wall.controller if wall.controller >= 0 else wall.last_actor)
			var momentum := b.vel.length() * b.mass
			wall.wall_damage_add(momentum / float(r.get("dmg_div", 900.0)))
			var d := b.vel.normalized()
			b.vel = -b.vel * 0.12
			b.vel.y = 1.0
			b.attack_id = 0
			w.emit("block", {"actor": owner.id if owner != null else -1, "body": b.id, "kind": "wall", "wall": wall.id,
				"power": res.tp, "mat": FxEvents.mat_of(b), "tier": b.tier, "dir": d})
			if wall.wall_damage >= 1.0:
				w._crumble_wall(wall)
			return true
		# Other barriers (zones, held plates) and active volumes stop the body.
		var d2 := b.vel.normalized()
		if b.form == Sim.Form.WAVE:
			w.emit("wave_blocked", {"body": b.id, "at": b.pos})
			w._settle_wave(b, "blocked")
		else:
			b.vel = -b.vel * float(r.get("keep", 0.12))
			b.vel.y = maxf(b.vel.y, 1.0)
			b.attack_id = 0
		w.emit("block", {"actor": _id(c), "body": b.id, "kind": String(r.get("kind", c.ccls)),
			"power": res.tp, "mat": FxEvents.mat_of(b), "tier": b.tier, "dir": d2})
		return true
	# A volume stopped by a barrier: the barrier takes part of its heat.
	if c.body != null and t.heat > 0.0 and float(r.get("heat_share", 0.0)) > 0.0:
		var used := w.heat_body(c.body, t.heat * float(r.heat_share))
		t.heat -= used
		res.heat_used = float(res.heat_used) + used
	return true


# ------------------------------------------------------------------ deflect / redirect / reflect

static func deflect(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.kind == "body" and t.body != null:
		var b := t.body
		var side := Vector3.ZERO
		var mult := 0.5
		var up := 2.5
		if c.kind == "guard":
			side = c.actor.forward().cross(Vector3.UP).normalized()
		elif c.kind == "volume" or c.kind == "move":
			side = c.dir.cross(Vector3.UP).normalized()
			mult = float(r.get("side", 0.45))
			up = float(r.get("up", 2.0))
		else:
			var n := (b.pos - c.pos)
			n.y = 0.0
			side = n.normalized() if n.length() > 1e-3 else Vector3.RIGHT
		if r.has("side") and c.kind == "guard":
			mult = float(r.side)
		if r.has("up") and c.kind == "guard":
			up = float(r.up)
		if side.dot(b.vel) < 0.0:
			side = -side
		b.vel = side * b.vel.length() * mult + Vector3(0, up, 0)
		b.attack_id = 0
		var ev := "perfect_deflect" if c.perfect else "deflect"
		w.emit(ev, {"actor": _id(c), "body": b.id, "verb": String(r.get("verb", "deflect")), "kind": String(r.get("kind", "stone"))})
		res.result = "perfect" if c.perfect else "deflect"
		return true
	# A volume (melee / cone / bolt) met a guard.
	if c.kind == "guard":
		var tg := c.actor
		var info: Dictionary = ctx.get("info", {})
		var kind := String(info.get("kind", ""))
		if c.perfect:
			w.emit("perfect_deflect", {"actor": tg.id, "attacker": info.get("attacker", -1), "kind": kind})
			tg.last_result = "perfect"
			var att := w.get_actor(int(info.get("attacker", -1)))
			var rng := float(r.get("perfect_range", 3.0))
			if att != null and float(r.get("perfect_balance", 0.0)) > 0.0 and att.pos.distance_to(tg.pos) < rng:
				att.balance -= float(r.perfect_balance)
				att.balance_idle = 0.0
				if att.balance <= 0.0:
					# Balance 0 is a knockdown for the attacker too.
					w._stagger(att, "knockdown", 1.1, info)
					att.balance = 45.0
			if float(r.get("absorb_reserve", 0.0)) > 0.0 and t.heat > 0.0:
				var gain := minf(t.heat * float(r.absorb_reserve), Sim.RESERVE_MAX - tg.heat_reserve)
				gain = maxf(gain, 0.0)
				tg.heat_reserve += gain
				t.heat -= gain
				res.absorbed = gain
			res.result = "perfect"
		else:
			w.emit("deflect", {"actor": tg.id, "attacker": info.get("attacker", -1), "kind": kind, "verb": String(r.get("verb", "deflect"))})
			tg.last_result = "deflect"
			res.result = "deflect"
	return true


## Back at the sender (aim "sender", legacy Earth redirect) or along the counter's direction (aim
## "dir", legacy gust). The body becomes the counter actor's attack. Lightning: redirect current.
static func redirect(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var req := String(r.get("requires", ""))
	if req == "redirect_current":
		if t.cls != &"lightning" or c.actor == null or not c.actor.has("redirect_current") \
				or w.guard_element(c.actor) != Sim.Element.FIRE or not ctx.get("allow_redirect", false):
			return false
		var cb: Callable = ctx.get("redirect_cb", Callable())
		if cb.is_valid():
			cb.call(float(r.get("factor", 0.8)))
		res.stopped = true
		res.pass_scale = 0.0
		res.result = "redirected"
		return true
	if t.kind != "body" or t.body == null or c.actor == null:
		return false
	var b := t.body
	var ca := c.actor
	if req == "stone_control" and not (b.is_stone() and b.mass <= ca.max_control_mass and b.attack_owner != ca.id):
		return false
	res.stopped = true
	res.pass_scale = 0.0
	if String(r.get("aim", "sender")) == "dir":
		var spd := b.vel.length()
		b.vel = (c.dir * spd * float(r.get("speed_mult", 0.8))) + Vector3(0, float(r.get("up", 1.5)), 0)
		b.attack_owner = ca.id
		b.attack_id = w.new_attack_id()
		b.hit_set.clear()
		b.hit_set[ca.id] = true
		w.emit("deflect", {"actor": ca.id, "body": b.id, "verb": String(r.get("verb", "gust")), "kind": "stone"})
		res.result = "deflect"
		return true
	var tgt := w.get_actor(b.attack_owner)
	var spd2 := maxf(Vector2(b.vel.x, b.vel.z).length(), float(r.get("min_speed", 12.0))) * float(r.get("speed_mult", 1.05))
	var fwd := ca.forward()
	if c.body != null and r.has("wall_push"):
		var dir := (tgt.chest() - b.pos).normalized() if tgt != null else fwd
		b.vel = ActEarth.launch_vel(b.pos, tgt.chest(), spd2) if tgt != null else dir * spd2
		b.attack_id = w.new_attack_id()
		b.attack_owner = ca.id
		b.hit_set.clear()
		b.hit_set[ca.id] = true
		if r.get("residual", false):
			b.residual_owner = ca.id
			b.residual_authority = Interactions.cohesion(b.tier)
		b.pos += dir * float(r.wall_push)
	else:
		b.vel = ActEarth.launch_vel(b.pos, tgt.chest(), spd2) if tgt != null else fwd * spd2
		b.attack_id = w.new_attack_id()
		b.attack_owner = ca.id
		b.hit_set.clear()
		b.hit_set[ca.id] = true
	b.touch(ca.id, "redirect", w.tick)
	w.emit("perfect_deflect", {"actor": ca.id, "body": b.id, "verb": "redirect", "kind": "stone"})
	res.result = "perfect"
	return true


## Straight back to the sender with the counter actor as the new owner (bodies), or the volume is
## sent back at its caster (res.reflected; the volume site re-applies it).
static func reflect(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.kind == "body" and t.body != null:
		var b := t.body
		var owner := c.actor
		var tgt := w.get_actor(b.attack_owner)
		var spd := maxf(b.vel.length() * float(r.get("speed_mult", 1.0)), 8.0)
		if tgt != null and tgt != owner:
			b.vel = ActEarth.launch_vel(b.pos, tgt.chest(), spd)
		else:
			b.vel = -b.vel.normalized() * spd + Vector3(0, 1.0, 0)
		if owner != null:
			b.attack_id = w.new_attack_id()
			b.attack_owner = owner.id
			b.hit_set.clear()
			b.hit_set[owner.id] = true
			b.residual_owner = owner.id
			b.residual_authority = Interactions.cohesion(b.tier)
			b.touch(owner.id, "reflect", w.tick)
		w.emit("perfect_deflect" if c.perfect else "deflect", {"actor": _id(c), "body": b.id, "verb": "reflect", "kind": FxEvents.mat_of(b)})
		res.result = "perfect" if c.perfect else "deflect"
		return true
	res["reflected"] = true
	if t.actor != null and c.actor != null and t.actor != c.actor:
		var info: Dictionary = ctx.get("info", {})
		var dmg := float(info.get("damage", float(res.tp))) * float(r.get("factor", 1.0))
		var src := c.actor
		if src.pos.distance_to(t.actor.pos) <= float(r.get("range", 14.0)) and w.los(src.chest(), t.actor.chest()):
			w.hit_actor(t.actor, {"attacker": src.id, "attack_id": w.new_attack_id(), "damage": dmg,
				"balance": float(info.get("balance", dmg)) * float(r.get("factor", 1.0)),
				"knock": (t.actor.pos - src.pos).normalized() * 3.0, "kind": String(info.get("kind", t.cls)), "from": src.chest()})
	w.emit("reflect", {"actor": _id(c), "to": _id(t), "cls": String(t.cls)})
	res.result = "perfect" if c.perfect else "deflect"
	return true


# ------------------------------------------------------------------ control

static func reclaim(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or c.actor == null or not t.body.alive:
		return false
	if t.body.mass > c.actor.max_control_mass:
		return false
	w.take_control(c.actor, t.body, float(r.get("authority", 0.9)), "reclaim")
	res.stopped = true
	res.pass_scale = 0.0
	return true


## The counter body (wave, vortex, zone) carries the threat body; released when the captor ends.
static func capture(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	if t.body == null:
		return false
	if c.body == null:
		return reclaim(w, t, c, res, r, ctx)
	var b := t.body
	if b.captured_by == c.body.id:
		res.stopped = true
		return true
	if c.body.captured.size() >= int(r.get("max_captured", 6)):
		return false
	c.body.captured.append(b.id)
	b.captured_by = c.body.id
	b.props["capture_off"] = b.pos - c.body.pos
	b.props["release_speed"] = float(r.get("release_speed", 0.0))
	b.attack_id = 0
	b.vel = c.body.vel
	w.emit("capture", {"body": b.id, "by": c.body.id, "actor": _id(c)})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## The threat is taken in: same-material bodies merge into the counter body, heat volumes feed the
## counter actor's reserve (share), hot bodies give `hu` of heat to the reserve, others decay.
static func absorb(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.kind == "body" and t.body != null and t.body.alive:
		var b := t.body
		if b.mass <= 1e-9:
			w.decay_body(b, "absorbed")   # massless (wind blade, spent vapour): nothing to merge
			return true
		if c.body != null and c.body.alive and c.body != b and c.body.mat == b.mat and b.form != Sim.Form.POOL:
			if c.body.form == Sim.Form.PUDDLE or c.body.form == Sim.Form.POOL or c.body.controller >= 0 or r.get("merge", true):
				w.merge_bodies(c.body, b)
				if c.body.form == Sim.Form.PUDDLE:
					c.body.update_radius_puddle()
				return true
		if c.actor != null and r.has("hu"):
			var room := Sim.RESERVE_MAX - c.actor.heat_reserve
			var take := minf(minf(float(r.hu), maxf(0.0, b.thermal_energy())), room)
			var got := -Thermal.heat(b, -take)
			c.actor.heat_reserve += got
			res.absorbed = got
			res.stopped = false
			return true
		w.decay_body(b, "absorbed")
		return true
	if c.actor != null and t.heat > 0.0:
		var gain := clampf(t.heat * float(r.get("share", 1.0)), 0.0, Sim.RESERVE_MAX - c.actor.heat_reserve)
		c.actor.heat_reserve += gain
		t.heat -= gain
		res.absorbed = gain
	return true


# ------------------------------------------------------------------ material change

## The body a material outcome acts on: the threat body, else the counter body (rule.target overrides).
static func _subject(t: Agent, c: Agent, r: Dictionary) -> MatBody:
	var tg := String(r.get("target", ""))
	if tg == "counter":
		return c.body
	if tg == "threat":
		return t.body
	return t.body if t.body != null else c.body


## The heat a volume brings to the interaction (the other side of the subject).
static func _heat_src(t: Agent, c: Agent, subject: MatBody) -> Agent:
	if t.body != subject and t.heat > 0.0:
		return t
	if c.body != subject and c.heat > 0.0:
		return c
	return null


static func transform(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := _subject(t, c, r)
	if b == null or not b.alive:
		return false
	var to := String(r.get("to", res.get("to", "")))
	var src := _heat_src(t, c, b)
	var hu := 0.0
	if r.has("energy"):
		hu = float(r.energy)
	elif r.has("rate"):
		hu = float(r.rate) * Sim.DT
	elif src != null:
		hu = src.heat * float(r.get("share", 1.0))
	else:
		hu = float(res.cp_eff) * float(r.get("hu_per_pu", Interactions.HU_PER_PU))
	match to:
		"steam":
			if not b.is_water():
				return false
			var share := hu
			if b.phase == Sim.Phase.FROZEN:
				var used := move_heat(w, src, b, share) if src != null else w.heat_body(b, share)
				res.heat_used = float(res.heat_used) + used
			else:
				if src != null:
					share = draw_heat(src, share)
				w.boil_water(b, share, b.pos)
				res.heat_used = float(res.heat_used) + share
				if b.mass <= 0.05:
					w.decay_body(b, "boiled")
			if b.controller >= 0:
				res["shielded"] = b.controller
			if r.has("event"):
				w.emit(String(r.event), {"actor": _id(t) if src == t else _id(c), "body": b.id})
		"water":
			if not b.is_water():
				return false
			var used2 := move_heat(w, src, b, hu) if src != null else w.heat_body(b, hu)
			res.heat_used = float(res.heat_used) + used2
		"rock", "obsidian", "hot_rock":
			if r.get("requires", "") == "liquid" and b.liquid <= 0.0:
				return false
			var water := c.body if c.body != null and c.body.is_water() else null
			if water != null and water.mass > 0.0:
				w.quench_energy(b, water, hu)
			else:
				var want := minf(hu, maxf(0.0, b.thermal_energy()))
				if to == "hot_rock":
					want = minf(want, b.liquid * b.mass * Materials.latent(b.mat))
				var got := -Thermal.heat(b, -want)
				w.ledger.ambient -= got     # convective cooling (wind) or the counter's cold: booked as ambient
				res.heat_used = float(res.heat_used) + got
			if to == "obsidian" and b.liquid <= 0.0:
				b.tag = &"obsidian"
		"lava", "molten_metal":
			var used3 := 0.0
			if src != null:
				used3 = move_heat(w, src, b, hu)
			else:
				used3 = w.heat_body(b, hu)
				w.ledger.generated += used3   # counter-driven heat with no paid source (rule.energy): booked as created
			res.heat_used = float(res.heat_used) + used3
		"ice", "snow":
			if not b.is_water():
				return false
			var e0 := b.thermal_energy()
			b.liquid = 0.0
			b.temp = minf(b.temp, -5.0)
			b.phase = Sim.Phase.FROZEN
			w.ledger.freeze_dump += b.thermal_energy() - e0
			if to == "snow":
				b.tag = &"snow"
		"mist":
			if not b.is_water():
				return false
			b.form = Sim.Form.CLOUD
			b.tag = &"mist"
			b.attack_id = 0
			if b.max_life < 0.0:
				b.max_life = b.age + 4.0
		"glass":
			if b.mat != Sim.Mat.SAND:
				return false
			w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
		"sandstone":
			if b.mat != Sim.Mat.SAND:
				return false
			w.convert_mat(b, Sim.Mat.STONE, "sand_to_sandstone")
			b.tag = &"sandstone"
		"mud":
			if b.mat != Sim.Mat.SAND and not b.is_water():
				return false
			b.tag = &"mud"
			b.props["wet"] = true
		"ash":
			if b.mat != Sim.Mat.PLANT:
				return false
			w.burn_plant(b, b.mass)
		_:
			return false
	res.stopped = bool(r.get("stops", false))
	res.pass_scale = 0.0 if res.stopped else 1.0
	if c.kind == "guard" and r.has("guard_kind"):
		# A guard whose barrier transformed the threat (water shield steams a flame): a clean block.
		var info: Dictionary = ctx.get("info", {})
		w.emit("block", {"actor": c.actor.id, "attacker": info.get("attacker", _id(t)), "kind": String(r.guard_kind)})
		res.result = "block"
		res.stopped = true
		res.pass_scale = 0.0
	# Heat flows (steam, water, rock, lava) announce themselves through phase changes; discrete
	# conversions get a transform event here.
	if b.alive and ["ice", "snow", "mist", "mud", "obsidian", "sandstone", "glass", "ash"].has(to):
		w.emit("transform", {"body": b.id, "at": b.pos, "from": String(t.cls), "to": to, "why": "interaction"})
	return true


static func heat(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := _subject(t, c, r)
	if b == null or not b.alive:
		return false
	var src := _heat_src(t, c, b)
	if src == null or src.heat <= 0.0:
		return true
	var used := move_heat(w, src, b, src.heat * float(r.get("share", 0.5)))
	res.heat_used = float(res.heat_used) + used
	return true


## Brittle threat bodies split into `pieces` scattering fragments; a volume that shatters a barrier breaks it (wall crumbles) and continues with TP - absorb x CP_eff.
static func shatter(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if t.kind != "body" and c.body != null and c.body.alive:
		_break_counter(w, c, res)
		res.pass_scale = _left(res, float(r.get("absorb_on_fail", 0.5)))
		res.stopped = res.pass_scale <= 0.0
		w.emit("shatter", {"body": c.body.id, "mass": c.body.mass, "by": String(t.cls)})
		return true
	if t.body == null or not t.body.alive:
		return false
	var b := t.body
	var n := int(r.get("pieces", 3))
	w.emit("shatter", {"body": b.id, "mass": b.mass, "by": String(c.ccls)})
	var v := b.vel
	b.attack_id = 0
	var share := b.mass / float(n)
	for k in n - 1:
		if b.mass <= share * 0.5:
			break
		var ang := TAU * float(k + 1) / float(n)
		var off := Vector3(cos(ang), 0.2, sin(ang)) * 0.3
		var p := w.split_body(b, share, b.pos + off)
		p.form = Sim.Form.CHUNK if p.form != Sim.Form.SHARD else Sim.Form.SHARD
		p.vel = v * 0.3 + off.normalized() * 3.0 + Vector3(0, 2.0, 0)
		p.max_life = Sim.REMNANT_LIFETIME
		p.attack_id = 0
	b.vel = v * 0.3 + Vector3(0, 2.0, 0)
	res.stopped = true
	res.pass_scale = 0.0
	return true


static func sink(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive:
		return false
	w.emit("sink", {"body": t.body.id, "mass": t.body.mass, "at": t.body.pos})
	w.decay_body(t.body, "sunk")
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Lightning into a conductor: the site spreads it through the conduction graph from this node;
## a conductor held by a fighter (shield, jet) carries it into them (res.conduct_to).
static func conduct(_w: CombatWorld, _t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	res["conduct"] = true
	res.pass_scale = float(r.get("factor", 1.0))
	if c.body != null and c.body.controller >= 0:
		res["conduct_to"] = c.body.controller
	return true


static func ground(w: CombatWorld, _t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	res.pass_scale = float(r.get("factor", 0.0))
	res.stopped = res.pass_scale <= 0.0
	if r.has("event"):
		w.emit(String(r.event), {"actor": _id(c)})
	return true


static func amplify(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var amp := float(r.get("amp", 1.3))
	res["amp"] = amp
	res.pass_scale = amp
	if t.body != null and t.body.alive and t.body.mat == Sim.Mat.FIRE:
		var add := t.body.heat_payload * (amp - 1.0)
		t.body.heat_payload += add
		w.ledger.generated += add     # fanned flames (fantasy oxygen): booked as created heat
	return true


static func extinguish(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.body != null and t.body.alive and (t.body.mat == Sim.Mat.FIRE or t.cls == &"fire_field"):
		w.emit("extinguish", {"body": t.body.id})
		w.decay_body(t.body, "extinguished")
	elif t.body == null and t.heat > 0.0:
		# A heat volume put out: its unspent heat is lost to the air (booked, so the ledger stays exact).
		# (A body's heat stays in the body - zeroing the agent's copy changes nothing.)
		w.ledger.spent += t.heat
		t.heat = 0.0
	return true


## Partial: the counter removes CP_eff from the threat. Bodies slow (K) and lose heat (H, booked:
## water counters boil, others cool to ambient); volumes continue at (TP - CP_eff) / TP.
static func weaken(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var f := _left(res, 1.0)
	res.pass_scale = f
	res.stopped = false
	if c.kind == "stance":
		res.knock_scale = f
		return true
	if t.body != null and t.body.alive:
		var b := t.body
		scale_damage(b, f)
		# Every channel keeps the fraction f, so TP' = TP - CP_eff (K is linear in speed).
		var w_r: Dictionary = r.get("w", {})
		if float(t.ch.K) > 0.0 and float(w_r.get("K", 1.0)) > 0.0:
			b.vel *= f
			if b.form == Sim.Form.WAVE:
				b.wave_budget *= lerpf(1.0, f, 0.5)
		if float(t.ch.H) > 0.0 and float(w_r.get("H", 1.0)) > 0.0 and b.thermal_energy() > 0.0:
			var take := minf((1.0 - f) * float(t.ch.H) * Interactions.HU_PER_PU * float(r.get("heat_mult", 1.0)), b.thermal_energy())
			# A counter cannot take more heat than its own mass can hold (2 kg of fog cannot quench 80 kg of lava).
			take = minf(take, heat_capacity(c.body))
			if c.body != null and c.body.is_water() and c.body.mass > 0.0:
				var got := -Thermal.heat(b, -take)
				w.boil_water(c.body, got, b.pos)
			else:
				var got2 := -Thermal.heat(b, -take)
				w.ledger.ambient -= got2
	return true


## Remaining hit strength of a body after partial counters (MOVESET §5.3: a crusted / slowed threat hits
## softer). Multiplied into props.dmg_scale; contact hits read it (CombatWorld.hit_scale).
static func scale_damage(b: MatBody, f: float) -> void:
	if b == null:
		return
	b.props["dmg_scale"] = clampf(float(b.props.get("dmg_scale", 1.0)) * f, 0.0, 1.0)


## HU a counter body can soak (water boils, solids heat to their melt point, plants scorch). Wind and fire
## counters (no mass / no limit to the air they move) are unbounded.
static func heat_capacity(b: MatBody) -> float:
	if b == null or not b.alive or b.mass <= 0.0:
		return INF
	match b.mat:
		Sim.Mat.WATER, Sim.Mat.STEAM:
			var per := Sim.WATER_LATENT_VAPOR + Sim.WATER_C * maxf(0.0, Sim.WATER_BOIL_C - minf(b.temp, Sim.WATER_BOIL_C))
			if b.mat == Sim.Mat.WATER and b.liquid < 1.0:
				per += Sim.WATER_LATENT_FUSION * (1.0 - b.liquid)
			return b.mass * per
		Sim.Mat.AIR, Sim.Mat.FIRE:
			return INF
		Sim.Mat.PLANT:
			return b.mass * Materials.c(b.mat) * 200.0
		Sim.Mat.STONE:
			return b.mass * Sim.STONE_C * maxf(0.0, Sim.STONE_MELT_C - b.temp) + b.mass * Sim.STONE_LATENT * (1.0 - b.liquid)
	if Materials.is_fusible(b.mat):
		return b.mass * Materials.c(b.mat) * maxf(0.0, Materials.melt(b.mat) - b.temp)
	return INF


static func bend(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	res.pass_scale = 1.0
	if t.body == null or not t.body.alive:
		return true
	var b := t.body
	if r.has("bend_impulse"):
		b.vel += c.dir * (float(r.bend_impulse) / maxf(b.mass, 0.1))
	else:
		var f := clampf((float(res.ratio) - 0.5) / 0.5, 0.0, 1.0)
		var ang := deg_to_rad(float(r.get("angle", 60.0))) * f
		var away := b.pos - (c.actor.pos if c.actor != null else c.pos)
		var side := b.vel.cross(Vector3.UP)
		var sgn := 1.0 if side.dot(away) <= 0.0 else -1.0
		b.vel = b.vel.rotated(Vector3.UP, ang * sgn)
	w.emit("bend", {"actor": _id(c), "body": b.id})
	return true


static func slow(_w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var k := float(r.get("factor", 0.6))
	res.pass_scale = k
	if t.body != null and t.body.alive:
		scale_damage(t.body, k)
		t.body.vel *= k
		if t.body.form == Sim.Form.WAVE:
			t.body.props["speed"] = float(t.body.props.get("speed", 7.5)) * k
	return true


static func _break_counter(w: CombatWorld, c: Agent, res: Dictionary) -> void:
	res.counter_broken = true
	if c.kind == "guard" and c.actor != null:
		w._stagger(c.actor, "guard_break", 0.7, {})
		c.actor.balance = minf(c.actor.balance, 35.0)
		return
	var cb := c.body
	if cb == null or not cb.alive or cb.static_body and cb.form == Sim.Form.POOL:
		return
	match cb.form:
		Sim.Form.WALL:
			w._crumble_wall(cb)
		Sim.Form.ZONE, Sim.Form.CLOUD:
			w.close_zone(cb, "broken")
		_:
			if cb.controller >= 0:
				var h := w.get_actor(cb.controller)
				if h != null:
					w.release_body(h, Vector3(0, -1, 0), false)
			w.emit("counter_broken", {"body": cb.id})


## Fail: the counter breaks (wall crumbles, zone closes, held barrier drops, guard breaks) and the threat
## continues with TP - absorb_on_fail x CP_eff.
static func overwhelm(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var f := _left(res, float(r.get("absorb_on_fail", 0.5)))
	res.pass_scale = f
	if c.kind != "env" and c.kind != "volume" and c.kind != "move":
		_break_counter(w, c, res)
	if t.body != null and t.body.alive:
		t.body.vel *= f
		scale_damage(t.body, f)
	w.emit("overwhelm", {"threat": String(t.cls), "counter": String(c.ccls), "left": f})
	return true


## Two threats collide: the stronger keeps going (slower by the weaker's share), the weaker is knocked
## aside and stops being an attack; near-equal powers both drop. Waves: the bigger wave absorbs the smaller.
static func clash(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var tp_a := float(res.tp)
	var tp_b := Interactions.threat_power(c, r)
	var a := t.body
	var b := c.body
	if a == null or b == null or not a.alive or not b.alive:
		return false
	var hi := maxf(tp_a, tp_b)
	var lo := minf(tp_a, tp_b)
	var even := hi <= 1e-6 or lo / hi >= float(r.get("even_at", 0.8))
	var win := a if tp_a >= tp_b else b
	var lose := b if win == a else a
	if a.form == Sim.Form.WAVE and b.form == Sim.Form.WAVE:
		if a.mat == b.mat:
			w.merge_bodies(win, lose)
		else:
			w._settle_wave(lose, "clash")
			win.wave_budget *= clampf(1.0 - lo / maxf(hi, 1e-6), 0.0, 1.0)
	elif even:
		for x in [a, b]:
			x.vel = x.vel * -0.2 + Vector3(0, 2.0, 0)
			x.attack_id = 0
	else:
		var side := lose.vel.cross(Vector3.UP).normalized()
		if side.dot(lose.pos - win.pos) < 0.0:
			side = -side
		lose.vel = side * lose.vel.length() * 0.4 + Vector3(0, 2.5, 0)
		lose.attack_id = 0
		win.vel *= sqrt(clampf(1.0 - lo / hi, 0.05, 1.0))
	res.stopped = true
	res.result = "even" if even else ("win" if win == a else "lose")
	w.emit("clash", {"a": a.id, "b": b.id, "winner": -1 if even else win.id, "pos": (a.pos + b.pos) * 0.5,
		"power": hi, "mat": FxEvents.mat_of(win)})
	return true


## Sound can break a CHARGE (MOVESET §4) or a technique CHANNEL; a held guard (a channel too) is a
## counter: the sound meets its rule cell instead (Null Bubble neutralizes, Sound Barrier reflects, a plain
## guard blocks), so it is never disrupted.
static func disruptable(who: ActorState) -> bool:
	var inst := who.action
	if inst == null:
		return false
	if inst.id == "guard" or who.guarding:
		return false
	return inst.phase == ActionInst.P.CHARGE or inst.phase == ActionInst.P.CHANNEL


## The counter (sound) breaks the threat actor's charge or channel when P >= cohesion 6 + 4·tier.
static func disrupt(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var who := t.actor
	if who == null or who.action == null:
		return false
	var inst := who.action
	if not disruptable(who):
		return false
	var p := float(res.cp_eff)
	if p < Interactions.disrupt_threshold(inst.tier()):
		return false
	w.interrupt_action(who, "disrupt")
	w.emit("disrupt", {"actor": who.id, "by": _id(c), "move": inst.id})
	res.stopped = true
	res.pass_scale = 0.0
	return true


static func neutralize(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.body != null and t.body.alive:
		if t.body.form == Sim.Form.ZONE or t.body.form == Sim.Form.CLOUD:
			w.close_zone(t.body, "neutralized")
		else:
			w.decay_body(t.body, "neutralized")
	elif t.body == null and t.heat > 0.0:
		w.ledger.spent += t.heat   # the volume's unspent heat leaves with it (booked)
		t.heat = 0.0
	if c.body != null and c.body.alive and (c.body.form == Sim.Form.ZONE or c.body.form == Sim.Form.CLOUD):
		w.close_zone(c.body, "neutralized")
	w.emit("neutralize", {"threat": String(t.cls), "counter": String(c.ccls)})
	return true


## Legacy gust on a loose light body: pushed along the gust (knock x push_mult / mass).
static func push(_w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive:
		return false
	var knock := float(c.data.get("knock", 7.0))
	t.body.vel += c.dir * (knock * float(r.get("push_mult", 12.0)) / maxf(t.body.mass, 1.0)) + Vector3(0, 1.0, 0)
	t.body.on_ground = false
	res.pass_scale = 1.0
	return true


## Legacy gust on a cloud: blown along and dispersed within 0.6 s.
static func disperse(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null and t.kind == "volume":
		# A vapour / grit volume (steam jet, sand spray) blown apart before it lands; its heat is lost to the air.
		w.ledger.spent += maxf(0.0, t.heat)
		t.heat = 0.0
		w.emit("disperse", {"actor": _id(c), "body": -1, "cls": String(t.cls)})
		res.stopped = true
		res.pass_scale = 0.0
		return true
	if t.body == null or not t.body.alive:
		return false
	var b := t.body
	b.vel += c.dir * 9.0
	b.max_life = minf(b.max_life, b.age + 0.6)
	w.emit("disperse", {"actor": _id(c), "body": b.id})
	res.stopped = true
	res.pass_scale = 0.0
	return true
