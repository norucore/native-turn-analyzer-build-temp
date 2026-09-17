#!/usr/bin/env python3
"""Genera la sezione `generated` di `object_categories.json` da WordNet 3.0.

A che serve. Il personaggio deve sapere che su un divano ci si siede e che una
spada non si indossa, per **qualunque** nome l'utente scriva, non solo per i
settanta scritti a mano. Non si catalogano oggetti: si scelgono poche radici di
WordNet per ogni categoria (`object_category_roots.tsv`) e WordNet porta tutti i
nomi che ci stanno sotto.

Perche' le radici si scelgono a mano e l'uscita si rivede. WordNet sbaglia per
larghezza: sotto `container` ci sono le ambulanze, sotto `seat` il `penalty box`.
Ogni radice ha la sua riga con il senso esatto (lemma#numero) e i sottoalberi da
escludere. **Una correzione si fa qui, come regola su un sottoalbero, mai come
nome scritto nel JSON**: `overrides` contiene solo esclusioni (liste vuote), cioe'
nomi che la revisione ha trovato sbagliati e che devono fallire con il motivo.
Decisione dell'utente del 2026-09-14 (tracker Analizzatore #74): il catalogo non
deve diventare un elenco di parole chiave.

Un nome con piu' sensi prende le categorie **solo dal suo senso piu' comune** fra
quelli coperti (WordNet ordina i sensi per frequenza in `index.noun`), e solo se
quel senso e' fra i primi `MAX_SENSE_RANK`: senza questa regola "grape" diventava
un'arma (grapeshot) e "revolver" una porta (revolving door). Il rango conta **solo
i sensi concreti** (`CONCRETE_ROOTS`: oggetti e materia, cioe' anche cibi e liquidi): "mug" come persona
credulona o "ring" come suono non sono oggetti in scena e non spingono indietro
il boccale o l'anello.

Dentro quel senso, un nome sotto piu' radici prende l'unione delle categorie,
ordinate dalla radice piu' profonda alla meno profonda: e' l'ordine d'importanza
che il motore usa per gli indizi fisici (i pantaloni sono `wearable_legs` prima
che `wearable_body`). Delle categorie di taglia (`SIZE_CATEGORIES`) resta solo
quella della radice piu' profonda: una poltrona e' `heavy` anche se sta sotto
`chair`, che e' `portable`.

Uso:

    python3 generate_object_categories.py --dict WordNet-3.0/dict \\
        --roots object_category_roots.tsv \\
        --catalogue ../../../llm_project_0.6/assets/config/object_categories.json

    python3 generate_object_categories.py --dict WordNet-3.0/dict --explore glass table

    python3 generate_object_categories.py --dict WordNet-3.0/dict --coverage 400 \
        --catalogue ../../../llm_project_0.6/assets/config/object_categories.json

La copertura non e' su una lista scelta da noi: sono i nomi di oggetti concreti
(sotto `artifact#1`, `food#2`, `beverage#1`) piu' frequenti nell'inglese annotato
di WordNet (`index.sense`, conteggio dei sensi). Stampa la percentuale e i nomi
scoperti, che sono la lista di lavoro per la revisione.

La procedura di download di WordNet (URL e sha256) sta in
`llm_project_0.6/LLM_Wiky/wiki/meta/native-analyzer-rebuild-procedure.md`.
"""
import argparse
import collections
import json
import pathlib

MAX_SENSE_RANK = 3
CONCRETE_ROOTS = ("object#1", "matter#3")
SIZE_CATEGORIES = ("handheld", "portable", "heavy", "fixed_in_place")


def read_nouns(dict_dir: pathlib.Path):
    synsets = {}
    with open(dict_dir / "data.noun", encoding="latin-1") as handle:
        for line in handle:
            if line.startswith("  "):
                continue
            head, _, gloss = line.partition(" | ")
            fields = head.split()
            offset, word_count = fields[0], int(fields[3], 16)
            words = [fields[4 + 2 * i] for i in range(word_count)]
            i = 4 + 2 * word_count
            pointer_count = int(fields[i])
            i += 1
            pointers = []
            for _ in range(pointer_count):
                pointers.append((fields[i], fields[i + 1], fields[i + 2]))
                i += 4
            synsets[offset] = (words, pointers, gloss.strip())
    index = {}
    with open(dict_dir / "index.noun", encoding="latin-1") as handle:
        for line in handle:
            if line.startswith(" "):
                continue
            fields = line.split()
            pointer_types = int(fields[3])
            index[fields[0]] = fields[4 + pointer_types + 2:]
    return synsets, index


def resolve(index, key: str) -> str:
    lemma, _, sense = key.partition("#")
    offsets = index.get(lemma.replace(" ", "_"))
    if not offsets or not sense.isdigit() or int(sense) > len(offsets):
        raise SystemExit(f"root not found in WordNet: {key}")
    return offsets[int(sense) - 1]


def hyponyms(synsets, offset: str, stop: set) -> set:
    seen, todo = {offset}, [offset]
    while todo:
        for symbol, target, pos in synsets[todo.pop()][1]:
            if symbol in ("~", "~i") and pos == "n" and target not in seen and target not in stop:
                seen.add(target)
                todo.append(target)
    return seen


def depth(synsets, offset: str) -> int:
    best, frontier = 0, [(offset, 0)]
    seen = set()
    while frontier:
        current, level = frontier.pop()
        if current in seen:
            continue
        seen.add(current)
        best = max(best, level)
        for symbol, target, pos in synsets[current][1]:
            if symbol in ("@", "@i") and pos == "n":
                frontier.append((target, level + 1))
    return best


