#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace norucore {

class SemanticContract {
public:
	static godot::Dictionary validate_graph(const godot::Dictionary &graph);
	static godot::Dictionary validate_link_result(const godot::Dictionary &result, const godot::Dictionary &capability_snapshot);
};

} // namespace norucore
