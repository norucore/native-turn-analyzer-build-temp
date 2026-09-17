extends RefCounted

const LEVELS := ["simple", "medium", "difficult", "extreme"]
const CASES_PER_LEVEL := 205
const EXPECTED_TOTAL := 820

const THEMES := [
	"adventure", "science_fiction", "noir", "dark_fantasy", "fantasy",
	"slice_of_life", "extreme_realism", "drama", "psychological",
	"thriller", "horror", "romance", "western", "historical", "cyberpunk",
]

const SETTINGS := [
	"the wind-carved observatory", "the silent orbital clinic", "the rain-soaked arcade",
	"the ash cathedral", "the moonlit orchard", "the cramped neighborhood bakery",
	"the freight depot at dawn", "the empty rehearsal hall", "the mirrored therapy room",
	"the locked coastal hotel", "the abandoned nursery", "the winter flower market",
	"the desert telegraph station", "the candlelit map archive", "the neon ferry terminal",
]

const NAMES := [
	"Rowan", "Iris", "Malik", "Selene", "Tomas", "June", "Nadia", "Elias",
	"Mina", "Dorian", "Avery", "Leona", "Caleb", "Yara", "Soren",
]

const ADJECTIVES := [
	"weathered", "luminous", "smoke-stained", "thorn-covered", "silver",
	"ordinary", "mud-caked", "fragile", "unsettling", "unmarked",
	"mildewed", "rose-colored", "sun-bleached", "lacquered", "flickering",
]

const UNRELATED_DETAILS := [
	"a copper weather vane turns without wind", "three diagnostic lights blink out of rhythm",
	"a taxi leaves a ribbon of water on the curb", "cold ash drifts from a sealed chimney",
	"an owl calls from beyond the orchard wall", "the baker has forgotten a tray of rolls",
	"a freight clock loses exactly one minute", "someone practices scales behind the curtain",
	"the wall mirror has fogged around its edges", "an elevator bell rings on an empty floor",
	"a toy horse rocks although the room is still", "a florist wraps white carnations in newspaper",
	"the telegraph wire hums above the dry road", "a cartographer sneezes among the old atlases",
	"a ferry display repeats yesterday's departure", "a moth rests on the greenhouse thermometer",
	"a loose screw rolls beneath the third bench", "a portrait frame casts two overlapping shadows",
	"the service indicator flashes between floors", "smoke stains form a line above the archive door",
	"the paramedic's radio plays half a weather report", "violet ink has dried on the blotting paper",
	"a numbered drawer refuses to stay closed", "the lantern glass clicks as it cools",
	"rainwater makes concentric rings in the trench", "a bell rope sways beside the cracked altar",
	"frost melts first along the lower railing", "the cot canvas creaks whenever the pipes knock",
	"cedar dust hangs in a narrow beam of light", "blue paint flakes onto the archive threshold",
	"static briefly resolves into a distant waltz", "the tram timetable lists a platform that vanished",
	"a violin string vibrates inside the closed case", "the compass needle points toward a brick wall",
	"tea leaves gather along one side of the cup", "striped tape curls away from the lever housing",
	"one trolley wheel squeaks only while stationary", "brown paper crackles inside the broker's parcel",
	"the drone projects an upside-down street map", "the generator smells faintly of wet stone",
	"footsteps echo twice inside the concrete stairwell",
]

