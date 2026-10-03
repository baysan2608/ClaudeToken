class_name PerfMonitor
extends RefCounted
## Rolling frame-time and sim-step statistics (avg / p95 / p99), plus a
## session log for sustained-performance evidence.

const N := 600
var _ft := PackedFloat32Array()
var _sim := PackedFloat32Array()
var _i := 0
var _j := 0
var total_frames := 0
var spikes_over_20ms := 0
var session := []        # per-second samples: {t, fps, p95, p99, sim_ms, mem_mb, bodies}
var _acc := 0.0
var _t := 0.0


func _init() -> void:
	_ft.resize(N)
	_sim.resize(N)


func frame(dt: float) -> void:
	var ms := dt * 1000.0
	_ft[_i % N] = ms
	_i += 1
	total_frames += 1
	if ms > 20.0:
		spikes_over_20ms += 1
	_acc += dt
	_t += dt
	if _acc >= 1.0:
		_acc = 0.0
		var st := stats()
		st["t"] = snappedf(_t, 0.1)
		st["mem_mb"] = snappedf(Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0, 0.1)
		st["objects"] = Performance.get_monitor(Performance.OBJECT_COUNT)
		st["nodes"] = Performance.get_monitor(Performance.OBJECT_NODE_COUNT)
		session.append(st)


func sim_us(us: int) -> void:
	_sim[_j % N] = us / 1000.0
	_j += 1


func _pct(arr: PackedFloat32Array, count: int, q: float) -> float:
	var n := mini(count, N)
	if n == 0:
		return 0.0
	var a := arr.slice(0, n)
	a.sort()
	return a[clampi(int(q * (n - 1)), 0, n - 1)]


func stats() -> Dictionary:
	var n := mini(_i, N)
	var s := 0.0
	for k in n:
		s += _ft[k]
	var avg := s / maxf(1.0, n)
	return {"avg_ms": snappedf(avg, 0.01), "p95_ms": snappedf(_pct(_ft, _i, 0.95), 0.01), "p99_ms": snappedf(_pct(_ft, _i, 0.99), 0.01),
		"fps": snappedf(1000.0 / maxf(avg, 0.001), 0.1), "sim_p95_ms": snappedf(_pct(_sim, _j, 0.95), 0.001)}


func summary() -> String:
	var st := stats()
	return "fps %.0f  avg %.1f  p95 %.1f  p99 %.1f ms  sim p95 %.2f ms" % [st.fps, st.avg_ms, st.p95_ms, st.p99_ms, st.sim_p95_ms]


func dump(path: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"final": stats(), "frames": total_frames, "spikes_over_20ms": spikes_over_20ms, "per_second": session}, "  "))
