// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QDialog>
#include <QString>

namespace rigger::editor {

// Écran « À propos » : version, rendu, version de Qt et mention LGPL (liaison dynamique), licences tierces
// (THIRD_PARTY_LICENSES.txt embarqué), lien vers la documentation, et la promesse « aucune télémétrie ».
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(const QString& renderInfo, QWidget* parent = nullptr);

    // Texte HTML affiché (testable sans ouvrir la fenêtre).
    static QString summaryHtml(const QString& renderInfo);
    // Chemin du guide utilisateur installé (ou du dépôt en développement) ; vide s'il est introuvable.
    static QString userGuidePath();
    // Texte des licences tierces (ressource embarquée).
    static QString licensesText();
};

}  // namespace rigger::editor