# Every entry maps to a capability name owned by Persona's real GOAP catalog.
# Surface forms are project-authored English and are intentionally independent
# from earlier acceptance examples and user-provided messages.
const ACTIONS := [
	{"action":"activate", "verb":"activate", "past":"activated", "ing":"activating", "target":"the brass signal console"},
	{"action":"approach", "verb":"approach", "past":"approached", "ing":"approaching", "target":"the masked courier"},
	{"action":"break", "verb":"break", "past":"broke", "ing":"breaking", "target":"the brittle wax seal"},
	{"action":"carry", "verb":"carry", "past":"carried", "ing":"carrying", "target":"the insulated medicine crate"},
	{"action":"climb", "verb":"climb", "past":"climbed", "ing":"climbing", "target":"the iron fire escape"},
	{"action":"close", "verb":"close", "past":"closed", "ing":"closing", "target":"the circular observation hatch"},
	{"action":"combine", "verb":"combine", "past":"combined", "ing":"combining", "target":"the silver dust with the black resin", "object":"silver dust", "destination":"black resin"},
	{"action":"crawl", "verb":"crawl through", "past":"crawled through", "ing":"crawling through", "target":"the narrow drainage tunnel"},
	{"action":"create", "verb":"create", "past":"created", "ing":"creating", "target":"a portable distress beacon"},
	{"action":"crouch", "verb":"crouch behind", "past":"crouched behind", "ing":"crouching behind", "target":"the marble ticket counter"},
	{"action":"deactivate", "verb":"deactivate", "past":"deactivated", "ing":"deactivating", "target":"the rooftop alarm grid"},
	{"action":"descend", "verb":"descend", "past":"descended", "ing":"descending", "target":"the basalt spiral stairway"},
	{"action":"destroy", "verb":"destroy", "past":"destroyed", "ing":"destroying", "target":"the corrupted navigation relay"},
	{"action":"drag", "verb":"drag", "past":"dragged", "ing":"dragging", "target":"the disabled survey android"},
	{"action":"drop", "verb":"drop", "past":"dropped", "ing":"dropping", "target":"the engraved brass key"},
	{"action":"enter", "verb":"enter", "past":"entered", "ing":"entering", "target":"the overgrown glasshouse"},
	{"action":"equip", "verb":"equip", "past":"equipped", "ing":"equipping", "target":"the thermal travel cloak"},
	{"action":"examine", "verb":"examine", "past":"examined", "ing":"examining", "target":"the faded family portrait"},
	{"action":"exit", "verb":"exit", "past":"exited", "ing":"exiting", "target":"the stalled service lift"},
	{"action":"flee", "verb":"flee", "past":"fled", "ing":"fleeing", "target":"the burning records annex"},
	{"action":"follow", "verb":"follow", "past":"followed", "ing":"following", "target":"the night-shift paramedic"},
	{"action":"give", "verb":"give", "past":"gave", "ing":"giving", "target":"the sealed violet letter to the user"},
	{"action":"hide_object", "verb":"hide", "past":"hid", "ing":"hiding", "target":"the numbered evidence pouch"},
	{"action":"hold", "verb":"hold", "past":"held", "ing":"holding", "target":"the green storm lantern"},
	{"action":"jump", "verb":"jump over", "past":"jumped over", "ing":"jumping over", "target":"the rain-filled trench"},
	{"action":"kneel", "verb":"kneel beside", "past":"knelt beside", "ing":"kneeling beside", "target":"the cracked stone altar"},
	{"action":"lean", "verb":"lean against", "past":"leaned against", "ing":"leaning against", "target":"the frost-coated railing"},
	{"action":"lie_down", "verb":"lie down on", "past":"lay down on", "ing":"lying down on", "target":"the canvas field cot"},
	{"action":"lift", "verb":"lift", "past":"lifted", "ing":"lifting", "target":"the fallen cedar beam"},
	{"action":"lock", "verb":"lock", "past":"locked", "ing":"locking", "target":"the blue archive door"},
	{"action":"modify", "verb":"modify", "past":"modified", "ing":"modifying", "target":"the emergency radio frequency"},
	{"action":"move_to", "verb":"move to", "past":"moved to", "ing":"moving to", "target":"the eastern tram platform"},
	{"action":"open", "verb":"open", "past":"opened", "ing":"opening", "target":"the lacquered instrument case"},
	{"action":"pickup", "verb":"pick up", "past":"picked up", "ing":"picking up", "target":"the copper survey compass"},
	{"action":"place", "verb":"place", "past":"placed", "ing":"placing", "target":"the ceramic cup on the oak tray", "object":"ceramic cup", "destination":"oak tray"},
	{"action":"pull", "verb":"pull", "past":"pulled", "ing":"pulling", "target":"the striped emergency lever"},
	{"action":"push", "verb":"push", "past":"pushed", "ing":"pushing", "target":"the red maintenance trolley"},
	{"action":"receive", "verb":"receive", "past":"received", "ing":"receiving", "target":"the wrapped parcel from the broker"},
	{"action":"release", "verb":"release", "past":"released", "ing":"releasing", "target":"the captive mapping drone"},
	{"action":"repair", "verb":"repair", "past":"repaired", "ing":"repairing", "target":"the damaged backup generator"},
	{"action":"retreat", "verb":"retreat to", "past":"retreated to", "ing":"retreating to", "target":"the concrete stairwell"},
]


