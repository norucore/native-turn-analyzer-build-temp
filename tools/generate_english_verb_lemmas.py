#!/usr/bin/env python3
"""Genera l'elenco dei lemmi verbali inglesi da WordNet 3.0.

A che serve, e perche' non e' un elenco di parole scritto a mano.

L'avviso "verbo non riconosciuto" ha bisogno di sapere una cosa sola che
l'analizzatore non sa: **se la parola che l'utente ha messo in testa alla frase
sia un verbo inglese.** Senza quella distinzione l'avviso si accende su
`goodnight Sasha`, `lol, ok fine.` e `so overall...` — misurato il 2026-09-10:
9 accensioni su 60 turni reali, di cui 8 sbagliate.

WordNet 3.0 ha 11.529 lemmi verbali. Non sono parole scelte da noi: sono il
vocabolario dell'inglese, e la stessa fonte da cui e' gia' derivato
`semantic_action_graph_v1.json`. Il file prodotto risponde a "questa parola e'
un verbo?", non a "questa parola e' una nostra azione?".

Uso, dalla cartella che contiene `WordNet-3.0/`:

    python3 generate_english_verb_lemmas.py \
        --dict WordNet-3.0/dict \
        --out ../../llm_project_0.6/assets/config/english_verb_lemmas.json

La procedura di download di WordNet (URL e sha256) sta in
`llm_project_0.6/LLM_Wiky/wiki/meta/native-analyzer-rebuild-procedure.md`.
"""
import argparse
import json
import pathlib

# Ausiliari e modali: classe chiusa della grammatica inglese, non una lista di
# parole scelte. Sono verbi a tutti gli effetti in WordNet, ma non nominano mai
# l'azione che l'utente chiede — `do u ever get bored` ha `do` come predicato.
# Tenerli fuori toglie l'ultimo grosso falso positivo senza toccare nient'altro.
AUXILIARIES = [
    "be", "have", "do",
    "can", "could", "may", "might", "must", "shall", "should", "will", "would",
]


def read_verb_lemmas(dict_dir: pathlib.Path) -> list[str]:
    out = []
    with open(dict_dir / "index.verb", encoding="latin-1") as handle:
        for line in handle:
            if not line or line.startswith(" "):
                continue
            out.append(line.split()[0].lower())
    return sorted(set(out))


def read_irregular_forms(dict_dir: pathlib.Path, lemmas: list[str]) -> dict:
    # Forme irregolari da `verb.exc` ("drew" -> "draw"). Una forma che e' anche un
    # lemma verbale a se' ("lay", "saw", "found", "fell") non si riduce: in testa a
    # una frase e' un imperativo di quel verbo, e ridurla lo farebbe diventare
    # racconto. Aggiunto il 2026-09-14 (tracker Analizzatore #5, #15, #50): e' la
    # tabella che l'analizzatore nativo usa al posto della sua, scritta a mano.
    known = set(lemmas)
    out = {}
    with open(dict_dir / "verb.exc", encoding="latin-1") as handle:
        for line in handle:
            fields = line.split()
            if len(fields) < 2 or fields[0] in known or "_" in fields[0] or "-" in fields[0]:
                continue
            out.setdefault(fields[0], fields[1])
    return dict(sorted(out.items()))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dict", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    lemmas = read_verb_lemmas(args.dict)
    payload = {
        "schema": "english_verb_lemmas_v1",
        "source": "Princeton WordNet 3.0, index.verb and verb.exc",
        "auxiliaries": AUXILIARIES,
        "lemmas": lemmas,
        "irregular_forms": read_irregular_forms(args.dict, lemmas),
    }
    args.out.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"{len(lemmas)} lemmi verbali -> {args.out}")


if __name__ == "__main__":
    main()
