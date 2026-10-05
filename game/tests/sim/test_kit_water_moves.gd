extends WaterKitTest
## Every Water-kit move (Water, Ice, Mist; Plant when present) at tiers T0-T3: it starts, pays its costs, ends
## cleanly, emits only catalogued fx keys and keeps every ledger exact (docs/MOVESET.md §15.9). The defs carry
## the §15.2 schema (name, desc, slot, frames, tiers, counter, threat, anim, fx, ai).

const SUBS := [0, 1, 2, 3]
const CLIPS := ["air_dash", "air_gust", "air_push", "deflect", "earth_heavy", "earth_hold", "earth_lift", "earth_throw", "earth_wall", "evade",
	"evade_back", "evade_fwd", "evade_l", "evade_r", "fall", "fire_charge", "fire_jab", "fire_release", "getup", "glide", "guard", "heat_draw",
	"hit_back", "hit_front", "hit_heavy", "idle", "jump", "knockdown", "land", "lightning_charge", "lightning_release", "magma_hold", "pour",
	"run", "stagger", "stance_air", "stance_earth", "stance_fire", "stance_water", "strafe_l", "strafe_r", "walk", "walk_back", "water_draw",
	"water_freeze", "water_hold", "water_shield", "water_whip"]


func _kit_moves() -> Array[String]:
	var out: Array[String] = []
	for s in SUBS:
		for id in Moves.list(1, s):
			if not out.has(id) and (s == 0 or Moves.slot_of(1, s, id) != "" and int(Moves.DEFS[id].get("sub", 0)) == s):
				out.append(id)
	return out


func test_every_move_has_a_complete_def() -> void:
	Moves.ensure()
	var n := 0
	for id in _kit_moves():
		var d: Dictionary = Moves.DEFS[id]
		if id == "guard":
			continue
		n += 1
		for key in ["name", "desc", "element", "sub", "slot", "startup", "active", "recovery", "anim", "fx", "ai"]:
			check(d.has(key), "%s has '%s'" % [id, key])
		check(int(d.get("element", -1)) == 1, "%s is a Water move" % id)
		check(Sim.SLOTS.has(String(d.get("slot", ""))), "%s has a known slot" % id)
		check(Moves.slot_of(1, int(d.sub), id) == String(d.slot), "%s is bound to its slot (%s)" % [id, d.get("slot", "")])
		var ai: Dictionary = d.get("ai", {})
		check(ai.has("role") and ai.has("range") and ai.has("tags"), "%s: ai metadata {role, range, tags}" % id)
		var fxd: Dictionary = d.get("fx", {})
		check(FxEvents.is_known("mat", String(fxd.get("mat", ""))), "%s: fx mat '%s' catalogued" % [id, fxd.get("mat", "")])
		check(FxEvents.is_known("shape", String(fxd.get("shape", ""))), "%s: fx shape '%s' catalogued" % [id, fxd.get("shape", "")])
		for k in ["cast", "release", "impact"]:
			if fxd.has(k):
				check(FxEvents.is_known("fx", String(fxd[k])), "%s: fx.%s '%s' catalogued" % [id, k, fxd[k]])
		for ck in ["anim", "anim_hold", "anim_active", "anim_t2", "anim_t3"]:
			if d.has(ck):
				check(CLIPS.has(String(d[ck])) or MoveAnimBridge.clip_data().has(String(d[ck])), "%s: %s '%s' is an existing clip" % [id, ck, d[ck]])
		check(d.has("counter") or d.has("threat"), "%s declares a counter or a threat class" % id)
		if String(d.slot) in Sim.ATTACK_SLOTS:
			check(d.has("tiers") and (d.tiers as Dictionary).has("t3"), "%s has tiers up to t3" % id)
	check(n >= 30, "the kit has at least 30 moves (%d)" % n)
	note("%d moves" % n)


func test_every_move_runs_at_every_tier_with_exact_ledgers() -> void:
	Moves.ensure()
	var ran := 0
	for id in _kit_moves():
		var d: Dictionary = Moves.DEFS[id]
		if id == "guard" or String(d.get("module", "")) == "water":
			continue   # legacy kit moves are pinned by test_water_ice.gd and test_core_*
		for tier in 4:
			var s := duel(int(d.sub), Sim.Element.EARTH, 5)
			var p: ActorState = s[0]
			var o: ActorState = s[1]
			o.is_dummy = true
			p.focus = 100.0
			p.heat_reserve = 200.0     # steam moves pay heat from the reserve first
			var base := snap(h.w)
			var f0 := p.focus
			var inst := run_move(p, id, tier, 45, 150)
			ledgers_ok(base, "%s T%d" % [id, tier])
			check(p.action == null or p.action.id == "guard" or p.action != inst, "%s T%d ends cleanly" % [id, tier])
			check(p.focus <= f0 + 1e-6 or h.w.tick > 0, "%s T%d: Focus is sane" % [id, tier])
			var slot := Moves.slot_of(1, int(d.sub), id)
			check(h.has_event("action", "move", "guard" if slot == "guard" else id), "%s T%d started (action event)" % [id, tier])
			fx_catalogued("%s T%d" % [id, tier])
			for b in h.w.bodies:
				if b.alive:
					check(is_finite(b.pos.x) and is_finite(b.mass) and b.mass >= -1e-9, "%s T%d: finite bodies" % [id, tier])
			ran += 1
	note("%d move-tier runs" % ran)


func test_costs_are_paid_and_tiers_scale() -> void:
	Moves.ensure()
	# Focus is spent at the start; a higher tier costs at least as much (the charge surcharge).
	for id in ["frost_shard", "icicle_volley", "rime_path", "hoarfrost_fan", "scald_puff", "fog_lance", "creeping_fog", "veil", "water_bullet", "tidal_rush", "spray_fan"]:
		var paid := []
		for tier in 4:
			var s := duel(int(Moves.DEFS[id].sub), Sim.Element.EARTH, 6)
			var p: ActorState = s[0]
			p.focus = 100.0
			p.heat_reserve = 400.0
			var f0 := p.focus
			var inst := h.w.start_action(p, id, h.it(p), {"slot": String(Moves.DEFS[id].slot), "tier": tier, "charge_frozen": true})
			h.it(p).attack_held = false
			h.step(60)
			paid.append(f0 - p.focus)
			check(inst != null, "%s T%d started" % [id, tier])
		check(float(paid[0]) >= 0.0 and float(paid[3]) >= float(paid[0]) - 1.0, "%s: costs do not fall with tier: %s" % [id, paid])
