#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace norucore {

class NativeTurnAnalyzer : public godot::RefCounted {
	GDCLASS(NativeTurnAnalyzer, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Dictionary analyze_turn(const godot::String &raw_input, const godot::Dictionary &scene_state = {}, const godot::Dictionary &options = {}) const;
	godot::Dictionary compile_turn(const godot::String &raw_input, const godot::Dictionary &analysis_result, const godot::Dictionary &capability_snapshot, const godot::Dictionary &scene_state = {}, const godot::Dictionary &options = {}) const;
	godot::Dictionary analyze_and_compile(const godot::String &raw_input, const godot::Dictionary &capability_snapshot, const godot::Dictionary &scene_state = {}, const godot::Dictionary &options = {}) const;
	godot::Dictionary propose_semantic_links(const godot::String &raw_input, const godot::Dictionary &analysis_result, const godot::Dictionary &capability_snapshot, const godot::Dictionary &options = {}) const;
	godot::Dictionary validate_contract(const godot::Dictionary &result) const;
	godot::Dictionary get_build_info() const;

private:
	static constexpr int MAX_INPUT_CHARS = 8192;
	static constexpr int MAX_TOKENS = 1024;
	static constexpr int MAX_FRAMES = 32;

	godot::String normalize(const godot::String &input) const;
	godot::Array tokenize(const godot::String &input) const;
	godot::String lemma(const godot::String &word) const;
	// Forme irregolari: la tabella di base scritta qui, costruita una volta, piu' quella
	// di WordNet che il registro dell'app manda nel capability snapshot.
	mutable godot::Dictionary base_irregular_forms;
	mutable godot::Dictionary supplied_irregular_forms;
	mutable godot::Dictionary supplied_verb_lemmas;
	void absorb_irregular_forms(const godot::Dictionary &capability_snapshot) const;
	int find_lemma_phrase_start(const godot::String &text, const godot::String &phrase) const;
	godot::Array split_clauses(const godot::String &input) const;
	godot::String infer_clause_speech_act(const godot::String &text, const godot::Array &tokens, const godot::Array &lemmas) const;
	godot::Dictionary extract_clause_roles(const godot::String &text, const godot::Array &tokens, const godot::Array &lemmas, const godot::String &speech_act) const;
	godot::Array split_action_coordinations(const godot::Array &clauses, const godot::Dictionary &registry, const godot::Dictionary &semantic_graph = {}) const;
	godot::Array build_compositional_frames(
		int first_index,
		const godot::String &raw,
		const godot::Dictionary &registry,
		const godot::Array &previous_frames,
		const godot::String &normalized_input,
		int span_start,
		int span_end
	) const;
	godot::Dictionary build_frame(int index, const godot::String &raw, const godot::Dictionary &registry, const godot::Array &previous_frames) const;
};

} // namespace norucore

using NativeTurnAnalyzer = norucore::NativeTurnAnalyzer;
