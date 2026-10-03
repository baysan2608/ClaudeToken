class_name AudioDirector
extends Node
## Positional SFX with per-sound voice caps (from assets/audio/manifest.json),
## managed loops that start/stop on state (never restarted per frame) and an
## ambience bed. Buses: SFX, UI, Ambience (created at runtime if missing).

const DIR := "res://assets/audio/"

var _manifest := {}
var _streams := {}
var _voices := {}      # name -> Array[AudioStreamPlayer3D]
var _next := {}        # name -> round-robin index
var _loops := {}       # key -> {player: AudioStreamPlayer3D, name: String, on: bool, fade: float}
var _ui: Array[AudioStreamPlayer] = []
var _amb: AudioStreamPlayer
var _step_i := 0
var enabled := true


func _ready() -> void:
	_ensure_bus("SFX")
	_ensure_bus("UI")
	_ensure_bus("Ambience")
	if FileAccess.file_exists(DIR + "manifest.json"):
		var j: Variant = JSON.parse_string(FileAccess.get_file_as_string(DIR + "manifest.json"))
		if j is Dictionary:
			_manifest = j
	for i in 3:
		var u := AudioStreamPlayer.new()
		u.bus = "UI"
		add_child(u)
		_ui.append(u)
	_amb = AudioStreamPlayer.new()
	_amb.bus = "Ambience"
	add_child(_amb)


func _ensure_bus(nm: String) -> void:
	if AudioServer.get_bus_index(nm) >= 0:
		return
	AudioServer.add_bus()
	var i := AudioServer.bus_count - 1
	AudioServer.set_bus_name(i, nm)
	AudioServer.set_bus_send(i, "Master")


func _stream(nm: String) -> AudioStream:
	if _streams.has(nm):
		return _streams[nm]
	var path := DIR + nm + ".wav"
	var s: AudioStream = load(path) if ResourceLoader.exists(path) else null
	if s is AudioStreamWAV and _manifest.get(nm, {}).get("loop", false):
		var w := s as AudioStreamWAV
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = int(w.get_length() * w.mix_rate)
	_streams[nm] = s
	return s


func _vol(nm: String) -> float:
	return float(_manifest.get(nm, {}).get("suggested_volume_db", -6.0))


func play(nm: String, pos: Vector3, vol_db: float = 0.0, pitch_var: float = 0.04) -> void:
	if not enabled:
		return
	var s := _stream(nm)
	if s == null:
		return
	var pool: Array = _voices.get(nm, [])
	var cap := int(_manifest.get(nm, {}).get("max_voices", 3))
	var p: AudioStreamPlayer3D = null
	for v in pool:
		if not (v as AudioStreamPlayer3D).playing:
			p = v
			break
	if p == null:
		if pool.size() < cap:
			p = AudioStreamPlayer3D.new()
			p.bus = "SFX"
			p.unit_size = 6.0
			p.max_distance = 45.0
			p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
			add_child(p)
			pool.append(p)
			_voices[nm] = pool
		else:
			var i: int = _next.get(nm, 0)
			p = pool[i % pool.size()]
			_next[nm] = i + 1
	p.stream = s
	p.volume_db = _vol(nm) + vol_db
	p.pitch_scale = 1.0 + randf_range(-pitch_var, pitch_var)
	p.global_position = pos
	p.play()


func ui(nm: String, vol_db: float = 0.0) -> void:
	var s := _stream(nm)
	if s == null:
		return
	for u in _ui:
		if not u.playing:
			u.stream = s
			u.volume_db = _vol(nm) + vol_db
			u.play()
			return


func step(pos: Vector3) -> void:
	_step_i = (_step_i + 1) % 4
	play("step_stone_%d" % (_step_i + 1), pos, randf_range(-1.5, 0.5), 0.04)


## Keeps a looping sound alive while `on` is true; fades out when it turns false.
func loop(key: String, nm: String, on: bool, pos: Vector3, vol_db: float = 0.0) -> void:
	var l: Dictionary = _loops.get(key, {})
	if l.is_empty():
		if not on:
			return
		var p := AudioStreamPlayer3D.new()
		p.bus = "SFX"
		p.unit_size = 6.0
		p.max_distance = 40.0
		add_child(p)
		l = {"player": p, "name": "", "on": false, "fade": 0.0}
		_loops[key] = l
	var pl: AudioStreamPlayer3D = l.player
	pl.global_position = pos
	if on:
		if l.name != nm or not pl.playing:
			pl.stream = _stream(nm)
			if pl.stream == null:
				return
			pl.volume_db = -30.0
			pl.play()
			l.name = nm
		l.on = true
		l.target = _vol(nm) + vol_db
	else:
		l.on = false


func _process(dt: float) -> void:
	var dead: Array = []
	for key in _loops:
		var l: Dictionary = _loops[key]
		var pl: AudioStreamPlayer3D = l.player
		if not pl.playing:
			if not l.on:
				dead.append(key)   # free idle loop players so per-body keys never accumulate
			continue
		if l.on:
			pl.volume_db = move_toward(pl.volume_db, float(l.get("target", -6.0)), 90.0 * dt)
		else:
			pl.volume_db = move_toward(pl.volume_db, -40.0, 60.0 * dt)
			if pl.volume_db <= -39.0:
				pl.stop()
	for key in dead:
		(_loops[key].player as Node).queue_free()
		_loops.erase(key)


func stop_all_loops() -> void:
	for key in _loops:
		(_loops[key].player as AudioStreamPlayer3D).stop()
		_loops[key].on = false


func ambience(on: bool) -> void:
	if on and not _amb.playing:
		_amb.stream = _stream("amb_courtyard_loop")
		if _amb.stream:
			_amb.volume_db = _vol("amb_courtyard_loop")
			_amb.play()
	elif not on:
		_amb.stop()
