class_name Thermal
extends RefCounted
## Bounded thermal transitions on MatBody. All heat moves through apply_heat(),
## which returns how much was actually absorbed (or removed, negative), so callers
## can charge the exact resource cost and the ledger stays balanced.

const STONE_MAX_C := 1350.0
const ICE_MIN_C := -20.0
## Ice warms from the air faster than the generic coefficient (small shards, game rule).
const ICE_AMBIENT_MULT := 6.0


## Adds (energy > 0) or removes (energy < 0) heat. Returns Vector2(applied_HU, vaporised_kg).
## Removal never cools below ambient for stone (extraction needs a temperature gradient).
static func apply_heat(b: MatBody, energy: float) -> Vector2:
	if b.mass <= 0.0 or energy == 0.0:
		return Vector2.ZERO
	if b.mat == Sim.Mat.STONE:
		return Vector2(_stone(b, energy), 0.0)
	if b.mat == Sim.Mat.WATER:
		return _water(b, energy)
	return Vector2.ZERO


static func _stone(b: MatBody, e: float) -> float:
	var m := b.mass
	var applied := 0.0
	if e > 0.0:
		var left := e
		if b.temp < Sim.STONE_MELT_C and b.liquid <= 0.0:
			var need := (Sim.STONE_MELT_C - b.temp) * m * Sim.STONE_C
			var use := minf(left, need)
			b.temp += use / (m * Sim.STONE_C)
			left -= use
			applied += use
		if left > 0.0 and b.liquid < 1.0:
			b.temp = maxf(b.temp, Sim.STONE_MELT_C)
			var need_l := (1.0 - b.liquid) * m * Sim.STONE_LATENT
			var use_l := minf(left, need_l)
			b.liquid = minf(1.0, b.liquid + use_l / (m * Sim.STONE_LATENT))
			left -= use_l
			applied += use_l
		if left > 0.0 and b.liquid >= 1.0:
			var need_s := (STONE_MAX_C - b.temp) * m * Sim.STONE_C
			var use_s := clampf(left, 0.0, maxf(need_s, 0.0))
			b.temp += use_s / (m * Sim.STONE_C)
			applied += use_s
		return applied
	# Cooling: superheat, then latent (solidification), then sensible heat to ambient.
	var take := -e
	if b.temp > Sim.STONE_MELT_C:
		var avail := (b.temp - Sim.STONE_MELT_C) * m * Sim.STONE_C
		var use := minf(take, avail)
		b.temp -= use / (m * Sim.STONE_C)
		take -= use
		applied -= use
	if take > 0.0 and b.liquid > 0.0:
		var avail_l := b.liquid * m * Sim.STONE_LATENT
		var use_l := minf(take, avail_l)
		b.liquid = maxf(0.0, b.liquid - use_l / (m * Sim.STONE_LATENT))
		if b.liquid < 1e-6:
			b.liquid = 0.0
		take -= use_l
		applied -= use_l
	if take > 0.0 and b.liquid <= 0.0 and b.temp > Sim.AMBIENT_C:
		var avail_s := (b.temp - Sim.AMBIENT_C) * m * Sim.STONE_C
		var use_s := minf(take, avail_s)
		b.temp -= use_s / (m * Sim.STONE_C)
		applied -= use_s
	return applied


static func _water(b: MatBody, e: float) -> Vector2:
	var m := b.mass
	var applied := 0.0
	var vapor := 0.0
	if e > 0.0:
		var left := e
		if b.temp < Sim.WATER_FREEZE_C:
			var need := (Sim.WATER_FREEZE_C - b.temp) * m * Sim.WATER_C
			var use := minf(left, need)
			b.temp += use / (m * Sim.WATER_C)
			left -= use
			applied += use
		if left > 0.0 and b.liquid < 1.0:
			var need_l := (1.0 - b.liquid) * m * Sim.WATER_LATENT_FUSION
			var use_l := minf(left, need_l)
			b.liquid = minf(1.0, b.liquid + use_l / (m * Sim.WATER_LATENT_FUSION))
			left -= use_l
			applied += use_l
		if left > 0.0 and b.liquid >= 1.0 and b.temp < Sim.WATER_BOIL_C:
			var need_s := (Sim.WATER_BOIL_C - b.temp) * m * Sim.WATER_C
			var use_s := minf(left, need_s)
			b.temp += use_s / (m * Sim.WATER_C)
			left -= use_s
			applied += use_s
		if left > 0.0 and b.liquid >= 1.0:
			vapor = minf(b.mass, left / Sim.WATER_LATENT_VAPOR)
			applied += vapor * Sim.WATER_LATENT_VAPOR
			b.mass -= vapor
		return Vector2(applied, vapor)
	var take := -e
	if b.temp > Sim.WATER_FREEZE_C:
		var avail := (b.temp - Sim.WATER_FREEZE_C) * m * Sim.WATER_C
		var use := minf(take, avail)
		b.temp -= use / (m * Sim.WATER_C)
		take -= use
		applied -= use
	if take > 0.0 and b.liquid > 0.0:
		var avail_l := b.liquid * m * Sim.WATER_LATENT_FUSION
		var use_l := minf(take, avail_l)
		b.liquid = maxf(0.0, b.liquid - use_l / (m * Sim.WATER_LATENT_FUSION))
		if b.liquid < 1e-6:
			b.liquid = 0.0
		take -= use_l
		applied -= use_l
	if take > 0.0 and b.liquid <= 0.0 and b.temp > ICE_MIN_C:
		var avail_s := (b.temp - ICE_MIN_C) * m * Sim.WATER_C
		var use_s := minf(take, avail_s)
		b.temp -= use_s / (m * Sim.WATER_C)
		applied -= use_s
	return Vector2(applied, 0.0)


