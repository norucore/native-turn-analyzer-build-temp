# Native Turn Analyzer

Proprietary C++ GDExtension for NoruCore. It parses English turns completely offline and emits `AnalysisResult v4` plus `deterministic_turn_compiler_v2`. It never grounds world objects, authorizes policy, or mutates the Persona; those responsibilities remain in `Persona_Brain`.

The current source candidate is `0.5.1-rc.3-semantic-runtime`. Its optional
`semantic_link_result_v1` proposes typed links against the caller's live
capability snapshot. Runtime arbitration may select a candidate only after the
Brain repeats capability, grounding, policy, consequence and GOAP gates; the
Bridge itself never executes an action. The approved packaged cross-platform
release remains `0.4.2-semantic-guards` until all six target artifacts and
export smoke gates are regenerated.

## Supported release targets

- Godot 4.6+
- Windows x86-64
- macOS ARM64
- Linux x86-64 (glibc)

Compatibility with later Godot 4.x versions is declared only after the matching CI and Engine smoke tests pass.
The bundled bindings are pinned to the stable `godot-4.5-stable` tag; Godot's
GDExtension compatibility contract allows extensions targeting an earlier 4.x
minor to load on later minors. Godot 4.6 is still the minimum declared by the
`.gdextension` manifest and the runtime test matrix.

## Local macOS build

```sh
../mio_progetto_env/bin/scons platform=macos arch=arm64 target=template_debug -j4
../mio_progetto_env/bin/scons platform=macos arch=arm64 target=template_release -j4
```

Build debug and release sequentially. Parallel SCons processes share generated
state and can produce an invalid library even when both commands report success.
The committed build profile limits godot-cpp generation to the four Engine
classes used by this extension.

The libraries are copied to `project/bin/macos/`. Product builds use the matching files under `llm_project_0.6/addons/native_turn_analyzer/bin/<platform>/`.

## Tests

Run from `project/` with Godot 4.6:

```sh
godot --headless --script native_semantic_tests.gd
godot --headless --script tests/native_corpus_contract_tests.gd
godot --headless --script tests/native_corpus_replay.gd
godot --headless --script tests/native_fuzz_property_tests.gd
godot --headless --script tests/native_user_log_replay.gd
godot --headless --script tests/native_semantic_bridge_tests.gd
godot --headless --script tests/native_semantic_bridge_performance_tests.gd
```

The corpus contains 820 cases. The property suite runs 100,000 deterministic inputs. The app-level smoke test additionally verifies the real Persona capability registry and Brain handoff.
The app-level smoke also checks canonical reachability for every action in the live Persona catalog (currently 148/148), so adding a GOAP capability cannot silently leave the native analyzer unable to address it.

## Packaging

The `.gdextension` file selects one native library for the current platform. End users receive the correct library inside their platform-specific export and do not install build tools or runtime packages.

`godot-cpp` is MIT-licensed. Its notice and every future third-party notice must remain in the distributed `licenses/` directory.

The compact semantic graph is generated offline from Princeton WordNet 3.0 by
`tools/generate_semantic_action_graph.py` (one Python step, no compilation). The
generator verifies the anchored synsets, traverses at most two semantic edges,
applies the reviewed TSV overrides and emits a byte-identical file on every run;
`--check` fails when the committed graph differs from a fresh generation. Only the derived
graph and `project/licenses/WORDNET-3.0.txt` are packaged.
