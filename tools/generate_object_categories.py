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

Un nome con piu' sensi prende l'**unione** delle categorie di tutti i suoi sensi
coperti, fermandosi ai primi `MAX_SENSE_RANK` (WordNet ordina i sensi per frequenza
in `index.noun`): il limite e' quello che impedisce a "grape" di diventare un'arma
(grapeshot) e a "revolver" di diventare una porta (revolving door). Il rango conta
**solo i sensi concreti** (`CONCRETE_ROOTS`: oggetti e materia, cioe' anche cibi e
liquidi): "mug" come persona credulona o "ring" come suono non sono oggetti in scena
e non spingono indietro il boccale o l'anello.

Fino al 2026-09-21 valeva il **solo senso migliore**: le radici che combaciavano con
un senso piu' raro venivano scartate. Quella regola rendeva ogni radice larga
pericolosa — aggiungendo `container#1`, 18 nomi *perdevano* le categorie che avevano,
perche' la radice larga vinceva al primo senso e cancellava quella precisa che vinceva
al secondo — e quindi spingeva a correggere parola per parola invece che per ramo
(`Analizzatore#118`, `#120`, scelta dell'utente del 2026-09-21). Con l'unione
aggiungere un ramo e' additivo: misurato, 163 nomi cambiano e **nessuno perde niente**.

L'ordine e' per rango, poi dalla radice piu' profonda alla meno profonda: e' l'ordine
d'importanza che il motore usa per gli indizi fisici (i pantaloni sono `wearable_legs`
prima che `wearable_body`), e mettere il senso piu' frequente davanti fa si' che la
lista di prima resti il **prefisso** di quella nuova, cioe' che taglia e campi fisici
non cambino per nessuno. Delle categorie di taglia (`SIZE_CATEGORIES`) resta solo la
prima: una poltrona e' `heavy` anche se sta sotto `chair`, che e' `portable`.

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
import re

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


# Parole che distinguono un senso dagli altri dello stesso nome: la definizione di WordNet
# (senza le frasi d'esempio) piu' i nomi del suo antenato diretto. Niente frasi d'esempio:
# servono a misurare la scelta, e usarle qui renderebbe la misura circolare.
SENSE_STOPWORDS = frozenset("""a an the of or and in to for with on used is are as by from that it its this which
someone something who whose their his her they he she you we not no but can may at be been was were has have had
into out up down over under more most other others such same very any each many one two often usually especially""".split())