static func build() -> Array[Dictionary]:
	var cases: Array[Dictionary] = []
	for action_index in ACTIONS.size():
		for variant in 5:
			cases.append(_simple_case(action_index, variant))
			cases.append(_medium_case(action_index, variant))
			cases.append(_difficult_case(action_index, variant))
			cases.append(_extreme_case(action_index, variant))
	return cases


static func _base(action_index: int, variant: int, level: String) -> Dictionary:
	var spec: Dictionary = ACTIONS[action_index]
	var selector := action_index * 5 + variant
	return {
		"id": "%s_%03d" % [level, selector + 1],
		"level": level,
		"theme": THEMES[selector % THEMES.size()],
		"action": spec.action,
		"expected_target": _expected_target(spec),
		"variant": variant,
		"construction_id": "%s_v%d_s%d" % [level, variant, action_index % 4],
		"turns": [],
	}


static func _expected_target(spec: Dictionary) -> String:
	# Dal 2026-09-14 la cosa e il posto (o il secondo oggetto) arrivano separati
	# (tracker Analizzatore #70): il bersaglio atteso e' "cosa|posto".
	if spec.has("destination"):
		return "%s|%s" % [spec.object, spec.destination]
	var target := str(spec.get("target", "")).strip_edges()
	for prefix in ["the ", "a ", "an "]:
		if target.to_lower().begins_with(prefix):
			target = target.substr(prefix.length()).strip_edges()
			break
	if str(spec.get("action", "")) == "give" and target.ends_with(" to the user"):
		target = target.trim_suffix(" to the user").strip_edges()
	return target


static func _simple_case(action_index: int, variant: int) -> Dictionary:
	var result := _base(action_index, variant, "simple")
	var spec: Dictionary = ACTIONS[action_index]
	var setting: String = SETTINGS[(action_index + variant) % SETTINGS.size()]
	var name: String = NAMES[(action_index * 3 + variant) % NAMES.size()]
	var style := action_index % 4
	match [variant, style]:
		[0, 0]:
			result.text = "%s, %s %s." % [name, spec.verb, spec.target]
			result.expected_kind = "request"
		[0, 1]:
			result.text = "%s—go ahead and %s %s." % [name, spec.verb, spec.target]
			result.expected_kind = "request"
		[0, 2]:
			result.text = "%s, I need you to %s %s." % [name, spec.verb, spec.target]
			result.expected_kind = "request"
		[0, 3]:
			result.text = "%s, would you kindly %s %s?" % [name, spec.verb, spec.target]
			result.expected_kind = "request"
		[1, 0]:
			result.text = "Please %s %s while we are at %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[1, 1]:
			result.text = "When you are ready at %s, %s %s for me." % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
		[1, 2]:
			result.text = "At %s, please go and %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
		[1, 3]:
			result.text = "Can you please %s %s while we remain at %s?" % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[2, 0]:
			result.text = "Could you %s %s near %s?" % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[2, 1]:
			result.text = "Would you mind %s %s beside %s?" % [spec.ing, spec.target, setting]
			result.expected_kind = "request"
		[2, 2]:
			result.text = "Can you %s %s before we leave %s?" % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[2, 3]:
			result.text = "I would like you to %s %s near %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[3, 0]:
			result.text = "Do not %s %s inside %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "non_executable"
		[3, 1]:
			result.text = "Please don't %s %s anywhere around %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "non_executable"
		[3, 2]:
			result.text = "Whatever happens at %s, never %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "non_executable"
		[3, 3]:
			result.text = "I am explicitly asking you not to %s %s at %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "non_executable"
		[4, 0]:
			result.text = "Did you %s %s before we reached %s?" % [spec.verb, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 1]:
			result.text = "Have you already %s %s near %s?" % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 2]:
			result.text = "Were you the one who %s %s at %s?" % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 3]:
			result.text = "I need to know whether you %s %s before %s." % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
	result.turns = [result.text]
	return result


