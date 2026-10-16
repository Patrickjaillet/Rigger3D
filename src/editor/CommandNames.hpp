// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QString>
#include <string>

#include "core/command/command.hpp"

namespace rigger::editor {

// Nom lisible (traduit) d'une commande : la clé anglaise de Command::name() passe par
// QCoreApplication::translate (contexte « Command »).
QString commandDisplayName(const std::string& key);

// Texte complet affiché dans l'historique et le menu Édition : nom traduit + détail éventuel (nom d'os).
QString commandText(const rigger::Command& command);

}  // namespace rigger::editor
