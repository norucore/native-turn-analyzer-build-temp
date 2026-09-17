extends SceneTree

var _failures: Array[String] = []

func _init() -> void:
	var analyzer := NativeTurnAnalyzer.new()
	var registry := {
		"actions": {
			"pickup": {"action": "pickup", "target_mode": "required"},
			"give": {"action": "give", "target_mode": "required"},
			"climb": {"action": "climb", "target_mode": "required"},
			"drop": {"action": "drop", "target_mode": "required"},
		},
		"alias_index": {
			"pick up": ["pickup"],
			"give": ["give"],
			"get on": ["climb"],
			"leave": ["drop"],
		},
	}
	_expect_fetch(analyzer, registry, "Could you retrieve the copper folio for me from the archive desk?", "copper folio", "user", "archive desk")
	_expect_fetch(analyzer, registry, "Bring the weather report to her from the marble lectern.", "weather report", "her", "marble lectern")
	_expect_fetch(analyzer, registry, "Bring the enamel compass from the glass cabinet to them.", "enamel compass", "them", "glass cabinet")
	_expect_fetch(analyzer, registry, "You fetch us the brass token please.", "brass token", "user", "")
	_expect_fetch(analyzer, registry, "Get the violet ledger for me.", "violet ledger", "user", "")
	_expect_fetch(analyzer, registry, "Please get me the sealed itinerary.", "sealed itinerary", "user", "")
	_expect_non_executable(analyzer, registry, "The archivist would get me the copper folio tomorrow.")
	_expect_non_executable(analyzer, registry, "I got the brass token during yesterday's inspection.")
	_expect_non_executable(analyzer, registry, "She will bring me the weather report later.")
	_expect_action(analyzer, registry, "Would you get on the cedar platform?", "climb")
	_expect_clarification(analyzer, registry, "Would you fetch me?")
	_expect_non_executable(analyzer, registry, "If the steel door is locked, would you leave it closed?")
	_expect_action(analyzer, registry, "Leave the book.", "drop")
	if _failures.is_empty():
		print("native_compositional_construction_tests ok cases=13")
		quit(0)
		return
	for failure in _failures:
		push_error(failure)
	quit(1)

func _frames(analyzer, registry: Dictionary, input: String) -> Array:
	return analyzer.analyze_and_compile(input, registry, {}, {}).get("compiler_output", {}).get("frames", [])

func _expect_fetch(analyzer, registry: Dictionary, input: String, object_name: String, recipient: String, source: String) -> void:
	var frames := _frames(analyzer, registry, input)
	if frames.size() != 2:
		_failures.append("fetch frame count input=%s frames=%s" % [input, JSON.stringify(frames)])
		return
	var pickup: Dictionary = frames[0].get("semantic_roles", {})
	var give: Dictionary = frames[1].get("semantic_roles", {})
	if str(pickup.get("action", "")) != "pickup" or str(give.get("action", "")) != "give":
		_failures.append("fetch action expansion input=%s frames=%s" % [input, JSON.stringify(frames)])
	if str(pickup.get("object", "")) != object_name or str(give.get("object", "")) != object_name:
		_failures.append("fetch object role input=%s frames=%s" % [input, JSON.stringify(frames)])
	if str(give.get("recipient_or_direction", "")) != recipient:
		_failures.append("fetch recipient role input=%s frame=%s" % [input, JSON.stringify(frames[1])])
	if str(pickup.get("source", "")) != source:
		_failures.append("fetch source role input=%s frame=%s" % [input, JSON.stringify(frames[0])])
	if frames[1].get("dependency_refs", []) != ["frame_001"]:
		_failures.append("fetch dependency input=%s frames=%s" % [input, JSON.stringify(frames)])

func _expect_non_executable(analyzer, registry: Dictionary, input: String) -> void:
	for frame in _frames(analyzer, registry, input):
		if str(frame.get("frame_type", "")) in ["world_action", "inventory_action"]:
			_failures.append("non-request became executable input=%s frame=%s" % [input, JSON.stringify(frame)])

func _expect_action(analyzer, registry: Dictionary, input: String, action: String) -> void:
	var frames := _frames(analyzer, registry, input)
	if frames.size() != 1 or str(frames[0].get("semantic_roles", {}).get("action", "")) != action:
		_failures.append("expected action=%s input=%s frames=%s" % [action, input, JSON.stringify(frames)])

func _expect_clarification(analyzer, registry: Dictionary, input: String) -> void:
	var frames := _frames(analyzer, registry, input)
	if frames.size() != 1 or str(frames[0].get("frame_type", "")) != "clarification":
		_failures.append("missing fetch target did not clarify input=%s frames=%s" % [input, JSON.stringify(frames)])