static func _medium_case(action_index: int, variant: int) -> Dictionary:
	var result := _base(action_index, variant, "medium")
	var spec: Dictionary = ACTIONS[action_index]
	var setting: String = SETTINGS[(action_index * 2 + variant) % SETTINGS.size()]
	var style := action_index % 4
	match [variant, style]:
		[0, 0]:
			result.text = "Yesterday at %s, you %s %s before noon." % [setting, spec.past, spec.target]
			result.expected_kind = "non_executable"
		[0, 1]:
			result.text = "Last evening, while we were leaving %s, you %s %s." % [setting, spec.past, spec.target]
			result.expected_kind = "non_executable"
		[0, 2]:
			result.text = "You %s %s earlier, back when %s was still open." % [spec.past, spec.target, setting]
			result.expected_kind = "non_executable"
		[0, 3]:
			result.text = "Before sunrise at %s, I watched you %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "non_executable"
		[1, 0]:
			result.text = "You are %s %s at %s right now." % [spec.ing, spec.target, setting]
			result.expected_kind = "non_executable"
		[1, 1]:
			result.text = "At this moment near %s, you seem to be %s %s." % [setting, spec.ing, spec.target]
			result.expected_kind = "non_executable"
		[1, 2]:
			result.text = "I can see that you are still %s %s beside %s." % [spec.ing, spec.target, setting]
			result.expected_kind = "non_executable"
		[1, 3]:
			result.text = "Right now, %s is where you are %s %s." % [setting, spec.ing, spec.target]
			result.expected_kind = "non_executable"
		[2, 0]:
			result.text = "Have you already %s %s since arriving at %s?" % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
		[2, 1]:
			result.text = "Had you %s %s before the doors at %s closed?" % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
		[2, 2]:
			result.text = "By the time we reached %s, had you already %s %s?" % [setting, spec.past, spec.target]
			result.expected_kind = "state_query"
		[2, 3]:
			result.text = "Can you tell me whether you have %s %s since %s?" % [spec.past, spec.target, setting]
			result.expected_kind = "state_query"
		[3, 0]:
			result.text = "Tomorrow, please %s %s after leaving %s." % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[3, 1]:
			result.text = "Once morning reaches %s, I need you to %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
		[3, 2]:
			result.text = "Later, when we return to %s, please %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
		[3, 3]:
			result.text = "You will %s %s after we depart from %s, please." % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[4, 0]:
			result.text = "Would you %s %s when the lights return at %s?" % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[4, 1]:
			result.text = "Could you %s %s if the signal at %s turns white?" % [spec.verb, spec.target, setting]
			result.expected_kind = "request"
		[4, 2]:
			result.text = "If we are allowed back into %s, will you %s %s?" % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
		[4, 3]:
			result.text = "Should the power return at %s, please %s %s." % [setting, spec.verb, spec.target]
			result.expected_kind = "request"
	result.turns = [result.text]
	return result