# 30 parole e non 12: misurato sui casi in cui WordNet porta una frase d'esempio, con 30 il
# meccanismo decide 56 volte invece di 45 e azzecca il 66% invece del 64%, e i nomi senza
# nessun appiglio scendono da 62 a 42. Costa una manciata di KB.
def sense_keywords(synsets, offset: str, name: str, limit: int = 30) -> list:
    definition = synsets[offset][2].split('"')[0]
    words = []
    for token in re.findall(r"[a-z]+", definition.lower()):
        if token in SENSE_STOPWORDS or len(token) < 3 or token in name.split(" "):
            continue
        if token not in words:
            words.append(token)
    # Antenato diretto e discendenti. I discendenti portano la parola che la scena usa
    # davvero: il senso "mazza" di `bat` ha come discendente "baseball bat", ed e' da li'
    # che arriva "baseball" — che nella definizione ("a club used for hitting a ball in
    # various games") non compare.
    neighbours = []
    for symbol, target, pos in synsets[offset][1]:
        if pos != "n" or target not in synsets:
            continue
        if symbol in ("@", "@i") and not neighbours:
            neighbours.append(target)
    for symbol, target, pos in synsets[offset][1]:
        if symbol in ("~", "~i") and pos == "n" and target in synsets:
            neighbours.append(target)
    for target in neighbours:
        for word in names_of(synsets, target):
            for token in word.split(" "):
                if token not in words and token not in SENSE_STOPWORDS and len(token) >= 3:
                    words.append(token)
    return words[:limit]


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

    # Quale RAMO manca, non quale parola.
    #
    # Fino al 2026-09-21 questa funzione elencava i nomi scoperti uno per uno, e chi doveva
    # chiudere il buco sceglieva a occhio quale radice aggiungere: si lavorava sul sintomo.
    # Un nome scoperto pero' non e' un caso isolato — e' un ramo dell'albero di WordNet che
    # nessuna radice tocca. Qui i mancanti si raggruppano per il loro antenato, e il ramo si
    # legge con quanti nomi frequenti porterebbe dentro: le radici si aggiungono per prova,
    # non per aneddoto.
    #
    # Attenzione a non inseguire il 100%: fra i mancanti ci sono "work", "system", "means",
    # "way", "thing", che oggetti non sono. Il numero da guardare e' quanti nomi un ramo
    # copre, non quanti ne restano fuori.
    ancestors = collections.Counter()
    carried = collections.defaultdict(list)
    for name in missing:
        for offset in index.get(name.replace(" ", "_"), []):
            if offset not in concrete:
                continue
            # Solo i due livelli sopra il nome. Risalire fino in cima raggruppa tutto sotto
            # `entity`, che come radice non serve a niente: il ramo utile e' quello appena
            # sopra il buco.
            level, frontier = 0, [offset]
            while frontier and level < 2:
                level += 1
                nxt = []
                for current in frontier:
                    for symbol, target, pos in synsets[current][1]:
                        if symbol in ("@", "@i") and pos == "n" and target in synsets:
                            nxt.append(target)
                            if name not in carried[target]:
                                ancestors[target] += 1
                                carried[target].append(name)
                frontier = nxt
            break
    print("\nrami scoperti, per quanti nomi frequenti porterebbero dentro:")
    shown, used = 0, set()
    for offset, count in ancestors.most_common():
        if shown >= 12:
            break
        names = [n for n in carried[offset] if n not in used]
        if len(names) < 2:
            continue
        head = synsets[offset][0][0].replace("_", " ")
        senses = index.get(head.replace(" ", "_"), [])
        rank = senses.index(offset) + 1 if offset in senses else 0
        gloss = synsets[offset][2].split('"')[0].strip(" ;")[:40]
        reach = len(hyponyms(synsets, offset, set()))
        print(f"  {len(names):3d} nomi | {head}#{rank:<3} | {reach:5d} nomi nel ramo | {gloss:<42} | {', '.join(names[:6])}")
        used.update(names)
        shown += 1


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
                noun_roots[name].append((rank, root_depth, key, categories, offset))

    generated = {}
    for name in sorted(noun_roots):
        usable = [item for item in noun_roots[name] if item[0] <= MAX_SENSE_RANK]
        if not usable:
            continue
        ordered = []
        for _rank, _root_depth, _key, categories, _offset in sorted(usable, key=lambda item: (item[0], -item[1], item[2])):
            for category in categories:
                if category in SIZE_CATEGORIES and any(c in SIZE_CATEGORIES for c in ordered):
                    continue
                if category not in ordered:
                    ordered.append(category)
        generated[name] = ordered
    # Sensi alternativi, per la scelta guidata dalla scena (`Analizzatore#116`, alternativa A
    # approvata dall'utente il 2026-09-20).
    #
    # `generated` resta esattamente com'era: e' cio' che vale quando la scena non dice niente,
    # e continua a fermarsi ai primi `MAX_SENSE_RANK` sensi. Qui accanto si scrivono gli ALTRI
    # sensi concreti dello stesso nome, anche piu' profondi, con le parole che li distinguono.
    # Il motore puo' sceglierne uno solo se le parole della scena lo sostengono: il limite dei
    # tre sensi resta la rete di sicurezza, il contesto puo' superarlo soltanto con delle prove.
    #
    # Si scrivono solo i nomi in cui i sensi portano categorie DIVERSE: dove la scelta non
    # cambia cosa si puo' fare con l'oggetto, non serve a niente e peserebbe e basta.
    # Misurato su 2.321 casi con la verita' di WordNet (le frasi d'esempio dei synset):
    # scegliere sempre il senso piu' frequente azzecca il 38,3%, guardare la scena il 60,7%.
    senses = {}
    for name in sorted(noun_roots):
        by_sense = {}
        for rank, root_depth, key, categories, offset in noun_roots[name]:
            entry = by_sense.setdefault(offset, {"rank": rank, "categories": []})
            for category in categories:
                if category in SIZE_CATEGORIES and any(c in SIZE_CATEGORIES for c in entry["categories"]):
                    continue
                if category not in entry["categories"]:
                    entry["categories"].append(category)
        # Due casi meritano di essere scritti: un nome i cui sensi portano categorie diverse,
        # e un nome che il limite dei tre sensi lascia **senza** categorie del tutto. Il
        # secondo e' "bat": in WordNet il pipistrello e' il primo senso e la mazza il quinto,
        # quindi oggi in un libro di baseball la mazza non esiste. Con la scena che parla di
        # palle e guanti, esiste.
        distinct = len({tuple(e["categories"]) for e in by_sense.values()})
        if distinct < 2 and name in generated:
            continue
        rows = []
        for offset, entry in sorted(by_sense.items(), key=lambda item: item[1]["rank"]):
            keywords = sense_keywords(synsets, offset, name)
            if keywords:
                rows.append({"rank": entry["rank"], "categories": entry["categories"], "keywords": keywords})
        # Una riga sola basta quando il nome altrimenti non avrebbe nessuna categoria: e'
        # il caso di "bat", coperto solo al quarto senso concreto. Se invece un default c'e'
        # gia', scrivere un senso solo non aggiunge nessuna scelta.
        if len(rows) > 1 or (rows and name not in generated):
            senses[name] = rows
    catalogue["senses"] = senses
    catalogue["generated"] = generated
    args.catalogue.write_text(json.dumps(catalogue, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
    per_category = collections.Counter(c for cats in generated.values() for c in cats)
    print(f"nouns={len(generated)} overrides={len(catalogue.get('overrides', {}))}")
    for category, count in sorted(per_category.items(), key=lambda item: -item[1]):
        print(f"  {category:<20} {count}")


if __name__ == "__main__":
    main()
