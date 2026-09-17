extends SceneTree

const CORPUS = preload("res://tests/native_acceptance_corpus.gd")


func _initialize() -> void:
	var output_path := OS.get_temp_dir().path_join("native_turn_analyzer_utterances.txt")
	var file := FileAccess.open(output_path, FileAccess.WRITE)
	if file == null:
		push_error("cannot create originality input: " + output_path)
		quit(1)
		return
	var count := 0
	for case in CORPUS.build():
		for raw_turn in case.get("turns", []):
			file.store_line(str(raw_turn).strip_edges())
			count += 1
	file.close()
	print("native_corpus_originality_dump ok utterances=%d path=%s" % [count, output_path])
	quit(0)