static func _difficult_case(action_index: int, variant: int) -> Dictionary:
	var result := _base(action_index, variant, "difficult")
	var spec: Dictionary = ACTIONS[action_index]
	var setting: String = SETTINGS[(action_index * 4 + variant) % SETTINGS.size()]
	var adjective: String = ADJECTIVES[(action_index + variant * 2) % ADJECTIVES.size()]
	var style := action_index % 4
	match [variant, style]:
		[0, 0]:
			result.text = "That %s thing you %s yesterday—tomorrow, if it is still at %s, %s %s again." % [adjective, spec.past, setting, spec.verb, spec.target]
			result.expected_kind = "conditional_request"
		[0, 1]:
			result.text = "Although yesterday you %s %s, if tomorrow finds it at %s, please %s %s again." % [spec.past, spec.target, setting, spec.verb, spec.target]
			result.expected_kind = "conditional_request"
		[0, 2]:
			result.text = "Tomorrow at %s—after remembering how you %s %s yesterday—%s %s again if possible." % [setting, spec.past, spec.target, spec.verb, spec.target]
			result.expected_kind = "conditional_request"
		[0, 3]:
			result.text = "What you %s yesterday is irrelevant; should it remain near %s tomorrow, you must %s %s." % [spec.past, setting, spec.verb, spec.target]
			result.expected_kind = "conditional_request"
		[1, 0]:
			result.text = "At %s you was %s %s, but now dont %s it unless I confirm." % [setting, spec.ing, spec.target, spec.verb]
			result.expected_kind = "non_executable"
		[1, 1]:
			result.text = "You had been %s %s at %s, yet from this moment forward never %s it." % [spec.ing, spec.target, setting, spec.verb]
			result.expected_kind = "non_executable"
		[1, 2]:
			result.text = "Even if yesterday you %s %s, I am asking you not to %s it now at %s." % [spec.past, spec.target, spec.verb, setting]
			result.expected_kind = "non_executable"
		[1, 3]:
			result.text = "At %s you will have been %s %s; that prediction is wrong—do not %s it." % [setting, spec.ing, spec.target, spec.verb]
			result.expected_kind = "non_executable"
		[2, 0]:
			result.text = "I thought you had %s %s; turns out you hadnt, so could you maybe %s it now?" % [spec.past, spec.target, spec.verb]
			result.expected_kind = "request"
		[2, 1]:
			result.text = "I said you had %s %s, but I was mistaken; please %s %s now." % [spec.past, spec.target, spec.verb, spec.target]
			result.expected_kind = "request"
		[2, 2]:
			result.text = "The report claims you are %s %s; disregard it and %s %s now." % [spec.ing, spec.target, spec.verb, spec.target]
			result.expected_kind = "request"
		[2, 3]:
			result.text = "By tomorrow they will say you had %s %s; before that happens, could you %s %s?" % [spec.past, spec.target, spec.verb, spec.target]
			result.expected_kind = "request"
		[3, 0]:
			result.text = "Before the %s alarm will have stopped, the %s %s—not the other one—you should %s." % [adjective, adjective, spec.target, spec.verb]
			result.expected_kind = "clarification"
		[3, 1]:
			result.text = "Before tomorrow's alarm ends at %s, you should %s the %s one—or was it the other?" % [setting, spec.verb, adjective]
			result.expected_kind = "clarification"
		[3, 2]:
			result.text = "The %s %s and its twin are both at %s; please %s the other one." % [adjective, spec.target, setting, spec.verb]
			result.expected_kind = "clarification"
		[3, 3]:
			result.text = "You must %s it before leaving %s, but two matching targets are visible." % [spec.verb, setting]
			result.expected_kind = "clarification"
		[4, 0]:
			result.text = "Youll %s %s later, yes? not now; I only ask whether at %s that remains possible." % [spec.verb, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 1]:
			result.text = "Did you %s %s yesterday at %s, or will that be possible tomorrow?" % [spec.verb, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 2]:
			result.text = "Have you ever %s %s at %s, and will you need to tomorrow?" % [spec.verb, spec.target, setting]
			result.expected_kind = "state_query"
		[4, 3]:
			result.text = "Are you currently %s %s at %s, or were you doing that yesterday?" % [spec.ing, spec.target, setting]
			result.expected_kind = "state_query"
	result.turns = [result.text]
	return result


