#include "semantic_bridge.h"

#include "semantic_contract.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace norucore {

namespace {

bool is_word_char(char32_t c) {
	// Lettere accentate comprese: "Zoë" o "café" non si spezzano (tracker #17).
	if (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) return true;
	return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c == U'_';
}

Array tokenize(String text) {
	text = text.to_lower();
	for (const char *marker : {".", ",", "!", "?", ";", ":", "\"", "'", "`", "(", ")", "[", "]", "*", "\n"}) text = text.replace(marker, " ");
	while (text.contains("  ")) text = text.replace("  ", " ");
	Array out;
	const PackedStringArray parts = text.strip_edges().split(" ", false);
	for (int i = 0; i < parts.size(); ++i) out.append(parts[i]);
	return out;
}

// Quanti caratteri possono stare fra il verbo e la sua particella. Oltre, non
// e' piu' lo stesso predicato: "put the box on the shelf and walk in" non e'
// "put in". Tetto dichiarato, non una grammatica.
// ponytail: finestra in caratteri; diventa in token il giorno che sbaglia.
static const int SEPARATED_PARTICLE_WINDOW = 40;

int find_phrase(const String &text, const String &phrase) {
	const String haystack = text.to_lower();
	const String needle = phrase.to_lower().replace("_", " ").strip_edges();
	int from = 0;
	while (from <= haystack.length() - needle.length()) {
		const int found = haystack.find(needle, from);
		if (found < 0) return -1;
		const bool left = found == 0 || !is_word_char(haystack.unicode_at(found - 1));
		const int after = found + needle.length();
		const bool right = after >= haystack.length() || !is_word_char(haystack.unicode_at(after));
		if (left && right) return found;
		from = found + 1;
	}
	return -1;
}

bool has_direct_alias(const String &clause, const Dictionary &capability_snapshot) {
	const Dictionary aliases = capability_snapshot.get("alias_index", Dictionary());
	const Array keys = aliases.keys();
	for (int i = 0; i < keys.size(); ++i) if (find_phrase(clause, String(keys[i])) >= 0) return true;
	return false;
}

bool overlaps_direct_alias(const String &clause, int position, int length, const Dictionary &capability_snapshot) {
	if (position < 0 || length <= 0) return false;
	const Dictionary aliases = capability_snapshot.get("alias_index", Dictionary());
	const Array keys = aliases.keys();
	for (int index = 0; index < keys.size(); ++index) {
		const String alias = keys[index];
		const int alias_position = find_phrase(clause, alias);
		if (alias_position >= 0 && position < alias_position + alias.length() && alias_position < position + length) return true;
	}
	return false;
}

bool is_reported(const String &lower) {
	return lower.contains(" told you to ") || lower.contains(" said to ") || lower.contains(" said \"") || lower.contains(" said '") || lower.contains("the word ") || lower.contains("what does ");
}

bool is_quoted(const String &raw, int position) {
	if (position < 0) return false;
	// Apostrophes are lexical characters in contractions (don't, we're), not
	// quotation delimiters. Only explicit quote-like delimiters affect scope.
	for (const char *delimiter : {"\"", "`", "*"}) {
		const String prefix = raw.left(position);
		int count = 0;
		int from = 0;
		while (from < prefix.length()) {
			const int found = prefix.find(delimiter, from);
			if (found < 0) break;
			count += 1;
			from = found + 1;
		}
		if (count % 2 == 1) return true;
	}
	return false;
}

bool is_non_directive_future(const String &lower, const Dictionary &linguistic) {
	if (String(linguistic.get("modal", "")) != "will" && !lower.contains("you will")) return false;
	const int modal_start = lower.find("you will");
	if (modal_start < 0) return false;
	const Array prefix_tokens = tokenize(lower.left(modal_start));
	for (const char *temporal : {"tomorrow", "later", "eventually", "someday", "soon", "tonight", "next"}) {
		if (prefix_tokens.has(temporal)) return true;
	}
	const String modal_tail = lower.substr(modal_start + String("you will").length()).strip_edges();
	for (const char *epistemic : {"probably ", "likely ", "perhaps ", "maybe ", "most likely "}) {
		if (modal_tail.begins_with(epistemic)) return true;
	}
	return false;
}

// Un vocativo iniziale non toglie all'imperativo la sua natura di ordine:
// "Sasha, put a muffin in my mouth" e' una richiesta quanto "put a muffin in my
// mouth". Senza questo, chiamare il personaggio per nome faceva scivolare la
// frase a "statement" e i candidati semantici venivano scartati in blocco —
// verificato il 2026-09-02, ed e' cio' che teneva ferma la catena del muffin.
// Regola grammaticale, non un elenco di nomi: e' il gruppo nominale isolato da
// una virgola in testa alla frase.
bool is_only_leading_vocative(const String &prefix) {
	const String trimmed = prefix.strip_edges();
	if (trimmed.is_empty() || !trimmed.ends_with(",")) return false;
	const String name = trimmed.substr(0, trimmed.length() - 1).strip_edges();
	if (name.is_empty() || name.length() > 32) return false;
	for (int i = 0; i < name.length(); ++i) {
		const char32_t character = name.unicode_at(i);
		if (!is_word_char(character) && character != ' ' && character != '\'' && character != '-') return false;
	}
	return true;
}

// Il verbo che nessuna azione copre, cosi' come l'utente l'ha scritto.
//
// Fino al 2026-09-02 il ramo senza corrispondenza azzerava `surface` e `lemma`,
// quindi la richiesta arrivava al cervello senza dire *cosa* non era stato
// capito: il turno diventava una chiacchierata e il modello improvvisava, senza
// che all'utente venisse detto niente. Con il verbo in mano il personaggio puo'
// dire che non sa farlo, e chi scrive puo' riformulare.
String unknown_predicate(const Dictionary &linguistic, const String &clause) {
	String lemma;
	const Array units = linguistic.get("predicate_units", Array());
	if (units.size() > 0 && units[0].get_type() == Variant::DICTIONARY) {
		const Dictionary unit = units[0];
		const Dictionary roles = unit.get("semantic_roles", Dictionary());
		lemma = String(roles.get("predicate_lemma", "")).strip_edges().to_lower();
	}
	// Con un vocativo in testa l'analizzatore prende il nome per predicato:
	// "Sasha, whittle the chair" dava `sasha`. Si guarda dopo la virgola.
	const int comma = clause.find(",");
	if (comma > 0 && is_only_leading_vocative(clause.left(comma + 1))) {
		const String vocative = clause.left(comma).strip_edges().to_lower();
		if (lemma.is_empty() || lemma == vocative) {
			const String rest = clause.substr(comma + 1).strip_edges().to_lower();
			const int space = rest.find(" ");
			const String first = space > 0 ? rest.left(space) : rest;
			if (!first.is_empty()) return first;
		}
	}
	return lemma;
}

String infer_speech_act(const String &clause, const Dictionary &, const Dictionary &linguistic, int predicate_position = -1) {
	const String lower = clause.to_lower().strip_edges();
	const String analyzer_speech_act = linguistic.get("speech_act", "");
	if (analyzer_speech_act != "request") return analyzer_speech_act == "question" || analyzer_speech_act == "fragment" ? analyzer_speech_act : "statement";
	if (is_non_directive_future(lower, linguistic)) return "statement";
	if (lower.begins_with("please ") || lower.begins_with("can you ") || lower.begins_with("could you ") || lower.begins_with("would you ") || lower.begins_with("will you ") || lower.begins_with("i need you to ") || lower.begins_with("i want you to ") || lower.begins_with("i would like you to ") || lower.begins_with("i am asking you to ")) return "request";
	if (predicate_position == 0) return "request";
	if (predicate_position > 0 && is_only_leading_vocative(lower.left(predicate_position))) return "request";
	if (predicate_position > 0 && lower.left(predicate_position).contains(" and ")) return "request";
	return "statement";
}

Dictionary source_span(const String &full_text, const String &clause, int fallback_start) {
	int start = full_text.to_lower().find(clause.to_lower().strip_edges(), fallback_start);
	if (start < 0) start = MAX(0, fallback_start);
	Dictionary span;
	span["start_index"] = start;
	span["end_index"] = MIN(full_text.length(), start + clause.strip_edges().length());
	span["text"] = clause.strip_edges();
	return span;
}

Dictionary source_span_from_analysis(const String &full_text, const Dictionary &linguistic) {
	const int start = CLAMP(int(linguistic.get("start_index", 0)), 0, full_text.length());
	const int supplied_end = int(linguistic.get("end_index", start));
	const int end = CLAMP(MAX(start, supplied_end), start, full_text.length());
	Dictionary span;
	span["start_index"] = start;
	span["end_index"] = end;
	span["text"] = full_text.substr(start, end - start);
	span["segment_id"] = linguistic.get("segment_id", "");
	return span;
}

Dictionary predicate_unit_at(const Dictionary &linguistic, int relative_position) {
	const int absolute_position = int(linguistic.get("start_index", 0)) + MAX(0, relative_position);
	const Array units = linguistic.get("predicate_units", Array());
	for (int index = 0; index < units.size(); ++index) {
		if (units[index].get_type() != Variant::DICTIONARY) continue;
		const Dictionary unit = units[index];
		if (absolute_position >= int(unit.get("start_index", -1)) && absolute_position < int(unit.get("end_index", -1))) return unit;
	}
	return Dictionary();
}

void sort_candidates(Array &candidates) {
	for (int i = 1; i < candidates.size(); ++i) {
		int cursor = i;
		while (cursor > 0) {
			const Dictionary left = candidates[cursor - 1];
			const Dictionary right = candidates[cursor];
			const double left_conf = left.get("confidence", 0.0);
			const double right_conf = right.get("confidence", 0.0);
			const bool swap = right_conf > left_conf || (right_conf == left_conf && String(right.get("action", "")) < String(left.get("action", "")));
			if (!swap) break;
			const Variant temp = candidates[cursor - 1];
			candidates[cursor - 1] = candidates[cursor];
			candidates[cursor] = temp;
			cursor -= 1;
		}
	}
}

void sort_literal_requests(Array &requests) {
	for (int i = 1; i < requests.size(); ++i) {
		int cursor = i;
		while (cursor > 0) {
			const Dictionary left = requests[cursor - 1];
			const Dictionary right = requests[cursor];
			const Dictionary left_span = left.get("predicate_span", left.get("source_span", Dictionary()));
			const Dictionary right_span = right.get("predicate_span", right.get("source_span", Dictionary()));
			const int left_start = left_span.get("start_index", 0);
			const int right_start = right_span.get("start_index", 0);
			if (right_start >= left_start) break;
			const Variant temp = requests[cursor - 1];
			requests[cursor - 1] = requests[cursor];
			requests[cursor] = temp;
			cursor -= 1;
		}
	}
	for (int index = 0; index < requests.size(); ++index) {
		Dictionary request = requests[index];
		request["request_id"] = String("literal_%03d") % static_cast<int64_t>(index + 1);
		requests[index] = request;
	}
}

Dictionary invalid_result(const uint64_t started, const Array &codes, const Dictionary &validation) {
	Dictionary out;
	out["schema_contract"] = "semantic_link_result_v1";
	out["status"] = "INVALID";
	out["literal_requests"] = Array();
	out["validation"] = validation;
	out["diagnostic_codes"] = codes;
	out["elapsed_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - started);
	return out;
}

} // namespace

Dictionary SemanticBridge::propose(const String &raw_input, const Dictionary &analysis_result, const Dictionary &capability_snapshot, const Dictionary &options) {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	const Dictionary graph = options.get("semantic_action_graph", capability_snapshot.get("semantic_action_graph", Dictionary()));
	const Dictionary graph_validation = SemanticContract::validate_graph(graph);
	if (!bool(graph_validation.get("valid", false))) return invalid_result(started, Array::make("SBA_GRAPH_INVALID"), graph_validation);
	if (String(analysis_result.get("analysis_version", "")) != "4" || String(analysis_result.get("language_status", "")) != "english") {
		Dictionary invalid_analysis;
		invalid_analysis["valid"] = false;
		invalid_analysis["errors"] = Array::make("analysis_result");
		return invalid_result(started, Array::make("SBA_ANALYSIS_INVALID"), invalid_analysis);
	}

	const Array entries = graph.get("entries", Array());
	const Dictionary actions = capability_snapshot.get("actions", Dictionary());
	Array clauses = analysis_result.get("linguistic_clauses", Array());
	if (clauses.is_empty()) {
		Dictionary fallback;
		fallback["text"] = raw_input;
		fallback["start_index"] = 0;
		fallback["end_index"] = raw_input.length();
		fallback["negated"] = false;
		clauses.append(fallback);
	}

	Array literal_requests;
	Array diagnostics;
	bool direct_seen = false;
	bool candidate_seen = false;
	bool ambiguous_seen = false;
	for (int clause_index = 0; clause_index < clauses.size(); ++clause_index) {
		if (clauses[clause_index].get_type() != Variant::DICTIONARY) continue;
		const Dictionary linguistic = clauses[clause_index];
		const String clause = String(linguistic.get("text", "")).strip_edges();
		if (clause.is_empty()) continue;
		// Direct aliases remain owned by the deterministic compiler, but they do
		// not suppress non-overlapping semantic predicates in the same clause.
		if (has_direct_alias(clause, capability_snapshot)) direct_seen = true;
		const String lower = clause.to_lower();
		int best_entry_index = -1;
		int best_position = -1;
		int best_surface_length = -1;
		for (int entry_index = 0; entry_index < entries.size(); ++entry_index) {
			if (entries[entry_index].get_type() != Variant::DICTIONARY) continue;
			const Dictionary entry = entries[entry_index];
			const String surface = entry.get("surface", "");
			int position = find_phrase(lower, surface);
			// Verbo a particella separata: "put a muffin in my mouth" e' l'entry
			// "put in" con l'oggetto in mezzo. Il confronto contiguo non lo vede,
			// e nessun dato puo' rimediare — il campo `particle` esisteva ma
			// veniva solo ricopiato, mai usato per riconoscere (verificato il
			// 2026-09-02, ed e' cio' che teneva ferma la catena del muffin).
			const String separable_particle = entry.get("particle", "");
			if (position < 0 && !separable_particle.is_empty() && surface.length() > separable_particle.length()) {
				const String head = surface.substr(0, surface.length() - separable_particle.length()).strip_edges();
				const int head_position = head.is_empty() ? -1 : find_phrase(lower, head);
				if (head_position >= 0) {
					const int after_head = head_position + head.length();
					const String rest = lower.substr(after_head);
					const int gap = find_phrase(rest, separable_particle);
					// Fra il verbo e la sua particella non puo' esserci una
					// congiunzione: "put the box on the shelf and walk in" non e'
					// "put in", e senza questo controllo lo diventava.
					bool same_predicate = gap >= 0 && gap <= SEPARATED_PARTICLE_WINDOW;
					if (same_predicate) {
						const String between = rest.substr(0, gap);
						if (between.find(",") >= 0 || find_phrase(between, "and") >= 0
								|| find_phrase(between, "then") >= 0 || find_phrase(between, "but") >= 0
								|| find_phrase(between, "or") >= 0) same_predicate = false;
					}
					if (same_predicate) position = head_position;
				}
			}
			if (position < 0) {
			const Array tokens = linguistic.get("tokens", Array());
			const Array lemmas = linguistic.get("lemmas", Array());
			for (int token_index = 0; token_index < tokens.size() && token_index < lemmas.size(); ++token_index) {
					if (String(lemmas[token_index]).to_lower() == String(entry.get("lemma", surface)).to_lower()) {
						position = find_phrase(lower, String(tokens[token_index]));
						break;
					}
				}
			}
			if (position >= 0 && overlaps_direct_alias(clause, position, surface.length(), capability_snapshot)) continue;
			if (position >= 0 && surface.length() > best_surface_length) {
				best_entry_index = entry_index;
				best_position = position;
				best_surface_length = surface.length();
			}
		}
		if (best_entry_index < 0) {
			Dictionary request;
			const String request_id = String("literal_%03d") % static_cast<int64_t>(literal_requests.size() + 1);
			request["clause_id"] = linguistic.get("clause_id", String("clause_%03d") % static_cast<int64_t>(clause_index + 1));
				request["request_id"] = request_id; request["predicate_id"] = linguistic.get("clause_id", String("clause_%03d") % static_cast<int64_t>(clause_index + 1)); request["clause_index"] = clause_index; request["source_span"] = source_span_from_analysis(raw_input, linguistic); const String unknown_verb = unknown_predicate(linguistic, clause);
			// La posizione del predicato serve a riconoscere l'imperativo: senza,
			// `infer_speech_act` non trovava mai una richiesta in questo ramo e
			// "flurbulate the lamp" restava un'affermazione, cioe' silenzio.
			const int unknown_position = unknown_verb.is_empty() ? -1 : find_phrase(lower, unknown_verb);
			request["surface"] = unknown_verb; request["lemma"] = unknown_verb; request["particle"] = ""; request["speech_act"] = infer_speech_act(clause, analysis_result, linguistic, unknown_position); request["negated"] = bool(linguistic.get("negated", false)); request["quoted"] = false; request["reported"] = is_reported(lower); request["semantic_roles"] = linguistic.get("semantic_roles", Dictionary()); request["dependency_refs"] = linguistic.get("dependency_refs", Array()); request["candidates"] = Array();
			literal_requests.append(request);
			diagnostics.append("SBA_NO_LEXICAL_ENTRY");
			continue;
		}

		const Dictionary entry = entries[best_entry_index];
		const String surface = entry.get("surface", "");
		const String request_id = String("literal_%03d") % static_cast<int64_t>(literal_requests.size() + 1);
			const String speech_act = infer_speech_act(clause, analysis_result, linguistic, best_position);
		const bool negated = bool(linguistic.get("negated", false)) || lower.begins_with("do not ") || lower.begins_with("don't ") || lower.begins_with("never ");
		const bool quoted = is_quoted(clause, best_position);
		const bool reported = is_reported(lower);
		// The Bridge is deliberately not a second parser. It transports roles
		// and dependencies emitted by AnalysisResult v4 without reconstructing an
		// object from the remaining clause text.
		const Dictionary predicate_unit = predicate_unit_at(linguistic, best_position);
		Dictionary roles = predicate_unit.get("semantic_roles", linguistic.get("semantic_roles", Dictionary()));
		if (!roles.has("target_mode")) roles["target_mode"] = entry.get("target_mode", "optional");
		Array candidates;
		if (speech_act == "request" && !negated && !quoted && !reported) {
			const Array raw_candidates = entry.get("candidates", Array());
			for (int candidate_index = 0; candidate_index < raw_candidates.size(); ++candidate_index) {
				if (raw_candidates[candidate_index].get_type() != Variant::DICTIONARY) continue;
				const Dictionary raw_candidate = raw_candidates[candidate_index];
				const String action = raw_candidate.get("action", "");
				if (!actions.has(action)) {
					diagnostics.append(String("SBA_CAPABILITY_FILTERED:") + action);
					continue;
				}
				Dictionary candidate;
				candidate["action"] = action;
				candidate["relation"] = raw_candidate.get("relation", "APPROXIMATE_EFFECT");
				candidate["confidence"] = raw_candidate.get("confidence", 0.0);
				candidate["evidence"] = raw_candidate.get("evidence", Array());
				candidate["role_compatibility"] = raw_candidate.get("role_compatibility", 1.0);
				candidate["precondition_delta"] = raw_candidate.get("precondition_delta", Dictionary());
				candidate["effect_delta"] = raw_candidate.get("effect_delta", Dictionary());
				candidates.append(candidate);
			}
			sort_candidates(candidates);
		}
		Dictionary request;
		request["clause_id"] = linguistic.get("clause_id", String("clause_%03d") % static_cast<int64_t>(clause_index + 1));
			request["request_id"] = request_id; request["predicate_id"] = predicate_unit.get("predicate_id", linguistic.get("clause_id", "")); request["clause_index"] = clause_index; request["source_span"] = source_span_from_analysis(raw_input, linguistic); request["predicate_span"] = source_span(raw_input, lower.substr(best_position, surface.length()), int(linguistic.get("start_index", 0)) + best_position); request["surface"] = surface; request["lemma"] = entry.get("lemma", surface); request["particle"] = entry.get("particle", ""); request["speech_act"] = speech_act; request["negated"] = negated; request["quoted"] = quoted; request["reported"] = reported; request["semantic_roles"] = roles; request["dependency_refs"] = predicate_unit.get("dependency_refs", linguistic.get("dependency_refs", Array())); request["candidates"] = candidates;
			literal_requests.append(request);
			if (speech_act == "statement" && is_non_directive_future(lower, linguistic)) diagnostics.append("SBA_NON_DIRECTIVE_FUTURE");
		if (!candidates.is_empty()) candidate_seen = true;
		if (candidates.size() > 1) {
			const double first = Dictionary(candidates[0]).get("confidence", 0.0);
			const double second = Dictionary(candidates[1]).get("confidence", 0.0);
			if (first - second < 0.20) ambiguous_seen = true;
		}
		// A clause may contain several unrelated predicates. Preserve each
		// non-overlapping Analyzer span instead of silently granting the longest
		// lexical graph entry exclusive authority.
		Array accepted_predicates;
		accepted_predicates.append(Array::make(best_position, best_position + surface.length()));
		for (int extra_index = 0; extra_index < entries.size(); ++extra_index) {
			if (extra_index == best_entry_index || entries[extra_index].get_type() != Variant::DICTIONARY) continue;
			const Dictionary extra_entry = entries[extra_index];
			const String extra_surface = extra_entry.get("surface", "");
			const int extra_position = find_phrase(lower, extra_surface);
			if (extra_position < 0 || overlaps_direct_alias(clause, extra_position, extra_surface.length(), capability_snapshot)) continue;
			bool overlaps_accepted = false;
			for (int span_index = 0; span_index < accepted_predicates.size(); ++span_index) {
				const Array accepted = accepted_predicates[span_index];
				if (extra_position < int(accepted[1]) && int(accepted[0]) < extra_position + extra_surface.length()) { overlaps_accepted = true; break; }
			}
			if (overlaps_accepted) continue;
			Array extra_candidates;
			if (speech_act == "request" && !negated && !quoted && !reported) {
				const Array raw_extra_candidates = extra_entry.get("candidates", Array());
				for (int candidate_index = 0; candidate_index < raw_extra_candidates.size(); ++candidate_index) {
					if (raw_extra_candidates[candidate_index].get_type() != Variant::DICTIONARY) continue;
					const Dictionary raw_candidate = raw_extra_candidates[candidate_index];
					const String action = raw_candidate.get("action", "");
					if (!actions.has(action)) { diagnostics.append(String("SBA_CAPABILITY_FILTERED:") + action); continue; }
					Dictionary extra_candidate;
					extra_candidate["action"] = action; extra_candidate["relation"] = raw_candidate.get("relation", "APPROXIMATE_EFFECT"); extra_candidate["confidence"] = raw_candidate.get("confidence", 0.0); extra_candidate["evidence"] = raw_candidate.get("evidence", Array()); extra_candidate["role_compatibility"] = raw_candidate.get("role_compatibility", 1.0); extra_candidate["precondition_delta"] = raw_candidate.get("precondition_delta", Dictionary()); extra_candidate["effect_delta"] = raw_candidate.get("effect_delta", Dictionary());
					extra_candidates.append(extra_candidate);
				}
				sort_candidates(extra_candidates);
			}
			const Dictionary extra_unit = predicate_unit_at(linguistic, extra_position);
			Dictionary extra_roles = extra_unit.get("semantic_roles", linguistic.get("semantic_roles", Dictionary()));
			if (!extra_roles.has("target_mode")) extra_roles["target_mode"] = extra_entry.get("target_mode", "optional");
			Dictionary extra_request;
			const String extra_request_id = String("literal_%03d") % static_cast<int64_t>(literal_requests.size() + 1);
			extra_request["clause_id"] = linguistic.get("clause_id", String("clause_%03d") % static_cast<int64_t>(clause_index + 1));
			extra_request["request_id"] = extra_request_id; extra_request["predicate_id"] = extra_unit.get("predicate_id", linguistic.get("clause_id", "")); extra_request["clause_index"] = clause_index; extra_request["source_span"] = source_span_from_analysis(raw_input, linguistic); extra_request["predicate_span"] = source_span(raw_input, lower.substr(extra_position, extra_surface.length()), int(linguistic.get("start_index", 0)) + extra_position); extra_request["surface"] = extra_surface; extra_request["lemma"] = extra_entry.get("lemma", extra_surface); extra_request["particle"] = extra_entry.get("particle", ""); extra_request["speech_act"] = speech_act; extra_request["negated"] = negated; extra_request["quoted"] = is_quoted(clause, extra_position); extra_request["reported"] = reported; extra_request["semantic_roles"] = extra_roles; extra_request["dependency_refs"] = extra_unit.get("dependency_refs", linguistic.get("dependency_refs", Array())); extra_request["candidates"] = extra_candidates;
			literal_requests.append(extra_request);
			accepted_predicates.append(Array::make(extra_position, extra_position + extra_surface.length()));
			if (!extra_candidates.is_empty()) candidate_seen = true;
			if (extra_candidates.size() > 1 && double(Dictionary(extra_candidates[0]).get("confidence", 0.0)) - double(Dictionary(extra_candidates[1]).get("confidence", 0.0)) < 0.20) ambiguous_seen = true;
		}
	}

	sort_literal_requests(literal_requests);
	Dictionary out;
	out["schema_contract"] = "semantic_link_result_v1";
	out["status"] = ambiguous_seen ? "AMBIGUOUS" : candidate_seen ? "CANDIDATES" : direct_seen ? "NOT_NEEDED" : "NO_LINK";
	out["literal_requests"] = literal_requests;
	out["diagnostic_codes"] = diagnostics;
	out["elapsed_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - started);
	Dictionary validation = SemanticContract::validate_link_result(out, capability_snapshot);
	out["validation"] = validation;
	if (!bool(validation.get("valid", false))) out["status"] = "INVALID";
	return out;
}

} // namespace norucore
