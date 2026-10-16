// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <thread>

#include "editor/ConsolePanel.hpp"
#include "editor_test_support.hpp"

using namespace rigger::editor;

class ConsolePanelTest : public QObject {
    Q_OBJECT
private slots:
    void spdlogMessagesAppearInThePanel() {
        ConsolePanel panel;
        spdlog::info("hello from spdlog");
        spdlog::warn("careful {}", 42);
        QCoreApplication::processEvents();  // les messages arrivent par signal en file d'attente
        QVERIFY(panel.visibleText().contains(QStringLiteral("hello from spdlog")));
        QVERIFY(panel.visibleText().contains(QStringLiteral("careful 42")));
        QVERIFY(panel.visibleText().contains(QStringLiteral("[info]")));
        QVERIFY(panel.visibleText().contains(QStringLiteral("[warning]")));
    }

    void messagesFromOtherThreadsAreDelivered() {
        ConsolePanel panel;
        std::thread worker([] { spdlog::error("from a worker thread"); });
        worker.join();
        QCoreApplication::processEvents();
        QVERIFY(panel.visibleText().contains(QStringLiteral("from a worker thread")));
    }

    void levelFilterHidesLowerLevelsAndRestoresThem() {
        ConsolePanel panel;
        panel.append(1, QStringLiteral("debug line"), 0);
        panel.append(2, QStringLiteral("info line"), 0);
        panel.append(3, QStringLiteral("warn line"), 0);
        panel.append(4, QStringLiteral("error line"), 0);
        QVERIFY(!panel.visibleText().contains(QStringLiteral("debug line")));  // niveau par défaut : Info
        QVERIFY(panel.visibleText().contains(QStringLiteral("info line")));

        panel.setMinimumLevel(3);  // avertissements et plus
        QVERIFY(!panel.visibleText().contains(QStringLiteral("info line")));
        QVERIFY(panel.visibleText().contains(QStringLiteral("warn line")));
        QVERIFY(panel.visibleText().contains(QStringLiteral("error line")));
        panel.setMinimumLevel(4);  // erreurs seulement
        QVERIFY(!panel.visibleText().contains(QStringLiteral("warn line")));

        panel.setMinimumLevel(0);  // tout, y compris les messages reçus avant le changement de filtre
        QVERIFY(panel.visibleText().contains(QStringLiteral("debug line")));
        QCOMPARE(panel.recordCount(), 4);
    }

    void exportWritesTheVisibleLogAndClearEmptiesIt() {
        ConsolePanel panel;
        panel.append(2, QStringLiteral("first"), 1000);
        panel.append(3, QStringLiteral("second"), 2000);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("log.txt"));
        QVERIFY(panel.exportTo(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(content.contains(QStringLiteral("first")));
        QVERIFY(content.contains(QStringLiteral("second")));
        QVERIFY(!panel.exportTo(dir.filePath(QStringLiteral("missing/dir/log.txt"))));

        panel.clear();
        QCOMPARE(panel.recordCount(), 0);
        QVERIFY(panel.visibleText().isEmpty());
    }

    void destroyingThePanelDetachesTheSink() {
        const auto before = spdlog::default_logger()->sinks().size();
        {
            ConsolePanel panel;
            QCOMPARE(spdlog::default_logger()->sinks().size(), before + 1);
        }
        QCOMPARE(spdlog::default_logger()->sinks().size(), before);
        spdlog::info("no crash after the panel is gone");
    }
};

RIGGER_REGISTER_QTEST(ConsolePanelTest);
#include "test_console.moc"
