extends SceneTree

const GRAPH_PATH := "res://data/semantic_action_graph_v1.json"

var _failures: Array[String] = []

func _init() -> void:
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		_fail("plugin unavailable")
		_finish()
		return
	var native = ClassDB.instantiate("NativeTurnAnalyzer")
	var graph := _load_graph()
	var snapshot := _snapshot(["search", "break", "pickup"])
	var locate := _propose(native, "Locate the key", snapshot, graph)
	_expect(str(locate.get("status", "")) == "CANDIDATES", "locate status")
	_expect(_first_action(locate) == "search", "locate -> search")
	_expect(str(_first_candidate(locate).get("relation", "")) == "EQUIVALENT", "locate relation")
	_expect(str(_first_request(locate).get("semantic_roles", {}).get("object", "")) == "key", "locate preserves analyzer object role")

	var dependency_graph := {
		"schema_contract": "semantic_action_graph_v1", "version": "dependency-fixture", "provenance": {"source": "fixture"},
		"entries": [
			{"surface": "locate", "lemma": "locate", "particle": "", "target_mode": "required", "candidates": [{"action": "search", "relation": "EQUIVALENT", "confidence": 0.97, "role_compatibility": 1.0, "precondition_delta": {}, "effect_delta": {}}]},
			{"surface": "inspect", "lemma": "inspect", "particle": "", "target_mode": "required", "candidates": [{"action": "examine", "relation": "EQUIVALENT", "confidence": 0.97, "role_compatibility": 1.0, "precondition_delta": {}, "effect_delta": {}}]}
		]
	}
	var ordered := _propose(native, "Locate the key, then inspect it", _snapshot(["search", "examine"]), dependency_graph)
	var inspect_request := _request_for_surface(ordered, "inspect")
	_expect(str(_request_for_surface(ordered, "locate").get("clause_id", "")) == "clause_001", "first OOV predicate exports stable clause identity")
	_expect(str(_request_for_surface(ordered, "locate").get("predicate_id", "")) == "clause_001_predicate_001", "first OOV predicate exports stable identity")
	_expect(str(inspect_request.get("predicate_id", "")) == "clause_002_predicate_001", "second OOV predicate exports stable identity")
	_expect(str(inspect_request.get("semantic_roles", {}).get("object", "")) == "it", "second OOV predicate preserves pronoun role")
	_expect(inspect_request.get("dependency_refs", []) == ["clause_001"], "second OOV predicate preserves analyzer dependency")
	var same_clause := _propose(native, "Locate the key and inspect it", _snapshot(["search", "examine"]), dependency_graph)
	_expect(_request_for_surface(same_clause, "inspect").get("dependency_refs", []) == ["clause_001_predicate_001"], "same-clause OOV dependency uses predicate identity")

	var direct := _propose(native, "Search the key", snapshot, graph)
	_expect(str(direct.get("status", "")) == "NOT_NEEDED", "direct match priority")
	var mixed := _propose(native, "Open the door and locate the key", _snapshot(["open", "search"]), graph)
	_expect(str(_request_for_surface(mixed, "locate").get("candidates", [])[0].get("action", "")) == "search" if not _request_for_surface(mixed, "locate").get("candidates", []).is_empty() else false, "mixed direct plus OOV preserves OOV predicate")
	_expect(str(_request_for_surface(mixed, "locate").get("semantic_roles", {}).get("object", "")) == "key", "mixed OOV predicate keeps its own target")
	_expect(_request_for_surface(mixed, "locate").get("dependency_refs", []) == ["clause_001_predicate_001"], "mixed OOV predicate depends on direct predicate")
	var mixed_packet: Dictionary = native.analyze_and_compile(
		"Open the door and locate the key",
		_snapshot(["open", "search"]),
		{},
		{"semantic_bridge_enabled": true, "semantic_action_graph": graph}
	)
	var mixed_frames: Array = mixed_packet.get("compiler_output", {}).get("frames", [])
	_expect(not mixed_frames.is_empty() and str(mixed_frames[0].get("semantic_roles", {}).get("object", "")) == "door", "mixed compile keeps direct target isolated")
	_expect(str(_request_for_surface(mixed_packet.get("semantic_link_result", {}), "locate").get("semantic_roles", {}).get("object", "")) == "key", "mixed compile keeps OOV target isolated")
	var narrative := _propose(native, "Alice will locate the key", snapshot, graph)
	_expect(_candidate_count(narrative) == 0, "third-person future narrative has no candidate")
	var hedged := _propose(native, "Perhaps locate the key later", snapshot, graph)
	_expect(_candidate_count(hedged) == 0, "hedged locate statement has no candidate")
	var negated := _propose(native, "Do not locate the key", snapshot, graph)
	_expect(_candidate_count(negated) == 0, "negation has no candidate")
	var quoted := _propose(native, "Say \"locate the key\"", snapshot, graph)
	_expect(_candidate_count(quoted) == 0, "quotation has no candidate")
	var reported := _propose(native, "Alice told you to locate the key", snapshot, graph)
	_expect(_candidate_count(reported) == 0, "reported speech has no candidate")
	var future_statement := _propose(native, "Tomorrow you will locate the key", snapshot, graph)
	_expect(_candidate_count(future_statement) == 0, "future statement has no candidate")

	var absent := _propose(native, "Locate the key", _snapshot(["break"]), graph)
	_expect(str(absent.get("status", "")) == "NO_LINK", "absent capability filtered")
	var malformed := _propose(native, "Locate the key", snapshot, {"schema_contract": "wrong"})
	_expect(str(malformed.get("status", "")) == "INVALID", "malformed graph fail closed")

	var step_on := _propose(native, "Step on the brittle shell", snapshot, graph)
	_expect(_first_action(step_on) == "break", "step on -> break approximate")
	_expect(str(_first_candidate(step_on).get("relation", "")) == "APPROXIMATE_EFFECT", "step on relation")

	var synthetic_graph := {
		"schema_contract": "semantic_action_graph_v1",
		"version": "synthetic",
		"provenance": {"source": "fixture"},
		"entries": [{
			"surface": "step on", "lemma": "step", "particle": "on",
			"candidates": [{"action": "crush", "relation": "APPROXIMATE_EFFECT", "confidence": 0.8}]
		}]
	}
	var synthetic := _propose(native, "Step on the can", _snapshot(["crush"]), synthetic_graph)
	_expect(_first_action(synthetic) == "crush", "synthetic step_on -> crush")
	var pickup := _propose(native, "Pick up the can", _snapshot(["pickup", "crush"]), synthetic_graph)
	_expect(_first_action(pickup) != "crush", "pick_up never -> crush")

	var first := _stable_copy(_propose(native, "Locate the key", snapshot, graph))
	var second := _stable_copy(_propose(native, "Locate the key", snapshot, graph))
	_expect(first == second, "deterministic output")
	_finish()

