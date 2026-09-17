extends SceneTree

const CORPUS = preload("res://tests/native_acceptance_corpus.gd")


func _initialize() -> void:
	var cases: Array[Dictionary] = CORPUS.build()
	if cases.size() != CORPUS.EXPECTED_TOTAL:
		_fail("expected %d cases, got %d" % [CORPUS.EXPECTED_TOTAL, cases.size()])
		return
	var ids := {}
	var texts := {}
	var utterances := {}
	var level_counts := {}
	var theme_counts := {}
	var construction_families := {}
	for case in cases:
		var id := str(case.get("id", ""))
		var text := str(case.get("text", ""))
		var level := str(case.get("level", ""))
		var theme := str(case.get("theme", ""))
		var construction_id := str(case.get("construction_id", ""))
		if id.is_empty() or ids.has(id):
			_fail("missing or duplicate id: " + id)
			return
		if text.is_empty() or texts.has(text):
			_fail("missing or duplicate text in " + id)
			return
		if not CORPUS.LEVELS.has(level):
			_fail("invalid level in " + id + ": " + level)
			return
		if not CORPUS.THEMES.has(theme):
			_fail("invalid theme in " + id + ": " + theme)
			return
		if str(case.get("action", "")).is_empty() or str(case.get("expected_target", "")).is_empty() or str(case.get("expected_kind", "")).is_empty():
			_fail("missing semantic expectation in " + id)
			return
		if construction_id.is_empty() or not construction_id.begins_with(level + "_"):
			_fail("missing or mismatched construction family in " + id + ": " + construction_id)
			return
		for raw_turn in case.get("turns", []):
			var turn := str(raw_turn).strip_edges()
			if turn.is_empty() or utterances.has(turn):
				_fail("missing or duplicate individual utterance in %s: %s" % [id, turn])
				return
			utterances[turn] = id
		ids[id] = true
		texts[text] = true
		level_counts[level] = int(level_counts.get(level, 0)) + 1
		theme_counts[theme] = int(theme_counts.get(theme, 0)) + 1
		var level_families: Dictionary = construction_families.get(level, {})
		level_families[construction_id] = int(level_families.get(construction_id, 0)) + 1
		construction_families[level] = level_families
	for level in CORPUS.LEVELS:
		if int(level_counts.get(level, 0)) != CORPUS.CASES_PER_LEVEL:
			_fail("unbalanced level %s: %s" % [level, level_counts.get(level, 0)])
			return
		var level_families: Dictionary = construction_families.get(level, {})
		if level_families.size() < 20:
			_fail("insufficient structural diversity in %s: %d construction families" % [level, level_families.size()])
			return
	if theme_counts.size() != CORPUS.THEMES.size():
		_fail("not every declared theme is represented: " + JSON.stringify(theme_counts))
		return
	print("native_corpus_contract_tests ok cases=%d utterances=%d levels=%s themes=%d construction_families=%s" % [cases.size(), utterances.size(), JSON.stringify(level_counts), theme_counts.size(), JSON.stringify(_family_counts(construction_families))])
	quit(0)


func _family_counts(construction_families: Dictionary) -> Dictionary:
	var counts := {}
	for level in construction_families:
		var families: Dictionary = construction_families[level]
		counts[level] = families.size()
	return counts


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
