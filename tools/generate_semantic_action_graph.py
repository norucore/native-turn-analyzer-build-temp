#!/usr/bin/env python3
"""Genera `semantic_action_graph_v1.json` da WordNet 3.0, in un passo solo.

A che serve. Il grafo dice all'analizzatore quali verbi inglesi valgono come
un'azione GOAP ("retrieve" -> pickup). Non si scrive a mano: si genera, e
rigenerarlo deve dare **lo stesso file byte per byte**. E' questo che rende
verificabile ogni modifica (tracker Analizzatore #52).

Fino al 2026-09-14 i passi erano due — `generate_semantic_inputs.py` e un
generatore C++ da compilare — e il grafo distribuito non si otteneva da nessuno
dei due: lo script scriveva confidenze 0.90/0.80 e relazione SPECIALIZATION,
il grafo aveva 0.96/0.88 e EQUIVALENT. Il C++ andava compilato sulla macchina
dell'utente, che le compilazioni le vuole solo su GitHub. Adesso e' uno script.

Le regole, nessun elenco di parole:
  1. l'azione e' ancorata al primo senso WordNet in cui e' la **parola capofila**
     del synset e il cui lexname e' ammesso per la **famiglia GOAP** (la cartella
     in cui l'autore ha messo l'azione); i sinonimi vengono da tutti quei sensi;
  2. un sinonimo che e' il nome di un'altra azione non diventa sinonimo;
  3. relazione, confidenza e compatibilita' delle righe generate stanno nei dati,
     nelle due righe `generated` di `semantic_overrides.tsv`, non nel codice;
  4. un verbo composto prende la particella quando la sua ultima parola e' un
     avverbio in WordNet (`index.adv`: "lead astray", "drop behind"): solo una
     particella avverbiale si stacca dal verbo ("lead him astray"); "call for" e
     "think of" hanno una preposizione, che non si stacca, e restano senza;
  5. le righe scritte a mano (`semantic_anchors.tsv`, `semantic_overrides.tsv`)
     vincono sempre, e una riga `forbid` toglie la coppia verbo -> azione;
  6. un sinonimo generato passa solo per un senso con cui la parola si usa davvero: il
     primo, o uno osservato nel corpus (`sense_in_use`). "sell" non e' "betray".

Uso, dalla radice del workspace:

    python3 "Godot C++/mio_plugin/tools/generate_semantic_action_graph.py" \\
        --dict WordNet-3.0/dict \\
        --goap llm_project_0.6/assets/code/Resource/Persona/Goap_resource \\
        --out "Godot C++/mio_plugin/project/data/semantic_action_graph_v1.json"

`--check` confronta invece di scrivere ed esce 1 se il file cambierebbe.
La procedura di download di WordNet (URL e sha256) sta in
`llm_project_0.6/LLM_Wiky/wiki/meta/native-analyzer-rebuild-procedure.md`.
"""
import argparse
import collections
import hashlib
import pathlib
import sys

TOOLS = pathlib.Path(__file__).resolve().parent
ARCHIVE_SHA256 = "640db279c949a88f61f851dd54ebbb22d003f8b90b85267042ef85a3781d3a52"
MAX_RELATION_DEPTH = 2

# Famiglia GOAP (la cartella dell'autore) -> lexname di WordNet ammessi.
FAMILY_LEX = {
    "BodyPositioning":              {"verb.body", "verb.motion", "verb.contact", "verb.stative"},
    "CombatConflict":               {"verb.competition", "verb.contact", "verb.body"},
    "CommunicationExpression":      {"verb.communication"},
    "EmotionalExpressionRegulation": {"verb.emotion", "verb.body", "verb.communication"},
    "EquipmentWearing":             {"verb.body", "verb.contact", "verb.change"},
    "Locomotion":                   {"verb.motion"},
    "Magia":                        {"verb.change", "verb.creation", "verb.social"},
    "MetaActions":                  {"verb.cognition", "verb.stative", "verb.social"},
    "ObjectAcquisitionDisposition": {"verb.possession", "verb.contact", "verb.change"},
    "ObjectInteractionTransformation": {"verb.change", "verb.contact", "verb.creation"},
    "PerceptionCognition":          {"verb.perception", "verb.cognition"},
    "PhysicalForceManipulation":    {"verb.contact", "verb.motion"},
    "SocialInteraction":            {"verb.social", "verb.communication", "verb.possession"},
    "StealthConcealment":           {"verb.perception", "verb.motion", "verb.contact"},
    "SurvivalSelfCare":             {"verb.body", "verb.consumption", "verb.stative"},
}