func _propose(native, text: String, snapshot: Dictionary, graph: Dictionary) -> Dictionary:
	var analysis: Dictionary = native.analyze_turn(text)
	return native.propose_semantic_links(text, analysis, snapshot, {"semantic_action_graph": graph})

func _snapshot(actions: Array[String]) -> Dictionary:
	var definitions := {}
	var aliases := {}
	for action in actions:
		definitions[action] = {"action": action, "target_mode": "required", "preconditions": {}, "effects": {}}
		aliases[action.replace("_", " ")] = [action]
	return {"schema": "brain_capability_registry_v1", "actions": definitions, "alias_index": aliases}

func _load_graph() -> Dictionary:
	var file := FileAccess.open(GRAPH_PATH, FileAccess.READ)
	if file == null:
		_fail("graph unavailable")
		return {}
	var parsed = JSON.parse_string(file.get_as_text())
	return parsed if parsed is Dictionary else {}

func _first_candidate(result: Dictionary) -> Dictionary:
	var requests: Array = result.get("literal_requests", [])
	if requests.is_empty() or not (requests[0] is Dictionary):
		return {}
	var candidates: Array = requests[0].get("candidates", [])
	return candidates[0] if not candidates.is_empty() and candidates[0] is Dictionary else {}

func _first_request(result: Dictionary) -> Dictionary:
	var requests: Array = result.get("literal_requests", [])
	return requests[0] if not requests.is_empty() and requests[0] is Dictionary else {}

func _request_for_surface(result: Dictionary, surface: String) -> Dictionary:
	for request in result.get("literal_requests", []):
		if request is Dictionary and str(request.get("surface", "")) == surface:
			return request
	return {}

func _first_action(result: Dictionary) -> String:
	return str(_first_candidate(result).get("action", ""))

func _candidate_count(result: Dictionary) -> int:
	var count := 0
	for request in result.get("literal_requests", []):
		if request is Dictionary:
			count += Array(request.get("candidates", [])).size()
	return count

func _stable_copy(result: Dictionary) -> Dictionary:
	var out := result.duplicate(true)
	out.erase("elapsed_usec")
	return out

func _expect(condition: bool, label: String) -> void:
	if not condition:
		_fail(label)

func _fail(message: String) -> void:
	_failures.append(message)

func _finish() -> void:
	if _failures.is_empty():
		print("native_semantic_bridge_tests ok cases=28")
		quit(0)
	else:
		for failure in _failures:
			push_error(failure)
		quit(1)
