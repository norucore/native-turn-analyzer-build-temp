#!/usr/bin/env python3
"""Genera `phrasal_verbs.json` da WordNet 3.0: le locuzioni verbali che cambiano senso.

A che serve. "take apart" non e' "take", "shut off" non e' "shut", "wipe out" non e' "wipe".
L'analizzatore pero' toglie la particella dal bersaglio — e deve farlo, altrimenti "put the box
back" avrebbe per bersaglio "the box back" (tracker #31, #84, #103) — e cosi' la locuzione
collassa sul verbo di testa, che viene eseguito con aria di esserci riuscito. Misurato il
2026-09-21: 149 scambi sbagliati su 293, tutti questo schema (`Analizzatore#123`).

Non e' un elenco scritto a mano: WordNet conosce `take_apart`, `shut_off`, `wipe_out` come lemmi
verbali a se'. Qui si estraggono tutte le locuzioni `testa_coda` in cui:

- la coda e' una delle particelle che l'analizzatore gia' toglie (classe chiusa, la stessa lista
  che sta in `native_turn_analyzer.cpp`: se il C++ la toglie, qui va riconosciuta);
- la testa e' a sua volta un verbo, cioe' e' proprio il caso in cui puo' collassare.

Chi legge il file decide poi che farne: se la locuzione e' un alias di un'azione, si esegue quella;
se non lo e', non si esegue la testa, si dichiara di non aver capito.

    python3 generate_phrasal_verbs.py --dict ~/Library/Caches/NoruCore/WordNet-3.0/dict \
        --out ../../../llm_project_0.6/assets/config/phrasal_verbs.json
"""
import argparse
import json
import pathlib

# La classe chiusa che `native_turn_analyzer.cpp` toglie gia' dal bersaglio: particelle
# (`strip_target_particles`) e complementi risultativi. Tenerle allineate e' il punto.
TAILS = ("back", "away", "up", "down", "over", "out", "off",
         "shut", "closed", "open", "loose", "free", "apart", "flat", "clean")


def verb_lemmas(dict_dir: pathlib.Path) -> set:
    out = set()
    with open(dict_dir / "index.verb", encoding="latin-1") as handle:
        for line in handle:
            if not line.startswith(" "):
                out.add(line.split()[0])
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dict", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    lemmas = verb_lemmas(args.dict)
    heads = {}
    for lemma in sorted(lemmas):
        parts = lemma.split("_")
        if len(parts) != 2 or parts[1] not in TAILS or parts[0] not in lemmas:
            continue
        heads.setdefault(parts[0], []).append(parts[1])
    payload = {"schema": "phrasal_verbs_v1", "source": "WordNet 3.0 index.verb", "heads": heads}
    args.out.write_text(json.dumps(payload, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"teste={len(heads)} locuzioni={sum(len(v) for v in heads.values())} -> {args.out}")


if __name__ == "__main__":
    main()
