// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPalette>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QToolBar>

#include "core/version.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Preferences.hpp"
#include "editor/Theme.hpp"
#include "editor/Translations.hpp"
#include "editor/WelcomeWidget.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::dataFile;

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language",
                             "en");  // sinon : langue du système (français sur certains postes)
        applyLanguage(QStringLiteral("en"));
    }

    void titleContainsVersion() {
        MainWindow w;
        QCOMPARE(w.windowTitle(), QString::fromStdString(versionBanner()) +
                                      QStringLiteral("[*]"));  // [*] : marqueur « modifié »
    }

    void hasMenusAndImportAction() {
        MainWindow w;
        for (const char* name : {"menuFile", "menuEdit", "menuView", "menuHelp"}) {
            QVERIFY2(w.findChild<QMenu*>(name) != nullptr, name);
        }
        QAction* import = w.actions()->action(ActionId::FileImport);
        QVERIFY(import->shortcut() == QKeySequence(QStringLiteral("Ctrl+I")));
        QVERIFY2(import->toolTip().contains(QStringLiteral("Ctrl+I")),
                 "le raccourci doit figurer dans l'info-bulle");
        // Les menus sont bien alimentés par le registre : l'action d'import est dans « Fichier ».
        QVERIFY(w.findChild<QMenu*>("menuFile")->actions().contains(import));
    }

    void startsOnWelcomeScreenThenNewSceneOpensWorkspace() {
        MainWindow w;
        QVERIFY(!w.isWorkspaceVisible());
        w.actions()->action(ActionId::FileNew)->trigger();
        QVERIFY(w.isWorkspaceVisible());
    }

    void welcomeButtonsAreWired() {
        MainWindow w;
        w.welcomeWidget()->newButton()->click();
        QVERIFY(w.isWorkspaceVisible());
    }

    void quitActionClosesWindow() {
        MainWindow w;
        w.show();
        w.actions()->action(ActionId::FileQuit)->trigger();
        QVERIFY(!w.isVisible());
    }

    void importFileLoadsCharacter() {
        MainWindow w;
        QVERIFY(w.character() == nullptr);
        QVERIFY(w.importFile(dataFile("arm.gltf")).ok());
        QVERIFY(w.character() != nullptr);
        QCOMPARE(static_cast<int>(w.character()->skeleton.boneCount()), 2);
        QCOMPARE(static_cast<int>(w.character()->clips.size()), 1);
        QVERIFY(w.isWorkspaceVisible());
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("arm.gltf")));
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("arm")));
        QVERIFY(w.document()->isPlaying());  // l'animation embarquée démarre
        QCOMPARE(w.preferences()->recentFiles().size(), 1);
    }

    void importErrorKeepsPreviousCharacter() {
        MainWindow w;
        QVERIFY(w.importFile(dataFile("cube.gltf")).ok());
        const auto before = w.character();
        const auto result = w.importFile(dataFile("missing.glb"));
        QVERIFY(!result.ok());
        QVERIFY(result.error().message.find("not found") != std::string::npos);
        QVERIFY(w.character() == before);
    }

    void asyncImportLoadsCharacterOffTheUiThread() {
        MainWindow w;
        QSignalSpy finished(&w, &MainWindow::importFinished);
        w.importFileAsync(dataFile("CesiumMan.glb"));
        QVERIFY(w.isImporting());
        // L'import tourne dans un thread : la boucle d'événements de l'interface reste réactive.
        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
        ticker.start(1);
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 20000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.at(0).at(0).toBool());
        QVERIFY(!w.isImporting());
        QVERIFY(w.character() != nullptr);
        QVERIFY(w.isWorkspaceVisible());
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("CesiumMan")));
    }

    void asyncImportErrorIsReportedAndKeepsDocument() {
        MainWindow w;
        QVERIFY(w.importFile(dataFile("cube.gltf")).ok());
        const auto before = w.character();
        QSignalSpy finished(&w, &MainWindow::importFinished);
        w.importFileAsync(dataFile("missing.glb"));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 10000);
        QVERIFY(!finished.at(0).at(0).toBool());
        QVERIFY(finished.at(0).at(1).toString().contains(QStringLiteral("not found")));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("Import failed")));
        QVERIFY(w.character() == before);
    }

    void asyncImportCanBeCancelled() {
        MainWindow w;
        QSignalSpy finished(&w, &MainWindow::importFinished);
        w.importFileAsync(dataFile("CesiumMan.glb"));
        w.cancelImport();  // la lecture du fichier dure plus longtemps que ce délai : l'annulation prend
                           // effet
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 20000);
        QVERIFY(!finished.at(0).at(0).toBool());
        QVERIFY(w.character() == nullptr);
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("cancelled")));
    }

    void droppedFileIsImported() {
        MainWindow w;
        QMimeData unsupported;
        unsupported.setUrls({QUrl::fromLocalFile(dataFile("CesiumMan.LICENSE.md"))});
        QVERIFY(!w.importDropped(unsupported));
        QVERIFY(w.character() == nullptr);

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(dataFile("sparse.gltf"))});
        QSignalSpy finished(&w, &MainWindow::importFinished);
        QVERIFY(w.importDropped(mime));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 10000);
        QVERIFY(w.character() != nullptr);
        QCOMPARE(static_cast<int>(w.character()->meshes.size()), 1);
    }

    void modesAndToolsDriveDocumentAndStatusBar() {
        MainWindow w;
        auto* modeLabel = w.findChild<QLabel*>("statusMode");
        auto* toolLabel = w.findChild<QLabel*>("statusTool");
        QVERIFY(modeLabel->text().contains(QStringLiteral("Animate")));
        QVERIFY(toolLabel->text().contains(QStringLiteral("Select")));

        w.actions()->action(ActionId::ModeRig)->trigger();
        QCOMPARE(w.document()->mode(), Mode::Rig);
        QVERIFY(modeLabel->text().contains(QStringLiteral("Rig")));
        w.actions()->action(ActionId::ModeSkin)->trigger();
        QCOMPARE(w.document()->mode(), Mode::Skin);
        w.actions()->action(ActionId::ToolRotate)->trigger();
        QCOMPARE(w.document()->tool(), Tool::Rotate);
        QVERIFY(toolLabel->text().contains(QStringLiteral("Rotate")));

        // Les actions sont exclusives (QActionGroup) et suivent le Document.
        w.document()->setMode(Mode::Animate);
        QVERIFY(w.actions()->action(ActionId::ModeAnimate)->isChecked());
        QVERIFY(!w.actions()->action(ActionId::ModeSkin)->isChecked());
        // Le raccourci par défaut est bien « 1 / 2 / 3 » et « Q / W / E / R » (tableau du §1 de la roadmap).
        QCOMPARE(w.actions()->shortcut(ActionId::ModeRig), QKeySequence(QStringLiteral("1")));
        QCOMPARE(w.actions()->shortcut(ActionId::ToolMove), QKeySequence(QStringLiteral("W")));
    }

    void playbackActionsAndFrameLabel() {
        MainWindow w;
        QVERIFY(w.importFile(dataFile("arm.gltf")).ok());
        auto* frameLabel = w.findChild<QLabel*>("statusFrame");
        w.actions()->action(ActionId::AnimPlay)->trigger();  // pause
        QVERIFY(!w.document()->isPlaying());
        w.document()->setFrame(5);
        w.actions()->action(ActionId::AnimNextFrame)->trigger();
        QCOMPARE(w.document()->frame(), 6);
        w.actions()->action(ActionId::AnimPrevFrame)->trigger();
        w.actions()->action(ActionId::AnimPrevFrame)->trigger();
        QCOMPARE(w.document()->frame(), 4);
        QVERIFY2(frameLabel->text().contains(QStringLiteral("4 / 30")), qPrintable(frameLabel->text()));
        w.actions()->action(ActionId::AnimPlay)->trigger();
        QVERIFY(w.document()->isPlaying());
    }

    void statusBarShowsStatistics() {
        MainWindow w;
        QVERIFY(w.importFile(dataFile("arm.gltf")).ok());
        QVERIFY2(w.findChild<QLabel*>("statusStats")->text().contains(QStringLiteral("2 bones, 30 vertices")),
                 qPrintable(w.findChild<QLabel*>("statusStats")->text()));
    }

    void panelsCanBeToggledFromTheViewMenu() {
        MainWindow w;
        auto* console = w.dock("dockConsole");
        QVERIFY(console != nullptr);
        QVERIFY(!console->isClosed());
        console->toggleViewAction()->trigger();
        QVERIFY(console->isClosed());
        console->toggleViewAction()->trigger();
        QVERIFY(!console->isClosed());
        // Le viewport central ne peut pas être fermé.
        QVERIFY(!w.dock("dockViewport")->features().testFlag(ads::CDockWidget::DockWidgetClosable));
    }

    void layoutIsSavedAndRestoredAcrossSessions() {
        {
            MainWindow w;
            w.dock("dockConsole")->toggleView(false);
            w.dock("dockCharacter")->toggleView(false);
            w.saveLayout();
        }
        MainWindow restored;
        QVERIFY(restored.dock("dockConsole")->isClosed());
        QVERIFY(restored.dock("dockCharacter")->isClosed());
        QVERIFY(!restored.dock("dockHierarchy")->isClosed());
        restored.actions()->action(ActionId::ViewResetLayout)->trigger();
        QVERIFY(!restored.dock("dockConsole")->isClosed());
        QVERIFY(!restored.dock("dockCharacter")->isClosed());
    }

    void languageSwitchesAtRuntime() {
        MainWindow w;
        auto* import = w.actions()->action(ActionId::FileImport);
        QCOMPARE(import->text(), QStringLiteral("&Import..."));
        QCOMPARE(w.findChild<QMenu*>("menuFile")->title(), QStringLiteral("&File"));

        w.preferences()->setLanguage(QStringLiteral("fr"));
        QCOMPARE(import->text(), QStringLiteral("&Importer..."));
        QCOMPARE(w.findChild<QMenu*>("menuFile")->title(), QStringLiteral("&Fichier"));
        QVERIFY(w.findChild<QLabel*>("statusMode")->text().contains(QStringLiteral("Mode : ")));
        QCOMPARE(w.dock("dockHierarchy")->windowTitle(), QStringLiteral("Hiérarchie"));
        QVERIFY(import->toolTip().contains(QStringLiteral("Importer")));

        w.preferences()->setLanguage(QStringLiteral("en"));
        QCOMPARE(import->text(), QStringLiteral("&Import..."));
        QCOMPARE(w.dock("dockHierarchy")->windowTitle(), QStringLiteral("Hierarchy"));
    }

    void themeSwitchesAtRuntime() {
        MainWindow w;
        const ThemeColors dark = themeColors(ThemeKind::Dark);
        const ThemeColors light = themeColors(ThemeKind::Light);
        QCOMPARE(QApplication::palette().color(QPalette::Window), dark.window);
        w.preferences()->setTheme(ThemeKind::Light);
        QCOMPARE(QApplication::palette().color(QPalette::Window), light.window);
        QCOMPARE(Theme::current(), ThemeKind::Light);
        w.preferences()->setTheme(ThemeKind::Dark);
        QCOMPARE(QApplication::palette().color(QPalette::Window), dark.window);
    }

    void quickStartAndAboutActionsExist() {
        MainWindow w;
        QVERIFY(w.actions()->action(ActionId::HelpQuickStart) != nullptr);
        QVERIFY(
            w.findChild<QMenu*>("menuHelp")->actions().contains(w.actions()->action(ActionId::HelpAbout)));
    }
};

RIGGER_REGISTER_QTEST(MainWindowTest);
#include "test_mainwindow.moc"
