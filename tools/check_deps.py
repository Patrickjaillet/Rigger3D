#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Vérifie les règles de dépendances entre modules de src/.

  app -> editor -> (render, io, core) ; render -> core ; io -> core ; core -> (rien)
  Qt uniquement dans editor/ et app/.  core/ : ni OpenGL, ni glad, ni tinygltf.

Usage : check_deps.py --root <racine du dépôt>   |   check_deps.py --self-test
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
EXTENSIONS = {".hpp", ".h", ".cpp", ".cc", ".cxx", ".inl", ".in"}

QT_RE = re.compile(r"^(Q[A-Z]\w*|Qt\w*/.*)$")
# Télémétrie : AUCUNE. Aucun module ne doit inclure de bibliothèque réseau (si un jour une télémétrie est ajoutée, elle devra
# être opt-in explicite et cette règle adaptée consciemment).
NETWORK_RE = re.compile(r"^(QNet\w*|QtNetwork/.*|QTcp\w*|QUdp\w*|QWebSocket\w*|curl/.*|winsock2?\.h|ws2tcpip\.h|winhttp\.h|wininet\.h|sys/socket\.h|netinet/.*|arpa/inet\.h|netdb\.h|httplib\.h|asio.*|boost/asio.*)$")
GL_RE = re.compile(r"^(glad/.*|GL/.*|GLFW/.*|OpenGL/.*|tiny_gltf.*|tinygltf.*|imgui.*)$")

# module -> modules internes autorisés
ALLOWED_INTERNAL = {
    "core": set(),
    "io": {"core"},
    "render": {"core"},
    "editor": {"core", "io", "render"},
    "app": {"core", "io", "render", "editor"},
}
QT_ALLOWED = {"editor", "app"}
# glad/GL autorisés uniquement dans render (tinygltf uniquement dans io)
GL_ALLOWED = {"render"}


def check(root: Path):
    errors = []
    src = root / "src"
    for module, allowed in ALLOWED_INTERNAL.items():
        mdir = src / module
        if not mdir.is_dir():
            continue
        for f in sorted(mdir.rglob("*")):
            if f.suffix not in EXTENSIONS:
                continue
            text = f.read_text(encoding="utf-8", errors="replace")
            for inc in INCLUDE_RE.findall(text):
                rel = f.relative_to(root)
                if NETWORK_RE.match(inc):
                    errors.append(f"{rel}: inclut '{inc}' - accès réseau interdit (aucune télémétrie)")
                if QT_RE.match(inc) and module not in QT_ALLOWED:
                    errors.append(f"{rel}: inclut Qt ('{inc}') - interdit dans '{module}'")
                if GL_RE.match(inc):
                    is_gltf = inc.lower().startswith("tiny")
                    ok = module == "io" if is_gltf else module in GL_ALLOWED
                    if not ok:
                        errors.append(f"{rel}: inclut '{inc}' - interdit dans '{module}'")
                head = inc.split("/")[0]
                if "/" in inc and head in ALLOWED_INTERNAL and head != module and head not in allowed:
                    errors.append(f"{rel}: '{module}' ne peut pas dépendre de '{head}' ('{inc}')")
    return errors


def self_test() -> int:
    """Vérifie que le script détecte bien de vraies violations."""
    cases = {
        "core/a.hpp": '#include <QString>\n',
        "core/b.hpp": '#include <glad/glad.h>\n',
        "core/c.hpp": '#include "render/camera.hpp"\n',
        "render/d.hpp": '#include <QOpenGLWidget>\n',
        "io/e.hpp": '#include "editor/MainWindow.hpp"\n',
    }
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for name, content in cases.items():
            p = root / "src" / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(content)
        errs = check(root)
        if len(errs) != len(cases):
            print("self-test ÉCHEC : attendu", len(cases), "violations, obtenu", len(errs))
            print("\n".join(errs))
            return 1
        # Un arbre valide ne doit rien signaler
        good = root / "src" / "editor" / "ok.cpp"
        good.parent.mkdir(parents=True, exist_ok=True)
        good.write_text('#include <QWidget>\n#include "core/version.hpp"\n#include "render/x.hpp"\n')
        if len(check(root)) != len(cases):
            print("self-test ÉCHEC : faux positif sur un include valide")
            return 1
    print("self-test OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    errors = check(Path(args.root))
    if errors:
        print("Violations des règles de dépendances :")
        print("\n".join("  " + e for e in errors))
        return 1
    print("check_deps : OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
