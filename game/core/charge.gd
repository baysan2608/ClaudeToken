class_name Charge
extends RefCounted
## Unified charge tiers (docs/MOVESET.md §4). T0 tap, T1 at the move's heavy_min (default 0.40 s),
## T2 1.00 s, T3 1.80 s (per-move `tier_times`). Focus drains at `charge_drain`/s from T1 on for
## moves that have t2/t3 data; an empty pool stalls the tier (one `insufficient` event), never drops it.
## The tier lives in inst.data.tier; released bodies and volumes carry it (MatBody.tier, Agent.tier).
##
## Tier data: def.tiers = {"t1": {key: value}, "t2": {...}, "t3": {...}}. Charge.param reads the
## reached tier, then lower tiers (a ladder: t3 inherits what it doesn't set from t2, t1), then the
## def, then the default.

const TIER_TIMES := [0.40, 1.00, 1.80]
const DEFAULT_DRAIN := 8.0


## The def that carries tier data for an action: the guard spec for guards, else the move def.
static func pdef(inst: ActionInst) -> Dictionary:
	return inst.data.get("spec_def", inst.def)


## Highest tier the def can reach: 3/2/1 from its tiers data; 1 for legacy charge moves (heavy_min); else 0.
static func max_tier(def: Dictionary) -> int:
	var tiers: Dictionary = def.get("tiers", {})
	if tiers.has("t3"):
		return 3
	if tiers.has("t2"):
		return 2
	if tiers.has("t1") or def.has("heavy_min"):
		return 1
	return 0


## Hold times of T1, T2, T3. `tier_times` overrides; heavy_min gates T1.
static func tier_times(def: Dictionary) -> Array:
	var t: Array = (def.tier_times as Array).duplicate() if def.has("tier_times") else [float(def.get("heavy_min", TIER_TIMES[0])), TIER_TIMES[1], TIER_TIMES[2]]
	while t.size() < 3:
		t.append(TIER_TIMES[t.size()])
	if def.has("heavy_min"):
		t[0] = maxf(float(t[0]), float(def.heavy_min))
	return t


## Tier reached after holding held_s seconds.
static func tier_for(def: Dictionary, held_s: float) -> int:
	var mt := max_tier(def)
	var times := tier_times(def)
	var tier := 0
	for k in mt:
		if held_s >= float(times[k]) - 1e-6:
			tier = k + 1
	return tier


## Tier parameter for a running action: def.tiers["t<tier>"][key] -> lower tiers -> def[key] -> default.
static func param(inst: ActionInst, key: String, default: Variant = null) -> Variant:
	return pget(pdef(inst), int(inst.data.get("tier", 0)), key, default)


## Same lookup without an action (AI/Lab predictions, kits).
static func pget(def: Dictionary, tier: int, key: String, default: Variant = null) -> Variant:
	var tiers: Dictionary = def.get("tiers", {})
	for t in range(clampi(tier, 0, 3), -1, -1):
		var td: Dictionary = tiers.get("t%d" % t, {})
		if td.has(key):
			return td[key]
	return def.get(key, default)


## Counter power of a def at a tier: def.counter.power[tier] (array or number), else -1.
static func counter_power(def: Dictionary, tier: int) -> float:
	var c: Dictionary = def.get("counter", {})
	if not c.has("power"):
		return -1.0
	var p: Variant = c.power
	if p is Array:
		var arr: Array = p
		if arr.is_empty():
			return -1.0
		return float(arr[clampi(tier, 0, arr.size() - 1)])
	return float(p)


## True while the input that holds this action is held (by slot).
static func held(inst: ActionInst, it: ActorIntent) -> bool:
	match inst.slot:
		"strike", "thrust", "ground", "sweep":
			return it.attack_held
		"guard", "push", "sink":
			return it.guard_held
		"tech":
			return it.tech_held
		"evade", "evade_hold":
			return it.evade_held
	if inst.phase == ActionInst.P.CHARGE:
		return it.attack_held
	return it.tech_held


## Per tick while CHARGE/CHANNEL: tier-ups (event 'charge'), drain from T1 on, stall on empty.
static func tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	var d := pdef(inst)
	var mt := max_tier(d)
	if mt <= 0 or inst.data.get("released", false) or inst.data.get("charge_frozen", false):
		return
	if not held(inst, it):
		return
	var tier := int(inst.data.get("tier", 0))
	var ct := float(inst.data.get("charge_t", inst.total - Sim.DT))
	if tier >= 1 and mt >= 2:
		var drain := float(pget(d, tier, "charge_drain", DEFAULT_DRAIN)) * Sim.DT
		if drain > 0.0 and not w.spend_focus(a, drain):
			if not inst.data.get("stalled", false):
				inst.data["stalled"] = true
				w.emit("insufficient", {"actor": a.id, "what": "focus", "move": inst.id, "reason": "charge", "tier": tier})
			return
	ct += Sim.DT
	inst.data["charge_t"] = ct
	var nt := tier_for(d, ct)
	if nt > tier:
		inst.data["tier"] = nt
		FxEvents.charge(w, a, inst, nt, nt >= mt)


## (tier, fraction to the next tier) for the HUD ring. Fraction is 1 at the top tier.
static func progress(inst: ActionInst) -> Vector2:
	var d := pdef(inst)
	var mt := max_tier(d)
	var tier := int(inst.data.get("tier", 0))
	if mt <= 0 or tier >= mt:
		return Vector2(tier, 1.0)
	var times := tier_times(d)
	var ct := float(inst.data.get("charge_t", inst.total))
	var t0 := 0.0 if tier == 0 else float(times[tier - 1])
	var t1 := float(times[tier])
	return Vector2(tier, clampf((ct - t0) / maxf(t1 - t0, 1e-3), 0.0, 1.0))
