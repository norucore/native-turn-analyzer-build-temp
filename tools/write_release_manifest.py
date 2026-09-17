#!/usr/bin/env python3
"""Scrive RELEASE_MANIFEST.json dalle librerie vere, invece di tenerlo a mano.

Fino al 2026-09-14 i manifest dell'addon erano scritti a mano e fermi a luglio:
dimensioni e SHA-256 non corrispondevano piu' a nessuna libreria installata
(tracker Analizzatore #53). Il file serve a `release-preview.yml`, che ne legge
la versione, e a chi deve sapere quali binari sono stati approvati.

Uso, dopo aver installato le librerie di un build CI:

    python3 tools/write_release_manifest.py \\
        --addon ../../llm_project_0.6/addons/native_turn_analyzer \\
        --version 0.5.5-analyzer-round --commit <sha del plugin> --run <id del build>
"""
import argparse
import datetime
import hashlib
import json
import pathlib

PLATFORMS = {"macos": "arm64", "linux": "x86_64", "windows": "x86_64"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--addon", required=True, type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--run", required=True)
    args = parser.parse_args()
    binaries = []
    for platform, architecture in PLATFORMS.items():
        for template in ("debug", "release"):
            found = sorted((args.addon / "bin" / platform).glob(f"*native_turn_analyzer.{platform}.template_{template}.{architecture}.*"))
            found = [f for f in found if f.suffix in (".dylib", ".so", ".dll")]
            if len(found) != 1:
                raise SystemExit(f"expected one {platform} {template} library, found {found}")
            data = found[0].read_bytes()
            binaries.append({
                "platform": platform, "architecture": architecture, "template": template,
                "path": str(found[0].relative_to(args.addon)), "size_bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            })
    manifest = {
        "manifest_version": "native_turn_analyzer_release_manifest_v1",
        "generated_at": datetime.date.today().isoformat(),
        "plugin": {
            "name": "native_turn_analyzer", "version": args.version, "godot_minimum": "4.6", "language": "en",
            "analysis_contract": "AnalysisResult v4", "compiler_contract": "deterministic_turn_compiler_v2",
            "packet_contract": "native_turn_packet_v1", "semantic_link_contract": "semantic_link_result_v1",
        },
        "provenance": {"plugin_commit": args.commit, "build_run": args.run, "repository": "norucore/native-turn-analyzer-build"},
        "binaries": binaries,
    }
    (args.addon / "RELEASE_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{len(binaries)} libraries -> {args.addon / 'RELEASE_MANIFEST.json'}")


if __name__ == "__main__":
    main()