def read_verbs(dict_dir: pathlib.Path):
    lexnames = {}
    for line in open(dict_dir / "lexnames", encoding="latin-1"):
        fields = line.strip().split("\t")
        if len(fields) >= 2:
            lexnames[int(fields[0])] = fields[1]
    index = {}
    attested = {}
    for line in open(dict_dir / "index.verb", encoding="latin-1"):
        if not line or line.startswith(" "):
            continue
        fields = line.split()
        index.setdefault(fields[0], fields[5 + int(fields[3]) + 1:])
        # `tagsense_cnt`: quanti sensi della parola compaiono nel corpus etichettato. I sensi
        # sono in ordine di frequenza, quindi quelli dal numero `tagsense_cnt` in poi non sono
        # mai stati osservati nell'uso.
        attested.setdefault(fields[0], int(fields[5 + int(fields[3])]))
    synsets = {}
    for line in open(dict_dir / "data.verb", encoding="latin-1"):
        if not line or line[0] == " ":
            continue
        fields = [f for f in line.split(" | ")[0].split(" ") if f]
        if len(fields) < 5:
            continue
        word_count = int(fields[3], 16)
        words = [fields[4 + 2 * i] for i in range(word_count)]
        cursor = 4 + 2 * word_count
        links = []
        for _ in range(int(fields[cursor])):
            links.append((fields[cursor + 1], fields[cursor + 2]))
            cursor += 4
        synsets[fields[0]] = {"lex": lexnames.get(int(fields[1]), "?"), "words": words, "links": links}
    adverbs = set()
    for line in open(dict_dir / "index.adv", encoding="latin-1"):
        if line and not line.startswith(" "):
            adverbs.add(line.split()[0])
    return index, synsets, adverbs, attested


def sense_in_use(word: str, offset: str, index: dict, attested: dict) -> bool:
    """Vero se `offset` e' un senso con cui `word` si usa davvero: il piu' frequente, o uno
    osservato nel corpus di WordNet.

    Senza questa regola il grafo mappava una parola attraverso un suo senso raro, e quel senso
    decideva l'azione per tutti gli usi: "sell" arrivava a `betray` (ottavo senso, "tradire"),
    "fly" a `flee` (undicesimo), "save" a `write` (tracker Analizzatore #110). Misurato il
    2026-09-23: 72 associazioni su 217 passavano per un senso mai osservato.
    """
    senses = index.get(word.lower(), [])
    if offset not in senses:
        return False
    rank = senses.index(offset)
    # Con meno di due sensi osservati l'ordine di frequenza non dice niente ("glower" ha un solo
    # senso etichettato): la regola vale solo per le parole il cui uso e' documentato.
    observed = attested.get(word.lower(), 0)
    return rank == 0 or observed < 2 or rank < observed


def read_rows(path: pathlib.Path):
    return [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines() if line.strip() and not line.startswith("#")]


def relation_path(source: str, target: str, adjacency: dict) -> list:
    # Cammino piu' corto fra due sensi, entro MAX_RELATION_DEPTH, con i legami in
    # ordine stabile: e' l'evidenza scritta nel grafo, quindi deve essere sempre la stessa.
    if source == target:
        return []
    pending = collections.deque([(source, [])])
    visited = {source}
    while pending:
        offset, path = pending.popleft()
        if len(path) >= MAX_RELATION_DEPTH:
            continue
        for symbol, next_offset in sorted(adjacency.get(offset, [])):
            if next_offset in visited:
                continue
            next_path = path + [symbol]
            if next_offset == target:
                return next_path
            visited.add(next_offset)
            pending.append((next_offset, next_path))
    return ["override-only"]


def json_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def json_list(csv: str) -> str:
    return "[" + ",".join(json_string(v) for v in csv.split(",") if v) + "]"


