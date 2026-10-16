# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Correctif de Qt Advanced Docking System 4.4.1 (exécuté dans le dossier des sources par FetchContent).
# Sous Linux, CDockManager connecte QApplication::focusWindowChanged à un lambda qui capture « this » SANS objet
# contexte : la connexion survit à la destruction du gestionnaire et le signal suivant lit de la mémoire libérée
# (heap-use-after-free détecté par ASan quand on ferme puis recrée des fenêtres). Avec « this » comme contexte, Qt
# supprime la connexion avec le gestionnaire. Idempotent.
set(_file "src/DockManager.cpp")
file(READ "${_file}" _text)
set(_old "connect(qApp, &QApplication::focusWindowChanged, [this](QWindow* focusWindow)")
set(_new "connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* focusWindow)")
string(FIND "${_text}" "${_old}" _pos)
if(_pos GREATER -1)
  string(REPLACE "${_old}" "${_new}" _text "${_text}")
  file(WRITE "${_file}" "${_text}")
  message(STATUS "ADS : correctif focusWindowChanged appliqué")
endif()
