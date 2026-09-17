#include "native_turn_analyzer.h"
#include "semantic_bridge.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace norucore {

namespace {

bool is_word_character(char32_t character) {
	// Lettere accentate comprese: "Zoë, open the door" non si spezza sulla ë (tracker #17).
	if (character >= 0xC0 && character <= 0x24F && character != 0xD7 && character != 0xF7) return true;
	return (character >= U'a' && character <= U'z') || (character >= U'A' && character <= U'Z') || (character >= U'0' && character <= U'9') || character == U'_';
}

bool is_lexical_continuation(char32_t character) {
	return is_word_character(character) || character == U'\'' || character == U'’';
}

bool has_word_character(const String &text) {
	for (int i = 0; i < text.length(); ++i) if (is_word_character(text.unicode_at(i))) return true;
	return false;
}

bool position_is_inside_delimiter(const String &text, int position, const String &delimiter) {
	if (position < 0 || delimiter.is_empty()) return false;
	const String prefix = text.left(position);
	int count = 0;
	int from = 0;
	while (from < prefix.length()) {
		const int found = prefix.find(delimiter, from);
		if (found < 0) break;
		count += 1;
		from = found + delimiter.length();
	}
	return count % 2 == 1;
}

int find_word(const String &text, const String &word) {
	const String searchable = text.to_lower().strip_edges();
	const String needle = word.to_lower().strip_edges();
	if (needle.is_empty()) return -1;
	int from = 0;
	while (from <= searchable.length() - needle.length()) {
		const int position = searchable.find(needle, from);
		if (position < 0) return -1;
		const bool left_boundary = position == 0 || !is_lexical_continuation(searchable.unicode_at(position - 1));
		const int after = position + needle.length();
		const bool right_boundary = after >= searchable.length() || !is_lexical_continuation(searchable.unicode_at(after));
		if (left_boundary && right_boundary) return position;
		from = position + 1;
	}
	return -1;
}

int find_last_word(const String &text, const String &word) {
	const String searchable = text.to_lower().strip_edges();
	const String needle = word.to_lower().strip_edges();
	if (needle.is_empty()) return -1;
	int from = searchable.length() - needle.length();
	while (from >= 0) {
		const int position = searchable.rfind(needle, from);
		if (position < 0) return -1;
		const bool left_boundary = position == 0 || !is_lexical_continuation(searchable.unicode_at(position - 1));
		const int after = position + needle.length();
		const bool right_boundary = after >= searchable.length() || !is_lexical_continuation(searchable.unicode_at(after));
		if (left_boundary && right_boundary) return position;
		from = position - 1;
	}
	return -1;
}

// Il prefisso che conta per decidere se una frase e' un ordine e' solo quello che sta
// **dopo l'ultimo confine di struttura**, non tutta la frase. Fino al 2026-09-04 questa
// regola viveva in linea dentro un solo ramo e conosceva `,` `;` `—` `:` ma **non il
// punto**: "i just put a red notebook on the desk. open it." contava nove parole di
// prefisso e finiva in `conversation`, mentre la stessa frase con una virgola al posto
// del punto veniva riconosciuta. Il punto e' un confine piu' forte della virgola, non
// piu' debole. Si cerca `". "` — punto **piu' spazio**, la stessa forma che
// `split_clauses` usa gia' per `"? "` — cosi' `2.5 kg` e le abbreviazioni attaccate
// restano intere.
int last_structural_boundary(const String &prefix) {
	int boundary = prefix.rfind(",");
	const String em_dash = String::chr(0x2014);
	for (const String &marker : {String(";"), em_dash, String(":")}) {
		const int found = prefix.rfind(marker);
		if (found > boundary) boundary = found;
	}
	// Il punto conta solo quando chiude davvero una frase: seguito da uno spazio, o
	// ultimo carattere del prefisso.
	const int sentence_stop = prefix.rfind(". ");
	if (sentence_stop > boundary) boundary = sentence_stop;
	if (prefix.strip_edges().ends_with(".")) {
		const int trailing = prefix.rfind(".");
		if (trailing > boundary) boundary = trailing;
	}
	return boundary;
}

// Segnali del discorso in testa o in coda al prefisso locale: "ok now open the
// door" e' un imperativo quanto "open the door" (tracker #33). Classe chiusa.
String without_discourse_markers(const String &prefix) {
	PackedStringArray words = prefix.strip_edges().trim_suffix(",").split(" ", false);
	// "and pass it to me after?", "maybe you can open the window", "actually, grab the mug": congiunzioni e
	// avverbi di attenuazione in testa non tolgono l'imperativo (corpus esterno del 2026-09-15). Classe chiusa.
	const Array markers = Array::make("ok", "okay", "now", "so", "alright", "well", "hey", "then", "right", "next", "first", "also", "afterwards", "finally", "later", "and", "but", "or", "oh", "um", "uh", "yeah", "maybe", "actually", "just");
	while (!words.is_empty() && markers.has(String(words[0]).trim_suffix(","))) words.remove_at(0);
	while (!words.is_empty() && markers.has(String(words[words.size() - 1]).trim_suffix(","))) words.remove_at(words.size() - 1);
	return String(" ").join(words);
}

String local_prefix_before(const String &prefix) {
	const int boundary = last_structural_boundary(prefix);
	return boundary >= 0 ? prefix.substr(boundary + 1).strip_edges() : prefix.strip_edges();
}

// Una virgola separa una proposizione nuova solo quando subito dopo comincia un
// soggetto. "give me back my notebook, you've had it long enough" va tagliata;
// "the red, round ball" no — li' la virgola separa due aggettivi dello stesso gruppo
// nominale, e tagliarla consegna `red`.
//
// La differenza non e' estetica. Un bersaglio illeggibile (`red, round ball`) non
// aggancia niente e fa **chiedere**; un bersaglio corto e plausibile (`red`) aggancia
// il primo oggetto rosso della scena e fa **agire**. Fra il 2026-09-04 e il 2026-09-05
// il taglio su qualunque virgola ha trasformato il primo modo di sbagliare nel secondo,
// che e' molto peggio: misurato su "pick up the red, round ball" e
// "open the big, heavy, wooden door".
//
// I pronomi soggetto sono una classe chiusa di sette parole: non e' un dizionario.
int clause_comma_boundary(const String &target) {
	int from = 0;
	while (true) {
		const int comma = target.find(",", from);
		if (comma < 0) return -1;
		const String rest = target.substr(comma + 1).strip_edges();
		int word_end = 0;
		while (word_end < rest.length() && is_word_character(rest.unicode_at(word_end))) word_end += 1;
		const String first_word = rest.left(word_end);
		for (const char *subject : {"i", "you", "he", "she", "it", "we", "they"}) {
			if (first_word == subject) return comma;
		}
		from = comma + 1;
	}
}

String trim_target_span(String target) {
	// Tutta la punteggiatura in coda, anche ripetuta ("door!!!") e le virgolette
	// spaiate ("door\"") (tracker #10, #68).
	{
		String previous;
		while (previous != target) {
			previous = target;
			target = target.strip_edges().trim_suffix(".").trim_suffix("!").trim_suffix("?").trim_suffix(";").trim_suffix("\"").trim_suffix("'").strip_edges();
		}
	}
	int boundary = -1;
	// Il bersaglio deve avere una **fine**. Fino al 2026-09-04 si fermava solo su queste
	// quindici frasi e su nient'altro: non su una virgola, non su un punto, non su una
	// congiunzione. Nella prova del 2026-09-03 undici bersagli su tredici erano
	// inutilizzabili — `back my notebook, you've had it long enough`, `patience, i lost
	// it around three pm`, `it for a second`, `notebook to the last page` — e un
	// bersaglio cosi' non aggancia niente nel registro di grounding.
	//
	// I confini aggiunti sono punteggiatura e parole di funzione: classe chiusa, finita,
	// che non cresce col dominio. Non e' un dizionario, ed e' la stessa natura dei
	// determinanti e delle preposizioni che questa funzione toglie gia' qui sotto.
	// `" to "` e `" for "` chiedono lo spazio a sinistra, quindi "to the kitchen" e
	// "for me" all'inizio del bersaglio non vengono toccati: li' la preposizione fa
	// parte del bersaglio e la toglie il ciclo dei prefissi.
	{
		const int clause_comma = clause_comma_boundary(target);
		if (clause_comma >= 0) boundary = clause_comma;
	}
	for (const char *raw_marker : {
		". ", " and ",
		" while ", " near ", " beside ", " before ", " after ", " when ", " if ", " unless ",
		" to ", " for ", " again", " now", " later", ", please", " please",
		// Un complemento che descrive o colloca non e' il bersaglio: "the ball with the
		// red stripe", "the ball where the dog sits", "the keys by the door" (tracker #25).
		" where ", " by ",
		// Tempo e vicinanza non sono la cosa: "hold onto them till tomorrow", "my jacket from
		// earlier", "my glasses right next to you" (tracker Analizzatore #84). Classi chiuse.
		" till ", " until ", " from earlier", " from before", " tomorrow", " tonight", " yesterday",
		" right next to ", " next to ",
		// "bring it over here", "move the backpack off the chair", "pass me the remote maybe" (corpus esterno).
		" over here", " right here", " here", " off the ", " closer to ", " away from ", " maybe", " real quick", " for a sec"
	}) {
		const int found = target.find(String(raw_marker));
		if (found >= 0 && (boundary < 0 || found < boundary)) boundary = found;
	}
	if (boundary >= 0) target = target.left(boundary).strip_edges();
	bool stripped = true;
	while (stripped) {
		stripped = false;
		// Determinanti, preposizioni e particelle verbali: classi chiuse.
		// Le particelle mancavano, ed e' perche' "give me back my notebook" dava per
		// bersaglio `back my notebook`. I pronomi oggetto **non** stanno qui: "me" in
		// "pass me the envelope" e' il destinatario e in "hug me" e' il bersaglio, e la
		// differenza e' posizionale, non lessicale. Se ne occupa
		// `extract_give_recipient`, che quel ruolo lo sa gia' leggere.
		for (const char *prefix : {"the ", "a ", "an ", "your ", "my ", "to ", "on ", "at ", "in ", "from ",
			"back ", "up ", "down ", "out ", "off ", "over "}) {
			if (target.begins_with(prefix)) {
				target = target.trim_prefix(prefix).strip_edges();
				stripped = true;
			}
		}
	}
	target = target.trim_suffix(",").strip_edges();
	// "hold onto them": davanti a un pronome "onto" e "into" sono la particella del verbo. Davanti
	// a un nome restano ("jump onto the mat" ha per bersaglio il posto).
	for (const char *particle : {"onto ", "into "}) {
		if (!target.begins_with(particle)) continue;
		for (const char *pronoun : {"it", "them", "this", "that", "him", "her"}) {
			if (target.trim_prefix(particle) == String(pronoun)) target = pronoun;
		}
	}
	// Particella rimasta in coda al bersaglio: "the box back" (tracker #31).
	for (const char *particle : {" back", " away", " up", " down", " over", " out", " off"}) {
		if (target.ends_with(particle)) target = target.left(target.length() - String(particle).length()).strip_edges();
	}
	// Lo stato in cui finisce la cosa non e' la cosa: "kick the locker shut", "fold them closed",
	// "pull the rope loose" (tracker Analizzatore #84, #103). Classe chiusa di complementi risultativi.
	for (const char *result : {" shut", " closed", " open", " loose", " free", " apart", " flat", " clean"}) {
		if (target.ends_with(result) && target.length() > String(result).length()) target = target.left(target.length() - String(result).length()).strip_edges();
	}
	return target;
}

// "put the banana in the box", "combine the dust with the resin": la cosa resta nel
// bersaglio, il posto o il secondo oggetto va in `location` (tracker #70, #25). Solo
// quando dopo la preposizione comincia un gruppo nominale con determinante.
void split_object_location(String &target, String &location) {
	int split_at = -1;
	int split_length = 0;
	// "throw your hat at the zombie": anche la direzione separa (tracker Analizzatore #103).
	for (const char *raw_preposition : {" on ", " in ", " into ", " onto ", " inside ", " under ", " next to ", " with ", " behind ", " at ", " toward ", " towards ", " against ", " across ", " through ", " over "}) {
		const String preposition = raw_preposition;
		const int found = target.find(preposition);
		if (found <= 0 || (split_at >= 0 && found >= split_at)) continue;
		const PackedStringArray after_words = target.substr(found + preposition.length()).strip_edges().split(" ", false);
		if (after_words.is_empty()) continue;
		for (const char *determiner : {"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that"}) {
			if (String(after_words[0]) == determiner) {
				split_at = found;
				split_length = preposition.length();
				break;
			}
		}
	}
	if (split_at <= 0) return;
	location = trim_target_span(target.substr(split_at + split_length));
	target = trim_target_span(target.left(split_at));
}

void extract_give_recipient(String &target, String &recipient) {
	for (const char *raw_recipient : {"the user", "user", "me", "us", "him", "her", "them"}) {
		const String surface = raw_recipient;
		const String normalized = (surface == "me" || surface == "us" || surface == "user" || surface == "the user") ? "user" : surface;
		if (target == surface) {
			recipient = normalized;
			target = "";
			return;
		}
		const String prefix = surface + String(" ");
		// "hang them by the door": dopo il pronome comincia un luogo, quindi il pronome e'
		// la cosa, non il destinatario come in "hand them the book" (tracker #80). Le particelle
		// (back, up, down...) non contano: in "give me back my notebook" "me" resta il destinatario.
		bool place_follows = false;
		for (const char *preposition : {"by ", "on ", "in ", "into ", "onto ", "at ", "to ", "under ", "over ", "near ", "next ", "beside ", "behind ", "inside ", "against ", "from ", "with "}) {
			if (target.trim_prefix(prefix).begins_with(preposition)) place_follows = true;
		}
		if (target.begins_with(prefix) && !place_follows) {
			recipient = normalized;
			target = target.trim_prefix(prefix).strip_edges();
			return;
		}
		for (const char *raw_relation : {" to ", " for "}) {
			const String suffix = String(raw_relation) + surface;
			const int suffix_start = target.find(suffix);
			if (suffix_start >= 0 && suffix_start + suffix.length() == target.length()) {
				recipient = normalized;
				target = target.left(suffix_start).strip_edges();
				return;
			}
		}
	}
}

int end_after_word_tokens(const String &text, int start, int token_count) {
	int cursor = MAX(0, start);
	for (int token = 0; token < token_count; ++token) {
		while (cursor < text.length() && !is_word_character(text.unicode_at(cursor))) cursor += 1;
		while (cursor < text.length() && is_word_character(text.unicode_at(cursor))) cursor += 1;
	}
	return cursor;
}

bool starts_any(const String &text, const Array &prefixes) {
	const String lower = text.to_lower().strip_edges();
	for (int i = 0; i < prefixes.size(); ++i) {
		if (lower.begins_with(String(prefixes[i]))) return true;
	}
	return false;
}

// Una richiesta modale invertita — "can you open it" — riconosciuta per **forma
// grammaticale** invece che per stringa letterale.
//
// Fino al 2026-09-09 questa costruzione viveva in tre elenchi copiati a mano di
// sei stringhe ciascuno (`"can you"`, `"could you"`, ...). Due difetti che
// vengono dallo stesso posto: i modali coperti erano sei su nove — `might`,
// `must` e `should` non c'erano — e una grafia diversa del pronome faceva
// fallire tutto, per cui `can u hand me the book` restava chiacchiera **pur
// avendo riconosciuto `give`**. Il verbo non mancava: mancava la corrispondenza
// con l'elenco.
//
// La differenza fra le due cose e' il motivo per cui questa forma e' preferibile,
// e va tenuta a mente prima di allungare qualcosa qui dentro:
//
// - i **modali** inglesi sono una classe **chiusa per grammatica**, non una lista
//   che qualcuno ha scelto: sono nove e non ne nascono di nuovi;
// - le grafie di **`you`** sono una lista, ma e' la lista delle grafie di *una
//   parola sola*. Non cresce col dominio: cresce solo se nasce un modo nuovo di
//   scrivere lo stesso pronome, e quando manca il costo e' un avviso a schermo
//   (`TURN_COMPREHENSION` / popup), non un fallimento muto.
//
// Non copre la forma soggetto-modale ("you should", "you will"), che resta dove
// era: li' serve la guardia sul futuro probabilistico ("you will probably"), e
// unificarle la perderebbe.
bool is_modal_word(const String &token) {
	for (const char *form : {"can", "could", "may", "might", "must", "shall", "should", "will", "would"}) {
		if (token == form) return true;
	}
	return false;
}

bool is_second_person_word(const String &token) {
	for (const char *form : {"you", "u", "ya", "yah"}) {
		if (token == form) return true;
	}
	return false;
}

// Modale immediatamente seguito dal pronome. L'adiacenza e' quello che distingue
// la domanda ("can you open it") dalla narrazione ("you can open it", che e' una
// constatazione) — ed e' la stessa distinzione che i due elenchi separati
// facevano prima, scrivendo le coppie una per una nei due ordini.
// Indice del primo token **dopo** la coppia modale+pronome, o -1 se non c'e'.
// Restituire la posizione e non un booleano serve a chi deve sapere dove
// comincia il predicato: "can you open it" -> il predicato e' a 2.
int inverted_modal_predicate_start(const Array &tokens, bool clause_initial_only) {
	const int last = clause_initial_only ? 1 : tokens.size() - 1;
	for (int i = 0; i + 1 < tokens.size() && i < last; ++i) {
		if (is_modal_word(String(tokens[i])) && is_second_person_word(String(tokens[i + 1]))) return i + 2;
	}
	return -1;
}

bool has_inverted_modal_request(const Array &tokens) {
	return inverted_modal_predicate_start(tokens, false) >= 0;
}

Dictionary empty_roles() {
	Dictionary roles;
	const char *keys[] = {"action", "object", "destination", "recipient_or_direction", "object_or_class", "query_kind", "event_kind", "hazard", "source", "imminence", "severity"};
	for (const char *key : keys) roles[key] = "";
	roles["target_mode"] = "optional";
	return roles;
}

Array string_array(std::initializer_list<const char *> values) {
	Array result;
	for (const char *value : values) result.append(String(value));
	return result;
}

Array source_span_array(const String &normalized_input, int start_index, int end_index) {
	const int safe_start = MAX(0, MIN(start_index, normalized_input.length()));
	const int safe_end = MAX(safe_start, MIN(end_index, normalized_input.length()));
	Dictionary span;
	span["segment_id"] = "segment_1";
	span["start_index"] = safe_start;
	span["end_index"] = safe_end;
	span["text"] = normalized_input.substr(safe_start, safe_end - safe_start);
	Array spans;
	spans.append(span);
	return spans;
}

// Una frase che ritira quella di prima: "open the door; actually, never mind"
// (tracker #30). Le formule di ritiro sono una classe chiusa del discorso, come
// quelle che `build_frame` gia' riconosce **dentro** la stessa frase; qui servono
// quando il ritiro sta nella frase dopo, cioe' da quando le frasi si separano.
// Quante volte: cifra o numero scritto in lettere fino a dieci (tracker #35). I
// numerali sono una classe chiusa della lingua.
int number_value(const String &token) {
	if (token.is_valid_int()) return token.to_int();
	const char *words[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
	for (int i = 0; i < 10; ++i) if (token == words[i]) return i + 1;
	return 0;
}

bool is_retraction(const String &clause) {
	String lower = clause.to_lower().strip_edges();
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const char *lead : {"actually", "no", "oh", "wait", "hmm", "sorry", "ok", "okay", ",", "-"}) {
			const String prefix = lead;
			if (lower.begins_with(prefix) && (lower.length() == prefix.length() || !is_word_character(lower.unicode_at(prefix.length())))) {
				lower = lower.substr(prefix.length()).strip_edges();
				stripped = true;
			}
		}
	}
	for (const char *formula : {"never mind", "nevermind", "cancel that", "cancel it", "cancel this", "forget it", "forget that", "forget about it", "scratch that", "don't do that", "don't do it", "do not do that", "not now"}) {
		if (lower.begins_with(formula)) return true;
	}
	return false;
}

