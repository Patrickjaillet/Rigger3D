#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Génère les icônes SVG de l'interface (assets/ui/icons + liste pour resources.qrc).

Icônes monochromes en `currentColor` : editor/Icons les recolore selon le thème.
Usage : python3 tools/make_ui_icons.py [--out assets/ui/icons]
"""
import argparse
from pathlib import Path

ICONS = {
    "file-new": '<path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4"/><path d="M12 11v6M9 14h6"/>',
    "folder-open": '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v1"/><path d="M3 10h19l-2 9H5z"/>',
    "save": '<path d="M5 3h12l3 3v15H5z"/><path d="M8 3v6h8V3"/><path d="M8 21v-7h8v7"/>',
    "import": '<path d="M12 3v12M7 10l5 5 5-5"/><path d="M4 19h16"/>',
    "export": '<path d="M12 15V3M7 8l5-5 5 5"/><path d="M4 19h16"/>',
    "undo": '<path d="M9 8L4 13l5 5"/><path d="M4 13h10a5 5 0 0 1 0 8"/>',
    "redo": '<path d="M15 8l5 5-5 5"/><path d="M20 13H10a5 5 0 0 0 0 8"/>',
    "settings": '<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>',
    "mode-rig": '<circle cx="6" cy="18" r="2.5"/><circle cx="18" cy="6" r="2.5"/><path d="M8 16l8-8"/>',
    "mode-skin": '<path d="M12 3c4 5 6 8 6 11a6 6 0 0 1-12 0c0-3 2-6 6-11z"/>',
    "mode-animate": '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 5v14M16 5v14M3 10h5M3 14h5M16 10h5M16 14h5"/>',
    "tool-select": '<path d="M5 3l14 8-6 2-3 6z"/>',
    "tool-move": '<path d="M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3"/>',
    "tool-rotate": '<path d="M20 12a8 8 0 1 1-3-6.2"/><path d="M20 4v5h-5"/>',
    "tool-scale": '<rect x="4" y="10" width="10" height="10"/><path d="M14 10l6-6M15 4h5v5"/>',
    "play": '<path d="M7 4l13 8-13 8z"/>',
    "pause": '<path d="M8 4v16M16 4v16"/>',
    "step-forward": '<path d="M6 5l9 7-9 7z"/><path d="M19 5v14"/>',
    "step-back": '<path d="M18 5l-9 7 9 7z"/><path d="M5 5v14"/>',
    "eye": '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    "eye-off": '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/><path d="M3 3l18 18"/>',
    "lock": '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
    "unlock": '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 7.5-2"/>',
    "bone": '<circle cx="6" cy="18" r="2.5"/><circle cx="18" cy="6" r="2.5"/><path d="M8 16l8-8"/>',
    "mesh": '<path d="M12 3l9 5v8l-9 5-9-5V8z"/><path d="M12 12l9-4M12 12L3 8M12 12v9"/>',
    "search": '<circle cx="11" cy="11" r="6"/><path d="M16 16l5 5"/>',
    "focus": '<circle cx="12" cy="12" r="4"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/>',
    "clear": '<path d="M4 7h16M9 7V4h6v3M6 7l1 14h10l1-14"/>',
    "copy": '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5a1 1 0 0 0-1-1H5a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3"/>',
    "help": '<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1 .9-1 1.7M12 17v.5"/>',
    "info": '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v.5"/>',
    "quit": '<path d="M9 4H5v16h4M16 8l4 4-4 4M20 12H9"/>',
    "reset-layout": '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16M9 12h12"/>',
    "home": '<path d="M3 11l9-8 9 8M5 10v10h14V10"/>',
    "ortho": '<rect x="5" y="5" width="14" height="14"/><path d="M5 9h14M9 5v14"/>',
    "gpu": '<rect x="6" y="6" width="12" height="12" rx="1"/><path d="M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"/>',
    "influence": '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1.5"/>',
    "keyframe": '<path d="M12 4l8 8-8 8-8-8z"/>',
    "tutorial": '<path d="M4 5a2 2 0 0 1 2-2h13v16H6a2 2 0 0 0-2 2z"/><path d="M4 19V5"/>',
    "add-bone": '<circle cx="6" cy="18" r="2.5"/><circle cx="16" cy="8" r="2.5"/><path d="M8 16l6-6"/><path d="M19 15v6M16 18h6"/>',
    "extrude": '<rect x="4" y="12" width="16" height="8" rx="1"/><path d="M12 12V3M8 7l4-4 4 4"/>',
    "subdivide": '<path d="M4 20L20 4"/><circle cx="12" cy="12" r="2.5"/><circle cx="4" cy="20" r="1.5"/><circle cx="20" cy="4" r="1.5"/>',
    "reset-pose": '<path d="M4 12a8 8 0 1 0 3-6.2"/><path d="M4 4v5h5"/>',
    "paste": '<rect x="6" y="5" width="12" height="16" rx="2"/><path d="M9 5V3h6v2"/>',
    "mirror": '<path d="M12 3v18"/><path d="M8 8L3 12l5 4z"/><path d="M16 8l5 4-5 4z"/>',
    "isolate": '<rect x="7" y="7" width="10" height="10"/><path d="M3 7V3h4M21 7V3h-4M3 17v4h4M21 17v4h-4"/>',
    "select-all": '<rect x="4" y="4" width="16" height="16" stroke-dasharray="3 2"/><path d="M9 12l2 2 4-4"/>',
    "select-none": '<rect x="4" y="4" width="16" height="16" stroke-dasharray="3 2"/>',
    "space": '<path d="M12 3v18M3 12h18"/><circle cx="12" cy="12" r="3"/>',
    "record": '<circle cx="12" cy="12" r="6"/>',
    "onion": '<circle cx="9" cy="12" r="6"/><circle cx="15" cy="12" r="6"/>',
    "loop": '<path d="M17 2l3 3-3 3"/><path d="M4 11V9a4 4 0 0 1 4-4h12"/><path d="M7 22l-3-3 3-3"/><path d="M20 13v2a4 4 0 0 1-4 4H4"/>',
    "pingpong": '<path d="M4 12h16M8 8l-4 4 4 4M16 8l4 4-4 4"/>',
    "skip-start": '<path d="M18 5v14L8 12z"/><path d="M5 5v14"/>',
    "skip-end": '<path d="M6 5v14l10-7z"/><path d="M19 5v14"/>',
    "curve": '<path d="M3 18C8 18 8 6 12 6s4 12 9 12"/>',
    "plus": '<path d="M12 5v14M5 12h14"/>',
    "flag": '<path d="M5 21V4h12l-2 4 2 4H5"/>',
    "pose": '<circle cx="12" cy="5" r="2"/><path d="M12 7v7M7 10l5-1 5 1M9 21l3-7 3 7"/>',
    "panels": '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M9 9v11"/>',
}

TEMPLATE = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
    'stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">{}</svg>\n'
)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="assets/ui/icons")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, body in ICONS.items():
        (out / f"{name}.svg").write_text(TEMPLATE.format(body), encoding="utf-8")
    print(f"{len(ICONS)} icônes écrites dans {out}")
    print("Entrées resources.qrc :")
    for name in ICONS:
        print(f'        <file>icons/{name}.svg</file>')
