// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <filesystem>
#include <vector>

namespace rigger::app {

// Rapport de crash local : en cas de plantage (segfault, exception non gérée, abort, terminate), un fichier
// texte (version, date, cause, pile d'appels) est écrit dans le dossier des rapports ; sous Windows un
// minidump (.dmp) l'accompagne. Au lancement suivant, pendingReports() liste les rapports non encore signalés
// à l'utilisateur. Aucune donnée n'est envoyée : tout reste sur la machine.

// Dossier des rapports : $RIGGER_CRASH_DIR s'il est défini, sinon <données utilisateur>/Rigger/crashes.
std::filesystem::path crashDirectory();

// Dossier du journal persistant (rigger.log, rotation 1 Mo x 3) : à côté du dossier des rapports de crash.
std::filesystem::path logDirectory();

// Installe les gestionnaires (à appeler le plus tôt possible dans main). `banner` est copié dans chaque
// rapport.
void installCrashHandler(const char* banner);

// Rapports présents dans le dossier (crash-*.txt), du plus ancien au plus récent.
std::vector<std::filesystem::path> pendingReports();

// Marque un rapport comme signalé (déplacé dans le sous-dossier « reported »).
void markReported(const std::filesystem::path& report);

// Provoque volontairement un plantage (test automatisé du gestionnaire : option --crash-test).
[[noreturn]] void triggerTestCrash();

}  // namespace rigger::app
