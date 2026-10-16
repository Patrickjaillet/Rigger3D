#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Génère CHANGELOG.md depuis les sections « Implémenté » de ROADMAP.md (une entrée par phase).

Usage : python3 tools/gen_changelog.py [--check] [--root .]
  --check : échoue si CHANGELOG.md n'est pas à jour (CI).
"""
import argparse
import re
import sys
from pathlib import Path

VERSION = "1.0.0"
DATE = "2026-10-16"
MAX_ITEM = 220


def first_sentence(text: str) -> str:
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) <= MAX_ITEM:
        return text
    cut = text.rfind(". ", 0, MAX_ITEM)
    if cut < 60:
        cut = text.rfind(" ", 0, MAX_ITEM - 1)
        return text[:cut] + " …"
    return text[: cut + 1]


def parse(roadmap: str):
    phases = []
    current = None
    in_impl = False
    for line in roadmap.splitlines():
        m = re.match(r"^## PHASE (\d+) — (.+)$", line)
        if m:
            current = {"n": int(m.group(1)), "title": m.group(2).strip(), "items": []}
            phases.append(current)
            in_impl = False
            continue
        if current is None:
            continue
        if line.startswith("## "):
            current = None
            continue
        if line.startswith("**Implémenté :**"):
            in_impl = True
            rest = line[len("**Implémenté :**"):].strip()
            if rest:
                current["items"].append(first_sentence(rest))
            continue
        if in_impl:
            if line.startswith("- "):
                current["items"].append(first_sentence(line[2:]))
            elif line.strip() and not line.startswith(" "):
                in_impl = False
    return [p for p in phases if p["items"]]


def render(phases) -> str:
    out = [
        "# Changelog",
        "",
        "Tous les changements notables de Rigger. Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),",
        "versionnage [sémantique](https://semver.org/lang/fr/). Ce fichier est **généré** par `tools/gen_changelog.py`",
        "à partir de `ROADMAP.md` : ne pas l'éditer à la main.",
        "",
        f"## [{VERSION}] - {DATE}",
        "",
        "Première version publique.",
        "",
    ]
    for p in phases:
        out.append(f"### Phase {p['n']} — {p['title']}")
        out.append("")
        out.extend(f"- {item}" for item in p["items"])
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    root = Path(args.root)
    roadmap = root / "ROADMAP.md"
    if not roadmap.is_file():
        # ROADMAP.md est un document local, non versionné : sans lui, le CHANGELOG.md commité fait foi.
        print("ROADMAP.md absent : contrôle du CHANGELOG ignoré" if args.check else "ROADMAP.md introuvable")
        return 0 if args.check else 1
    text = render(parse(roadmap.read_text(encoding="utf-8")))
    target = root / "CHANGELOG.md"
    if args.check:
        if not target.exists() or target.read_text(encoding="utf-8") != text:
            print("CHANGELOG.md n'est pas à jour : python3 tools/gen_changelog.py")
            return 1
        return 0
    target.write_text(text, encoding="utf-8", newline="\n")
    print(f"CHANGELOG.md : {len(text.splitlines())} lignes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
