// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/Translations.hpp"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QEvent>
#include <QLocale>
#include <QTranslator>
#include <memory>

#include "editor/Theme.hpp"

namespace rigger::editor {

QStringList availableLanguages() { return {QStringLiteral("en"), QStringLiteral("fr")}; }

QString resolveLanguage(const QString& requested) {
    const QStringList available = availableLanguages();
    if (available.contains(requested)) {
        return requested;
    }
    const QString system = QLocale::system().name().section('_', 0, 0);  // « fr_FR » -> « fr »
    return available.contains(system) ? system : QStringLiteral("en");
}

bool applyLanguage(const QString& language) {
    initEditorResources();
    static QTranslator* installed = nullptr;
    if (installed != nullptr) {
        QCoreApplication::removeTranslator(installed);
        delete installed;
        installed = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    }
    const QString code = resolveLanguage(language);
    QLocale::setDefault(QLocale(code));
    if (code == QLatin1String("en")) {
        return true;  // les sources sont en anglais : aucun catalogue à charger
    }
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/rigger_%1.qm").arg(code))) {
        spdlog::warn("translation catalog for '{}' not found", code.toStdString());
        return false;
    }
    installed = translator.release();
    QCoreApplication::installTranslator(installed);
    QCoreApplication::sendPostedEvents(nullptr,
                                       QEvent::LanguageChange);  // retraduction immédiate des widgets
    return true;
}

}  // namespace rigger::editor
