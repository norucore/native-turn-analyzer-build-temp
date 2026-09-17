#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace norucore {

class SemanticBridge {
public:
	static godot::Dictionary propose(
		const godot::String &raw_input,
		const godot::Dictionary &analysis_result,
		const godot::Dictionary &capability_snapshot,
		const godot::Dictionary &options
	);
};

} // namespace norucore