static func _extreme_case(action_index: int, variant: int) -> Dictionary:
	var result := _base(action_index, variant, "extreme")
	var spec: Dictionary = ACTIONS[action_index]
	var setting: String = SETTINGS[(action_index * 7 + variant) % SETTINGS.size()]
	var adjective: String = ADJECTIVES[(action_index * 2 + variant) % ADJECTIVES.size()]
	var detail: String = UNRELATED_DETAILS[action_index]
	var style := action_index % 4
	match [variant, style]:
		[0, 0]:
			result.turns = [
				"%s it—the %s one, after... wait, which one did you see?" % [spec.verb.capitalize(), adjective],
				"While we wait at %s, %s." % [setting, detail],
				"The target I meant was %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[0, 1]:
			result.turns = [
				"Please %s it." % spec.verb,
				"Before I identify it, note that %s." % detail,
				"After checking my notes, I was referring to %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[0, 2]:
			result.turns = [
				"Could you %s it?" % spec.verb,
				"We are still at %s." % setting,
				"Another distraction: %s." % detail,
				"For clarity, by it I mean %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[0, 3]:
			result.turns = [
				"%s—" % spec.verb.capitalize(),
				"I stopped because %s." % detail,
				"Now I can answer: the one I mean is %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[1, 0]:
			result.turns = [
				"Not that. the other... %s it when what happened yesterday hasnt happened tomorrow." % spec.verb,
				"No action yet; while I correct myself, %s." % detail,
				"To be precise, %s %s now." % [spec.verb, spec.target],
			]
			result.expected_kind = "clarification_resolution"
		[1, 1]:
			result.turns = [
				"Could you eventually %s it?" % spec.verb,
				"I am checking the notes from %s." % setting,
				"I meant %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[1, 2]:
			result.turns = [
				"%s the other one?" % spec.verb.capitalize(),
				"Do nothing while %s." % detail,
				"The target is %s." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[1, 3]:
			result.turns = [
				"Please %s it." % spec.verb,
				"At %s I still need a moment." % setting,
				"Use %s as the target." % spec.target,
			]
			result.expected_kind = "clarification_resolution"
		[2, 0]:
			result.turns = [
				"If the signal that was green becomes red, %s—" % spec.verb,
				"Someone pauses at %s while %s." % [setting, detail],
				"Even after noticing that detail, I still have not named the target for %s." % spec.verb,
				"Use %s as the target, but only under that condition." % spec.target,
			]
			result.expected_kind = "conditional_clarification"
		[2, 1]:
			result.turns = [
				"Unless the beacon stays dark, %s it." % spec.verb,
				"Meanwhile at %s, %s." % [setting, detail],
				"I was referring to %s." % spec.target,
			]
			result.expected_kind = "conditional_clarification"
		[2, 2]:
			result.turns = [
				"When the siren has stopped tomorrow, please %s it." % spec.verb,
				"We still have no verified signal at %s." % setting,
				"By it I mean %s." % spec.target,
			]
			result.expected_kind = "conditional_clarification"
		[2, 3]:
			result.turns = [
				"If yesterday's warning returns next week, could you %s it?" % spec.verb,
				"Before answering, remember that %s." % detail,
				"The one I mean is %s." % spec.target,
			]
			result.expected_kind = "conditional_clarification"
		[3, 0]:
			result.turns = [
				"%s the %s... no, dont do anything yet." % [spec.verb.capitalize(), adjective],
				"We can discuss why %s while we wait." % detail,
				"The instruction to %s remains cancelled." % spec.verb,
				"Now I am issuing a new one: %s %s." % [spec.verb, spec.target],
			]
			result.expected_kind = "clarification_resolution"
		[3, 1]:
			result.turns = [
				"%s it—actually, cancel that instruction." % spec.verb.capitalize(),
				"Let us wait while %s." % detail,
				"This is a fresh request: please %s %s." % [spec.verb, spec.target],
			]
			result.expected_kind = "clarification_resolution"
		[3, 2]:
			result.turns = [
				"I almost asked you to %s the %s object, but do not act." % [spec.verb, adjective],
				"The cancelled thought concerned %s." % setting,
				"New instruction—%s %s now." % [spec.verb.capitalize(), spec.target],
			]
			result.expected_kind = "clarification_resolution"
		[3, 3]:
			result.turns = [
				"Never %s it; that was not a valid order." % spec.verb,
				"Nothing should happen while %s." % detail,
				"I am replacing it with this: %s %s." % [spec.verb, spec.target],
			]
			result.expected_kind = "clarification_resolution"
		[4, 0]:
			result.turns = [
				"Yesterday you %s it, presently you might be %s it, and tomorrow—if I meant %s—would you %s the other?" % [spec.past, spec.ing, spec.target, spec.verb],
				"By other I do not mean anything visible at %s, where %s." % [setting, detail],
				"There are two possible targets for %s, so ask me which one." % spec.verb,
			]
			result.expected_kind = "clarification"
		[4, 1]:
			result.turns = [
				"%s the other one, whichever that is." % spec.verb.capitalize(),
				"The records at %s name no target." % setting,
				"Two candidates remain, so ask which target I intend for %s." % spec.verb,
			]
			result.expected_kind = "clarification"
		[4, 2]:
			result.turns = [
				"Could you %s it, or perhaps the other one?" % spec.verb,
				"I cannot resolve that while %s." % detail,
				"I still have not named the target for %s." % spec.verb,
			]
			result.expected_kind = "clarification"
		[4, 3]:
			result.turns = [
				"Before tomorrow, %s it—but which one?" % spec.verb,
				"At %s there are matching objects." % setting,
				"There are two possible targets for %s, so no action yet." % spec.verb,
			]
			result.expected_kind = "clarification"
	result.text = "\n".join(result.turns)
	return result
