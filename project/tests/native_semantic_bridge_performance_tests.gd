extends SceneTree

const GRAPH_PATH := "res://data/semantic_action_graph_v1.json"
const WARMUP_TURNS := 100
const MEASURED_TURNS := 5000

func _init() -> void:
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		push_error("NativeTurnAnalyzer unavailable")
		quit(1)
		return
	var graph := _load_graph()
	if graph.is_empty():
		push_error("semantic graph unavailable")
		quit(1)
		return
	var native = ClassDB.instantiate("NativeTurnAnalyzer")
	var snapshot := _snapshot()
	var analysis: Dictionary = native.analyze_turn("Locate the key")
	var options := {"semantic_action_graph": graph}
	for _index in WARMUP_TURNS:
		native.propose_semantic_links("Locate the key", analysis, snapshot, options)
	var durations: Array[int] = []
	for _index in MEASURED_TURNS:
		var started := Time.get_ticks_usec()
		var result: Dictionary = native.propose_semantic_links("Locate the key", analysis, snapshot, options)
		durations.append(Time.get_ticks_usec() - started)
		if str(result.get("status", "")) != "CANDIDATES":
			push_error("unexpected bridge status")
			quit(1)
			return
	durations.sort()
	var p95_usec := durations[int(ceil(MEASURED_TURNS * 0.95)) - 1]
	var max_usec := durations[-1]
	print("native_semantic_bridge_performance_tests turns=%d p95_usec=%d max_usec=%d" % [MEASURED_TURNS, p95_usec, max_usec])
	quit(0 if p95_usec < 50000 else 1)

func _load_graph() -> Dictionary:
	var file := FileAccess.open(GRAPH_PATH, FileAccess.READ)
	if file == null:
		return {}
	var parsed = JSON.parse_string(file.get_as_text())
	return parsed if parsed is Dictionary else {}

func _snapshot() -> Dictionary:
	return {
		"schema": "brain_capability_registry_v1",
		"actions": {
			"search": {"action": "search", "target_mode": "required", "preconditions": {}, "effects": {}},
			"break": {"action": "break", "target_mode": "required", "preconditions": {}, "effects": {}},
		},
		"alias_index": {"search": ["search"], "break": ["break"]},
	}
