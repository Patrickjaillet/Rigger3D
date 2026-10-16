// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace rigger::editor {

// Langues proposées dans les préférences (codes ISO). L'anglais est la langue des sources.
QStringList availableLanguages();

// Langue effective pour un code éventuellement vide (« langue du système »), parmi availableLanguages().
QString resolveLanguage(const QString& requested);

// Installe la traduction `language` (remplace la précédente) : QEvent::LanguageChange est alors envoyé à
// tous les widgets, qui se retraduisent à chaud. Renvoie false si le catalogue est introuvable.
bool applyLanguage(const QString& language);

}  // namespace rigger::editor
