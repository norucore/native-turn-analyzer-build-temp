#include "semantic_contract.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

namespace norucore {

namespace {

Dictionary validation_result(const Array &errors) {
	Dictionary validation;
	validation["valid"] = errors.is_empty();
	validation["errors"] = errors;
	return validation;
}

bool valid_relation(const String &relation) {
	return relation == "EQUIVALENT" || relation == "SPECIALIZATION" || relation == "APPROXIMATE_EFFECT" || relation == "ENABLES" || relation == "ENTAILS" || relation == "CONFLICTS";
}

bool valid_span(const Variant &value) {
	if (value.get_type() != Variant::DICTIONARY) return false;
	const Dictionary span = value;
	if (!span.has("start_index") || !span.has("end_index") || !span.has("text")) return false;
	const int start = span.get("start_index", -1);
	const int end = span.get("end_index", -1);
	return start >= 0 && end >= start && String(span.get("text", "")).length() == end - start;
}

bool valid_delta(const Variant &value) {
	if (value.get_type() != Variant::DICTIONARY) return false;
	const Dictionary delta = value;
	for (const Variant key : delta.keys()) if (String(key).strip_edges().is_empty()) return false;
	return true;
}

} // namespace

Dictionary SemanticContract::validate_graph(const Dictionary &graph) {
	Array errors;
	if (String(graph.get("schema_contract", "")) != "semantic_action_graph_v1") errors.append("schema_contract");
	if (!graph.has("version") || String(graph.get("version", "")).is_empty()) errors.append("version");
	if (!graph.has("provenance") || graph.get("provenance", Variant()).get_type() != Variant::DICTIONARY) errors.append("provenance");
	if (!graph.has("entries") || graph.get("entries", Variant()).get_type() != Variant::ARRAY) {
		errors.append("entries");
		return validation_result(errors);
	}
	const Array entries = graph.get("entries", Array());
	for (int i = 0; i < entries.size(); ++i) {
		if (entries[i].get_type() != Variant::DICTIONARY) {
			errors.append(String("entry_not_dictionary:") + String::num_int64(i));
			continue;
		}
		const Dictionary entry = entries[i];
		if (String(entry.get("surface", "")).strip_edges().is_empty()) errors.append(String("entry_surface:") + String::num_int64(i));
		if (entry.get("candidates", Variant()).get_type() != Variant::ARRAY) {
			errors.append(String("entry_candidates:") + String::num_int64(i));
			continue;
		}
		const Array candidates = entry.get("candidates", Array());
		for (int j = 0; j < candidates.size(); ++j) {
			if (candidates[j].get_type() != Variant::DICTIONARY) {
				errors.append(String("candidate_not_dictionary:") + String::num_int64(i) + ":" + String::num_int64(j));
				continue;
			}
			const Dictionary candidate = candidates[j];
			if (String(candidate.get("action", "")).strip_edges().is_empty()) errors.append(String("candidate_action:") + String::num_int64(i) + ":" + String::num_int64(j));
			if (!valid_relation(String(candidate.get("relation", "")))) errors.append(String("candidate_relation:") + String::num_int64(i) + ":" + String::num_int64(j));
			const double confidence = candidate.get("confidence", -1.0);
			if (confidence < 0.0 || confidence > 1.0) errors.append(String("candidate_confidence:") + String::num_int64(i) + ":" + String::num_int64(j));
		}
	}
	return validation_result(errors);
}

Dictionary SemanticContract::validate_link_result(const Dictionary &result, const Dictionary &capability_snapshot) {
	Array errors;
	if (String(result.get("schema_contract", "")) != "semantic_link_result_v1") errors.append("schema_contract");
	const String status = result.get("status", "");
	if (!(status == "NOT_NEEDED" || status == "CANDIDATES" || status == "NO_LINK" || status == "AMBIGUOUS" || status == "INVALID")) errors.append("status");
	if (result.has("execute") || result.has("selected_action")) errors.append("forbidden_authority_field");
	if (result.get("literal_requests", Variant()).get_type() != Variant::ARRAY) {
		errors.append("literal_requests");
		return validation_result(errors);
	}
	const Dictionary actions = capability_snapshot.get("actions", Dictionary());
	const Array requests = result.get("literal_requests", Array());
	for (int i = 0; i < requests.size(); ++i) {
		if (requests[i].get_type() != Variant::DICTIONARY) {
			errors.append(String("literal_request:") + String::num_int64(i));
			continue;
		}
		const Dictionary request = requests[i];
		for (const char *key : {"request_id", "clause_id", "predicate_id", "clause_index", "source_span", "surface", "lemma", "speech_act", "negated", "quoted", "reported", "semantic_roles", "dependency_refs", "candidates"}) {
			if (!request.has(key)) errors.append(String("request_missing:") + key + ":" + String::num_int64(i));
		}
		if (String(request.get("request_id", "")).strip_edges().is_empty()) errors.append(String("request_id:") + String::num_int64(i));
		if (String(request.get("clause_id", "")).strip_edges().is_empty()) errors.append(String("clause_id:") + String::num_int64(i));
		if (String(request.get("predicate_id", "")).strip_edges().is_empty()) errors.append(String("predicate_id:") + String::num_int64(i));
		if (int(request.get("clause_index", -1)) < 0) errors.append(String("clause_index:") + String::num_int64(i));
		if (!valid_span(request.get("source_span", Variant()))) errors.append(String("source_span:") + String::num_int64(i));
		if (request.has("predicate_span") && !valid_span(request.get("predicate_span", Variant()))) errors.append(String("predicate_span:") + String::num_int64(i));
		if (request.get("semantic_roles", Variant()).get_type() != Variant::DICTIONARY) errors.append(String("semantic_roles:") + String::num_int64(i));
		if (request.get("dependency_refs", Variant()).get_type() != Variant::ARRAY) errors.append(String("dependency_refs:") + String::num_int64(i));
		const bool actionable = !bool(request.get("negated", false)) && !bool(request.get("quoted", false)) && !bool(request.get("reported", false)) && String(request.get("speech_act", "")) == "request";
		const Array candidates = request.get("candidates", Array());
		if (!actionable && !candidates.is_empty()) errors.append(String("non_actionable_candidates:") + String::num_int64(i));
		for (int j = 0; j < candidates.size(); ++j) {
			if (candidates[j].get_type() != Variant::DICTIONARY) {
				errors.append(String("request_candidate:") + String::num_int64(i) + ":" + String::num_int64(j));
				continue;
			}
			const Dictionary candidate = candidates[j];
			const String action = candidate.get("action", "");
			if (!actions.has(action)) errors.append(String("action_not_in_snapshot:") + action);
			if (!valid_relation(String(candidate.get("relation", "")))) errors.append(String("request_candidate_relation:") + String::num_int64(i) + ":" + String::num_int64(j));
			const double confidence = candidate.get("confidence", -1.0);
			const double role_compatibility = candidate.get("role_compatibility", -1.0);
			if (confidence < 0.0 || confidence > 1.0) errors.append(String("request_candidate_confidence:") + String::num_int64(i) + ":" + String::num_int64(j));
			if (role_compatibility < 0.0 || role_compatibility > 1.0) errors.append(String("request_candidate_role_compatibility:") + String::num_int64(i) + ":" + String::num_int64(j));
			if (!valid_delta(candidate.get("precondition_delta", Variant())) || !valid_delta(candidate.get("effect_delta", Variant()))) errors.append(String("request_candidate_delta:") + String::num_int64(i) + ":" + String::num_int64(j));
			if (candidate.has("execute") || candidate.has("selected_action")) errors.append("candidate_forbidden_authority_field");
		}
	}
	return validation_result(errors);
}

} // namespace norucore
