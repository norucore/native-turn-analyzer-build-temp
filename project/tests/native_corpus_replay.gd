extends SceneTree

const CORPUS = preload("res://tests/native_acceptance_corpus.gd")
const ACTIONABLE_TYPES := ["world_action", "inventory_action"]


func _initialize() -> void:
	var report_path := OS.get_temp_dir().path_join("native_turn_analyzer_820_report.json")
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		_fail("NativeTurnAnalyzer class is unavailable")
		return
	var analyzer = ClassDB.instantiate("NativeTurnAnalyzer")
	var registry := _build_registry()
	var cases: Array[Dictionary] = CORPUS.build()
	var failures: Array[Dictionary] = []
	var level_stats := {}
	var timings: Array[int] = []
	for case in cases:
		var evaluation := _evaluate_case(analyzer, registry, case, timings)
		var level := str(case.level)
		var stats: Dictionary = level_stats.get(level, {"total": 0, "passed": 0, "failed": 0})
		stats.total = int(stats.total) + 1
		if bool(evaluation.passed):
			stats.passed = int(stats.passed) + 1
		else:
			stats.failed = int(stats.failed) + 1
			failures.append({
				"id": case.id,
				"level": level,
				"theme": case.theme,
				"expected_kind": case.expected_kind,
				"expected_action": case.action,
				"expected_target": case.expected_target,
				"turns": case.turns,
				"reason": evaluation.reason,
				"observed": evaluation.observed,
			})
		level_stats[level] = stats
	timings.sort()
	var report := {
		"report_version": "native_820_replay_v1",
		"case_count": cases.size(),
		"passed": cases.size() - failures.size(),
		"failed": failures.size(),
		"level_stats": level_stats,
		"timings_usec": {
			"p50": _percentile(timings, 0.50),
			"p95": _percentile(timings, 0.95),
			"p99": _percentile(timings, 0.99),
			"max": timings[-1] if not timings.is_empty() else 0,
		},
		"failures": failures,
	}
	var file := FileAccess.open(report_path, FileAccess.WRITE)
	if file == null:
		_fail("cannot write replay report: " + report_path)
		return
	file.store_string(JSON.stringify(report, "  "))
	file.close()
	print("native_corpus_replay cases=%d passed=%d failed=%d p95_usec=%d report=%s" % [
		cases.size(), report.passed, report.failed, report.timings_usec.p95, report_path,
	])
	print("native_corpus_replay levels=" + JSON.stringify(level_stats))
	quit(0)


func _build_registry() -> Dictionary:
	var actions := {}
	var aliases := {}
	for spec in CORPUS.ACTIONS:
		var action := str(spec.action)
		actions[action] = {"action": action, "target_mode": "required"}
		_add_alias(aliases, action.replace("_", " "), action)
		_add_alias(aliases, str(spec.verb), action)
	return {
		"schema": "brain_capability_registry_v1",
		"actions": actions,
		"alias_index": aliases,
	}


func _add_alias(aliases: Dictionary, surface: String, action: String) -> void:
	var values: Array = aliases.get(surface, [])
	if not values.has(action):
		values.append(action)
	aliases[surface] = values


