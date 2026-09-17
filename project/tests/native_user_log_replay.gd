extends SceneTree

func _init() -> void:
	var analyzer := NativeTurnAnalyzer.new()
	var capabilities := {
		"actions": {
			"pickup": {"action": "pickup", "target_mode": "required"},
			"move_to": {"action": "move_to", "target_mode": "required"},
			"climb": {"action": "climb", "target_mode": "required"},
			"jump": {"action": "jump", "target_mode": "optional"},
			"gesture": {"action": "gesture", "target_mode": "optional"},
			"give": {"action": "give", "target_mode": "required"},
		},
		"alias_index": {
			"pick up": ["pickup"],
			"take": ["pickup"],
			"go": ["move_to"],
			"return": ["move_to"],
			"come back": ["move_to"],
			"get on": ["climb"],
			"jump": ["jump"],
			"high five": ["gesture"],
			"hit five": ["gesture"],
			"gift": ["give"],
		},
	}
	var messages := [
		"hello there, what's your name? you have a sword with you?",
		"So, you can gift me your book?",
		"Gulia go to the sofa and then get on it do 3 jumps on the spot and with the last jump instead of landing on the sofa land on the floor and then come back here where I am and hit five.",
		"Hi what's your name? Wait before you tell me would you get me the newspaper on the table please.",
		"Nice to meet you Gulia, You get me the newspaper on the table please.",
	]
	var expected_actions := [
		["", ""],
		["give"],
		["move_to", "climb", "jump", "jump", "jump", "move_to", "gesture"],
		["", "pickup", "give"],
		["", "pickup", "give"],
	]
	var expected_targets := [
		["", "sword"],
		["book"],
		["sofa", "sofa", "sofa", "sofa", "floor", "user", "user"],
		["", "newspaper", "newspaper"],
		["", "newspaper", "newspaper"],
	]
	for message_index in messages.size():
		var message: String = messages[message_index]
		var packet: Dictionary = analyzer.analyze_and_compile(message, capabilities, {}, {})
		var frames: Array = packet.get("compiler_output", {}).get("frames", [])
		var normalized_input := str(packet.get("analysis_result", {}).get("normalized_input", ""))
		print("NATIVE_USER_LOG_REPLAY ", JSON.stringify({
			"input": message,
			"frames": frames,
		}))
		var actions: Array[String] = []
		var targets: Array[String] = []
		for raw_frame in frames:
			var frame: Dictionary = raw_frame if raw_frame is Dictionary else {}
			var source_spans: Array = frame.get("source_spans", []) if frame.get("source_spans", []) is Array else []
			if source_spans.is_empty():
				push_error("native_user_log_replay missing source span index=%d frame=%s" % [message_index, frame.get("frame_id", "")])
				quit(1)
				return
			var source_span: Dictionary = source_spans[0] if source_spans[0] is Dictionary else {}
			var span_start := int(source_span.get("start_index", -1))
			var span_end := int(source_span.get("end_index", -1))
			var span_text := str(source_span.get("text", ""))
			if span_start < 0 or span_end <= span_start or span_end > normalized_input.length() or normalized_input.substr(span_start, span_end - span_start) != span_text:
				push_error("native_user_log_replay invalid source span index=%d frame=%s span=%s" % [message_index, frame.get("frame_id", ""), source_span])
				quit(1)
				return
			var roles: Dictionary = frame.get("semantic_roles", {}) if frame.get("semantic_roles", {}) is Dictionary else {}
			actions.append(str(roles.get("action", "")))
			targets.append(str(roles.get("object", "")))
		if actions != expected_actions[message_index] or targets != expected_targets[message_index]:
			push_error("native_user_log_replay mismatch index=%d actions=%s targets=%s" % [message_index, actions, targets])
			quit(1)
			return
	print("native_user_log_replay ok cases=5")
	quit(0)
