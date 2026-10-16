#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Génère THIRD_PARTY_LICENSES.txt : licences des bibliothèques tierces (sources FetchContent, police Inter, Qt).

Usage : gen_third_party.py [--deps build/debug/_deps] [--output THIRD_PARTY_LICENSES.txt] [--check]
  --check : échoue si le fichier existant diffère du fichier régénéré (utilisé en CI).
"""
import argparse
import sys
from pathlib import Path

# (dossier _deps, nom, rôle, licence SPDX, expédié avec l'application ?)
LIBRARIES = [
    ("glm-src", "GLM 1.0.1", "mathématiques 3D (header-only)", "MIT", True),
    ("spdlog-src", "spdlog 1.14", "journalisation", "MIT", True),
    ("nlohmann_json-src", "nlohmann/json 3.11", "JSON (projets .rig)", "MIT", True),
    ("glad-src", "glad 2.0.8", "chargeur OpenGL", "MIT (le code généré est du domaine public)", True),
    ("tinygltf-src", "TinyGLTF 2.9.3 (+ stb_image, stb_image_write)", "import / export glTF", "MIT (stb : domaine public / MIT)", True),
    ("qtads-src", "Qt Advanced Docking System 4.4.1", "panneaux ancrables", "LGPL-2.1", True),
    ("doctest-src", "doctest 2.4.11", "tests unitaires (non distribué)", "MIT", False),
]

QT_NOTICE = """Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, Svg, Concurrent) — https://www.qt.io
Licence : GNU Lesser General Public License v3 (LGPLv3) — https://www.gnu.org/licenses/lgpl-3.0.html

Rigger utilise Qt par LIAISON DYNAMIQUE (bibliothèques partagées livrées à côté de l'exécutable) : vous pouvez remplacer
ces bibliothèques par une version modifiée de Qt compatible. Le code source de Qt est disponible sur https://code.qt.io
et https://download.qt.io. Qt est une marque de The Qt Company Ltd.
"""

INTER_NOTICE_FILE = Path("assets/ui/fonts/OFL.txt")
LICENSE_NAMES = ("LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "COPYING.txt", "LICENSE.MIT", "License.txt")


def find_license(folder: Path):
    # Insensible à la casse (Linux : « copying.txt » pour GLM, « LICENSE » ailleurs).
    entries = {f.name.lower(): f for f in folder.iterdir() if f.is_file()} if folder.is_dir() else {}
    for name in LICENSE_NAMES:
        if name.lower() in entries:
            return entries[name.lower()]
    for key in sorted(entries):
        if key.startswith(("license", "copying")):
            return entries[key]
    return None


def build(root: Path, deps: Path) -> str:
    out = ["THIRD_PARTY_LICENSES — bibliothèques tierces utilisées par Rigger", "=" * 72, ""]
    out.append("Rigger est distribué avec les composants suivants. Chaque section reproduit la licence d'origine.")
    out.append("")
    missing = []
    for folder, name, role, spdx, shipped in LIBRARIES:
        out += ["-" * 72, f"{name} — {role}", f"Licence : {spdx}" + ("" if shipped else "  [outil de développement, non distribué]"), "-" * 72]
        lic = find_license(deps / folder)
        if lic is None:
            missing.append(folder)
            out.append("(fichier de licence introuvable dans les sources téléchargées)")
        else:
            out.append(lic.read_text(encoding="utf-8", errors="replace").strip())
        out.append("")
    out += ["-" * 72, "Inter (police de l'interface) — https://rsms.me/inter", "Licence : SIL Open Font License 1.1", "-" * 72]
    ofl = root / INTER_NOTICE_FILE
    out.append(ofl.read_text(encoding="utf-8", errors="replace").strip() if ofl.is_file() else "(OFL.txt introuvable)")
    out.append("")
    out += ["-" * 72, "Qt", "-" * 72, QT_NOTICE.strip(), ""]
    out += ["-" * 72, "Icônes de l'interface et icône de l'application", "-" * 72,
            "Icônes SVG et icône de l'application dessinées pour Rigger (tools/make_ui_icons.py, tools/make_app_icon.py) :",
            "CC0 1.0 — https://creativecommons.org/publicdomain/zero/1.0/", ""]
    if missing:
        raise SystemExit("licences introuvables pour : " + ", ".join(missing))
    return "\n".join(out).replace("\r\n", "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--deps", default=str(root / "build" / "debug" / "_deps"))
    parser.add_argument("--output", default=str(root / "THIRD_PARTY_LICENSES.txt"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = build(root, Path(args.deps))
    output = Path(args.output)
    if args.check:
        current = output.read_text(encoding="utf-8") if output.is_file() else ""
        if current != text:
            print(f"{output} n'est plus à jour : relancer tools/gen_third_party.py")
            return 1
        print("THIRD_PARTY_LICENSES.txt à jour")
        return 0
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"{output} écrit ({len(text.splitlines())} lignes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
