// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QApplication>
#include <QFile>
#include <QMetaObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <memory>
#include <vector>

#include "editor_test_support.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RiggerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("RiggerEditorTests"));
    // Les préférences de test vivent dans un dossier temporaire : jamais dans la configuration de
    // l'utilisateur.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QTemporaryDir logDir;

    int failures = 0;
    QTextStream out(stdout);
    for (const auto& make : rigger::testing::TestRegistry::factories()) {
        const std::unique_ptr<QObject> test(make());
        // Sous Windows la sortie standard de Qt Test peut se perdre : le résultat de chaque classe est écrit
        // dans un fichier puis recopié ici, afin que « ctest --output-on-failure » affiche les échecs.
        const QString logPath =
            logDir.filePath(QString::fromLatin1(test->metaObject()->className()) + ".txt");
        const QString outputArg = logPath + QStringLiteral(",txt");
        std::vector<QByteArray> storage;
        for (int i = 0; i < argc; ++i) {
            storage.emplace_back(argv[i]);
        }
        storage.emplace_back("-o");
        storage.emplace_back(outputArg.toLocal8Bit());
        std::vector<char*> args;
        for (QByteArray& arg : storage) {
            args.push_back(arg.data());
        }
        const int result = QTest::qExec(test.get(), static_cast<int>(args.size()), args.data());
        failures += result;
        QFile log(logPath);
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(log.readAll());
            if (result != 0) {
                out << text;
            } else {
                // Succès : on ne garde que la ligne « Totals ».
                for (const QString& line : text.split(QLatin1Char('\n'))) {
                    if (line.startsWith(QStringLiteral("Totals"))) {
                        out << test->metaObject()->className() << ": " << line << "\n";
                    }
                }
            }
            out.flush();
        }
    }
    return failures;
}
