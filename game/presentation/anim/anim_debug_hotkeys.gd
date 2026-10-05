class_name AnimDebugHotkeys
extends CanvasLayer
## In-game toggles for the animation layers, installed once when the game is started with
## `-- --animdebug` (FighterView.setup). Keys: F5 blend space, F6 foot IK, F7 look-at,
## F8 hit springs, F9 lean/landing, F10 debug markers, Shift+F10 cycles a forced detail level.
## The label in the top-left corner shows the current state.

static var _installed: AnimDebugHotkeys = null
var _label := Label.new()


static func install(from: Node) -> void:
	if is_instance_valid(_installed) or not from.is_inside_tree():
		return
	_installed = AnimDebugHotkeys.new()
	from.get_tree().root.add_child.call_deferred(_installed)


func _ready() -> void:
	layer = 50
	_label.position = Vector2(12, 64)
	_label.add_theme_font_size_override("font_size", 13)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	add_child(_label)
	_refresh()


func _unhandled_key_input(event: InputEvent) -> void:
	var k := event as InputEventKey
	if k == null or not k.pressed or k.echo:
		return
	match k.physical_keycode:
		KEY_F5:
			AnimRigSettings.loco_blend = not AnimRigSettings.loco_blend
		KEY_F6:
			AnimRigSettings.foot_ik = not AnimRigSettings.foot_ik
		KEY_F7:
			AnimRigSettings.look_at = not AnimRigSettings.look_at
		KEY_F8:
			AnimRigSettings.hit_springs = not AnimRigSettings.hit_springs
		KEY_F9:
			AnimRigSettings.lean = not AnimRigSettings.lean
		KEY_F10:
			if k.shift_pressed:
				AnimRigSettings.force_lod = AnimRigSettings.force_lod + 1 if AnimRigSettings.force_lod < 2 else -1
			else:
				AnimRigSettings.debug_draw = not AnimRigSettings.debug_draw
		_:
			return
	_refresh()


func _refresh() -> void:
	_label.text = "anim  " + AnimRigSettings.summary() + "\nF5 blend  F6 IK  F7 look  F8 hits  F9 lean  F10 markers  Shift+F10 LOD"
