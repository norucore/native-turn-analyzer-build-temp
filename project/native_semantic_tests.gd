extends SceneTree


func _initialize() -> void:
	if not ClassDB.class_exists("NativeTurnAnalyzer"):
		_fail("NativeTurnAnalyzer class is unavailable")
		return
	var analyzer = ClassDB.instantiate("NativeTurnAnalyzer")
	var registry := {
		"schema": "brain_capability_registry_v1",
		"actions": {
			"jump": {"action": "jump", "target_mode": "optional"},
			"give": {"action": "give", "target_mode": "required"},
			"open": {"action": "open", "target_mode": "required"},
			"exit": {"action": "exit", "target_mode": "required"},
			"lift": {"action": "lift", "target_mode": "required"},
			"examine": {"action": "examine", "target_mode": "required"},
			"close": {"action": "close", "target_mode": "required"},
			"pickup": {"action": "pickup", "target_mode": "required"},
			"move_to": {"action": "move_to", "target_mode": "required"},
			"destroy": {"action": "destroy", "target_mode": "required"},
			"break": {"action": "break", "target_mode": "required"},
			"focus": {"action": "focus", "target_mode": "required"},
			"wait": {"action": "wait", "target_mode": "optional"},
			"climb": {"action": "climb", "target_mode": "required"},
			"equip": {"action": "equip", "target_mode": "required"},
		},
		"alias_index": {
			"jump": ["jump"],
			"pass": ["give"],
			"open": ["open"],
			"exit": ["exit"],
			"lift": ["lift"],
			"examine": ["examine"],
			"inspect": ["examine"],
			"close": ["close"],
			"pick up": ["pickup"],
			"move to": ["move_to"],
			"destroy": ["destroy", "break"],
			"focus": ["focus"],
			"wait": ["wait"],
			"get on": ["climb"],
			"don": ["equip"],
		},
	}
	var canonical_destroy: Dictionary = analyzer.analyze_and_compile("Please destroy the obsolete telemetry node.", registry, {}, {})
	var destroy_frames: Array = canonical_destroy.get("compiler_output", {}).get("frames", [])
	if destroy_frames.size() != 1 or str(destroy_frames[0].get("semantic_roles", {}).get("action", "")) != "destroy":
		_fail("canonical capability did not outrank a synonym collision: " + JSON.stringify(destroy_frames))
		return
	var terminal_s_verb: Dictionary = analyzer.analyze_and_compile("Please focus the optical array.", registry, {}, {})
	var focus_frames: Array = terminal_s_verb.get("compiler_output", {}).get("frames", [])
	if focus_frames.size() != 1 or str(focus_frames[0].get("semantic_roles", {}).get("action", "")) != "focus":
		_fail("base verb ending in -us was reduced as a plural: " + JSON.stringify(focus_frames))
		return
	var packet: Dictionary = analyzer.analyze_and_compile(
		"After three jumps, jump onto the mat.",
		registry,
		{},
		{}
	)
	# Dal 2026-09-14 la virgola davanti a un verbo separa, e "After three jumps" arriva
	# come conversazione a se'. Il controllo resta sull'unica azione: "jumps" non e' "jump".
	var frames: Array = Array(packet.get("compiler_output", {}).get("frames", [])).filter(func(frame): return str(frame.get("frame_type", "")) != "conversation")
	if frames.size() != 1:
		_fail("expected one frame: " + JSON.stringify(frames))
		return
	var roles: Dictionary = frames[0].get("semantic_roles", {})
	if str(roles.get("action", "")) != "jump":
		_fail("expected exact jump action: " + JSON.stringify(frames[0]))
		return
	if str(roles.get("object", "")) != "onto the mat":
		_fail("target starts from an earlier substring: " + JSON.stringify(frames[0]))
		return
	var questions: Dictionary = analyzer.analyze_and_compile(
		"What woke you? Who followed you?",
		registry,
		{},
		{}
	)
	var question_frames: Array = questions.get("compiler_output", {}).get("frames", [])
	if question_frames.size() != 2:
		_fail("consecutive questions were collapsed: " + JSON.stringify(question_frames))
		return
	for frame in question_frames:
		if str(frame.get("speech_act", "")) != "question":
			_fail("question boundary lost speech act: " + JSON.stringify(question_frames))
			return
	var modal_request: Dictionary = analyzer.analyze_and_compile(
		"You could pass me the sealed envelope?",
		registry,
		{},
		{}
	)
	var modal_frames: Array = modal_request.get("compiler_output", {}).get("frames", [])
	if modal_frames.size() != 1:
		_fail("expected one modal request frame: " + JSON.stringify(modal_frames))
		return
	var modal_roles: Dictionary = modal_frames[0].get("semantic_roles", {})
	if str(modal_frames[0].get("speech_act", "")) != "request" or str(modal_roles.get("action", "")) != "give":
		_fail("addressed modal request remained a capability question: " + JSON.stringify(modal_frames))
		return
	if str(modal_roles.get("object", "")) != "sealed envelope" or str(modal_roles.get("recipient_or_direction", "")) != "user":
		_fail("ditransitive roles were not separated: " + JSON.stringify(modal_frames[0]))
		return
	var future_prediction: Dictionary = analyzer.analyze_and_compile(
		"Tomorrow you will probably examine the bookshelf.",
		registry,
		{},
		{}
	)
	var future_prediction_frames: Array = future_prediction.get("compiler_output", {}).get("frames", [])
	if future_prediction_frames.size() != 1 or ["world_action", "inventory_action"].has(str(future_prediction_frames[0].get("frame_type", ""))):
		_fail("probabilistic future statement became executable: " + JSON.stringify(future_prediction_frames))
		return
	var future_directive: Dictionary = analyzer.analyze_and_compile(
		"You will examine the bookshelf now.",
		registry,
		{},
		{}
	)
	var future_directive_frames: Array = future_directive.get("compiler_output", {}).get("frames", [])
	if future_directive_frames.size() != 1 or str(future_directive_frames[0].get("semantic_roles", {}).get("action", "")) != "examine" or str(future_directive_frames[0].get("speech_act", "")) != "request":
		_fail("explicit future directive was lost: " + JSON.stringify(future_directive_frames))
		return
	var contraction_question: Dictionary = analyzer.analyze_and_compile(
		"Above the camp the goat lives under the bench the goat cracks. Don't you agree too?",
		registry,
		{},
		{}
	)
	var contraction_frames: Array = contraction_question.get("compiler_output", {}).get("frames", [])
	# Dal 2026-09-14 il punto separa le due frasi: il controllo vale per ciascuna.
	var contraction_leaked := contraction_frames.is_empty()
	for contraction_frame in contraction_frames:
		if ["world_action", "inventory_action", "state_query"].has(str(contraction_frame.get("frame_type", ""))) or str(contraction_frame.get("semantic_roles", {}).get("action", "")) == "equip":
			contraction_leaked = true
	if contraction_leaked:
		_fail("alias don matched inside the contraction don't: " + JSON.stringify(contraction_frames))
		return
	var equip_directive: Dictionary = analyzer.analyze_and_compile(
		"Don the silver helmet.",
		registry,
		{},
		{}
	)
	var equip_frames: Array = equip_directive.get("compiler_output", {}).get("frames", [])
	if equip_frames.size() != 1 or str(equip_frames[0].get("semantic_roles", {}).get("action", "")) != "equip" or str(equip_frames[0].get("semantic_roles", {}).get("object", "")) != "silver helmet":
		_fail("standalone don directive was lost: " + JSON.stringify(equip_frames))
		return
	var fetch_request: Dictionary = analyzer.analyze_and_compile(
		"Nice to see you, You get me the silver circular on the oak bench please.",
		registry,
		{},
		{}
	)
	var fetch_frames: Array = fetch_request.get("compiler_output", {}).get("frames", [])
	var fetch_actions: Array[String] = []
	for frame in fetch_frames:
		fetch_actions.append(str(frame.get("semantic_roles", {}).get("action", "")))
	if fetch_actions != ["", "pickup", "give"]:
		_fail("fetch-for construction did not preserve preface and expand transfer: " + JSON.stringify(fetch_frames))
		return
	var pickup_roles: Dictionary = fetch_frames[1].get("semantic_roles", {})
	var give_roles: Dictionary = fetch_frames[2].get("semantic_roles", {})
	if str(pickup_roles.get("object", "")) != "silver circular" or str(pickup_roles.get("source", "")) != "oak bench":
		_fail("fetch-for object/source roles were not separated: " + JSON.stringify(fetch_frames[1]))
		return
	if str(give_roles.get("object", "")) != "silver circular" or str(give_roles.get("recipient_or_direction", "")) != "user" or fetch_frames[2].get("dependency_refs", []) != ["frame_002"]:
		_fail("fetch-for delivery roles or dependency were lost: " + JSON.stringify(fetch_frames[2]))
		return
	var discourse_fetch: Dictionary = analyzer.analyze_and_compile(
		"Wait before you answer would you fetch us the folded gazette from the walnut counter?",
		registry,
		{},
		{}
	)
	var discourse_frames: Array = discourse_fetch.get("compiler_output", {}).get("frames", [])
	var discourse_actions: Array[String] = []
	for frame in discourse_frames:
		discourse_actions.append(str(frame.get("semantic_roles", {}).get("action", "")))
	if discourse_actions != ["pickup", "give"] or str(discourse_frames[0].get("semantic_roles", {}).get("source", "")) != "walnut counter":
		_fail("discourse wait displaced the embedded fetch request: " + JSON.stringify(discourse_frames))
		return
	var physical_wait: Dictionary = analyzer.analyze_and_compile("Wait here.", registry, {}, {})
	var physical_wait_frames: Array = physical_wait.get("compiler_output", {}).get("frames", [])
	if physical_wait_frames.size() != 1 or str(physical_wait_frames[0].get("semantic_roles", {}).get("action", "")) != "wait":
		_fail("physical wait was lost while adding discourse wait: " + JSON.stringify(physical_wait_frames))
		return
	var phrasal_get: Dictionary = analyzer.analyze_and_compile("Would you get on the cedar platform?", registry, {}, {})
	var phrasal_get_frames: Array = phrasal_get.get("compiler_output", {}).get("frames", [])
	if phrasal_get_frames.size() != 1 or str(phrasal_get_frames[0].get("semantic_roles", {}).get("action", "")) != "climb":
		_fail("polysemous get-on was captured by fetch-for: " + JSON.stringify(phrasal_get_frames))
		return
	var reported_get: Dictionary = analyzer.analyze_and_compile("I got the silver circular yesterday.", registry, {}, {})
	var reported_get_frames: Array = reported_get.get("compiler_output", {}).get("frames", [])
	if reported_get_frames.size() != 1 or ["world_action", "inventory_action"].has(str(reported_get_frames[0].get("frame_type", ""))):
		_fail("past get report became executable: " + JSON.stringify(reported_get_frames))
		return
	var capability_question: Dictionary = analyzer.analyze_and_compile(
		"Can you jump?",
		registry,
		{},
		{}
	)
	var capability_frames: Array = capability_question.get("compiler_output", {}).get("frames", [])
	if capability_frames.size() != 1 or str(capability_frames[0].get("frame_type", "")) != "state_query":
		_fail("optional action capability question became executable: " + JSON.stringify(capability_frames))
		return
	var negated: Dictionary = analyzer.analyze_and_compile(
		"I was checking the corridor, but now dont open the ivory gate unless I confirm.",
		registry,
		{},
		{}
	)
	var negated_frames: Array = negated.get("compiler_output", {}).get("frames", [])
	if negated_frames.size() != 1 or ["world_action", "inventory_action"].has(str(negated_frames[0].get("frame_type", ""))):
		_fail("local negation became executable: " + JSON.stringify(negated_frames))
		return
	var perfect_question: Dictionary = analyzer.analyze_and_compile(
		"Have you already opened the ivory gate?",
		registry,
		{},
		{}
	)
	var perfect_frames: Array = perfect_question.get("compiler_output", {}).get("frames", [])
	var perfect_roles: Dictionary = perfect_frames[0].get("semantic_roles", {}) if perfect_frames.size() == 1 else {}
	if perfect_frames.size() != 1 or str(perfect_frames[0].get("frame_type", "")) != "state_query" or str(perfect_roles.get("action", "")) != "open":
		_fail("perfect action question was not recognized safely: " + JSON.stringify(perfect_frames))
		return
	var noun_collision: Dictionary = analyzer.analyze_and_compile(
		"Yesterday at the depot, you exited the service lift.",
		registry,
		{},
		{}
	)
	var noun_frames: Array = noun_collision.get("compiler_output", {}).get("frames", [])
	if noun_frames.size() != 1 or ["world_action", "inventory_action"].has(str(noun_frames[0].get("frame_type", ""))):
		_fail("action-shaped noun became executable: " + JSON.stringify(noun_frames))
		return
	var selected_predicate: Dictionary = analyzer.analyze_and_compile(
		"I thought you had exited the service lift; turns out you hadnt, so could you maybe exit it now?",
		registry,
		{},
		{}
	)
	var selected_frames: Array = selected_predicate.get("compiler_output", {}).get("frames", [])
	var selected_roles: Dictionary = selected_frames[-1].get("semantic_roles", {}) if not selected_frames.is_empty() else {}
	if selected_frames.is_empty() or str(selected_roles.get("action", "")) != "exit" or str(selected_frames[-1].get("speech_act", "")) != "request":
		_fail("request predicate lost to an earlier action-shaped noun: " + JSON.stringify(selected_frames))
		return
	var ambiguous: Dictionary = analyzer.analyze_and_compile(
		"Before dawn, the painted gate—not the other one—you should open.",
		registry,
		{},
		{}
	)
	var ambiguous_frames: Array = ambiguous.get("compiler_output", {}).get("frames", [])
	if ambiguous_frames.size() != 1 or str(ambiguous_frames[0].get("frame_type", "")) != "clarification":
		_fail("contrastive reference did not abstain: " + JSON.stringify(ambiguous_frames))
		return
	var corrected: Dictionary = analyzer.analyze_and_compile(
		"Open the ivory... no, dont do anything yet.",
		registry,
		{},
		{}
	)
	var corrected_frames: Array = corrected.get("compiler_output", {}).get("frames", [])
	if corrected_frames.size() != 1 or ["world_action", "inventory_action"].has(str(corrected_frames[0].get("frame_type", ""))):
		_fail("post-command cancellation did not override action: " + JSON.stringify(corrected_frames))
		return
	var replacement: Dictionary = analyzer.analyze_and_compile(
		"Now I am issuing a replacement: open the ivory gate.",
		registry,
		{},
		{}
	)
	var replacement_frames: Array = replacement.get("compiler_output", {}).get("frames", [])
	var replacement_roles: Dictionary = replacement_frames[0].get("semantic_roles", {}) if replacement_frames.size() == 1 else {}
	if replacement_frames.size() != 1 or str(replacement_roles.get("action", "")) != "open" or str(replacement_frames[0].get("speech_act", "")) != "request":
		_fail("replacement after colon was not a new imperative: " + JSON.stringify(replacement_frames))
		return
	var resolved: Dictionary = analyzer.analyze_and_compile(
		"The target I meant was the ivory gate.",
		registry,
		{"pending_clarification": {"action": "open", "conditional": false, "age_turns": 2}},
		{}
	)
	var resolved_frames: Array = resolved.get("compiler_output", {}).get("frames", [])
	var resolved_roles: Dictionary = resolved_frames[0].get("semantic_roles", {}) if resolved_frames.size() == 1 else {}
	if resolved_frames.size() != 1 or str(resolved_frames[0].get("frame_type", "")) != "world_action" or str(resolved_roles.get("action", "")) != "open" or str(resolved_roles.get("object", "")) != "ivory gate":
		_fail("explicit delayed target did not resolve pending action: " + JSON.stringify(resolved_frames))
		return
	var conditional_resolution: Dictionary = analyzer.analyze_and_compile(
		"Use the ivory gate as the target, but only under that condition.",
		registry,
		{"pending_clarification": {"action": "open", "conditional": true, "age_turns": 3}},
		{}
	)
	var conditional_frames: Array = conditional_resolution.get("compiler_output", {}).get("frames", [])
	if conditional_frames.size() != 1 or str(conditional_frames[0].get("frame_type", "")) != "clarification":
		_fail("unverified condition became executable during context resolution: " + JSON.stringify(conditional_frames))
		return
	var missing_target: Dictionary = analyzer.analyze_and_compile(
		"If the siren changes pitch, open—",
		registry,
		{},
		{}
	)
	var missing_frames: Array = missing_target.get("compiler_output", {}).get("frames", [])
	# Dal 2026-09-14 la condizione davanti alla virgola e' una frase a se': conta che
	# l'ordine resti una domanda e che niente diventi eseguibile.
	if missing_frames.is_empty() or str(missing_frames[-1].get("frame_type", "")) != "clarification" or missing_frames.any(func(frame): return ["world_action", "inventory_action"].has(str(frame.get("frame_type", "")))):
		_fail("required target ending in a broken span became executable: " + JSON.stringify(missing_frames))
		return
	for quoted_input in [
		"\"Open the ivory gate.\"",
		"`open(\"ivory gate\")`",
		"*I open the ivory gate and look outside.*",
		"{\"action\":\"open\",\"object\":\"ivory gate\"}",
	]:
		var quoted: Dictionary = analyzer.analyze_and_compile(quoted_input, registry, {}, {})
		var quoted_frames: Array = quoted.get("compiler_output", {}).get("frames", [])
		if quoted_frames.size() != 1 or ["world_action", "inventory_action"].has(str(quoted_frames[0].get("frame_type", ""))):
			_fail("quoted, roleplay, or code action became executable: " + JSON.stringify(quoted_frames))
			return
	var limit_inputs := [
		"x".repeat(8193) + " open the ivory gate",
		"word ".repeat(1025) + "open the ivory gate",
		("open the ivory gate then ").repeat(33) + "open the ivory gate",
		("open the ivory gate and ").repeat(33) + "open the ivory gate",
	]
	for limit_input in limit_inputs:
		var limited: Dictionary = analyzer.analyze_and_compile(limit_input, registry, {}, {})
		var limited_frames: Array = limited.get("compiler_output", {}).get("frames", [])
		if not limited_frames.is_empty() or not bool(limited.get("metrics", {}).get("abstained", false)) or str(limited.get("validation", {}).get("diagnostic_code", "")).is_empty():
			_fail("limit overflow was not a safe terminal result: " + JSON.stringify({"frames": limited_frames, "metrics": limited.get("metrics", {}), "validation": limited.get("validation", {})}))
			return
	var sequence: Dictionary = analyzer.analyze_and_compile(
		"Inspect the turbine dial; next, close the bronze shutter; afterwards pick up the linen pouch; finally move to the north balcony.",
		registry,
		{},
		{}
	)
	var sequence_frames: Array = sequence.get("compiler_output", {}).get("frames", [])
	var sequence_actions: Array[String] = []
	for frame in sequence_frames:
		sequence_actions.append(str(frame.get("semantic_roles", {}).get("action", "")))
	if sequence_actions != ["examine", "close", "pickup", "move_to"]:
		_fail("explicit temporal sequence lost actions or order: " + JSON.stringify(sequence_frames))
		return
	for frame_index in sequence_frames.size():
		var expected_dependencies := [] if frame_index == 0 else ["frame_%03d" % frame_index]
		if sequence_frames[frame_index].get("dependency_refs", []) != expected_dependencies:
			_fail("sequence dependency graph is not linear: " + JSON.stringify(sequence_frames))
			return
	var comma_sequence: Dictionary = analyzer.analyze_and_compile(
		"Move to the weather station, next inspect the cracked barometer.",
		registry,
		{},
		{}
	)
	var comma_frames: Array = comma_sequence.get("compiler_output", {}).get("frames", [])
	var comma_actions: Array[String] = []
	for frame in comma_frames:
		comma_actions.append(str(frame.get("semantic_roles", {}).get("action", "")))
	if comma_actions != ["move_to", "examine"]:
		_fail("comma temporal connector was absorbed into the first target: " + JSON.stringify(comma_frames))
		return
	var delayed_resolution: Dictionary = analyzer.analyze_and_compile(
		"I was referring to the amber navigation chart.",
		registry,
		{"pending_clarification": {"action": "examine", "age_turns": 2}},
		{}
	)
	var delayed_frames: Array = delayed_resolution.get("compiler_output", {}).get("frames", [])
	var delayed_roles: Dictionary = delayed_frames[0].get("semantic_roles", {}) if delayed_frames.size() == 1 else {}
	if delayed_frames.size() != 1 or str(delayed_frames[0].get("frame_type", "")) != "world_action" or str(delayed_roles.get("action", "")) != "examine" or str(delayed_roles.get("object", "")) != "amber navigation chart":
		_fail("delayed explicit reference did not reconnect to the pending action: " + JSON.stringify(delayed_frames))
		return
	var competing_resolution: Dictionary = analyzer.analyze_and_compile(
		"I was referring to the amber navigation chart or the cobalt route ledger.",
		registry,
		{"pending_clarification": {"action": "examine", "age_turns": 2}},
		{}
	)
	var competing_frames: Array = competing_resolution.get("compiler_output", {}).get("frames", [])
	if competing_frames.size() != 1 or str(competing_frames[0].get("frame_type", "")) != "clarification":
		_fail("competing delayed targets became an arbitrary action: " + JSON.stringify(competing_frames))
		return
	var coordinated: Dictionary = analyzer.analyze_and_compile(
		"Open the cedar locker and close the bronze shutter and pick up the linen pouch.",
		registry,
		{},
		{}
	)
	var coordinated_frames: Array = coordinated.get("compiler_output", {}).get("frames", [])
	var coordinated_actions: Array[String] = []
	for frame in coordinated_frames:
		coordinated_actions.append(str(frame.get("semantic_roles", {}).get("action", "")))
	if coordinated_actions != ["open", "close", "pickup"]:
		_fail("capability-aware and coordination failed: " + JSON.stringify(coordinated_frames))
		return
	# Tracker Analizzatore #76: il pronome prende la sola cosa nominata dalla domanda di prima.
	var question_topic: Dictionary = analyzer.analyze_and_compile("Remember the cedar locker? Open it.", registry, {}, {})
	var topic_frames: Array = question_topic.get("compiler_output", {}).get("frames", [])
	if topic_frames.is_empty() or str(topic_frames[-1].get("frame_type", "")) != "world_action" or str(topic_frames[-1].get("semantic_roles", {}).get("object", "")) != "cedar locker":
		_fail("pronoun after a question did not take the question topic: " + JSON.stringify(topic_frames))
		return
	var two_topics: Dictionary = analyzer.analyze_and_compile("Is the cedar locker near the bronze shutter? Open it.", registry, {}, {})
	var two_topic_frames: Array = two_topics.get("compiler_output", {}).get("frames", [])
	if two_topic_frames.is_empty() or str(two_topic_frames[-1].get("frame_type", "")) != "clarification":
		_fail("pronoun after a question with two things chose one instead of asking: " + JSON.stringify(two_topic_frames))
		return
	# Tracker Analizzatore #4: "open windows let the cold in" constata, non ordina.
	var with_verbs := registry.duplicate(true)
	with_verbs["verb_lemmas"] = {"let": true, "open": true, "close": true}
	var subject_phrase: Dictionary = analyzer.analyze_and_compile("The door is stuck. Open windows let the cold in.", with_verbs, {}, {})
	for frame in subject_phrase.get("compiler_output", {}).get("frames", []):
		if str(frame.get("frame_type", "")) in ["world_action", "inventory_action", "clarification"]:
			_fail("noun phrase subject became an order: " + JSON.stringify(frame))
			return
	var plain_order: Dictionary = analyzer.analyze_and_compile("The door is stuck. Open the window.", with_verbs, {}, {})
	var plain_frames: Array = plain_order.get("compiler_output", {}).get("frames", [])
	if plain_frames.is_empty() or str(plain_frames[-1].get("semantic_roles", {}).get("action", "")) != "open" or str(plain_frames[-1].get("frame_type", "")) != "world_action":
		_fail("an order after a statement was lost: " + JSON.stringify(plain_frames))
		return
	# Tracker Analizzatore #78: un intercalare in testa non trasforma un racconto in un ordine.
	var interjected: Dictionary = analyzer.analyze_and_compile("Wait, I just checked my pocket and my phone is not there.", registry, {}, {})
	for frame in interjected.get("compiler_output", {}).get("frames", []):
		if str(frame.get("frame_type", "")) in ["world_action", "inventory_action", "clarification"]:
			_fail("interjection before a narration became an order: " + JSON.stringify(frame))
			return
	var told: Dictionary = analyzer.analyze_and_compile("Oh, and I left the linen pouch on the shelf.", registry, {}, {})
	var told_clauses: Array = told.get("analysis_result", {}).get("linguistic_clauses", [])
	if told_clauses.is_empty() or str(told_clauses[0].get("speech_act", "")) != "statement":
		_fail("interjection hid the subject of a narration: " + JSON.stringify(told_clauses))
		return
	var wait_order: Dictionary = analyzer.analyze_and_compile("Wait, open the cedar locker.", registry, {}, {})
	var wait_actions: Array = wait_order.get("compiler_output", {}).get("frames", []).filter(func(frame): return str(frame.get("frame_type", "")) == "world_action").map(func(frame): return str(frame.get("semantic_roles", {}).get("action", "")))
	if not wait_actions.has("open") or wait_actions.has("wait"):
		_fail("an order after an interjection was lost: " + JSON.stringify(wait_order.get("compiler_output", {}).get("frames", [])))
		return
	# Tracker Analizzatore #80: il secondo verbo coniugato non si perde, e "them" seguito da
	# un luogo e' la cosa, non il destinatario.
	var gerunds: Dictionary = analyzer.analyze_and_compile("Would you mind picking up the linen pouch and closing the bronze shutter?", registry, {}, {})
	var gerund_actions: Array = gerunds.get("compiler_output", {}).get("frames", []).map(func(frame): return str(frame.get("semantic_roles", {}).get("action", "")))
	if not (gerund_actions.has("pickup") and gerund_actions.has("close")):
		_fail("the second gerund of a coordination was lost: " + JSON.stringify(gerunds.get("compiler_output", {}).get("frames", [])))
		return
	var placed_pronoun: Dictionary = analyzer.analyze_and_compile("Pick up the linen pouches and lift them onto the shelf.", registry, {}, {})
	for frame in placed_pronoun.get("compiler_output", {}).get("frames", []):
		if str(frame.get("semantic_roles", {}).get("action", "")) == "lift" and str(frame.get("semantic_roles", {}).get("object", "")).begins_with("onto"):
			_fail("a pronoun followed by a place became the recipient: " + JSON.stringify(frame))
			return
	# La particella dopo il pronome non e' un luogo: "pass me back the envelope" da' a me.
	var particle_recipient: Dictionary = analyzer.analyze_and_compile("Pass me back the sealed envelope.", registry, {}, {})
	var particle_frames: Array = particle_recipient.get("compiler_output", {}).get("frames", [])
	if particle_frames.is_empty() or str(particle_frames[0].get("semantic_roles", {}).get("recipient_or_direction", "")) != "user" or str(particle_frames[0].get("semantic_roles", {}).get("object", "")).begins_with("me"):
		_fail("a particle after the recipient pronoun kept the pronoun in the target: " + JSON.stringify(particle_frames))
		return
	# Le frasi vere della prova del 2026-09-14 (#78, #79).
	var real_wait: Dictionary = analyzer.analyze_and_compile("Wait, I just checked my pocket and my phone's not there. Is it still on the bench?", registry, {}, {})
	for frame in real_wait.get("compiler_output", {}).get("frames", []):
		if str(frame.get("frame_type", "")) in ["world_action", "inventory_action", "clarification"]:
			_fail("interjection and a noun phrase with a verb became an order: " + JSON.stringify(frame))
			return
	var tag_question: Dictionary = analyzer.analyze_and_compile("Oh, and I left the linen pouch on the shelf, did you notice it?", registry, {}, {})
	var tag_clauses: Array = tag_question.get("analysis_result", {}).get("linguistic_clauses", [])
	if tag_clauses.size() != 2 or str(tag_clauses[0].get("speech_act", "")) != "statement" or str(tag_clauses[1].get("speech_act", "")) != "question":
		_fail("a tag question swallowed the narration before it: " + JSON.stringify(tag_clauses))
		return
	# Tracker Analizzatore #82, #83, #84: frasi della prova vera del 2026-09-14.
	var told_registry := registry.duplicate(true)
	told_registry["verb_lemmas"] = {"close": true, "open": true, "fold": true}
	for spec in [["sit", "posture"], ["hold", "physicalforcemanipulation"], ["modify", "objectinteractiontransformation"], ["think", "perceptioncognition"]]:
		told_registry["actions"][spec[0]] = {"action": spec[0], "target_mode": "required", "semantic_metadata": {"semantic_family": spec[1]}}
	told_registry["actions"]["examine"]["semantic_metadata"] = {"semantic_family": "perception"}
	for alias in [["sit", "sit"], ["hold", "hold"], ["gift", "give"], ["check", "examine"], ["fold", "modify"], ["think", "think"]]:
		told_registry["alias_index"][alias[0]] = [alias[1]]
	var expectations := [
		# [frase, tipo dell'ultimo frame, azione, oggetto, evidenza]
		["I just picked the linen pouch up to check.", "conversation", "pickup", "linen pouch", "self_narration:subject_verb"],
		["I'm holding a small wrapped gift behind my back.", "conversation", "hold", "small wrapped gift", "self_narration:subject_verb"],
		["Sit here with me, please.", "world_action", "sit", "user", "deixis:user_location"],
		["Pick up the linen pouches and fold them closed.", "world_action", "modify", "linen pouches", ""],
		["Pick up the brass phone and examine if it has a message.", "world_action", "examine", "brass phone", "coreference:clause_complement_previous_target"],
		["Hold the linen pouch till tomorrow.", "world_action", "hold", "linen pouch", ""],
		["I lifted the linen pouch.open the cedar locker", "world_action", "open", "cedar locker", ""],
		# Corpus esterno del 2026-09-15: congiunzione in testa, e il posto e il tempo in coda.
		["And pass the sealed envelope to me?", "inventory_action", "give", "sealed envelope", ""],
		["Maybe lift the linen pouch off the shelf.", "world_action", "lift", "linen pouch", ""],
		["Last thing: I'm lifting the linen pouch up. Can you hold onto it till tomorrow?", "world_action", "hold", "linen pouch", ""],
		# Pensare non racconta un gesto: nessun racconto con una cosa.
		["I'm thinking about the cedar locker.", "conversation", "", "", ""],
	]
	for expected in expectations:
		var told_frames: Array = analyzer.analyze_and_compile(str(expected[0]), told_registry, {}, {}).get("compiler_output", {}).get("frames", [])
		var last: Dictionary = told_frames[-1] if not told_frames.is_empty() else {}
		var told_roles: Dictionary = last.get("semantic_roles", {})
		if str(last.get("frame_type", "")) != expected[1] or str(told_roles.get("action", "")) != expected[2] or str(told_roles.get("object", "")) != expected[3] or not str(told_roles.get("recipient_or_direction", "")).is_empty() and expected[2] == "modify" or (not str(expected[4]).is_empty() and not Array(last.get("parser_evidence", [])).has(expected[4])):
			_fail("told or pointed sentence misread: %s -> %s" % [expected[0], JSON.stringify(told_frames)])
			return
		if expected[1] == "conversation" and told_frames.any(func(frame): return str(frame.get("frame_type", "")) in ["world_action", "inventory_action"]):
			_fail("a narration became an order: %s -> %s" % [expected[0], JSON.stringify(told_frames)])
			return
	var charger_question: Array = analyzer.analyze_and_compile("Actually, wait, do you have a charger?", registry, {}, {}).get("compiler_output", {}).get("frames", [])
	if charger_question.any(func(frame): return str(frame.get("frame_type", "")) == "world_action"):
		_fail("wait before a question became an order: " + JSON.stringify(charger_question))
		return
	print("native_semantic_tests ok canonical_collision terminal_s_verb exact_alias_span consecutive_questions modal_request_roles future_modality contraction_boundary compositional_fetch discourse_wait polysemy negation perfect_question predicate_context ambiguity corrective_scope delayed_context delayed_clarification quoted_code limits temporal_connectors multi_action")
	quit(0)


func _fail(message: String) -> void:
	push_error(message)
	quit(1)
