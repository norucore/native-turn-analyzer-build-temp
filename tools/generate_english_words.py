#!/usr/bin/env python3
"""Genera l'insieme delle parole inglesi da WordNet 3.0.

A che serve, e perche' non e' un elenco di parole scritto a mano.

Quando l'utente nomina una cosa, l'app deve distinguere due casi che si somigliano:
"day" o "trouble" sono parole inglesi che non sono oggetti (si ignorano in
silenzio), "waffer" non e' una parola inglese (un refuso: l'utente va avvisato,
tracker Analizzatore #79). Lo dice WordNet: tutti i lemmi di nomi, verbi, aggettivi
e avverbi, piu' le forme irregolari dei file `.exc` ("mice" -> "mouse").

Uso, dalla cartella che contiene `WordNet-3.0/`:

    python3 generate_english_words.py \\
        --dict WordNet-3.0/dict \\
        --out ../../llm_project_0.6/assets/config/english_words.json
"""
import argparse
import json
import pathlib


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dict", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    words, inflections = set(), {}
    for part in ["noun", "verb", "adj", "adv"]:
        for line in open(args.dict / ("index." + part), encoding="latin-1"):
            if line and not line.startswith(" "):
                lemma = line.split()[0].lower()
                if "_" not in lemma:
                    words.add(lemma)
        for line in open(args.dict / (part + ".exc"), encoding="latin-1"):
            fields = line.split()
            if len(fields) >= 2 and "_" not in fields[0]:
                inflections.setdefault(fields[0].lower(), fields[1].lower())
    payload = {
        "schema": "english_words_v1",
        "source": "Princeton WordNet 3.0, index.noun/verb/adj/adv and the .exc files",
        "words": sorted(words),
        "inflections": dict(sorted(inflections.items())),
    }
    args.out.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print("words=%d inflections=%d -> %s" % (len(words), len(inflections), args.out))


if __name__ == "__main__":
    main()
