# Semantic action graph provenance

`semantic_action_graph_v1.json` is a deterministic, compact derivative of Princeton WordNet 3.0 verb data, anchored only around NoruCore GOAP capabilities.

- Source archive: `WordNet-3.0.tar.gz`
- Official URL: `https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.gz`
- SHA-256: `640db279c949a88f61f851dd54ebbb22d003f8b90b85267042ef85a3781d3a52`
- Inputs consumed: `dict/lexnames`, `dict/index.verb`, `dict/data.verb`, `dict/index.adv`, and the GOAP action folders of the app
- Generator: `tools/generate_semantic_action_graph.py`
- Hand-written rows (always win): `tools/semantic_anchors.tsv`, `tools/semantic_overrides.tsv` (including the two `generated` rows that set relation and confidence of every generated synonym)
- Maximum semantic relation depth: 2
- Generated graph SHA-256: `0070f5eb3985731f18a46f7696af362d526ca1461e6298a72280b11320d21502`
- The same file ships in `llm_project_0.6/addons/native_turn_analyzer/data/`; the two copies must stay identical.

Regenerate and verify:

```sh
python3 tools/generate_semantic_action_graph.py --dict WordNet-3.0/dict \
    --goap ../../llm_project_0.6/assets/code/Resource/Persona/Goap_resource \
    --out project/data/semantic_action_graph_v1.json --check
```

The full WordNet archive is not redistributed. The required WordNet license is preserved in `project/licenses/WORDNET-3.0.txt`.