def generate(dict_dir: pathlib.Path, goap_dir: pathlib.Path, generalize: int = 0) -> str:
    index, synsets, adverbs, attested = read_verbs(dict_dir)
    actions = {path.stem: path.parent.name for path in goap_dir.rglob("*.tres")}

    hand_anchors = read_rows(TOOLS / "semantic_anchors.tsv")
    hand_override_rows = read_rows(TOOLS / "semantic_overrides.tsv")
    policy = {row[1]: row for row in hand_override_rows if row[0] == "generated"}
    if not {"anchor_sense", "other_sense"} <= set(policy):
        raise SystemExit("semantic_overrides.tsv needs the two 'generated' rows: anchor_sense and other_sense")
    hand_overrides = [row for row in hand_override_rows if row[0] != "generated"]
    hand_anchor_actions = {row[0] for row in hand_anchors}
    hand_pairs = {(row[1], row[5]) for row in hand_overrides}

    anchors = [row[:7] for row in hand_anchors]
    generated = []
    for action, folder in sorted(actions.items()):
        if action in hand_anchor_actions or folder not in FAMILY_LEX:
            continue
        lemma = action.lower()
        kept = [o for o in index.get(lemma, []) if synsets[o]["words"] and synsets[o]["words"][0].lower() == lemma and synsets[o]["lex"] in FAMILY_LEX[folder]]
        if not kept:
            continue
        anchor_offset = kept[0]
        anchors.append([action, anchor_offset, folder.lower(), "object", "", "conditional", ""])
        for offset in kept:
            for word in synsets[offset]["words"]:
                surface = word.lower().replace("_", " ")
                if surface == lemma.replace("_", " ") or (surface, action) in hand_pairs:
                    continue
                if "'" in surface or any(c.isdigit() for c in surface):
                    continue
                if surface.replace(" ", "_") in actions:
                    continue
                if not sense_in_use(word, offset, index, attested):
                    continue
                rule = policy["anchor_sense" if offset == anchor_offset else "other_sense"]
                words = surface.split(" ")
                particle = words[-1] if len(words) > 1 and words[-1] in adverbs else ""
                generated.append(["promote", surface, word, particle, offset, action, rule[6], rule[7], rule[8], "", ""])

    # Generalizzazione: un verbo che in WordNet e' un MODO di fare un'azione che abbiamo
    # ("sprint" e' un modo di "travel", cioe' move_to) vale come quell'azione, con confidenza
    # piu' bassa perche' la meccanica e' quella generale e il modo lo racconta il narratore.
    # Serve a coprire la coda lunga dei verbi senza scrivere elenchi: la gerarchia la sa gia'.
    # Spento per default: senza `--generalize` il file generato resta identico byte per byte.
    if generalize > 0 and "generalization" in policy:
        rule = policy["generalization"]
        for action, anchor_offset, *_ in sorted(anchors, key=lambda row: row[0]):
            frontier, depth, visited = [anchor_offset], 0, {anchor_offset}
            while frontier and depth < generalize:
                depth += 1
                nxt = []
                for offset in frontier:
                    for symbol, target in synsets[offset]["links"]:
                        if symbol not in ("~", "~i") or target in visited or target not in synsets:
                            continue
                        visited.add(target)
                        nxt.append(target)
                        for word in synsets[target]["words"]:
                            surface = word.lower().replace("_", " ")
                            if "'" in surface or any(c.isdigit() for c in surface):
                                continue
                            if surface.replace(" ", "_") in actions or (surface, action) in hand_pairs:
                                continue
                            # Solo il senso piu' usato della parola. Senza questo vincolo
                            # "cut" arrivava a `write` (nel senso di incidere un disco) e
                            # "fill" a `eat`: la gerarchia collega sensi rari che nessuno
                            # intende. E' la stessa regola del catalogo oggetti.
                            senses = index.get(word.lower(), [])
                            if not senses or senses[0] != target:
                                continue
                            words = surface.split(" ")
                            particle = words[-1] if len(words) > 1 and words[-1] in adverbs else ""
                            generated.append(["promote", surface, word, particle, target, action, rule[6], rule[7], rule[8], "", ""])
                frontier = nxt

    seen, overrides = set(), list(hand_overrides)
    for row in generated:
        if (row[1], row[5]) not in seen:
            seen.add((row[1], row[5]))
            overrides.append(row)

    anchors.sort(key=lambda row: row[0])
    anchor_by_action = {row[0]: row for row in anchors}
    for row in overrides:
        if row[4] not in synsets:
            raise SystemExit("unknown source synset " + row[4])
        if row[5] not in anchor_by_action:
            raise SystemExit("unknown action " + row[5])
        if row[2] not in synsets[row[4]]["words"]:
            raise SystemExit("lemma/synset mismatch " + row[2])
    overrides.sort(key=lambda row: (row[1], row[5]))

    adjacency = collections.defaultdict(list)
    for offset in sorted(synsets):
        for symbol, target in synsets[offset]["links"]:
            if target not in synsets:
                continue
            adjacency[offset].append((symbol, target))
            adjacency[target].append(("reverse:" + symbol, offset))

    out = ['{\n  "schema_contract": "semantic_action_graph_v1",\n  "version": "wordnet-3.0-goap-v1",\n  "provenance": {\n    "source": "Princeton WordNet 3.0",\n    "archive": "WordNet-3.0.tar.gz",\n    "archive_sha256": ' + json_string(ARCHIVE_SHA256) + ',\n    "max_relation_depth": 2,\n    "inputs": ["index.verb","data.verb","verb.exc"],\n    "license": "WordNet-3.0"\n  },\n  "action_metadata": {\n']
    for i, a in enumerate(anchors):
        out.append("    " + json_string(a[0]) + ': {"concept_ids":[' + json_string("wn30-v-" + a[1]) + '],"semantic_family":' + json_string(a[2]) + ',"role_signature":' + json_list(a[3]) + ',"effect_tags":' + json_list(a[4]) + ',"reversibility":' + json_string(a[5]) + ',"risk_tags":' + json_list(a[6]) + ',"explicit_relations":[]}' + ("," if i + 1 < len(anchors) else "") + "\n")
    out.append('  },\n  "entries": [\n')
    # Una voce per superficie, con un candidato per ogni senso: "draw" e' tirare e anche
    # sfoderare, e fra i due sceglie l'oggetto nel Cervello (tracker Analizzatore #50).
    by_surface = collections.OrderedDict()
    for o in overrides:
        if o[0] != "forbid":
            by_surface.setdefault(o[1], []).append(o)
    entries = []
    for rows in by_surface.values():
        first = rows[0]
        candidates = []
        for o in rows:
            path = relation_path(o[4], anchor_by_action[o[5]][1], adjacency)
            evidence = [json_string("wordnet:wn30-v-" + o[4]), json_string("override:" + o[0])] + [json_string("wordnet-relation:" + edge) for edge in path]
            candidates.append('{"action":' + json_string(o[5]) + ',"relation":' + json_string(o[6]) + ',"confidence":' + "%.2f" % float(o[7]) + ',"role_compatibility":' + "%.2f" % float(o[8]) + ',"evidence":[' + ",".join(evidence) + '],"precondition_delta":{"summary":' + json_string(o[9]) + '},"effect_delta":{"summary":' + json_string(o[10]) + "}}")
        entries.append('    {"surface":' + json_string(first[1]) + ',"lemma":' + json_string(first[2]) + ',"particle":' + json_string(first[3]) + ',"concept_id":' + json_string("wn30-v-" + first[4]) + ',"target_mode":"optional","candidates":[' + ",".join(candidates) + "]}")
    out.append(",\n".join(entries))
    out.append('\n  ],\n  "forbidden_relations": [' + ",".join('{"surface":' + json_string(o[1]) + ',"action":' + json_string(o[5]) + "}" for o in overrides if o[0] == "forbid") + "]\n}\n")
    return "".join(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dict", required=True, type=pathlib.Path)
    parser.add_argument("--goap", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--generalize", type=int, default=0, help="profondita' dei troponimi da mappare sull'azione (0 = spento)")
    args = parser.parse_args()
    graph = generate(args.dict, args.goap, args.generalize)
    digest = hashlib.sha256(graph.encode("utf-8")).hexdigest()
    if args.check:
        same = args.out.exists() and args.out.read_text(encoding="utf-8") == graph
        print(("unchanged " if same else "DIFFERS ") + digest)
        sys.exit(0 if same else 1)
    args.out.write_text(graph, encoding="utf-8")
    print("written " + digest)


if __name__ == "__main__":
    main()