## Flash-boil at a water contact surface (game rule): fire energy boils water off
## the contact layer instead of warming the bulk. Returns Vector2(vaporised_kg, HU used).
static func flash_boil(b: MatBody, energy: float) -> Vector2:
	if b.mat != Sim.Mat.WATER or b.mass <= 0.0 or energy <= 0.0:
		return Vector2.ZERO
	var per_kg := Sim.WATER_LATENT_VAPOR + Sim.WATER_C * maxf(0.0, Sim.WATER_BOIL_C - b.temp)
	if b.liquid < 1.0:
		per_kg += Sim.WATER_LATENT_FUSION * (1.0 - b.liquid)
	var kg := minf(b.mass, energy / per_kg)
	b.mass -= kg
	return Vector2(kg, kg * per_kg)


## Heat (above ambient) carried away by vapour leaving at boiling point.
static func vapor_energy(kg: float) -> float:
	return kg * (Sim.WATER_LATENT_VAPOR + Sim.WATER_C * (Sim.WATER_BOIL_C - Sim.AMBIENT_C))


## Passive exchange with ambient air for one tick. Returns HU moved (negative = lost to air).
static func ambient_step(b: MatBody, dt: float) -> float:
	var k := Sim.AMBIENT_LOSS * pow(maxf(b.mass, 0.01), 2.0 / 3.0)
	if b.mat == Sim.Mat.STONE:
		var over := b.temp - Sim.AMBIENT_C
		if over <= 0.5 and b.liquid <= 0.0:
			return 0.0
		return _stone(b, -k * over / 100.0 * dt)
	if b.mat == Sim.Mat.WATER:
		if b.liquid < 1.0 or b.temp < Sim.AMBIENT_C - 0.5:
			var gain := k * ICE_AMBIENT_MULT * (Sim.AMBIENT_C - minf(b.temp, 0.0)) / 100.0 * dt
			return _water(b, gain).x
		if b.temp > Sim.AMBIENT_C + 0.5:
			return _water(b, -k * (b.temp - Sim.AMBIENT_C) / 100.0 * dt).x
	return 0.0


## Updates the phase label with hysteresis. Returns true when the label changed.
static func update_phase(b: MatBody) -> bool:
	var old := b.phase
	if b.mat == Sim.Mat.STONE:
		match b.phase:
			Sim.Phase.SOLID:
				if b.liquid >= Sim.MOLTEN_UP:
					b.phase = Sim.Phase.MOLTEN
				elif b.liquid >= Sim.SOFTEN_UP:
					b.phase = Sim.Phase.SOFTENED
			Sim.Phase.SOFTENED:
				if b.liquid >= Sim.MOLTEN_UP:
					b.phase = Sim.Phase.MOLTEN
				elif b.liquid <= Sim.SOLID_DOWN:
					b.phase = Sim.Phase.SOLID
			Sim.Phase.MOLTEN:
				if b.liquid <= Sim.SOLID_DOWN:
					b.phase = Sim.Phase.SOLID
				elif b.liquid <= Sim.MOLTEN_DOWN:
					b.phase = Sim.Phase.SOFTENED
			_:
				b.phase = Sim.Phase.SOLID
	elif b.mat == Sim.Mat.WATER:
		match b.phase:
			Sim.Phase.LIQUID:
				if b.liquid <= Sim.ICE_DOWN:
					b.phase = Sim.Phase.FROZEN
			Sim.Phase.FROZEN:
				if b.liquid >= Sim.ICE_UP:
					b.phase = Sim.Phase.LIQUID
			_:
				b.phase = Sim.Phase.LIQUID
	return old != b.phase


## Viscosity factor for flowing lava: 1 when fully molten, 0 when set.
static func flow_factor(b: MatBody) -> float:
	return smoothstep(0.05, 0.85, b.liquid)


static func heat01(b: MatBody) -> float:
	if b.mat != Sim.Mat.STONE:
		return 0.0
	return clampf((b.temp - 250.0) / (Sim.STONE_MELT_C - 250.0), 0.0, 1.0)