func _evaluate_case(analyzer, registry: Dictionary, case: Dictionary, timings: Array[int]) -> Dictionary:
	var observed: Array[Dictionary] = []
	var turns: Array = case.turns
	var pending := {}
	for turn_index in turns.size():
		var scene_state := {"pending_clarification": pending.duplicate(true)} if not pending.is_empty() else {}
		var packet: Dictionary = analyzer.analyze_and_compile(str(turns[turn_index]), registry, scene_state, {})
		timings.append(int(packet.get("metrics", {}).get("total_usec", 0)))
		var frames: Array = packet.get("compiler_output", {}).get("frames", [])
		observed.append({"turn": turn_index, "frames": _summarize(frames)})
		for frame in frames:
			var frame_type := str(frame.get("frame_type", ""))
			var roles: Dictionary = frame.get("semantic_roles", {})
			var action := str(roles.get("action", ""))
			if frame_type == "clarification" and not action.is_empty():
				var lower_turn := str(turns[turn_index]).strip_edges().to_lower()
				pending = {
					"action": action,
					"conditional": lower_turn.begins_with("if ") or lower_turn.begins_with("unless ") or lower_turn.begins_with("when ") or bool(pending.get("conditional", false)),
					"age_turns": int(pending.get("age_turns", -1)) + 1,
				}
			elif ACTIONABLE_TYPES.has(frame_type):
				pending = {}
	var expected_kind := str(case.expected_kind)
	var expected_action := str(case.action)
	var expected_target := str(case.expected_target)
	var last_frames: Array = observed[-1].frames if not observed.is_empty() else []
	var last_actions := _action_names(last_frames)
	var last_targets := _action_targets(last_frames)
	match expected_kind:
		"request", "conditional_request":
			if last_actions == [expected_action] and last_targets == [expected_target]:
				return {"passed": true, "reason": "", "observed": observed}
			return {"passed": false, "reason": "expected_unique_action_and_target", "observed": observed}
		"non_executable":
			if _all_turns_safe(observed):
				return {"passed": true, "reason": "", "observed": observed}
			return {"passed": false, "reason": "unexpected_execution", "observed": observed}
		"state_query":
			if _has_type(last_frames, "state_query") and last_actions.is_empty():
				return {"passed": true, "reason": "", "observed": observed}
			return {"passed": false, "reason": "expected_state_query", "observed": observed}
		"clarification", "conditional_clarification":
			if _all_turns_safe(observed) and (_has_type(last_frames, "clarification") or expected_kind == "conditional_clarification"):
				return {"passed": true, "reason": "", "observed": observed}
			return {"passed": false, "reason": "expected_safe_clarification", "observed": observed}
		"clarification_resolution":
			if _all_but_last_safe(observed) and last_actions == [expected_action] and last_targets == [expected_target]:
				return {"passed": true, "reason": "", "observed": observed}
			return {"passed": false, "reason": "pending_context_not_resolved", "observed": observed}
	return {"passed": false, "reason": "unsupported_expectation", "observed": observed}


func _summarize(frames: Array) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for frame in frames:
		var roles: Dictionary = frame.get("semantic_roles", {})
		output.append({
			"type": str(frame.get("frame_type", "")),
			"speech_act": str(frame.get("speech_act", "")),
			"action": str(roles.get("action", "")),
			"object": str(roles.get("object", "")),
			"destination": str(roles.get("destination", "")),
			"query_kind": str(roles.get("query_kind", "")),
		})
	return output


func _action_names(frames: Array) -> Array[String]:
	var actions: Array[String] = []
	for frame in frames:
		if not ACTIONABLE_TYPES.has(str(frame.get("type", ""))):
			continue
		var action := str(frame.get("action", ""))
		if not action.is_empty():
			actions.append(action)
	return actions


func _action_targets(frames: Array) -> Array[String]:
	var targets: Array[String] = []
	for frame in frames:
		if ACTIONABLE_TYPES.has(str(frame.get("type", ""))):
			var object := str(frame.get("object", ""))
			var destination := str(frame.get("destination", ""))
			targets.append(object if destination.is_empty() or destination == object else "%s|%s" % [object, destination])
	return targets


func _has_type(frames: Array, expected: String) -> bool:
	for frame in frames:
		if str(frame.get("type", "")) == expected:
			return true
	return false


func _all_turns_safe(observed: Array[Dictionary]) -> bool:
	for turn in observed:
		if not _action_names(turn.frames).is_empty():
			return false
	return true


func _all_but_last_safe(observed: Array[Dictionary]) -> bool:
	for index in maxi(0, observed.size() - 1):
		if not _action_names(observed[index].frames).is_empty():
			return false
	return true


func _percentile(sorted_values: Array[int], ratio: float) -> int:
	if sorted_values.is_empty():
		return 0
	var index := clampi(int(ceil(ratio * sorted_values.size())) - 1, 0, sorted_values.size() - 1)
	return sorted_values[index]


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
