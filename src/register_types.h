#ifndef NATIVE_TURN_ANALYZER_REGISTER_TYPES_H
#define NATIVE_TURN_ANALYZER_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

// Le firme vere, con il livello d'inizializzazione: quelle senza argomenti non
// avevano definizione (tracker #55).
void initialize_gdextension_types(godot::ModuleInitializationLevel p_level);
void uninitialize_gdextension_types(godot::ModuleInitializationLevel p_level);

#endif // NATIVE_TURN_ANALYZER_REGISTER_TYPES_H
