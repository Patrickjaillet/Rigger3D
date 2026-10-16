#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Conformité de licence : en-têtes SPDX (GPL-3.0-or-later) dans chaque fichier source, fichiers LICENSE / COPYING,
et anonymat (aucune mention d'outil d'IA dans les sources, documents et scripts livrés).

Usage : license_headers.py --root <racine> [--fix]
  sans --fix : vérifie et échoue (code 1) si un fichier est non conforme ;
  --fix      : ajoute l'en-tête manquant (idempotent).
"""
import argparse
import re
import sys
from pathlib import Path

SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"
COPYRIGHT = "Copyright (C) 2026 Patrick JAILLET"

# extension / nom -> préfixe de commentaire (None : bloc /* */)
LINE_COMMENT = {".cpp": "//", ".hpp": "//", ".h": "//", ".in": "//", ".rc": "//", ".py": "#", ".cmake": "#", ".yml": "#"}
SKIP_DIRS = {"build", "third_party", ".git", ".vscode", ".vs", ".idea", "out", "__pycache__"}
# Documents locaux, exclus du dépôt (.git/info/exclude) : non livrés, donc non contrôlés pour l'anonymat.
LOCAL_ONLY = {"ROADMAP.md", "CLAU" + "DE.md"}
# Les motifs sont écrits en deux morceaux pour que ce script ne se détecte pas lui-même.
FORBIDDEN = re.compile("|".join(["clau" + "de", "anthro" + "pic", "chat" + "gpt", "open" + "ai", "copi" + "lot", "co-auth" + "ored-by"]), re.I)
TEXT_SUFFIXES = {".cpp", ".hpp", ".h", ".in", ".rc", ".py", ".cmake", ".yml", ".qss", ".md", ".txt", ".json", ".qrc", ".ts", ".svg"}
# Fichiers générés ou textes tiers : pas d'en-tête, et pas de contrôle d'anonymat sur le contenu cité.
NO_HEADER = {"THIRD_PARTY_LICENSES.txt", "LICENSE", "COPYING"}


def files(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        yield path


def header_for(path: Path):
    if path.suffix in LINE_COMMENT:
        p = LINE_COMMENT[path.suffix]
        return f"{p} {SPDX}\n{p} {COPYRIGHT}\n"
    if path.name == "CMakeLists.txt":
        return f"# {SPDX}\n# {COPYRIGHT}\n"
    if path.suffix == ".qss":
        return f"/* {SPDX}\n * {COPYRIGHT} */\n"
    return None


def has_header(text: str) -> bool:
    return SPDX in "\n".join(text.splitlines()[:6])


def fix_text(path: Path, text: str) -> str:
    header = header_for(path)
    lines = text.splitlines(keepends=True)
    prefix = ""
    if path.suffix == ".py":  # shebang et déclaration d'encodage restent en tête
        while lines and (lines[0].startswith("#!") or re.match(r"#.*coding[:=]", lines[0])):
            prefix += lines.pop(0)
    return prefix + header + ("\n" if lines and lines[0].strip() else "") + "".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--fix", action="store_true")
    args = ap.parse_args()
    root = Path(args.root).resolve()
    errors = []

    for name in ("LICENSE", "COPYING"):
        f = root / name
        if not f.is_file() or "GNU GENERAL PUBLIC LICENSE" not in f.read_text(encoding="utf-8", errors="replace")[:400]:
            errors.append(f"{name} : texte de la GPL-3.0 manquant à la racine")

    for path in files(root):
        rel = path.relative_to(root).as_posix()
        if path.name in NO_HEADER or path.name in LOCAL_ONLY and path.parent == root:
            continue
        header = header_for(path)
        if header is not None:
            text = path.read_text(encoding="utf-8")
            if not has_header(text):
                if args.fix:
                    path.write_text(fix_text(path, text), encoding="utf-8", newline="\n")
                else:
                    errors.append(f"{rel} : en-tête SPDX manquant")
        if path.suffix in TEXT_SUFFIXES or path.name == "CMakeLists.txt":
            text = path.read_text(encoding="utf-8", errors="replace")
            m = FORBIDDEN.search(text)
            if m:
                errors.append(f"{rel} : mention interdite « {m.group(0)} » (anonymat)")

    for e in errors:
        print(e)
    if not errors:
        print("conformité de licence : OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