def names_of(synsets, offset: str) -> list:
    out = []
    for word in synsets[offset][0]:
        # Nomi propri (maiuscole), marcatori di aggettivo ("(a)") e cifre non sono
        # nomi di oggetti che l'utente scrive per indicare una cosa in scena. Unica
        # maiuscola ammessa: la lettera che fa da forma ("T-shirt", "X-ray").
        capital_shape = len(word) > 2 and word[0].isupper() and word[1] == "-" and word[2:] == word[2:].lower()
        if (word != word.lower() and not capital_shape) or "(" in word or any(ch.isdigit() for ch in word):
            continue
        out.append(word.replace("_", " ").lower())
    return out


def read_roots(path: pathlib.Path):
    roots = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) < 2:
            raise SystemExit(f"{path}:{number}: expected root<TAB>categories[<TAB>exclusions]")
        exclusions = [c.strip() for c in cells[2].split(";") if c.strip()] if len(cells) > 2 else []
        roots.append((cells[0].strip(), [c.strip() for c in cells[1].split(",") if c.strip()], exclusions))
    return roots


def explore(synsets, index, lemmas):
    for lemma in lemmas:
        for sense, offset in enumerate(index.get(lemma.replace(" ", "_"), []), 1):
            size = len(hyponyms(synsets, offset, set()))
            print(f"{lemma}#{sense}  hyponyms={size:<5} {synsets[offset][2][:110]}")


def coverage(args, synsets, index):
    catalogue = json.loads(args.catalogue.read_text(encoding="utf-8"))
    known = set(catalogue.get("overrides", {})) | set(catalogue.get("generated", {}))
    concrete = set()
    for key in ("artifact#1", "food#2", "beverage#1"):
        concrete |= hyponyms(synsets, resolve(index, key), set())
    counts = collections.Counter()
    with open(args.dict / "index.sense", encoding="latin-1") as handle:
        for line in handle:
            sense_key, offset, _, tag_count = line.split()
            lemma, _, rest = sense_key.partition("%")
            if not rest.startswith("1:") or offset not in concrete or int(tag_count) == 0:
                continue
            name = lemma.replace("_", " ")
            if name == name.lower() and not any(ch.isdigit() for ch in name):
                counts[name] = max(counts[name], int(tag_count))
    top = [name for name, _ in counts.most_common(args.coverage)]
    missing = [name for name in top if name not in known and not (name.endswith("s") and name[:-1] in known)]
    print(f"coverage: {len(top) - len(missing)}/{len(top)} most frequent concrete nouns classified")
    print("missing:", ", ".join(missing))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dict", required=True, type=pathlib.Path)
    parser.add_argument("--roots", type=pathlib.Path)
    parser.add_argument("--catalogue", type=pathlib.Path)
    parser.add_argument("--explore", nargs="*")
    parser.add_argument("--coverage", type=int)
    args = parser.parse_args()
    synsets, index = read_nouns(args.dict)
    if args.explore:
        explore(synsets, index, args.explore)
        return
    if args.coverage:
        coverage(args, synsets, index)
        return

    catalogue = json.loads(args.catalogue.read_text(encoding="utf-8"))
    corrections = {name: cats for name, cats in catalogue.get("overrides", {}).items() if cats}
    if corrections:
        raise SystemExit(f"overrides may only exclude (empty list); move these corrections into the roots: {sorted(corrections)}")
    known_categories = set(catalogue["categories"])
    concrete = set()
    for key in CONCRETE_ROOTS:
        concrete |= hyponyms(synsets, resolve(index, key), set())
    noun_roots = collections.defaultdict(list)
    for key, categories, exclusions in read_roots(args.roots):
        unknown = [c for c in categories if c not in known_categories]
        if unknown:
            raise SystemExit(f"root {key}: unknown categories {unknown}")
        root = resolve(index, key)
        stop = set()
        for excluded in exclusions:
            stop |= hyponyms(synsets, resolve(index, excluded), set())
        root_depth = depth(synsets, root)
        for offset in hyponyms(synsets, root, stop):
            for name in names_of(synsets, offset):
                senses = [o for o in index.get(name.replace(" ", "_"), []) if o in concrete]
                rank = senses.index(offset) + 1 if offset in senses else len(senses) + 1
                noun_roots[name].append((rank, root_depth, key, categories))

    generated = {}
    for name in sorted(noun_roots):
        best_rank = min(item[0] for item in noun_roots[name])
        if best_rank > MAX_SENSE_RANK:
            continue
        ordered = []
        for _, _, _, categories in sorted((item for item in noun_roots[name] if item[0] == best_rank), key=lambda item: (-item[1], item[2])):
            for category in categories:
                if category in SIZE_CATEGORIES and any(c in SIZE_CATEGORIES for c in ordered):
                    continue
                if category not in ordered:
                    ordered.append(category)
        generated[name] = ordered
    catalogue["generated"] = generated
    args.catalogue.write_text(json.dumps(catalogue, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
    per_category = collections.Counter(c for cats in generated.values() for c in cats)
    print(f"nouns={len(generated)} overrides={len(catalogue.get('overrides', {}))}")
    for category, count in sorted(per_category.items(), key=lambda item: -item[1]):
        print(f"  {category:<20} {count}")


if __name__ == "__main__":
    main()
