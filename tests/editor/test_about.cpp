// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include "core/version.hpp"
#include "editor/AboutDialog.hpp"
#include "editor/Document.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Translations.hpp"
#include "editor/TutorialGuide.hpp"
#include "editor/WelcomeWidget.hpp"
#include "editor_test_support.hpp"

using namespace rigger::editor;

class AboutTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void summaryMentionsVersionLgplAndPrivacy() {
        const QString html = AboutDialog::summaryHtml(QStringLiteral("OpenGL 4.5 test"));
        QVERIFY(html.contains(QString::fromStdString(std::string(rigger::kVersionString))));
        QVERIFY(html.contains(QStringLiteral("LGPL")));
        QVERIFY(html.contains(QStringLiteral("dynamic linking")));
        QVERIFY(html.contains(QStringLiteral("no telemetry")));
        QVERIFY(html.contains(QStringLiteral("OpenGL 4.5 test")));
        // Mentions légales obligatoires : copyright, licence, e-mail de contact, site web.
        QVERIFY(html.contains(QStringLiteral("Copyright (C) 2026 Patrick JAILLET")));
        QVERIFY(html.contains(QStringLiteral("GPL-3.0-or-later")));
        QVERIFY(html.contains(QStringLiteral("sandefjord.development@proton.me")));
        QVERIFY(html.contains(QStringLiteral("https://patrickjaillet.github.io/Rigger3D")));
        QVERIFY(html.contains(QStringLiteral("Rigger3D")));
    }

    void thirdPartyLicensesAreEmbedded() {
        const QString text = AboutDialog::licensesText();
        QVERIFY(text.size() > 1000);
        QVERIFY(text.contains(QStringLiteral("GLM"), Qt::CaseInsensitive));
        QVERIFY(text.contains(QStringLiteral("Qt")));
    }

    void exampleButtonOpensTheBundledCharacter() {
        MainWindow window;
        QVERIFY(window.welcomeWidget()->exampleButton() != nullptr);
        window.welcomeWidget()->exampleButton()->click();
        QVERIFY(window.document()->hasCharacter());
        QCOMPARE(window.document()->skeleton()->boneCount(), std::size_t(19));
        QVERIFY(window.document()->character()->clips.size() >= 2);
    }

    void guidedTourWalksThroughEveryStepAndCanBeSkipped() {
        MainWindow window;
        window.show();
        window.welcomeWidget()->exampleButton()->click();
        TutorialGuide* tour = window.tutorial();
        QVERIFY(tour != nullptr);
        QVERIFY(tour->isRunning());
        QCOMPARE(tour->stepCount(), 5);
        QCOMPARE(tour->stepIndex(), 0);
        QSignalSpy finished(tour, &TutorialGuide::finished);
        for (int i = 1; i < tour->stepCount(); ++i) {
            tour->findChild<QPushButton*>("tutorialNext")->click();
            QCOMPARE(tour->stepIndex(), i);
            QVERIFY(tour->isVisible());
            QVERIFY(window.rect().contains(tour->geometry()));  // la bulle reste dans la fenêtre
        }
        tour->findChild<QPushButton*>("tutorialNext")->click();  // « Done »
        QVERIFY(!tour->isRunning());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toBool(), true);

        window.startTutorial();
        QSignalSpy skipped(tour, &TutorialGuide::finished);
        tour->findChild<QPushButton*>("tutorialSkip")->click();
        QVERIFY(!tour->isRunning());
        QCOMPARE(skipped.first().first().toBool(), false);
    }
};

RIGGER_REGISTER_QTEST(AboutTest);
#include "test_about.moc"