int find_clause_start(const String &normalized_input, const String &clause, int search_from) {
	const String needle = clause.strip_edges();
	if (needle.is_empty()) return MAX(0, MIN(search_from, normalized_input.length()));
	int start = normalized_input.to_lower().find(needle.to_lower(), MAX(0, search_from));
	if (start < 0) start = normalized_input.to_lower().find(needle.to_lower());
	return start;
}

} // namespace

void NativeTurnAnalyzer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("analyze_turn", "raw_input", "scene_state", "options"), &NativeTurnAnalyzer::analyze_turn, DEFVAL(Dictionary()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("compile_turn", "raw_input", "analysis_result", "capability_snapshot", "scene_state", "options"), &NativeTurnAnalyzer::compile_turn, DEFVAL(Dictionary()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("analyze_and_compile", "raw_input", "capability_snapshot", "scene_state", "options"), &NativeTurnAnalyzer::analyze_and_compile, DEFVAL(Dictionary()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("propose_semantic_links", "raw_input", "analysis_result", "capability_snapshot", "options"), &NativeTurnAnalyzer::propose_semantic_links, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("validate_contract", "result"), &NativeTurnAnalyzer::validate_contract);
	ClassDB::bind_method(D_METHOD("get_build_info"), &NativeTurnAnalyzer::get_build_info);
}

String NativeTurnAnalyzer::normalize(const String &input) const {
	String out = input.replace("\r\n", "\n").replace("\r", "\n").strip_edges();
	while (out.contains("  ")) out = out.replace("  ", " ");
	return out;
}

Array NativeTurnAnalyzer::tokenize(const String &input) const {
	String clean = input.to_lower();
	const char *punctuation[] = {".", ",", "!", "?", ";", ":", "\"", "(", ")", "[", "]", "*", "\n"};
	for (const char *marker : punctuation) clean = clean.replace(marker, " ");
	while (clean.contains("  ")) clean = clean.replace("  ", " ");
	Array result;
	PackedStringArray parts = clean.strip_edges().split(" ", false);
	// Keep one overflow sentinel so callers can distinguish an exact-limit
	// input from a truncated one without allocating unbounded token arrays.
	for (int i = 0; i < parts.size() && i <= MAX_TOKENS; ++i) result.append(parts[i]);
	return result;
}

String NativeTurnAnalyzer::lemma(const String &raw) const {
	String word = raw.to_lower().strip_edges();
	// Costruita una volta sola: fino al 2026-09-14 questa tabella si ricostruiva a ogni
	// parola di ogni frase (tracker #55).
	Dictionary &irregular = base_irregular_forms;
	if (irregular.is_empty()) {
	irregular["am"] = "be"; irregular["is"] = "be"; irregular["are"] = "be"; irregular["was"] = "be"; irregular["were"] = "be"; irregular["been"] = "be";
	irregular["has"] = "have"; irregular["had"] = "have"; irregular["does"] = "do"; irregular["did"] = "do"; irregular["done"] = "do";
	irregular["goes"] = "go"; irregular["went"] = "go"; irregular["gone"] = "go"; irregular["takes"] = "take"; irregular["took"] = "take"; irregular["taken"] = "take";
	irregular["gives"] = "give"; irregular["gave"] = "give"; irregular["given"] = "give"; irregular["makes"] = "make"; irregular["made"] = "make";
	irregular["finds"] = "find"; irregular["found"] = "find"; irregular["wears"] = "wear"; irregular["wore"] = "wear"; irregular["worn"] = "wear";
	irregular["sits"] = "sit"; irregular["sat"] = "sit"; irregular["stands"] = "stand"; irregular["stood"] = "stand"; irregular["lies"] = "lie"; irregular["lying"] = "lie"; irregular["lay"] = "lie"; irregular["lain"] = "lie";
	irregular["broke"] = "break"; irregular["broken"] = "break"; irregular["fled"] = "flee"; irregular["hid"] = "hide"; irregular["hidden"] = "hide";
	irregular["held"] = "hold"; irregular["knelt"] = "kneel"; irregular["received"] = "receive";
	}
	if (irregular.has(word)) return irregular[word];
	// Le altre forme irregolari vengono da WordNet (`verb.exc`), attraverso il registro:
	// "drew the sword" riduce "drew" a "draw" (tracker #5, #15, #50).
	if (supplied_irregular_forms.has(word)) return supplied_irregular_forms[word];
	if (word.ends_with("ies") && word.length() > 4) return word.left(word.length() - 3) + "y";
	if (word.ends_with("ing") && word.length() > 5) {
		String stem = word.left(word.length() - 3);
		if (stem.length() >= 2 && stem.right(1) == stem.substr(stem.length() - 2, 1)) stem = stem.left(stem.length() - 1);
		return stem;
	}
	if (word.ends_with("ied") && word.length() > 4) return word.left(word.length() - 3) + "y";
	if (word.ends_with("ed") && word.length() > 4) return word.left(word.length() - 2);
	if (word.ends_with("s") && !word.ends_with("ss") && !word.ends_with("us") && !word.ends_with("is") && !word.ends_with("ous") && !word.ends_with("ics") && word.length() > 3) return word.left(word.length() - 1);
	return word;
}

void NativeTurnAnalyzer::absorb_irregular_forms(const Dictionary &capability_snapshot) const {
	const Dictionary supplied = capability_snapshot.get("verb_irregular_forms", Dictionary());
	if (!supplied.is_empty()) supplied_irregular_forms = supplied;
	const Dictionary verbs = capability_snapshot.get("verb_lemmas", Dictionary());
	if (!verbs.is_empty()) supplied_verb_lemmas = verbs;
}

int NativeTurnAnalyzer::find_lemma_phrase_start(const String &text, const String &phrase) const {
	const Array surface_tokens = tokenize(text);
	const Array phrase_tokens = tokenize(phrase);
	if (surface_tokens.is_empty() || phrase_tokens.is_empty() || phrase_tokens.size() > surface_tokens.size()) return -1;
	Vector<int> token_starts;
	token_starts.resize(surface_tokens.size());
	int search_from = 0;
	const String lower_text = text.to_lower();
	for (int i = 0; i < surface_tokens.size(); ++i) {
		const String token = surface_tokens[i];
		int position = lower_text.find(token.to_lower(), search_from);
		while (position >= 0) {
			const bool left_boundary = position == 0 || !is_word_character(lower_text.unicode_at(position - 1));
			const int after = position + token.length();
			const bool right_boundary = after >= lower_text.length() || !is_word_character(lower_text.unicode_at(after));
			if (left_boundary && right_boundary) break;
			position = lower_text.find(token.to_lower(), position + 1);
		}
		if (position < 0) return -1;
		token_starts.write[i] = position;
		search_from = position + token.length();
	}
	for (int start = 0; start <= surface_tokens.size() - phrase_tokens.size(); ++start) {
		bool matches = true;
		for (int offset = 0; offset < phrase_tokens.size(); ++offset) {
			const String surface = surface_tokens[start + offset];
			const String base = phrase_tokens[offset];
			const String reduced = lemma(surface);
			bool token_matches = reduced == base;
			// Compare inflection candidates against the known capability lemma.
			// This avoids relying on an unrestricted dictionary while covering
			// regular e-drop, consonant doubling, -y and participle forms.
			if (!token_matches && base.ends_with("e")) token_matches = surface == base + String("d") || surface == base.left(base.length() - 1) + String("ing");
			if (!token_matches && base.ends_with("y") && base.length() > 1) token_matches = surface == base.left(base.length() - 1) + String("ied") || surface == base.left(base.length() - 1) + String("ies");
			if (!token_matches) token_matches = surface == base + String("ed") || surface == base + String("ing") || surface == base + String("s") || surface == base + String("es");
			if (!token_matches && base.length() >= 2) token_matches = surface == base + base.right(1) + String("ed") || surface == base + base.right(1) + String("ing");
			if (!token_matches) {
				matches = false;
				break;
			}
		}
		if (matches) return token_starts[start];
	}
	return -1;
}

Array NativeTurnAnalyzer::split_clauses(const String &input) const {
	String marked = input.strip_edges();
	// Sentence-final punctuation is a structural boundary when more text
	// follows. Keep the punctuation on the preceding clause so speech-act
	// classification can still distinguish questions and exclamations.
	marked = marked.replace("? ", "?| ").replace("! ", "!| ").replace("\n", "| ");
	// Il punto seguito da uno spazio chiude una frase (tracker #26, #32). Fino al
	// 2026-09-14 non separava niente: "open the door. close the window" era una frase
	// sola e un verbo spariva. Restano intere le abbreviazioni: una lettera sola o una
	// parola con un punto dentro ("p.m.", "e.g.") e i titoli, classe chiusa.
	{
		String split_on_stops;
		int from = 0;
		while (true) {
			const int stop = marked.find(". ", from);
			if (stop < 0) break;
			int word_start = stop;
			while (word_start > 0 && (is_word_character(marked.unicode_at(word_start - 1)) || marked.unicode_at(word_start - 1) == U'.')) word_start -= 1;
			const String word = marked.substr(word_start, stop - word_start).to_lower();
			bool abbreviation = word.length() <= 1 || word.contains(".");
			for (const char *title : {"mr", "mrs", "ms", "dr", "st", "jr", "sr", "vs", "etc", "no"}) if (word == title) abbreviation = true;
			split_on_stops += marked.substr(from, stop + 1 - from) + (abbreviation ? " " : "| ");
			from = stop + 2;
		}
		marked = split_on_stops + marked.substr(from);
	}
	const char *markers[] = {
		", and finally ", ", finally ", " and finally ",
		", and then ", ", then ", " and then ", " then ",
		", and after that ", ", after that ", " and after that ",
		", and next ", ", next ", " and next ",
		", and afterwards ", ", afterwards ", " and afterwards ",
		", and later ", ", later ", " and later ",
		";", " and create ", " and make ", " and are you ", " and do you "
	};
	for (const char *marker : markers) {
		const String replacement = String(marker).contains("create") ? "| create " : String(marker).contains("make") ? "| make " : String(marker).contains("are you") ? "| are you " : String(marker).contains("do you") ? "| do you " : "| ";
		marked = marked.replacen(marker, replacement);
	}
	// "I left a tin box on the desk, did you notice it?": la domanda in coda comincia con
	// ausiliare e soggetto, classe chiusa, e fa frase a se'. Senza, il racconto finiva
	// dentro una domanda e l'oggetto non nasceva (tracker Analizzatore #79).
	for (const char *tail : {"did you ", "do you ", "can you ", "could you ", "would you ", "will you ", "have you ", "are you ", "is it ", "was it "}) {
		marked = marked.replace(String(", ") + tail, String("| ") + tail);
	}
	Array output;
	PackedStringArray parts = marked.split("|", false);
	// As with tokens, preserve one overflow clause for a safe limit decision.
	for (int i = 0; i < parts.size() && i <= MAX_FRAMES; ++i) {
		String clause = parts[i].strip_edges().trim_suffix(",");
		if (!clause.is_empty()) output.append(clause);
	}
	return output;
}

String NativeTurnAnalyzer::infer_clause_speech_act(const String &text, const Array &tokens, const Array &lemmas) const {
	const String lower = text.to_lower().strip_edges();
	if (tokens.is_empty()) return "fragment";
	// Stessa regola grammaticale del resto del file, non un quarto elenco:
	// `clause_initial_only` perche' qui conta solo la coppia in testa alla frase.
	const bool modal_request = inverted_modal_predicate_start(tokens, true) >= 0;
	if (lower.ends_with("?") && !modal_request) return "question";
	if (modal_request || starts_any(lower, string_array({"please ", "i need you to ", "i want you to ", "i would like you to ", "i'd like you to ", "i am asking you to ", "go ahead and "}))) return "request";
	// Un intercalare seguito dalla virgola ("oh,", "wait,", "Sasha,") e una congiunzione in
	// testa non sono il soggetto ne' il verbo: "oh, and I left a tin box on the desk" e
	// "wait, I just checked my pocket" raccontano, non ordinano (tracker Analizzatore #78).
	// Si decide sulla parola dopo, con la stessa regola di sempre.
	int head = 0;
	String rest = lower;
	while (head < tokens.size() - 1) {
		const String token = tokens[head];
		if (rest.begins_with(token + String(",")) ) rest = rest.substr(token.length() + 1).strip_edges();
		else if (string_array({"and", "but", "so", "or"}).has(token) && rest.begins_with(token + String(" "))) rest = rest.substr(token.length()).strip_edges();
		else break;
		++head;
	}
	const String first = tokens[head];
	const String first_lemma = head < lemmas.size() ? String(lemmas[head]) : first;
	const Array declarative_starters = string_array({
		"i", "you", "he", "she", "it", "we", "they", "someone", "people", "the", "this", "that", "there",
		"yesterday", "tomorrow", "later", "eventually", "someday", "soon", "tonight", "next", "maybe", "perhaps", "probably"
	});
	// Un soggetto contratto e' pur sempre un soggetto. `tokenize()` non sostituisce
	// l'apostrofo — e' un carattere lessicale nelle contrazioni — quindi "I'm"
	// arriva qui come il token `i'm`, che non combacia con `i`, e ogni frase che
	// comincia per "I'm" veniva classificata **richiesta**. Misurato il 2026-09-07:
	// e' l'origine di `i'm` fra i verbi dichiarati sconosciuti, e un rilevatore di
	// richieste fallite costruito su questo `speech_act` produceva 17 falsi
	// positivi su 60 turni reali — si accendeva su "goodnight Sasha".
	//
	// Si confronta la parte davanti all'apostrofo invece di allungare l'elenco:
	// una regola sola, che copre anche you're, we're, they're, he'd, she'll.
	// `it's` e `that's` funzionavano gia', ma per caso — il lemmatizzatore taglia
	// la `s` finale e il confronto falliva per un motivo diverso da questo.
	const int contraction = first.find("'");
	const String first_head = contraction > 0 ? first.left(contraction) : first;
	// Bare imperatives use the Analyzer's base-form morphology at clause start.
	// Inflected/past forms and explicit declarative starters remain statements.
	if (!declarative_starters.has(first_head) && first == first_lemma) return "request";
	return "statement";
}

Dictionary NativeTurnAnalyzer::extract_clause_roles(const String &text, const Array &tokens, const Array &lemmas, const String &speech_act) const {
	Dictionary roles = empty_roles();
	if (tokens.is_empty()) return roles;
	int predicate_index = 0;
	const String lower = text.to_lower().strip_edges();
	// Il predicato di una richiesta modale invertita sta **dopo** la coppia, e
	// dov'e' lo dice la stessa funzione che la riconosce. Prima lo diceva un
	// elenco di sei stringhe, e con una grafia diversa del pronome ("can u open
	// it") l'indice restava 0: il predicato diventava `can` e l'oggetto tutto il
	// resto della frase, che e' cio' che il ponte semantico poi trasporta.
	const int modal_predicate_start = inverted_modal_predicate_start(tokens, true);
	if (modal_predicate_start >= 0) predicate_index = modal_predicate_start;
	else if (lower.begins_with("please ")) predicate_index = 1;
	else if (starts_any(lower, string_array({"i need you to ", "i want you to ", "i would like you to ", "i'd like you to ", "i am asking you to "}))) {
		for (int index = 0; index < tokens.size(); ++index) if (String(tokens[index]) == "to") predicate_index = index + 1;
	} else if (lower.begins_with("go ahead and ")) predicate_index = 3;
	if (predicate_index >= tokens.size()) return roles;
	roles["predicate_lemma"] = predicate_index < lemmas.size() ? lemmas[predicate_index] : tokens[predicate_index];
	roles["predicate_token_index"] = predicate_index;
	String payload;
	for (int index = predicate_index + 1; index < tokens.size(); ++index) payload += (payload.is_empty() ? "" : " ") + String(tokens[index]);
	// Il destinatario si legge **prima** del taglio, non dopo. Dal 2026-09-04
	// `trim_target_span` si ferma anche su `" to "`, quindi tagliando per primo il
	// `find(" to ")` qui sotto non trova piu' niente e "give the book to me" perde il
	// proprio destinatario. L'ordine e' la correzione: separa, poi ripulisci i pezzi.
	String recipient;
	const int recipient_boundary = payload.find(" to ");
	if (recipient_boundary >= 0) {
		recipient = trim_target_span(payload.substr(recipient_boundary + 4));
		payload = trim_target_span(payload.left(recipient_boundary));
	} else {
		payload = trim_target_span(payload);
	}
	roles["object"] = payload;
	roles["object_or_class"] = payload;
	roles["recipient_or_direction"] = recipient;
	roles["target_mode"] = payload.is_empty() ? "optional" : "required";
	roles["speech_act"] = speech_act;
	return roles;
}

Array NativeTurnAnalyzer::build_compositional_frames(
	int first_index,
	const String &raw,
	const Dictionary &registry,
	const Array &previous_frames,
	const String &normalized_input,
	int span_start,
	int span_end
) const {
	Array output;
	const String lower = raw.to_lower().strip_edges();
	const Dictionary actions = registry.get("actions", Dictionary());
	String predicate;
	int predicate_start = -1;
	for (const char *surface : {"get", "fetch", "bring", "retrieve"}) {
		const int found = find_last_word(lower, surface);
		if (found > predicate_start) {
			predicate = surface;
			predicate_start = found;
		}
	}
	if (predicate_start < 0) return output;

	const int predicate_end = end_after_word_tokens(lower, predicate_start, 1);
	const String predicate_prefix = lower.left(predicate_start).strip_edges();
	String predicate_payload = lower.substr(predicate_end).strip_edges().trim_suffix(".").trim_suffix("!").trim_suffix("?").strip_edges();
	if (predicate == "get" && starts_any(predicate_payload, string_array({"on ", "up ", "off ", "out ", "in ", "close ", "away ", "back ", "through ", "over ", "down "}))) return output;
	String recipient;
	// Recipient adjuncts can occur on either side of a source adjunct:
	// "bring the file from the desk to me" and
	// "bring the file to me from the desk" are structurally equivalent.
	extract_give_recipient(predicate_payload, recipient);
	String source_location;
	int location_boundary = -1;
	int location_marker_length = 0;
	for (const char *raw_marker : {" on the ", " from the ", " at the ", " in the "}) {
		const String marker = raw_marker;
		const int found = predicate_payload.find(marker);
		if (found >= 0 && (location_boundary < 0 || found < location_boundary)) {
			location_boundary = found;
			location_marker_length = marker.length();
		}
	}
	if (location_boundary >= 0) {
		source_location = trim_target_span(predicate_payload.substr(location_boundary + location_marker_length));
		predicate_payload = predicate_payload.left(location_boundary).strip_edges();
	}
	if (recipient.is_empty()) extract_give_recipient(predicate_payload, recipient);
	const bool explicit_recipient = !recipient.is_empty();
	// A canonical Brain capability remains directly reachable.  The
	// pickup->give expansion is a semantic interpretation of a recipient-
	// bearing construction, not a replacement for registered fetch/bring/
	// retrieve actions used on their own.
	if (predicate != "get" && !explicit_recipient && actions.has(predicate)) return output;
	// The polysemous verb "get" is admitted only by its recipient-bearing
	// valency frame. This excludes get on/up/close and ordinary possession.
	if (predicate == "get" && !explicit_recipient) return output;

	// Stessa forma grammaticale usata in `build_frame`, non un terzo elenco.
	const bool modal_request = has_inverted_modal_request(tokenize(predicate_prefix));
	// Lo stesso confine della scelta del verbo, punto compreso (tracker #6, #49):
	// prima qui c'era un terzo calcolo, senza il punto e senza la lineetta.
	const int boundary = last_structural_boundary(predicate_prefix);
	const String local_prefix = boundary >= 0 ? predicate_prefix.substr(boundary + 1).strip_edges() : predicate_prefix;
	const bool second_person_request = local_prefix == "you" || local_prefix.begins_with("you please") || local_prefix.ends_with(" you");
	const bool imperative_request = local_prefix.is_empty() || local_prefix == "please";
	const bool trailing_politeness = lower.trim_suffix(".").trim_suffix("!").trim_suffix("?").strip_edges().ends_with(" please");
	const bool discourse_preface = predicate_prefix.begins_with("wait before ") || predicate_prefix.begins_with("wait,") || predicate_prefix.begins_with("hold on");
	const bool request_context = modal_request || imperative_request || discourse_preface || (second_person_request && (explicit_recipient || trailing_politeness));
	if (!request_context) return output;

	const String target = trim_target_span(predicate_payload);
	if (recipient.is_empty() && predicate != "get") recipient = "user";

	auto make_base_frame = [&](int index, const String &frame_raw, int frame_start, int frame_end) {
		Dictionary frame;
		frame["frame_id"] = String("frame_%03d") % static_cast<int64_t>(index);
		frame["raw_text"] = frame_raw;
		frame["source_spans"] = source_span_array(normalized_input, frame_start, frame_end);
		frame["candidate_targets"] = Array();
		frame["reference_targets"] = Array();
		frame["semantic_authority"] = "deterministic_parser";
		frame["speech_act"] = "request";
		frame["surface_kind"] = "request";
		return frame;
	};
	int action_index = first_index;
	int action_span_start = span_start;
	String action_raw = raw;
	Array contextual_previous = previous_frames.duplicate();
	const String preface = boundary >= 0 ? lower.left(boundary).strip_edges() : String();
	const bool preserve_preface = !preface.is_empty() && !preface.begins_with("wait") && !preface.begins_with("hold on");
	if (preserve_preface) {
		const int preface_end = span_start + boundary;
		const String preface_raw = normalized_input.substr(span_start, MAX(0, preface_end - span_start)).strip_edges();
		Dictionary conversation = make_base_frame(action_index, preface_raw, span_start, preface_end);
		conversation["frame_type"] = "conversation";
		conversation["speech_act"] = "statement";
		conversation["surface_kind"] = "statement";
		Dictionary conversation_roles = empty_roles();
		conversation_roles["query_kind"] = "conversation";
		conversation["semantic_roles"] = conversation_roles;
		conversation["parser_evidence"] = string_array({"semantic_authority:deterministic_parser", "construction:discourse_preface"});
		Array conversation_deps;
		if (!contextual_previous.is_empty()) conversation_deps.append(Dictionary(contextual_previous[contextual_previous.size() - 1])["frame_id"]);
		conversation["dependency_refs"] = conversation_deps;
		output.append(conversation);
		contextual_previous.append(conversation);
		action_index += 1;
		int relative_start = boundary + 1;
		while (relative_start < raw.length() && !is_word_character(raw.unicode_at(relative_start))) relative_start += 1;
		action_span_start = span_start + relative_start;
		action_raw = normalized_input.substr(action_span_start, MAX(0, span_end - action_span_start)).strip_edges();
	}

	if (target.is_empty() || !actions.has("pickup") || !actions.has("give")) {
		Dictionary clarification = make_base_frame(action_index, action_raw, action_span_start, span_end);
		clarification["frame_type"] = "clarification";
		Array candidates;
		if (actions.has("pickup")) candidates.append("pickup");
		if (actions.has("give")) candidates.append("give");
		clarification["candidate_actions"] = candidates;
		Dictionary roles = empty_roles();
		roles["object"] = target;
		roles["object_or_class"] = target;
		roles["recipient_or_direction"] = recipient;
		roles["source"] = source_location;
		roles["target_mode"] = "required";
		clarification["semantic_roles"] = roles;
		Array evidence = string_array({"semantic_authority:deterministic_parser", "construction:fetch_for"});
		evidence.append(target.is_empty() ? "missing_required_role:object" : "capability_missing:fetch_for");
		clarification["parser_evidence"] = evidence;
		Array deps;
		if (!contextual_previous.is_empty()) deps.append(Dictionary(contextual_previous[contextual_previous.size() - 1])["frame_id"]);
		clarification["dependency_refs"] = deps;
		output.append(clarification);
		return output;
	}

	Dictionary pickup_frame = make_base_frame(action_index, action_raw, action_span_start, span_end);
	pickup_frame["frame_type"] = "inventory_action";
	Dictionary pickup_roles = empty_roles();
	pickup_roles["action"] = "pickup";
	pickup_roles["object"] = target;
	pickup_roles["object_or_class"] = target;
	pickup_roles["source"] = source_location;
	pickup_roles["target_mode"] = Dictionary(actions["pickup"]).get("target_mode", "required");
	pickup_frame["semantic_roles"] = pickup_roles;
	Array pickup_targets; pickup_targets.append(target); pickup_frame["candidate_targets"] = pickup_targets;
	Array pickup_deps;
	if (!contextual_previous.is_empty()) pickup_deps.append(Dictionary(contextual_previous[contextual_previous.size() - 1])["frame_id"]);
	pickup_frame["dependency_refs"] = pickup_deps;
	Array pickup_evidence = string_array({"semantic_authority:deterministic_parser", "construction:fetch_for", "semantic_expansion:pickup", "roles:recipient_object"});
	if (!source_location.is_empty()) pickup_evidence.append("role:source_location");
	if (discourse_preface) pickup_evidence.append("discourse_marker:wait");
	pickup_frame["parser_evidence"] = pickup_evidence;
	output.append(pickup_frame);

	Dictionary give_frame = make_base_frame(action_index + 1, action_raw, action_span_start, span_end);
	give_frame["frame_type"] = "inventory_action";
	Dictionary give_roles = empty_roles();
	give_roles["action"] = "give";
	give_roles["object"] = target;
	give_roles["object_or_class"] = target;
	give_roles["recipient_or_direction"] = recipient;
	give_roles["target_mode"] = Dictionary(actions["give"]).get("target_mode", "required");
	give_frame["semantic_roles"] = give_roles;
	Array give_targets; give_targets.append(target); give_frame["candidate_targets"] = give_targets;
	Array give_deps; give_deps.append(pickup_frame["frame_id"]); give_frame["dependency_refs"] = give_deps;
	give_frame["parser_evidence"] = string_array({"semantic_authority:deterministic_parser", "construction:fetch_for", "semantic_expansion:give", "roles:recipient_object"});
	output.append(give_frame);
	return output;
}

Array NativeTurnAnalyzer::split_action_coordinations(const Array &clauses, const Dictionary &registry, const Dictionary &semantic_graph) const {
	Array output;
	const Dictionary aliases = registry.get("alias_index", Dictionary());
	Array predicate_keys = aliases.keys();
	const Array semantic_entries = semantic_graph.get("entries", Array());
	for (int entry_index = 0; entry_index < semantic_entries.size(); ++entry_index) {
		if (semantic_entries[entry_index].get_type() != Variant::DICTIONARY) continue;
		const String surface = String(Dictionary(semantic_entries[entry_index]).get("surface", "")).strip_edges().to_lower();
		if (!surface.is_empty() && !predicate_keys.has(surface)) predicate_keys.append(surface);
	}
	for (int clause_index = 0; clause_index < clauses.size() && output.size() <= MAX_FRAMES; ++clause_index) {
		String remaining = String(clauses[clause_index]).strip_edges();
		// A counted continuation may omit an explicit coordinator:
		// "get on it do 3 jumps". Split only when the left side already starts
		// with a registered action and the right side is the closed
		// "do <integer> <registered-action>" construction. This prevents a
		// narrative such as "I went there to do three jumps" from becoming a
		// request merely because it contains an action word.
		const int counted_boundary = remaining.to_lower().find(" do ");
		if (counted_boundary >= 0) {
			const String left = remaining.left(counted_boundary).strip_edges();
			const String right = remaining.substr(counted_boundary + 1).strip_edges();
			const Array right_tokens = tokenize(right);
			bool left_begins_action = false;
			const bool counted_shape = right_tokens.size() >= 3 && String(right_tokens[0]) == "do" && number_value(right_tokens[1]) > 0;
			bool matched_counted_action = false;
			for (int alias_index = 0; alias_index < predicate_keys.size(); ++alias_index) {
				const String alias = predicate_keys[alias_index];
				if (!left_begins_action) {
					const int action_start = find_word(left, alias);
					if (action_start >= 0 && tokenize(left.left(action_start)).size() <= 1) left_begins_action = true;
				}
				if (counted_shape && tokenize(alias).size() == 1 && lemma(String(right_tokens[2])) == alias) matched_counted_action = true;
			}
			if (left_begins_action && counted_shape && matched_counted_action) {
				if (!left.is_empty()) output.append(left);
				remaining = right;
			}
		}
		// Il verbo che apre un pezzo di frase (al piu' una parola davanti), la sua
		// posizione e dove finisce. Vuoto se il pezzo non comincia con un'azione.
		auto leading_action = [&](const String &text, int &alias_start, int &alias_end) -> String {
			String found;
			alias_start = -1;
			alias_end = -1;
			for (int alias_index = 0; alias_index < predicate_keys.size(); ++alias_index) {
				const String alias = predicate_keys[alias_index];
				const int start = find_word(text, alias);
				if (start < 0 || tokenize(text.left(start).strip_edges().trim_suffix(",")).size() > 1) continue;
				if (alias_start < 0 || start < alias_start || (start == alias_start && alias.length() > found.length())) {
					found = alias;
					alias_start = start;
				}
			}
			// "grabbing my keys and hanging them by the door": il verbo coniugato apre il pezzo
			// come quello base, e prima il secondo verbo spariva (tracker Analizzatore #80).
			// Solo nelle prime due parole, come la regola qui sopra, e solo per il gerundio.
			const String lower_text = text.to_lower();
			const Array head_tokens = tokenize(lower_text);
			for (int token_index = 0; found.is_empty() && token_index < MIN(2, head_tokens.size()); ++token_index) {
				const String token = head_tokens[token_index];
				const String base = lemma(token);
				const int token_start = find_word(lower_text, token);
				// Solo il gerundio: "you exited the lift" dopo una virgola resta un racconto intero.
				if (base == token || token_start < 0 || !token.ends_with("ing")) continue;
				// "closing" si riduce a "clos": come nel ramo del gerundio di `build_frame`, si
				// prova anche con la "e" finale.
				for (const String &stem : {base, base + String("e")}) {
					const String base_text = lower_text.left(token_start) + stem + lower_text.substr(token_start + token.length());
					for (int alias_index = 0; alias_index < predicate_keys.size(); ++alias_index) {
						const String alias = predicate_keys[alias_index];
						if (alias.length() > found.length() && find_word(base_text, alias) == token_start) found = alias;
					}
				}
				if (!found.is_empty()) alias_start = token_start;
			}
			if (!found.is_empty()) alias_end = end_after_word_tokens(text.to_lower(), alias_start, tokenize(found).size());
			return found;
		};
		// Separatori fra due richieste: " and " e la virgola nuda (tracker #32, #40).
		// La virgola separa solo quando dopo comincia un verbo: "the red, round ball"
		// resta un bersaglio solo, "stand up, wave, and smile" diventa tre azioni.
		int search_from = 0;
		while (output.size() <= MAX_FRAMES) {
			const String lower_remaining = remaining.to_lower();
			int boundary = -1;
			int separator_length = 0;
			for (const char *separator : {" and ", ", "}) {
				const int found = lower_remaining.find(separator, search_from);
				if (found >= 0 && (boundary < 0 || found < boundary)) {
					boundary = found;
					separator_length = String(separator).length();
				}
			}
			if (boundary < 0) break;
			const bool is_and = separator_length == 5;
			String right = remaining.substr(boundary + separator_length).strip_edges();
			if (right.to_lower().begins_with("and ")) right = right.substr(4).strip_edges();
			String left = remaining.left(boundary).strip_edges().trim_suffix(",");
			int right_start = -1, right_end = -1, left_start = -1, left_end = -1;
			const String right_alias = leading_action(right, right_start, right_end);
			const String left_alias = leading_action(left, left_start, left_end);
			if (right_alias.is_empty()) {
				// Un verbo, piu' oggetti: "open the door and the window" apre anche la
				// finestra (tracker #38). Solo quando la destra e' un gruppo nominale
				// corto che comincia con un determinante e non contiene verbi: "open
				// the door and the dog runs out" non diventa "open the dog".
				const Array right_tokens = tokenize(right);
				// "wait, I just checked my pocket and my phone's not there": "wait" seguito dalla
				// virgola e' un intercalare, e "phone's not there" ha un verbo dentro (#78).
				bool noun_phrase = is_and && !left_alias.is_empty() && !left.substr(left_end).strip_edges().begins_with(",") && !right.contains("'") && !right_tokens.has("not") && right_tokens.size() >= 1 && right_tokens.size() <= 4 &&
						string_array({"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that", "these", "those", "it", "them"}).has(right_tokens[0]);
				for (int token_index = 1; noun_phrase && token_index < right_tokens.size(); ++token_index) {
					const String token = right_tokens[token_index];
					if (predicate_keys.has(token) || predicate_keys.has(lemma(token))) noun_phrase = false;
				}
				if (!noun_phrase) {
					search_from = boundary + separator_length;
					continue;
				}
				right = left.left(left_end).strip_edges() + String(" ") + right;
			} else if (is_and && !left_alias.is_empty() && left.substr(left_end).strip_edges().is_empty() && !right.substr(right_end).strip_edges().is_empty()) {
				// Piu' verbi, un oggetto: "open and close the door" apre la porta invece
				// di chiedere che cosa aprire (tracker #38). Solo con "and": in "wait, open
				// the door" la virgola separa un richiamo, e "wait" non prende la porta (#78).
				left = left + String(" ") + right.substr(right_end).strip_edges();
			}
			// Un pezzo fatto solo di connettivi ("next", "first") non e' una frase: in
			// "; next, close the shutter" la virgola non deve produrre un frame "next".
			if (!left.is_empty() && !without_discourse_markers(left).is_empty()) output.append(left);
			remaining = right;
			search_from = 0;
		}
		if (!remaining.is_empty() && output.size() <= MAX_FRAMES) output.append(remaining);
	}
	return output;
}

Dictionary NativeTurnAnalyzer::analyze_turn(const String &raw_input, const Dictionary &, const Dictionary &) const {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	Dictionary result;
	const String normalized = normalize(raw_input);
	result["analysis_version"] = "4";
	result["supported_language"] = "en";
	result["raw_input"] = raw_input;
	result["normalized_input"] = normalized;
	if (normalized.is_empty()) {
		result["language_status"] = "empty"; result["request_count"] = 0; result["segments"] = Array();
		result["global_flags"] = string_array({"empty_input"}); result["structural_confidence"] = 1.0; result["segmentation_status"] = "reliable";
		return result;
	}
	if (normalized.length() > MAX_INPUT_CHARS) {
		result["language_status"] = "english"; result["request_count"] = 1; result["segments"] = Array();
		result["global_flags"] = string_array({"limit_exceeded", "full_input_fallback"}); result["structural_confidence"] = 0.0; result["segmentation_status"] = "fallback";
		result["diagnostic_code"] = "NTA_LIMIT_EXCEEDED";
		return result;
	}
	Array clauses = split_clauses(normalized);
	const Array all_tokens = tokenize(normalized);
	if (all_tokens.size() > MAX_TOKENS || clauses.size() > MAX_FRAMES) {
		result["language_status"] = "english"; result["request_count"] = 1; result["segments"] = Array();
		result["global_flags"] = string_array({"limit_exceeded", "full_input_fallback"}); result["structural_confidence"] = 0.0; result["segmentation_status"] = "fallback";
		result["diagnostic_code"] = all_tokens.size() > MAX_TOKENS ? "NTA_TOKEN_LIMIT_EXCEEDED" : "NTA_CLAUSE_LIMIT_EXCEEDED";
		return result;
	}
	Array clause_items;
	Array linguistic;
	int clause_search_from = 0;
	for (int i = 0; i < clauses.size(); ++i) {
		String text = clauses[i];
		int start = normalized.find(text, clause_search_from);
		if (start < 0) start = 0;
		clause_search_from = start + text.length();
		const String clause_id = String("clause_%03d") % static_cast<int64_t>(i + 1);
		Array dependency_refs;
		Dictionary clause; clause["clause_id"] = clause_id; clause["clause_role"] = i == 0 ? "main" : "coordinate"; clause["text"] = text; clause["start_index"] = start; clause["end_index"] = start + text.length();
		Array tokens = tokenize(text); Array lemmas; Array references; String modal;
		for (int j = 0; j < tokens.size(); ++j) {
			String token = tokens[j]; lemmas.append(lemma(token));
			if (string_array({"it", "this", "that", "there", "them", "him", "her"}).has(token) && !references.has(token)) references.append(token);
			if (modal.is_empty() && string_array({"can", "could", "may", "might", "must", "shall", "should", "will", "would", "ought"}).has(token)) modal = token;
		}
		// Ogni clausola dopo la prima dipende dalla precedente: il ramo sui riferimenti era
		// morto, perche' `clause_role` dopo la prima e' sempre "coordinate" (tracker #55).
		if (i > 0) dependency_refs.append(String("clause_%03d") % static_cast<int64_t>(i));
		const String speech_act = infer_clause_speech_act(text, tokens, lemmas);
		const Dictionary semantic_roles = extract_clause_roles(text, tokens, lemmas, speech_act);
		Array predicate_units;
		const PackedStringArray coordinated_parts = text.split(" and ", false);
		int unit_search_from = 0;
		for (int unit_index = 0; unit_index < coordinated_parts.size(); ++unit_index) {
			const String unit_text = String(coordinated_parts[unit_index]).strip_edges();
			if (unit_text.is_empty()) continue;
			int unit_relative_start = text.to_lower().find(unit_text.to_lower(), unit_search_from);
			if (unit_relative_start < 0) unit_relative_start = unit_search_from;
			unit_search_from = unit_relative_start + unit_text.length() + 5;
			const Array unit_tokens = tokenize(unit_text);
			Array unit_lemmas;
			for (int unit_token_index = 0; unit_token_index < unit_tokens.size(); ++unit_token_index) unit_lemmas.append(lemma(unit_tokens[unit_token_index]));
			const String unit_speech_act = unit_index > 0 && speech_act == "request" ? String("request") : infer_clause_speech_act(unit_text, unit_tokens, unit_lemmas);
			Array unit_dependencies = unit_index == 0 ? dependency_refs.duplicate() : Array::make(String("%s_predicate_%03d") % Array::make(clause_id, unit_index));
			Dictionary unit; unit["predicate_id"] = String("%s_predicate_%03d") % Array::make(clause_id, unit_index + 1); unit["text"] = unit_text; unit["start_index"] = start + unit_relative_start; unit["end_index"] = start + unit_relative_start + unit_text.length(); unit["speech_act"] = unit_speech_act; unit["semantic_roles"] = extract_clause_roles(unit_text, unit_tokens, unit_lemmas, unit_speech_act); unit["dependency_refs"] = unit_dependencies; predicate_units.append(unit);
		}
		clause["speech_act"] = speech_act; clause["semantic_roles"] = semantic_roles; clause["dependency_refs"] = dependency_refs; clause["predicate_units"] = predicate_units; clause_items.append(clause);
		Dictionary lc; lc["clause_id"] = clause_id; lc["segment_id"] = "segment_1"; lc["text"] = text; lc["start_index"] = start; lc["end_index"] = start + text.length(); lc["tokens"] = tokens; lc["lemmas"] = lemmas; lc["speech_act"] = speech_act; lc["semantic_roles"] = semantic_roles; lc["dependency_refs"] = dependency_refs; lc["predicate_units"] = predicate_units; lc["question"] = speech_act == "question"; lc["negated"] = tokens.has("not") || tokens.has("never") || text.to_lower().begins_with("don't "); lc["conditional"] = tokens.has("if") || tokens.has("would"); lc["modal"] = modal; lc["references"] = references; linguistic.append(lc);
	}
	String segment_surface = "statement";
	for (int i = 0; i < linguistic.size(); ++i) {
		const String clause_speech_act = Dictionary(linguistic[i]).get("speech_act", "statement");
		if (clause_speech_act == "request") { segment_surface = "request"; break; }
		if (clause_speech_act == "question") segment_surface = "question";
	}
	Dictionary segment; segment["segment_id"] = "segment_1"; segment["text"] = normalized; segment["start_index"] = 0; segment["end_index"] = normalized.length(); segment["surface_kind"] = segment_surface; segment["clauses"] = clause_items; segment["structural_flags"] = clauses.size() > 1 ? string_array({"multiple_clauses"}) : Array();
	// Le richieste vere, non piu' sempre 1 (tracker Analizzatore #55). Gli span dei segmenti
	// (reference/candidate/constraint, coordination_groups, evidence) e debug_lines non si
	// scrivono piu': erano sempre vuoti, i bersagli arrivano dai frame compilati.
	int64_t request_clauses = 0;
	for (int i = 0; i < linguistic.size(); ++i) if (String(Dictionary(linguistic[i]).get("speech_act", "")) == "request") ++request_clauses;
	Array segments; segments.append(segment); result["segments"] = segments; result["request_count"] = request_clauses; result["language_status"] = "english"; result["global_flags"] = clauses.size() > 1 ? string_array({"multiple_clauses", "reliable_parse"}) : string_array({"reliable_parse"}); result["structural_confidence"] = 0.88; result["segmentation_status"] = "reliable"; result["linguistic_clauses"] = linguistic; result["elapsed_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - started);
	return result;
}

Dictionary NativeTurnAnalyzer::build_frame(int index, const String &raw, const Dictionary &registry, const Array &previous_frames) const {
	const String lower = raw.to_lower().strip_edges();
	Dictionary frame; frame["frame_id"] = String("frame_%03d") % static_cast<int64_t>(index); frame["raw_text"] = raw; frame["source_spans"] = source_span_array(raw, 0, raw.length()); frame["candidate_targets"] = Array(); frame["reference_targets"] = Array(); frame["parser_evidence"] = string_array({"semantic_authority:deterministic_parser", "native_turn_analyzer:validated"}); frame["semantic_authority"] = "deterministic_parser";
	Array deps; if (!previous_frames.is_empty()) deps.append(Dictionary(previous_frames[previous_frames.size() - 1])["frame_id"]); frame["dependency_refs"] = deps;
	Dictionary roles = empty_roles();
	const bool indirect_query = lower.contains("need to know whether ") || lower.contains("tell me whether ") || lower.contains("want to know whether ") || lower.contains("wonder whether ");
	const bool wh_prefix = starts_any(lower, string_array({"what ", "where ", "when ", "why ", "who ", "how "}));
	const bool auxiliary_question = starts_any(lower, string_array({"do ", "does ", "did ", "are ", "is ", "have ", "has "}));
	// A leading subordinate "when ...," is not an interrogative. Treating it
	// as one turns commands such as "When ready, close ..." into state queries.
	// "come sit here with me?" resta un ordine: il punto interrogativo dopo un
	// imperativo non fa una domanda (tracker #28). Imperativo = la frase comincia con
	// un verbo del registro, e non con un ausiliare, un modale o una parola wh.
	bool imperative_shape = false;
	{
		const Array head_tokens = tokenize(without_discourse_markers(lower));
		if (!head_tokens.is_empty() && !wh_prefix && !auxiliary_question && !is_modal_word(head_tokens[0])) {
			const String head_word = head_tokens[0];
			const Array registry_aliases = Dictionary(registry.get("alias_index", Dictionary())).keys();
			const Dictionary registry_alias_index = registry.get("alias_index", Dictionary());
			const Dictionary registry_actions = registry.get("actions", Dictionary());
			for (int alias_index = 0; alias_index < registry_aliases.size() && !imperative_shape; ++alias_index) {
				const String alias = registry_aliases[alias_index];
				if (alias != head_word && !alias.begins_with(head_word + String(" "))) continue;
				// "remember the scarf?" resta una domanda: con i verbi del pensiero e della
				// percezione il punto interrogativo verifica, non chiede. La famiglia viene
				// dal grafo semantico, non da un elenco di verbi.
				const Array alias_actions = registry_alias_index.get(alias, Array());
				const String first_action = alias_actions.is_empty() ? String() : String(alias_actions[0]);
				const Dictionary metadata = Dictionary(registry_actions.get(first_action, Dictionary())).get("semantic_metadata", Dictionary());
				const String family = metadata.get("semantic_family", "");
				if (family != "perceptioncognition" && family != "metaactions" && family != "perception" && family != "information") imperative_shape = true;
			}
		}
	}
	bool question = (lower.ends_with("?") && !imperative_shape) || indirect_query || auxiliary_question || (wh_prefix && !lower.contains(","));
	const bool indirect_request = lower.contains("i need you to ") || lower.contains("i would like you to ") || lower.contains("i'd like you to ") || lower.contains("i want you to ") || lower.contains("i am asking you to ") || lower.contains("go ahead and ") || lower.contains("new instruction") || lower.contains("fresh request") || lower.contains("replacing it with this");
	const bool prohibited = starts_any(lower, string_array({"do not ", "don't ", "never ", "stop ", "avoid "}));
	const bool reported = lower.contains(" told you to ") || lower.contains("the word ") || lower.contains("what does ") || lower.contains("said \"") || lower.contains("said '");
	if (prohibited || reported) { frame["frame_type"] = "conversation"; frame["speech_act"] = question ? "question" : "statement"; frame["surface_kind"] = frame["speech_act"]; frame["semantic_roles"] = roles; return frame; }
	if (lower.contains("on your head") || lower.contains("what are you wearing") || lower.contains("your equipment")) { frame["frame_type"] = "inventory_query"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "equipment"; roles["object"] = lower.contains("cap") ? "cap" : "equipment"; roles["object_or_class"] = roles["object"]; frame["semantic_roles"] = roles; return frame; }
	if (question) {
		String possession_target;
		for (const char *marker : {"do you have ", "have you got ", "you have "}) {
			const int marker_start = lower.find(marker);
			if (marker_start < 0) continue;
			if (String(marker) == "you have " && lower.find(" with you") < 0) continue;
			possession_target = lower.substr(marker_start + String(marker).length()).strip_edges().trim_suffix("?").trim_suffix(".");
			const int with_you = possession_target.find(" with you");
			if (with_you >= 0) possession_target = possession_target.left(with_you).strip_edges();
			break;
		}
		if (!possession_target.is_empty()) {
			bool stripped = true;
			while (stripped) {
				stripped = false;
				for (const char *prefix : {"the ", "a ", "an ", "your "}) {
					if (possession_target.begins_with(prefix)) { possession_target = possession_target.trim_prefix(prefix).strip_edges(); stripped = true; }
				}
			}
			frame["frame_type"] = "inventory_query"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "has_item"; roles["object"] = possession_target; roles["object_or_class"] = possession_target; frame["semantic_roles"] = roles; if (!possession_target.is_empty()) { Array targets; targets.append(possession_target); frame["candidate_targets"] = targets; } return frame;
		}
	}
	if (lower.contains("how angry") || lower.contains("are you angry") || lower.contains("how are you feeling") || lower.contains("how do you feel")) { frame["frame_type"] = "emotion_signal"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "emotion_query"; frame["semantic_roles"] = roles; return frame; }
	if (lower.contains("what do you like") || lower.contains("things do you like") || lower.contains("your preferences")) { frame["frame_type"] = "emotion_signal"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "preference_query"; frame["semantic_roles"] = roles; return frame; }
	if (lower.contains("watch out") || lower.contains("arrow is coming") || lower.contains("attack you")) { frame["frame_type"] = "threat_signal"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["object"] = lower.contains("arrow") ? "arrow" : lower.contains("attack") ? "attack" : "danger"; roles["hazard"] = roles["object"]; roles["event_kind"] = "incoming_threat"; roles["imminence"] = "imminent"; roles["severity"] = "high"; frame["semantic_roles"] = roles; return frame; }
	// Quale verbo, fra quelli che compaiono nella frase, e' quello **comandato**.
	//
	// Fino al 2026-09-04 il criterio era `alias.length()`: vinceva la parola piu' lunga,
	// ovunque si trovasse. Misurato sulla prova del 2026-09-03: "make me a coffee, you
	// know i drink it all day" sceglieva `drink`, e a parita' di lunghezza decideva
	// l'ordine delle chiavi di un `Dictionary` — `tell`/`wait` e `stand up`/`sit down`
	// sono stati risolti cosi', cioe' da nessuno.
	//
	// Il criterio giusto era gia' in questo file, venti righe piu' sotto: il cancello
	// calcola `direct_imperative` — prefisso locale vuoto, o solo "please" — ma **dopo**
	// aver scelto il verbo. Qui lo si calcola per ciascun candidato e lo si usa per
	// scegliere. Quattro livelli, in ordine:
	//   1. il bonus di cortesia/modale, che resta il primo criterio: senza, "wait a
	//      second, can you open the door" tornerebbe a scegliere `wait`;
	//   2. il verbo che e' un imperativo diretto nel proprio punto della frase;
	//   3. la posizione, il piu' a sinistra;
	//   4. la lunghezza, il piu' specifico — due alias condividono l'indice iniziale solo
	//      se uno e' prefisso dell'altro (`sit` dentro `sit down`), e li' vince il lungo.
	// La chiave e' totale: non restano pareggi, e l'ordine delle chiavi non decide piu'.
	Dictionary aliases = registry.get("alias_index", Dictionary()); String best; int best_start = -1; int best_score = -1; bool best_imperative = false; Array candidates;
	int best_particle_at = -1; int best_particle_length = 0; bool best_gerund = false; bool best_narration = false;
	Array alias_keys = aliases.keys();
	const Array clause_tokens_for_gerund = tokenize(lower);
	// L'utente che racconta se stesso: "I'm sitting on the floor", "I just picked it up to check",
	// "I'm holding a small wrapped gift" (tracker Analizzatore #83). Il verbo del racconto e' quello
	// subito dopo il soggetto, non un sinonimo qualunque piu' avanti ("check", "gift"). Soggetto,
	// ausiliari e avverbi di tempo sono classi chiuse.
	int narration_token = -1;
	int narration_at = -1;
	if (!question && clause_tokens_for_gerund.size() >= 2) {
		// Il soggetto apre la frase, o segue un breve preambolo chiuso da due punti o virgola:
		// "last thing: I'm setting my glasses down", "oh, and I left the box".
		int subject_token = 0;
		for (int s = 0; s < clause_tokens_for_gerund.size() && s <= 3; ++s) {
			const String candidate = clause_tokens_for_gerund[s];
			const int candidate_apostrophe = candidate.find("'");
			const String bare = candidate_apostrophe > 0 ? candidate.left(candidate_apostrophe) : candidate;
			if (bare != "i" && bare != "we") continue;
			const int word_at = find_word(lower, candidate);
			if (s == 0 || (word_at > 0 && (lower.left(word_at).contains(":") || lower.left(word_at).contains(",")))) subject_token = s;
			else subject_token = -1;
			break;
		}
		const String first = subject_token >= 0 ? String(clause_tokens_for_gerund[subject_token]) : String();
		const int apostrophe = first.find("'");
		const String subject = apostrophe > 0 ? first.left(apostrophe) : first;
		if (subject == "i" || subject == "we") {
			int k = subject_token + 1;
			while (k < clause_tokens_for_gerund.size() && k <= subject_token + 3 && string_array({"am", "are", "was", "were", "have", "had", "just", "now", "also", "really", "still", "already", "then", "again"}).has(clause_tokens_for_gerund[k])) ++k;
			if (k < clause_tokens_for_gerund.size()) {
				int search_from = 0;
				for (int i = 0; i <= k; ++i) {
					const String token = clause_tokens_for_gerund[i];
					narration_at = lower.find(token, search_from);
					if (narration_at < 0) break;
					search_from = narration_at + token.length();
				}
				if (narration_at >= 0) narration_token = k;
			}
		}
	}
	for (int i = 0; i < alias_keys.size(); ++i) {
		String alias = alias_keys[i];
		int alias_start = find_word(lower, alias);
		// Verbo a particella staccata: "pick the phone up", "take your jacket off"
		// (tracker #31). La particella e' una classe chiusa di avverbi, e vale solo
		// in fondo alla frase: in "put your jacket on the bed" `on` e' una
		// preposizione, non la particella di "put on".
		int particle_at = -1;
		int particle_length = 0;
		if (alias_start < 0) {
			const PackedStringArray alias_words = alias.split(" ", false);
			const String particle = alias_words.size() >= 2 ? String(alias_words[alias_words.size() - 1]) : String();
			if (!particle.is_empty() && string_array({"up", "down", "off", "on", "out", "in", "away", "back", "over"}).has(particle)) {
				const String head = alias.left(alias.length() - particle.length()).strip_edges();
				const int head_start = find_word(lower, head);
				if (head_start >= 0) {
					// `find_word` conta le posizioni sul testo senza spazi in testa: si toglie
					// lo spazio qui, cosi' le posizioni tornano.
					int head_end = end_after_word_tokens(lower, head_start, tokenize(head).size());
					while (head_end < lower.length() && lower.unicode_at(head_end) == U' ') head_end += 1;
					const String after_head = lower.substr(head_end);
					const int gap = find_word(after_head, particle);
					if (gap >= 0) {
						const String between = after_head.left(gap);
						const Array between_tokens = tokenize(between);
						const Array tail_tokens = tokenize(after_head.substr(gap + particle.length()));
						bool tail_is_closing = true;
						for (int tail_index = 0; tail_index < tail_tokens.size(); ++tail_index) {
							if (!string_array({"please", "now", "again", "for", "me", "us"}).has(tail_tokens[tail_index])) tail_is_closing = false;
						}
						if (between_tokens.size() >= 1 && between_tokens.size() <= 4 && !between.contains(",") && !between_tokens.has("and") && tail_is_closing) {
							alias_start = head_start;
							particle_at = head_end + gap;
							particle_length = particle.length();
						}
					}
				}
			}
		}
		// Gerundio che chiede: "would you mind opening the door", "how about opening
		// it", o in testa alla frase, "opening the door" (tracker #27, #33, #36). Non
		// dopo "are you" o "instead of": li' il gerundio racconta o esclude.
		bool gerund = false;
		if (alias_start < 0) {
			const PackedStringArray alias_words = alias.split(" ", false);
			const String head = alias_words.is_empty() ? String() : String(alias_words[0]);
			const String rest = alias.substr(head.length());
			for (int token_index = 0; token_index < clause_tokens_for_gerund.size() && alias_start < 0 && !head.is_empty(); ++token_index) {
				const String token = clause_tokens_for_gerund[token_index];
				if (!token.ends_with("ing") || token == head) continue;
				const String reduced = lemma(token);
				if (reduced != head && reduced + String("e") != head) continue;
				const int found = find_word(lower, token + rest);
				if (found < 0) continue;
				const String gerund_prefix = without_discourse_markers(local_prefix_before(lower.left(found)));
				if (gerund_prefix.is_empty() || gerund_prefix.ends_with("mind") || gerund_prefix == "how about" || gerund_prefix == "what about") {
					alias_start = found;
					gerund = true;
				}
			}
		}
		bool narration = false;
		if (narration_token >= 0) {
			const PackedStringArray alias_words = alias.split(" ", false);
			const String head = alias_words.is_empty() ? String() : String(alias_words[0]);
			const String verb = clause_tokens_for_gerund[narration_token];
			const String reduced = lemma(verb);
			// Pensare, guardare, dire non raccontano un gesto: la famiglia viene dal grafo.
			const Array narration_actions = aliases.get(alias, Array());
			const Dictionary narration_metadata = Dictionary(Dictionary(registry.get("actions", Dictionary())).get(narration_actions.is_empty() ? String() : String(narration_actions[0]), Dictionary())).get("semantic_metadata", Dictionary());
			const String narration_family = narration_metadata.get("semantic_family", "");
			const bool physical = narration_family != "perceptioncognition" && narration_family != "perception" && narration_family != "information" && narration_family != "metaactions";
			if (physical && !head.is_empty() && (verb == head || reduced == head || reduced + String("e") == head)) {
				bool matched = true;
				int particle_token = -1;
				for (int w = 1; w < alias_words.size() && matched; ++w) {
					if (narration_token + w >= clause_tokens_for_gerund.size() || String(clause_tokens_for_gerund[narration_token + w]) != String(alias_words[w])) matched = false;
				}
				// "picked it up": la particella staccata, entro quattro parole. Seguita da un
				// determinante e' una preposizione: "putting my phone on the table" non e' "put on".
				if (!matched && alias_words.size() == 2 && string_array({"up", "down", "off", "on", "out", "in", "away", "back", "over"}).has(alias_words[1])) {
					for (int j = narration_token + 2; j < clause_tokens_for_gerund.size() && j <= narration_token + 4; ++j) {
						if (String(clause_tokens_for_gerund[j]) != String(alias_words[1])) continue;
						const bool preposition = j + 1 < clause_tokens_for_gerund.size() && string_array({"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that"}).has(clause_tokens_for_gerund[j + 1]);
						if (!preposition) particle_token = j;
						break;
					}
					matched = particle_token >= 0;
				}
				if (matched) {
					narration = true;
					alias_start = narration_at;
					particle_at = -1;
					particle_length = 0;
					gerund = false;
					if (particle_token >= 0) {
						int search_from = narration_at;
						for (int i = narration_token; i <= particle_token; ++i) {
							const String token = clause_tokens_for_gerund[i];
							particle_at = lower.find(token, search_from);
							search_from = particle_at + token.length();
						}
						particle_length = String(alias_words[1]).length();
					}
				}
			}
		}
		if (alias_start < 0) continue;
		// "a small wrapped gift": una parola dopo un determinante e' un nome, non un verbo (#83).
		if (!narration && !gerund && particle_at < 0) {
			const Array before_alias = tokenize(lower.left(alias_start));
			if (!before_alias.is_empty() && string_array({"the", "a", "an", "my", "your", "his", "their", "our", "these", "those", "some"}).has(before_alias[before_alias.size() - 1])) continue;
		}
		const String alias_prefix = lower.left(alias_start);
		int alias_score = 0;
		// Il bonus vale per **entrambi** gli ordini: la forma invertita per
		// grammatica, quella soggetto-modale ancora per elenco. Qui serve solo a
		// dire quale verbo e' quello comandato, non se la frase e' una richiesta.
		bool modal_context = has_inverted_modal_request(tokenize(alias_prefix));
		if (!modal_context) {
			for (const char *construction : {"you can", "you could", "you would", "you will", "you may"}) {
				if (find_word(alias_prefix, construction) >= 0) {
					modal_context = true;
					break;
				}
			}
		}
		if (modal_context) alias_score += 10000;
		if (find_word(alias_prefix, "please") >= 0) alias_score += 1000;
		const String alias_local_prefix = without_discourse_markers(local_prefix_before(alias_prefix));
		// "go open the door": il verbo di moto seguito da un altro verbo lo introduce,
		// non e' lui l'azione (tracker #41).
		const bool serial_motion = alias_local_prefix == "go" || alias_local_prefix == "come" || alias_local_prefix == "please go" || alias_local_prefix == "please come";
		if (serial_motion) alias_score += 500;
		const bool alias_imperative = alias_local_prefix.is_empty() || alias_local_prefix == "please" || serial_motion || gerund || narration;
		bool takes_it = best_start < 0 || alias_score > best_score;
		if (!takes_it && alias_score == best_score) {
			// Un ordine dopo un confine di frase batte il racconto che lo precede: "i put the
			// notebook on the desk.open the door" chiede di aprire (tracker Analizzatore #83).
			const bool order_after_narration = best_narration && !narration && alias_start > best_start && last_structural_boundary(lower.left(alias_start)) > best_start;
			const bool narration_before_order = narration && !best_narration && best_start > alias_start && last_structural_boundary(lower.left(best_start)) > alias_start;
			if (alias_imperative != best_imperative) takes_it = alias_imperative;
			else if (order_after_narration || narration_before_order) takes_it = order_after_narration;
			else if (alias_start != best_start) takes_it = alias_start < best_start;
			else takes_it = alias.length() > best.length();
		}
		if (takes_it) {
			best = alias;
			best_start = alias_start;
			best_score = alias_score;
			best_imperative = alias_imperative;
			best_particle_at = particle_at;
			best_gerund = gerund;
			best_narration = narration;
			best_particle_length = particle_length;
			candidates = aliases[alias];
		}
	}
	// Inflected action mentions are allowed to identify the subject of a
	// question, but never become executable through this fallback.
	if (best.is_empty() && question) {
		for (int i = 0; i < alias_keys.size(); ++i) {
			String alias = alias_keys[i];
			const int lemma_start = find_lemma_phrase_start(lower, alias);
			if (lemma_start >= 0 && alias.length() > best.length()) {
				best = alias;
				best_start = lemma_start;
				candidates = aliases[alias];
			}
		}
	}
	if (best.is_empty()) { frame["frame_type"] = "conversation"; frame["speech_act"] = question ? "question" : "statement"; frame["surface_kind"] = frame["speech_act"]; roles["query_kind"] = "conversation"; frame["semantic_roles"] = roles; return frame; }
	Dictionary actions = registry.get("actions", Dictionary());
	const String canonical_surface = best.replace(" ", "_").replace("-", "_");
	if (actions.has(canonical_surface) && candidates.has(canonical_surface)) {
		candidates.clear();
		candidates.append(canonical_surface);
	}
	if (candidates.size() != 1) { frame["frame_type"] = "clarification"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; frame["candidate_actions"] = candidates; frame["semantic_roles"] = roles; return frame; }
	String action = candidates[0]; Dictionary contract = actions.get(action, Dictionary());
	const String target_mode = contract.get("target_mode", "optional");
	const int matched_action_end = best_start >= 0 ? end_after_word_tokens(lower, best_start, tokenize(best).size() - (best_particle_at >= 0 ? 1 : 0)) : -1;
	const String action_prefix = best_start >= 0 ? lower.left(best_start).strip_edges() : lower;
	const String action_suffix_for_context = matched_action_end >= 0 ? lower.substr(matched_action_end).strip_edges() : String();
	const bool quoted_or_roleplay = position_is_inside_delimiter(lower, best_start, "\"") || position_is_inside_delimiter(lower, best_start, "`") || position_is_inside_delimiter(lower, best_start, "*");
	const bool code_like = action_suffix_for_context.begins_with("(") || lower.begins_with("{") || lower.begins_with("[") || lower.contains("\"action\"");
	// "wait, I just checked my pocket": il verbo seguito da virgola e da un soggetto e' un
	// intercalare, e la frase che segue racconta (tracker Analizzatore #78). "wait, open the
	// door" resta un ordine: dopo la virgola viene un verbo, non un soggetto.
	if (action_suffix_for_context.begins_with(",")) {
		const Array after_comma = tokenize(action_suffix_for_context);
		if (!after_comma.is_empty()) {
			const String subject = after_comma[0];
			const int apostrophe = subject.find("'");
			// Anche un ausiliare, un modale o una parola wh: "actually, wait, do you have a charger?" chiede,
			// non ordina di aspettare (corpus esterno del 2026-09-15).
			if (string_array({"i", "you", "he", "she", "we", "they", "do", "does", "did", "can", "could", "would", "will", "is", "are", "was", "there", "what", "where", "who", "how", "why"}).has(apostrophe > 0 ? subject.left(apostrophe) : subject)) {
				frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["query_kind"] = "action_mention"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("non_request:interjection_before_subject"); frame["parser_evidence"] = evidence; return frame;
			}
		}
	}
	if (quoted_or_roleplay || code_like) { frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = quoted_or_roleplay ? "quotation" : "code"; roles["query_kind"] = "action_mention"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append(quoted_or_roleplay ? "non_request:delimited_span" : "non_request:code_like"); frame["parser_evidence"] = evidence; return frame; }
	if (indirect_query) { frame["frame_type"] = "state_query"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "action_history"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("speech_act:indirect_query"); frame["parser_evidence"] = evidence; return frame; }
	// "why don't you open the door" chiede, non vieta: il "don't" appartiene alla
	// cortesia, non al verbo (tracker #34).
	const bool why_not_request = find_word(action_prefix, "why don't you") >= 0 || find_word(action_prefix, "why not") >= 0;
	if (why_not_request || best_gerund) question = false;
	bool locally_negated = false;
	const Array prefix_tokens = tokenize(action_prefix);
	const int negation_window_start = prefix_tokens.size() > 4 ? prefix_tokens.size() - 4 : 0;
	for (int i = negation_window_start; i < prefix_tokens.size(); ++i) {
		if (!why_not_request && string_array({"not", "never", "dont", "don't", "avoid", "stop"}).has(prefix_tokens[i])) {
			locally_negated = true;
			break;
		}
	}
	const String action_suffix = matched_action_end >= 0 ? lower.substr(matched_action_end) : String();
	const bool subsequently_cancelled = action_suffix.contains("don't do") || action_suffix.contains("dont do") || action_suffix.contains("do not do") || action_suffix.contains("not now") || action_suffix.contains("cancel that") || action_suffix.contains("cancel this") || action_suffix.contains("actually, cancel") || action_suffix.contains("instruction is cancelled") || action_suffix.contains("instruction remains cancelled");
	if (locally_negated || subsequently_cancelled) { frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["query_kind"] = "prohibition"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append(subsequently_cancelled ? "negated_action:corrective_scope" : "negated_action:local_scope"); frame["parser_evidence"] = evidence; return frame; }
	// "Leave" is polysemous.  A resultative/state complement such as
	// "leave the door closed" requests preservation of a state; it is not the
	// inventory operation "drop the door closed".  The current Brain has no
	// maintain-state action, so preserve the request as response-only evidence
	// instead of manufacturing an executable inventory target.
	const Array leave_suffix_tokens = tokenize(action_suffix_for_context);
	const String leave_state = leave_suffix_tokens.is_empty() ? String() : String(leave_suffix_tokens[leave_suffix_tokens.size() - 1]);
	const bool state_preserving_leave = best == "leave" && action == "drop" && string_array({
		"closed", "open", "locked", "unlocked", "alone", "untouched"
	}).has(leave_state);
	if (state_preserving_leave) {
		frame["frame_type"] = "conversation";
		frame["speech_act"] = question ? "question" : "request";
		frame["surface_kind"] = frame["speech_act"];
		roles["query_kind"] = "state_preservation";
		frame["semantic_roles"] = roles;
		Array evidence = frame["parser_evidence"];
		evidence.append("non_executable:state_preservation");
		frame["parser_evidence"] = evidence;
		return frame;
	}
	// "only open the door if I say so": la condizione sta dopo il verbo e non e'
	// verificata, quindi non si agisce (tracker #30). "only" e' cio' che distingue
	// una condizione vera da una cortesia come "open the door if you can".
	const bool deferred_condition = (find_word(action_prefix, "only") >= 0 && (find_word(action_suffix, "if") >= 0 || find_word(action_suffix, "when") >= 0 || find_word(action_suffix, "unless") >= 0))
			|| find_word(action_suffix, "only if") >= 0 || find_word(action_suffix, "only when") >= 0;
	if (deferred_condition) { frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["query_kind"] = "conditional_request"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("constraint:deferred_condition"); frame["parser_evidence"] = evidence; return frame; }
	const bool ambiguous_reference = lower.contains("the other one") || lower.contains("the other?") || lower.contains("which one") || lower.contains("which target") || lower.contains("two matching targets") || lower.contains("matching targets are visible") || lower.contains("two possible targets");
	if (ambiguous_reference) { frame["frame_type"] = "clarification"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; Array action_candidates; action_candidates.append(action); frame["candidate_actions"] = action_candidates; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("unresolved_reference:contrast_set"); frame["parser_evidence"] = evidence; return frame; }
	bool addressed_modal_request = false;
	if (question && target_mode == "required") {
		// La forma invertita per grammatica (vedi `has_inverted_modal_request`),
		// piu' la forma soggetto-modale, che qui e' altrettanto una richiesta:
		// "you can open it?" con il punto interrogativo chiede, non constata.
		addressed_modal_request = has_inverted_modal_request(tokenize(action_prefix));
		if (!addressed_modal_request) {
			for (const char *construction : {"you can", "you could", "you would", "you will", "you may"}) {
				if (find_word(action_prefix, construction) >= 0) {
					addressed_modal_request = true;
					break;
				}
			}
		}
	}
	// "can you open doors?" chiede se sa farlo, non di farlo: il bersaglio e' un
	// plurale generico, senza determinante (tracker #30).
	if (question && addressed_modal_request) {
		const Array object_tokens = tokenize(action_suffix_for_context);
		if (object_tokens.size() >= 1 && object_tokens.size() <= 3) {
			const String first_object = object_tokens[0];
			const String last_object = object_tokens[object_tokens.size() - 1];
			const bool determined = string_array({"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that", "these", "those", "it", "them", "me", "us", "some"}).has(first_object);
			if (!determined && lemma(last_object) != last_object && last_object.ends_with("s")) addressed_modal_request = false;
		}
	}
	if (question && !addressed_modal_request) { frame["frame_type"] = "state_query"; frame["speech_act"] = "question"; frame["surface_kind"] = "question"; roles["query_kind"] = "action_capability"; roles["action"] = action; frame["semantic_roles"] = roles; return frame; }
	if (!question) {
		const bool polite_request = find_word(action_prefix, "please") >= 0;
		bool probabilistic_future_statement = false;
		for (const char *construction : {"will probably", "will likely", "will perhaps", "will maybe", "will most likely"}) {
			if (find_word(action_prefix, construction) >= 0) {
				probabilistic_future_statement = true;
				break;
			}
		}
		bool addressed_modal_statement = false;
		for (const char *construction : {"you should", "you must", "you will", "you can", "you could", "you would"}) {
			if (find_word(action_prefix, construction) >= 0) {
				if (String(construction) == "you will" && probabilistic_future_statement) continue;
				addressed_modal_statement = true;
				break;
			}
		}
		// La stessa richiesta, con il modale **invertito**: "can you open it".
		// L'elenco che riconosce questa forma esiste gia' venti righe piu' sotto,
		// ma vive dentro `if (question && ...)`, e senza punto interrogativo
		// `question` e' falso — nessun ramo la leggeva piu'. Misurato il
		// 2026-09-07 su 53 varianti: "can you open the door?" era un'azione,
		// "can you open the door" una chiacchiera, **4 fallimenti su 4**. Sui 60
		// turni reali sono i turni 15, 26, 35 e 52.
		//
		// Si legge dal prefisso dell'azione, non dall'inizio della frase, cosi'
		// vale anche dopo una subordinata: "if the window is closed, can you open
		// it a little" e' il turno 15, e comincia per "if".
		if (!addressed_modal_statement && has_inverted_modal_request(tokenize(action_prefix))) addressed_modal_statement = true;
		// Stesso confine usato per scegliere il verbo, punto incluso: vedi
		// `last_structural_boundary`.
		const String em_dash = String::chr(0x2014);
		const String imperative_prefix = without_discourse_markers(local_prefix_before(action_prefix));
		const bool serial_motion_prefix = imperative_prefix == "go" || imperative_prefix == "come" || imperative_prefix == "please go" || imperative_prefix == "please come";
		const bool trailing_boundary_request = action_prefix.ends_with(em_dash) || action_prefix.ends_with(";") || action_prefix.ends_with(":") || action_prefix.count(em_dash) >= 2;
		const bool direct_imperative = tokenize(imperative_prefix).size() <= 1 || trailing_boundary_request;
		// "the door is stuck. open windows let the cold in": dopo il verbo un nome senza
		// determinante e poi un secondo verbo in forma base con il suo complemento. "open
		// windows" e' il soggetto di "let", quindi e' una constatazione (tracker Analizzatore
		// #4). I verbi arrivano da WordNet (`verb_lemmas`); determinanti e particelle sono
		// classi chiuse. ponytail: "open windows cause drafts", senza determinante dopo il
		// secondo verbo, resta un ordine; si allarga quando il C++ avra' le parti del discorso.
		bool noun_phrase_subject = false;
		if (!best_narration && !polite_request && !addressed_modal_statement && !indirect_request && !why_not_request && !best_gerund && !supplied_verb_lemmas.is_empty()) {
			const Array after_verb = tokenize(action_suffix_for_context);
			if (after_verb.size() >= 3) {
				const String object_head = after_verb[0];
				const String second_verb = after_verb[1];
				const bool bare_object = !string_array({"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that", "these", "those", "it", "them", "me", "us", "some", "all", "every"}).has(object_head);
				const bool particle = string_array({"up", "down", "back", "out", "off", "over", "in", "on", "to", "for", "with", "from", "into", "onto", "away", "around", "by", "and", "or", "then", "now", "please"}).has(second_verb);
				const bool complement = string_array({"the", "a", "an", "my", "your", "his", "her", "their", "our", "this", "that", "it", "them", "me", "us", "him"}).has(after_verb[2]);
				noun_phrase_subject = bare_object && !particle && complement && supplied_verb_lemmas.has(second_verb);
			}
		}
		if (noun_phrase_subject) {
			frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["query_kind"] = "action_mention"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("non_request:noun_phrase_subject"); frame["parser_evidence"] = evidence; return frame;
		}
		if (!best_narration && !polite_request && !addressed_modal_statement && !indirect_request && !direct_imperative && !why_not_request && !best_gerund && !serial_motion_prefix) {
			frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement"; roles["query_kind"] = "action_mention"; roles["action"] = action; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("non_request:predicate_context"); evidence.append(String("predicate_context:alias_start=") + String::num_int64(best_start)); evidence.append(String("predicate_context:dash_count=") + String::num_int64(action_prefix.count(em_dash))); evidence.append(String("predicate_context:prefix_tokens=") + String::num_int64(tokenize(imperative_prefix).size())); frame["parser_evidence"] = evidence; return frame;
		}
	}
	// Il complemento di termine si toglie dal bersaglio per **ogni** azione, non solo per
	// `give`: fino al 2026-09-04 "make me a coffee" consegnava per bersaglio
	// `me a coffee`, che nel registro di grounding non e' niente. La guardia sul resto
	// non vuoto e' cio' che distingue i due usi dello stesso pronome — in "hug me" non
	// resta nulla dopo, quindi "me" e' il bersaglio e va lasciato dov'e'.
	//
	// Si legge dal payload **grezzo**, prima di qualunque taglio: `trim_target_span` si
	// ferma su `" to "`, quindi tagliando per primo "give the book to me" arriverebbe
	// qui gia' ridotto a "the book" e il destinatario sarebbe perso. Separare prima e
	// ripulire dopo e' cio' che fa funzionare tutte e due le forme, quella con il
	// pronome davanti e quella con la preposizione in coda.
	const String action_payload = best_particle_at >= 0
			? (lower.substr(matched_action_end, best_particle_at - matched_action_end) + lower.substr(best_particle_at + best_particle_length)).strip_edges()
			: lower.substr(matched_action_end).strip_edges();
	String target;
	String recipient;
	// "fold them closed": pronome e participio. Il participio dice come resta la cosa, e il
	// pronome e' la cosa, non il destinatario (tracker Analizzatore #84). Il verbo lo dice WordNet.
	String resultative_pronoun;
	{
		const Array payload_tokens = tokenize(action_payload);
		if (payload_tokens.size() == 2 && string_array({"it", "them", "this", "that", "him", "her"}).has(payload_tokens[0])) {
			const String complement = payload_tokens[1];
			if (complement.ends_with("ed") && (supplied_verb_lemmas.has(lemma(complement)) || supplied_verb_lemmas.has(lemma(complement) + String("e")))) resultative_pronoun = payload_tokens[0];
		}
	}
	if (!resultative_pronoun.is_empty()) {
		target = resultative_pronoun;
	} else {
		String without_recipient = action_payload;
		String demoted;
		extract_give_recipient(without_recipient, demoted);
		if (!demoted.is_empty() && !without_recipient.strip_edges().is_empty()) {
			target = trim_target_span(without_recipient);
			recipient = demoted;
		} else {
			target = trim_target_span(action_payload);
		}
	}
	// "put the banana in the box": la cosa e' "banana", il posto e' "box" (tracker #70).
	// Prima arrivava tutto come bersaglio, e il grounding non sapeva fra quali due
	// oggetti scegliere.
	String location;
	split_object_location(target, location);
	// "pick up the phone and check if there's a message": il complemento e' una frase, e la cosa
	// da guardare e' quella dell'azione di prima (tracker Analizzatore #84).
	if (action_payload.begins_with("if ") || action_payload.begins_with("whether ")) {
		target = "";
		location = "";
		if (!previous_frames.is_empty()) {
			const Array previous_targets = Dictionary(previous_frames[previous_frames.size() - 1]).get("candidate_targets", Array());
			if (previous_targets.size() == 1 && !String(previous_targets[0]).strip_edges().is_empty()) {
				target = String(previous_targets[0]).strip_edges();
				Array evidence = frame["parser_evidence"]; evidence.append("coreference:clause_complement_previous_target"); frame["parser_evidence"] = evidence;
			}
		}
	}
	const bool local_pronoun = string_array({"it", "this", "that", "them", "those", "these", "him", "her", "there"}).has(target);
	if (local_pronoun && !previous_frames.is_empty()) {
		const Dictionary previous = previous_frames[previous_frames.size() - 1];
		const Array previous_targets = previous.get("candidate_targets", Array());
		if (previous_targets.size() == 1 && !String(previous_targets[0]).strip_edges().is_empty()) {
			Array references; references.append(target); frame["reference_targets"] = references;
			target = String(previous_targets[0]).strip_edges();
			Array evidence = frame["parser_evidence"]; evidence.append("coreference:unique_previous_target"); frame["parser_evidence"] = evidence;
		} else {
			// A corrective request often refers back to a non-executable report:
			// "I thought you had opened the case; ... open it now?". The report
			// is evidence for the noun phrase, never authority to execute the
			// earlier action. Resolve only when the immediately preceding frame
			// names the same canonical action and yields one non-pronominal span.
			const String previous_text = String(previous.get("raw_text", "")).to_lower();
			const int antecedent_action_start = find_lemma_phrase_start(previous_text, best);
			if (antecedent_action_start >= 0) {
				const int antecedent_action_end = end_after_word_tokens(previous_text, antecedent_action_start, tokenize(best).size());
				String antecedent = trim_target_span(previous_text.substr(antecedent_action_end));
				String ignored_recipient;
				if (action == "give") extract_give_recipient(antecedent, ignored_recipient);
				if (!antecedent.is_empty() && !string_array({"it", "this", "that", "them", "him", "her", "there"}).has(antecedent)) {
					Array references; references.append(target); frame["reference_targets"] = references;
					target = antecedent;
					Array evidence = frame["parser_evidence"]; evidence.append("coreference:unique_previous_action_mention"); frame["parser_evidence"] = evidence;
				}
			}
			// Il soggetto della frase di prima: "if the window is closed, open it",
			// "the door is stuck. open it" (tracker #39). Solo un gruppo nominale con
			// determinante seguito da un verbo di stato, quindi mai un'azione.
			if (string_array({"it", "this", "that", "them"}).has(target) && String(previous.get("frame_type", "")) == "conversation") {
				String subject_text = previous_text.strip_edges();
				for (const char *lead : {"if ", "when ", "since ", "once ", "because "}) if (subject_text.begins_with(lead)) subject_text = subject_text.substr(String(lead).length()).strip_edges();
				// "about that book; put it on the shelf": il tema annunciato e' l'antecedente.
				for (const char *topic : {"about ", "regarding ", "as for "}) {
					if (subject_text.begins_with(topic)) subject_text = subject_text.substr(String(topic).length()).strip_edges().trim_suffix(",") + String(" is ");
				}
				int verb_at = -1;
				for (const char *state_verb : {" is ", " are ", " was ", " were ", " looks ", " seems ", " feels "}) {
					const int found = subject_text.find(state_verb);
					if (found > 0 && (verb_at < 0 || found < verb_at)) verb_at = found;
				}
				const Array subject_tokens = verb_at > 0 ? tokenize(subject_text.left(verb_at)) : Array();
				if (subject_tokens.size() >= 2 && subject_tokens.size() <= 4 && string_array({"the", "my", "your", "this", "that"}).has(subject_tokens[0])) {
					const String subject = trim_target_span(subject_text.left(verb_at));
					if (!subject.is_empty()) {
						Array references; references.append(target); frame["reference_targets"] = references;
						target = subject;
						Array evidence = frame["parser_evidence"]; evidence.append("coreference:previous_clause_subject"); frame["parser_evidence"] = evidence;
					}
				}
			}
			// "remember the scarf? put it on the bed": la domanda di prima nomina una cosa
			// sola, in fondo, e quella e' l'antecedente (tracker Analizzatore #76). Con due
			// gruppi nominali ("is the scarf on the chair?") non si sceglie: si chiede.
			if (string_array({"it", "this", "that", "them"}).has(target) && String(previous.get("surface_kind", "")) == "question") {
				const Array question_tokens = tokenize(previous_text);
				int determiner_at = -1;
				int determiners = 0;
				for (int i = 0; i < question_tokens.size(); ++i) {
					if (string_array({"the", "my", "your", "this", "that"}).has(question_tokens[i])) { determiner_at = i; ++determiners; }
				}
				const int phrase_length = question_tokens.size() - determiner_at - 1;
				if (determiners == 1 && phrase_length >= 1 && phrase_length <= 3) {
					String phrase;
					for (int i = determiner_at + 1; i < question_tokens.size(); ++i) phrase += (phrase.is_empty() ? String() : String(" ")) + String(question_tokens[i]);
					const String antecedent = trim_target_span(phrase);
					if (!antecedent.is_empty()) {
						Array references; references.append(target); frame["reference_targets"] = references;
						target = antecedent;
						Array evidence = frame["parser_evidence"]; evidence.append("coreference:previous_question_topic"); frame["parser_evidence"] = evidence;
					}
				}
			}
		}
	}
	// Il racconto resta un racconto, con la sua cosa: il filtro dell'app lo toglie dagli ordini e
	// ne fa un fatto sull'utente (#83).
	if (best_narration) {
		frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement";
		roles["query_kind"] = "self_narration"; roles["action"] = action; roles["object"] = target; roles["object_or_class"] = target; roles["destination"] = location.is_empty() ? target : location; roles["recipient_or_direction"] = recipient; roles["target_mode"] = target_mode; frame["semantic_roles"] = roles;
		if (!target.is_empty()) { Array targets; targets.append(target); frame["candidate_targets"] = targets; }
		Array evidence = frame["parser_evidence"]; evidence.append("self_narration:subject_verb"); frame["parser_evidence"] = evidence;
		return frame;
	}
	// "come sit here with me", "wait next to me": il posto e' quello dell'utente (tracker
	// Analizzatore #82). Prima valeva solo per come back e return, e "sit" riceveva per bersaglio
	// "here with me". Le forme sono una classe chiusa di deittici.
	{
		String deixis = action_payload;
		String previous;
		while (previous != deixis) {
			previous = deixis;
			deixis = deixis.strip_edges().trim_suffix(".").trim_suffix("!").trim_suffix("?").trim_suffix(",").trim_suffix(" please").trim_suffix(" now").strip_edges();
		}
		if (string_array({"here", "over here", "right here", "here with me", "with me", "next to me", "right next to me", "beside me", "near me", "by me", "close to me", "closer", "closer to me"}).has(deixis)) {
			target = "user";
			Array evidence = frame["parser_evidence"]; evidence.append("deixis:user_location"); frame["parser_evidence"] = evidence;
		}
	}
	if ((best == "come back" || best == "return" || best == "return to") && (target == "here" || target.begins_with("here where i am") || target == "me")) {
		target = "user";
		Array evidence = frame["parser_evidence"]; evidence.append("deixis:user_location"); frame["parser_evidence"] = evidence;
	}
	if (action == "gesture" && (best == "high five" || best == "hit five") && target.is_empty()) {
		target = "user";
		recipient = "user";
		Array evidence = frame["parser_evidence"]; evidence.append(best == "hit five" ? "typo_resolution:hit_five_to_high_five" : "gesture:addressee_user"); frame["parser_evidence"] = evidence;
	}
	// "break free", "get loose": il verbo con il solo risultato non ha una cosa da toccare. E' un gesto
	// del racconto, non un ordine su un oggetto: niente chiarimento (tracker Analizzatore #103).
	if (string_array({"free", "loose", "shut", "closed", "open", "apart"}).has(target)) {
		frame["frame_type"] = "conversation"; frame["speech_act"] = "statement"; frame["surface_kind"] = "statement";
		roles["query_kind"] = "action_mention"; roles["action"] = action; roles["target_mode"] = "optional"; frame["semantic_roles"] = roles;
		Array evidence = frame["parser_evidence"]; evidence.append("resultative_without_object"); frame["parser_evidence"] = evidence;
		return frame;
	}
	const char32_t target_last = target.is_empty() ? 0 : target.unicode_at(target.length() - 1);
	const bool broken_target_span = !has_word_character(target) || target_last == U'—' || target_last == U'-';
	const bool missing_required_target = target_mode == "required" && (target.is_empty() || string_array({"it", "this", "that", "them", "those", "these", "him", "her", "there", "the other", "other"}).has(target) || target.contains("...") || broken_target_span || string_array({"to", "on", "at", "in", "from", "through", "behind", "beside", "against", "over"}).has(target));
	if (missing_required_target) { frame["frame_type"] = "clarification"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; Array action_candidates; action_candidates.append(action); frame["candidate_actions"] = action_candidates; roles["action"] = action; roles["target_mode"] = target_mode; frame["semantic_roles"] = roles; Array evidence = frame["parser_evidence"]; evidence.append("missing_required_role:object"); if (starts_any(lower, string_array({"if ", "unless "})) || lower.contains(" only if ") || lower.contains(" when ")) evidence.append("constraint:unverified_condition"); frame["parser_evidence"] = evidence; return frame; }
	frame["frame_type"] = string_array({"equip", "unequip", "pickup", "drop", "give", "take_from"}).has(action) ? "inventory_action" : "world_action"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; roles["action"] = action; roles["object"] = target; roles["object_or_class"] = target; roles["destination"] = location.is_empty() ? target : location; roles["recipient_or_direction"] = recipient; roles["target_mode"] = target_mode; frame["semantic_roles"] = roles; if (!target.is_empty()) { Array targets; targets.append(target); frame["candidate_targets"] = targets; } return frame;
}

Dictionary NativeTurnAnalyzer::compile_turn(const String &raw_input, const Dictionary &analysis_result, const Dictionary &capability_snapshot, const Dictionary &scene_state, const Dictionary &options) const {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	absorb_irregular_forms(capability_snapshot);
	const String normalized_input = analysis_result.get("normalized_input", normalize(raw_input));
	Array frames;
	bool coordination_limit_exceeded = false;
	const Dictionary pending = scene_state.get("pending_clarification", Dictionary());
	const String pending_action = pending.get("action", "");
	const Dictionary actions = capability_snapshot.get("actions", Dictionary());
	if (!pending_action.is_empty() && actions.has(pending_action)) {
		const String lower = normalize(raw_input).to_lower();
		String resolved_target;
		for (const char *raw_marker : {
			"the target i meant was ",
			"the target is ",
			"i was referring to ",
			"by it i mean ",
			"the one i mean is ",
			"i meant "
		}) {
			const String marker = raw_marker;
			const int marker_start = lower.find(marker);
			if (marker_start < 0) continue;
			resolved_target = lower.substr(marker_start + marker.length()).strip_edges();
			break;
		}
		const String use_marker = "use ";
		const String as_target_marker = " as the target";
		const int use_start = lower.find(use_marker);
		const int as_target_start = lower.find(as_target_marker, use_start >= 0 ? use_start + use_marker.length() : 0);
		if (resolved_target.is_empty() && use_start >= 0 && as_target_start > use_start) resolved_target = lower.substr(use_start + use_marker.length(), as_target_start - use_start - use_marker.length()).strip_edges();
		resolved_target = trim_target_span(resolved_target);
		String resolved_location;
		split_object_location(resolved_target, resolved_location);
		const bool competing_target = resolved_target.contains(" or ") || resolved_target.contains(" but ") || resolved_target.contains(" not ");
		if (competing_target) resolved_target = "";
		const bool asks_for_clarification = competing_target || lower.contains("which one") || lower.contains("which target") || lower.contains("two possible targets") || lower.contains("still have not named the target");
		const bool conditional_pending = bool(pending.get("conditional", false));
		if (!resolved_target.is_empty() || asks_for_clarification) {
			Dictionary frame;
			frame["frame_id"] = "frame_001"; frame["raw_text"] = raw_input; frame["source_spans"] = source_span_array(normalized_input, 0, normalized_input.length()); frame["candidate_targets"] = Array(); frame["reference_targets"] = Array(); frame["dependency_refs"] = Array(); frame["semantic_authority"] = "deterministic_parser";
			String recipient;
			if (pending_action == "give") {
				extract_give_recipient(resolved_target, recipient);
				resolved_target = trim_target_span(resolved_target);
			}
			Dictionary roles = empty_roles(); roles["action"] = pending_action; roles["target_mode"] = Dictionary(actions[pending_action]).get("target_mode", "required"); roles["object"] = resolved_target; roles["object_or_class"] = resolved_target; roles["destination"] = resolved_location.is_empty() ? resolved_target : resolved_location; roles["recipient_or_direction"] = recipient;
			if (!resolved_target.is_empty()) { Array targets; targets.append(resolved_target); frame["candidate_targets"] = targets; }
			if (asks_for_clarification || conditional_pending) {
				frame["frame_type"] = "clarification"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; Array action_candidates; action_candidates.append(pending_action); frame["candidate_actions"] = action_candidates; frame["parser_evidence"] = conditional_pending ? string_array({"context_resolution:pending_action", "constraint:unverified_condition"}) : string_array({"context_resolution:pending_action", "unresolved_reference:contrast_set"});
			} else {
				frame["frame_type"] = string_array({"equip", "unequip", "pickup", "drop", "give", "take_from"}).has(pending_action) ? "inventory_action" : "world_action"; frame["speech_act"] = "request"; frame["surface_kind"] = "request"; frame["parser_evidence"] = string_array({"context_resolution:pending_action", "context_resolution:explicit_target"});
			}
			frame["semantic_roles"] = roles;
			frames.append(frame);
		}
	}
	if (frames.is_empty()) {
		const Dictionary semantic_graph = options.get("semantic_action_graph", Dictionary());
		Array clauses = split_action_coordinations(split_clauses(raw_input), capability_snapshot, semantic_graph);
		coordination_limit_exceeded = clauses.size() > MAX_FRAMES;
		if (!coordination_limit_exceeded) {
			const Dictionary aliases = capability_snapshot.get("alias_index", Dictionary());
			const Array alias_keys = aliases.keys();
			int clause_search_from = 0;
			for (int i = 0; i < clauses.size(); ++i) {
				const String clause = clauses[i];
				const int clause_start = find_clause_start(normalized_input, clause, clause_search_from);
				const int safe_clause_start = clause_start >= 0 ? clause_start : 0;
				const int clause_end = clause_start >= 0 ? clause_start + clause.length() : normalized_input.length();
				if (clause_start >= 0) clause_search_from = clause_end;
				const Array clause_tokens = tokenize(clause);
				if (is_retraction(clause)) {
					for (int frame_index = 0; frame_index < frames.size(); ++frame_index) {
						Dictionary retracted = frames[frame_index];
						const String retracted_type = retracted.get("frame_type", "");
						if (retracted_type != "world_action" && retracted_type != "inventory_action" && retracted_type != "clarification") continue;
						Dictionary retracted_roles = retracted.get("semantic_roles", Dictionary());
						retracted_roles["query_kind"] = "prohibition";
						retracted["semantic_roles"] = retracted_roles;
						retracted["frame_type"] = "conversation"; retracted["speech_act"] = "statement"; retracted["surface_kind"] = "statement";
						Array evidence = retracted.get("parser_evidence", Array()); evidence.append("negated_action:retracted_by_following_clause"); retracted["parser_evidence"] = evidence;
						frames[frame_index] = retracted;
					}
				}
				const Array compositional_frames = build_compositional_frames(frames.size() + 1, clause, capability_snapshot, frames, normalized_input, safe_clause_start, clause_end);
				if (!compositional_frames.is_empty()) {
					if (frames.size() + compositional_frames.size() > MAX_FRAMES) {
						coordination_limit_exceeded = true;
						break;
					}
					for (int compositional_index = 0; compositional_index < compositional_frames.size(); ++compositional_index) frames.append(compositional_frames[compositional_index]);
					continue;
				}
				// "jump twice", "open the door 3 times": il conteggio in coda ripete l'azione
				// della frase (tracker #35). Si ripete solo se la frase senza il conteggio e'
				// davvero un'azione; altrimenti la frase segue la strada normale.
				{
					String counted_base = clause.strip_edges().trim_suffix(".").trim_suffix("!").strip_edges();
					const String counted_lower = counted_base.to_lower();
					int suffix_total = 0;
					if (counted_lower.ends_with(" twice")) { suffix_total = 2; counted_base = counted_base.left(counted_base.length() - 6); }
					else if (counted_lower.ends_with(" thrice")) { suffix_total = 3; counted_base = counted_base.left(counted_base.length() - 7); }
					else if (clause_tokens.size() >= 3 && (String(clause_tokens[clause_tokens.size() - 1]) == "times" || String(clause_tokens[clause_tokens.size() - 1]) == "time")) {
						suffix_total = number_value(clause_tokens[clause_tokens.size() - 2]);
						const int count_word = find_last_word(counted_lower, String(clause_tokens[clause_tokens.size() - 2]));
						if (count_word > 0) counted_base = counted_base.left(count_word);
						else suffix_total = 0;
					}
					if (suffix_total > 1) {
						const Dictionary probe_frame = build_frame(frames.size() + 1, counted_base.strip_edges(), capability_snapshot, frames);
						const String probe_type = probe_frame.get("frame_type", "");
						if (probe_type == "world_action" || probe_type == "inventory_action") {
							if (frames.size() + suffix_total > MAX_FRAMES) {
								coordination_limit_exceeded = true;
								break;
							}
							for (int repetition = 0; repetition < suffix_total; ++repetition) {
								Dictionary counted_frame = build_frame(frames.size() + 1, counted_base.strip_edges(), capability_snapshot, frames);
								counted_frame["raw_text"] = clause;
								counted_frame["source_spans"] = source_span_array(normalized_input, safe_clause_start, clause_end);
								Dictionary counted_roles = counted_frame.get("semantic_roles", Dictionary());
								counted_roles["count_index"] = String::num_int64(repetition + 1);
								counted_roles["count_total"] = String::num_int64(suffix_total);
								counted_frame["semantic_roles"] = counted_roles;
								Array evidence = counted_frame.get("parser_evidence", Array()); evidence.append("quantifier:expanded_counted_action"); evidence.append(String("counted_action:%d/%d") % Array::make(repetition + 1, suffix_total)); counted_frame["parser_evidence"] = evidence;
								frames.append(counted_frame);
							}
							continue;
						}
					}
				}
				String counted_alias;
				int counted_total = 0;
				if (clause_tokens.size() >= 3 && String(clause_tokens[0]) == "do" && number_value(clause_tokens[1]) > 0) {
					counted_total = number_value(clause_tokens[1]);
					for (int alias_index = 0; alias_index < alias_keys.size(); ++alias_index) {
						const String alias = alias_keys[alias_index];
						const Array candidates = aliases.get(alias, Array());
						if (tokenize(alias).size() == 1 && lemma(String(clause_tokens[2])) == alias && candidates.size() == 1) {
							counted_alias = alias;
							break;
						}
					}
				}
				if (!counted_alias.is_empty() && counted_total > 0) {
					if (frames.size() + counted_total > MAX_FRAMES) {
						coordination_limit_exceeded = true;
						break;
					}
					String default_target;
					if (!frames.is_empty()) {
						const Array previous_targets = Dictionary(frames[frames.size() - 1]).get("candidate_targets", Array());
						if (previous_targets.size() == 1) default_target = String(previous_targets[0]).strip_edges();
					}
					const String lower_clause = clause.to_lower();
					String final_target;
					const int final_land = lower_clause.rfind("land on ");
					if (final_land >= 0) final_target = lower_clause.substr(final_land + 8).strip_edges().trim_suffix(".").trim_suffix("!").trim_suffix("?");
					for (const char *prefix : {"the ", "a ", "an ", "to ", "on "}) if (final_target.begins_with(prefix)) final_target = final_target.trim_prefix(prefix).strip_edges();
					for (int repetition = 0; repetition < counted_total; ++repetition) {
						const String repetition_target = repetition == counted_total - 1 && !final_target.is_empty() ? final_target : default_target;
						Dictionary counted_frame = build_frame(frames.size() + 1, counted_alias + (!repetition_target.is_empty() ? " " + repetition_target : ""), capability_snapshot, frames);
						counted_frame["raw_text"] = clause;
						counted_frame["source_spans"] = source_span_array(normalized_input, safe_clause_start, clause_end);
						Dictionary counted_roles = counted_frame.get("semantic_roles", Dictionary());
						counted_roles["count_index"] = String::num_int64(repetition + 1);
						counted_roles["count_total"] = String::num_int64(counted_total);
						counted_frame["semantic_roles"] = counted_roles;
						Array evidence = counted_frame.get("parser_evidence", Array()); evidence.append("quantifier:expanded_counted_action"); evidence.append(String("counted_action:%d/%d") % Array::make(repetition + 1, counted_total)); if (repetition == counted_total - 1 && !final_target.is_empty()) evidence.append("override:final_destination"); counted_frame["parser_evidence"] = evidence;
						frames.append(counted_frame);
					}
					continue;
				}
				Dictionary frame = build_frame(frames.size() + 1, clause, capability_snapshot, frames);
				frame["source_spans"] = source_span_array(normalized_input, safe_clause_start, clause_end);
				frames.append(frame);
			}
			// "wait—open the door": un richiamo dell'attenzione detto da solo, subito
			// prima di un'altra richiesta, e' un intercalare e non un'azione (tracker #67).
			// Detto da solo, "wait", resta un'azione. I richiami sono una classe chiusa.
			for (int frame_index = 0; frame_index + 1 < frames.size(); ++frame_index) {
				Dictionary interjection = frames[frame_index];
				const Dictionary next = frames[frame_index + 1];
				const Dictionary interjection_roles = interjection.get("semantic_roles", Dictionary());
				const String bare = String(interjection.get("raw_text", "")).to_lower().strip_edges().trim_suffix(",").trim_suffix(";").trim_suffix("!").trim_suffix(".").strip_edges();
				const String next_type = next.get("frame_type", "");
				const bool attention_call = string_array({"wait", "hold on", "hang on", "look", "listen", "hey"}).has(bare);
				if (attention_call && String(interjection.get("frame_type", "")) == "world_action" && String(interjection_roles.get("object", "")).is_empty() && (next_type == "world_action" || next_type == "inventory_action" || next_type == "clarification" || String(next.get("speech_act", "")) == "question")) {
					Dictionary roles = interjection_roles.duplicate();
					roles["query_kind"] = "discourse_marker";
					interjection["semantic_roles"] = roles;
					interjection["frame_type"] = "conversation"; interjection["speech_act"] = "statement"; interjection["surface_kind"] = "statement";
					Array evidence = interjection.get("parser_evidence", Array()); evidence.append("discourse_marker:attention_call"); interjection["parser_evidence"] = evidence;
					frames[frame_index] = interjection;
				}
			}
			if (coordination_limit_exceeded) frames.clear();
			if (!coordination_limit_exceeded && frames.is_empty() && !raw_input.strip_edges().is_empty()) frames.append(build_frame(1, raw_input, capability_snapshot, frames));
		}
	}
	Dictionary validation; validation["valid"] = coordination_limit_exceeded || (!frames.is_empty() && frames.size() <= MAX_FRAMES); validation["errors"] = Array(); validation["semantic_authority"] = "deterministic_parser"; validation["frame_count"] = frames.size();
	Dictionary out; out["schema_contract"] = "deterministic_turn_compiler_v2"; out["language_status"] = analysis_result.get("language_status", "english"); out["frames"] = frames; out["validation"] = validation; out["extractor_source"] = "native_turn_analyzer"; out["extractor_contract"] = "deterministic_turn_compiler_v2"; out["elapsed_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - started); out["capability_count"] = Dictionary(capability_snapshot.get("actions", Dictionary())).size(); if (coordination_limit_exceeded) { out["terminal_result"] = "LIMIT_EXCEEDED"; out["diagnostic_code"] = "NTA_CLAUSE_LIMIT_EXCEEDED"; } return out;
}

Dictionary NativeTurnAnalyzer::analyze_and_compile(const String &raw_input, const Dictionary &capability_snapshot, const Dictionary &scene_state, const Dictionary &options) const {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	absorb_irregular_forms(capability_snapshot);
	Dictionary analysis = analyze_turn(raw_input, scene_state, options);
	Dictionary compiled;
	const String diagnostic_code = analysis.get("diagnostic_code", "");
	if (!diagnostic_code.is_empty()) {
		Dictionary validation; validation["valid"] = true; validation["errors"] = Array(); validation["semantic_authority"] = "deterministic_parser"; validation["frame_count"] = 0;
		compiled["schema_contract"] = "deterministic_turn_compiler_v2"; compiled["language_status"] = analysis.get("language_status", "english"); compiled["frames"] = Array(); compiled["validation"] = validation; compiled["extractor_source"] = "native_turn_analyzer"; compiled["extractor_contract"] = "deterministic_turn_compiler_v2"; compiled["terminal_result"] = "LIMIT_EXCEEDED"; compiled["diagnostic_code"] = diagnostic_code; compiled["elapsed_usec"] = 0; compiled["capability_count"] = Dictionary(capability_snapshot.get("actions", Dictionary())).size();
	} else {
		compiled = compile_turn(raw_input, analysis, capability_snapshot, scene_state, options);
	}
	const String effective_diagnostic_code = !diagnostic_code.is_empty() ? diagnostic_code : String(compiled.get("diagnostic_code", ""));
	Dictionary metrics; metrics["total_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - started); metrics["token_count"] = tokenize(raw_input).size(); metrics["clause_count"] = split_clauses(raw_input).size(); metrics["frame_count"] = Array(compiled.get("frames", Array())).size(); metrics["abstained"] = !effective_diagnostic_code.is_empty();
	Dictionary packet_validation; packet_validation["valid"] = bool(Dictionary(compiled["validation"])["valid"]); packet_validation["errors"] = Array(); if (!effective_diagnostic_code.is_empty()) packet_validation["diagnostic_code"] = effective_diagnostic_code;
	Dictionary packet; packet["native_packet_version"] = "native_turn_packet_v1"; packet["engine"] = get_build_info(); packet["analysis_result"] = analysis; packet["compiler_output"] = compiled; packet["metrics"] = metrics; packet["validation"] = packet_validation;
	if (bool(options.get("semantic_bridge_enabled", false))) packet["semantic_link_result"] = propose_semantic_links(raw_input, analysis, capability_snapshot, options);
	return packet;
}

Dictionary NativeTurnAnalyzer::propose_semantic_links(const String &raw_input, const Dictionary &analysis_result, const Dictionary &capability_snapshot, const Dictionary &options) const {
	return SemanticBridge::propose(raw_input, analysis_result, capability_snapshot, options);
}

Dictionary NativeTurnAnalyzer::validate_contract(const Dictionary &result) const { Array errors; for (const char *key : {"analysis_version", "supported_language", "language_status", "raw_input", "normalized_input", "request_count", "segments", "global_flags", "structural_confidence", "segmentation_status"}) if (!result.has(key)) errors.append(String("missing:") + key); Dictionary out; out["valid"] = errors.is_empty() && String(result.get("analysis_version", "")) == "4"; out["errors"] = errors; return out; }

Dictionary NativeTurnAnalyzer::get_build_info() const { Dictionary out; out["name"] = "native_turn_analyzer"; out["version"] = "0.5.7-analyzer-round"; out["build"] = Engine::get_singleton()->is_editor_hint() ? "editor" : "runtime"; out["platform"] = OS::get_singleton()->get_name(); out["language"] = "en"; out["analysis_contract"] = "AnalysisResult v4"; out["compiler_contract"] = "deterministic_turn_compiler_v2"; out["semantic_link_contract"] = "semantic_link_result_v1"; out["candidate_augmenter"] = "typed_semantic_bridge_runtime"; out["max_input_chars"] = MAX_INPUT_CHARS; out["max_tokens"] = MAX_TOKENS; out["max_frames"] = MAX_FRAMES; return out; }

} // namespace norucore
