#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Test automatisé du rapport de crash : lance `rigger --crash-test` et vérifie qu'un rapport local est écrit."""
import glob
import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_crash_report.py <rigger>")
        return 2
    exe = sys.argv[1]
    with tempfile.TemporaryDirectory() as folder:
        env = dict(os.environ, RIGGER_CRASH_DIR=folder, QT_QPA_PLATFORM="offscreen")
        result = subprocess.run([exe, "--crash-test"], env=env, capture_output=True, timeout=120)
        if result.returncode == 0:
            print("le processus aurait dû planter")
            return 1
        reports = glob.glob(os.path.join(folder, "crash-*.txt"))
        if len(reports) != 1:
            print(f"rapport texte attendu (trouvé {len(reports)}) dans {folder}: {os.listdir(folder)}")
            return 1
        text = open(reports[0], encoding="utf-8", errors="replace").read()
        for needle in ("crash report", "Time:", "Cause:", "Stack trace"):
            if needle not in text:
                print(f"« {needle} » absent du rapport :\n{text}")
                return 1
        if os.name == "nt":
            dumps = glob.glob(os.path.join(folder, "crash-*.dmp"))
            if len(dumps) != 1 or os.path.getsize(dumps[0]) < 1000:
                print("minidump absent ou vide")
                return 1
        print("rapport de crash OK :", os.path.basename(reports[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
