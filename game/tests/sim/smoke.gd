extends SceneTree
func _init() -> void:
	var w := CombatWorld.new(7)
	var p := w.add_actor("player", Vector3(0, 0, 7), 0, {"magma": true}, Sim.Element.EARTH)
	var o := w.add_actor("opp", Vector3(0, 0, -7), 1, {"heat_draw": true}, Sim.Element.EARTH)
	var it := ActorIntent.new()
	it.attack_pressed = true
	for i in 120:
		w.step({o.id: it})
		it.attack_pressed = false
	for e in w.take_events():
		print(e)
	print(p.health, " ", p.pos)
	quit(0)
