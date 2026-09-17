extends SceneTree

const CORPUS = preload("res://tests/native_acceptance_corpus.gd")
const ITERATIONS := 100_000
const ACTIONABLE_TYPES := ["world_action", "inventory_action"]


func _initialize() -> void:
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		_fail("NativeTurnAnalyzer class is unavailable", {})
		return
	var analyzer = ClassDB.instantiate("NativeTurnAnalyzer")
	var registry := _build_registry()
	var started := Time.get_ticks_usec()
	var result_counts := {}
	for index in ITERATIONS:
		var spec: Dictionary = CORPUS.ACTIONS[index % CORPUS.ACTIONS.size()]
		var category := index % 10
		var input := _build_input(index, category, spec)
		var packet: Dictionary = analyzer.analyze_and_compile(input, registry, {}, {})
		if not bool(packet.get("validation", {}).get("valid", false)):
			_fail("invalid packet", {"index": index, "input": input, "packet": packet})
			return
		var frames: Array = packet.get("compiler_output", {}).get("frames", [])
		if frames.size() > 32:
			_fail("frame limit exceeded", {"index": index, "input": input, "frames": frames.size()})
			return
		var actions := _action_names(frames)
		if category == 0:
			if actions != [str(spec.action)]:
				_fail("direct request lost unique action", {"index": index, "input": input, "expected": spec.action, "actions": actions, "frames": frames})
				return
		elif not actions.is_empty():
			_fail("non-executable construction produced an action", {"index": index, "category": category, "input": input, "actions": actions, "frames": frames})
			return
		if category in [6, 8] and not _has_type(frames, "clarification"):
			_fail("incomplete or ambiguous request did not clarify", {"index": index, "category": category, "input": input, "frames": frames})
			return
		var kind := str(frames[0].get("frame_type", "empty")) if not frames.is_empty() else "empty"
		result_counts[kind] = int(result_counts.get(kind, 0)) + 1
		if index % 1000 == 0:
			var repeated: Dictionary = analyzer.analyze_and_compile(input, registry, {}, {})
			if JSON.stringify(_stable_summary(packet)) != JSON.stringify(_stable_summary(repeated)):
				_fail("same input produced a different contract", {"index": index, "input": input, "first": _stable_summary(packet), "second": _stable_summary(repeated)})
				return
	var elapsed := Time.get_ticks_usec() - started
	print("native_fuzz_property_tests ok iterations=%d elapsed_ms=%.3f avg_usec=%.3f result_types=%s" % [
		ITERATIONS, float(elapsed) / 1000.0, float(elapsed) / float(ITERATIONS), JSON.stringify(result_counts),
	])
	quit(0)


func _build_input(index: int, category: int, spec: Dictionary) -> String:
	var setting: String = CORPUS.SETTINGS[(index * 7) % CORPUS.SETTINGS.size()]
	var name: String = CORPUS.NAMES[(index * 11) % CORPUS.NAMES.size()]
	match category:
		0:
			return "%s, please %s %s near %s." % [name, spec.verb, spec.target, setting]
		1:
			return "At %s, do not %s %s." % [setting, spec.verb, spec.target]
		2:
			return "\"%s %s immediately.\"" % [str(spec.verb).capitalize(), spec.target]
		3:
			return "{\"action\":\"%s\",\"target\":\"%s\"}" % [spec.verb, spec.target]
		4:
			return "Yesterday near %s, you %s %s." % [setting, spec.past, spec.target]
		5:
			return "Did you %s %s before %s arrived?" % [spec.verb, spec.target, name]
		6:
			return "Please %s—" % spec.verb
		7:
			return "%s %s... no, dont do anything yet." % [str(spec.verb).capitalize(), spec.target]
		8:
			return "Please %s the other one near %s." % [spec.verb, setting]
		_:
			return "Please transmogrif the %s marker beside %s." % [name.to_lower(), setting]


func _build_registry() -> Dictionary:
	var actions := {}
	var aliases := {}
	for spec in CORPUS.ACTIONS:
		var action := str(spec.action)
		actions[action] = {"action": action, "target_mode": "required"}
		_add_alias(aliases, action.replace("_", " "), action)
		_add_alias(aliases, str(spec.verb), action)
	return {"schema": "brain_capability_registry_v1", "actions": actions, "alias_index": aliases}


func _add_alias(aliases: Dictionary, surface: String, action: String) -> void:
	var values: Array = aliases.get(surface, [])
	if not values.has(action):
		values.append(action)
	aliases[surface] = values


func _action_names(frames: Array) -> Array[String]:
	var output: Array[String] = []
	for frame in frames:
		if not ACTIONABLE_TYPES.has(str(frame.get("frame_type", ""))):
			continue
		var roles: Dictionary = frame.get("semantic_roles", {}) if frame.get("semantic_roles", {}) is Dictionary else {}
		var action := str(roles.get("action", ""))
		if not action.is_empty():
			output.append(action)
	return output


func _has_type(frames: Array, expected: String) -> bool:
	for frame in frames:
		if str(frame.get("frame_type", "")) == expected:
			return true
	return false


func _stable_summary(packet: Dictionary) -> Dictionary:
	var analysis: Dictionary = packet.get("analysis_result", {}).duplicate(true) if packet.get("analysis_result", {}) is Dictionary else {}
	analysis.erase("elapsed_usec")
	return {
		"native_packet_version": packet.get("native_packet_version", ""),
		"analysis_result": analysis,
		"compiler_frames": packet.get("compiler_output", {}).get("frames", []),
		"validation": packet.get("validation", {}),
	}


func _fail(message: String, evidence: Dictionary) -> void:
	push_error(message + ": " + JSON.stringify(evidence))
	quit(1)
