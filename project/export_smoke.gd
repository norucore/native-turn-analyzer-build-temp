extends Node


func _ready() -> void:
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		_fail("NativeTurnAnalyzer class is unavailable in the exported application")
		return

	var analyzer := NativeTurnAnalyzer.new()
	var packet: Dictionary = analyzer.analyze_and_compile(
		"Walk toward the observatory, next inspect the brass console.",
		{
			"actions": {
				"move_to": {"action": "move_to", "target_mode": "required"},
				"inspect": {"action": "inspect", "target_mode": "required"},
			},
			"alias_index": {
				"walk toward": ["move_to"],
				"inspect": ["inspect"],
			},
		},
		{},
		{}
	)
	var validation: Dictionary = packet.get("validation", {})
	if not bool(validation.get("valid", false)):
		_fail("native packet failed validation: " + JSON.stringify(packet))
		return
	var frames: Array = packet.get("compiler_output", {}).get("frames", [])
	if frames.size() != 2:
		_fail("expected two exported-runtime frames: " + JSON.stringify(packet))
		return
	var actions: Array[String] = []
	for raw_frame in frames:
		var frame: Dictionary = raw_frame if raw_frame is Dictionary else {}
		var roles: Dictionary = frame.get("semantic_roles", {}) if frame.get("semantic_roles", {}) is Dictionary else {}
		actions.append(str(roles.get("action", "")))
	if actions != ["move_to", "inspect"]:
		_fail("unexpected exported-runtime actions: " + JSON.stringify(actions))
		return
	print("NTA_EXPORT_RUNTIME_OK " + JSON.stringify({
		"actions": actions,
		"build": analyzer.get_build_info(),
		"frame_count": frames.size(),
	}))
	get_tree().quit(0)


func _fail(message: String) -> void:
	push_error("NTA_EXPORT_RUNTIME_FAILED " + message)
	get_tree().quit(1)
