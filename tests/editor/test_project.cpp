// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>

#include "core/animation/keys.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"
#include "io/gltf_import.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::dataFile;
using rigger::testing::loadCharacter;

namespace {

// Fenêtre avec le personnage de test déjà affiché (sans passer par le thread d'import).
void showFigure(MainWindow& window) {
    auto character = loadCharacter("RiggedFigure.glb");
    character->clips.clear();  // le clip importé contient déjà ces clés : on part de zéro
    window.document()->setCharacter(character, dataFile("RiggedFigure.glb"));
    window.document()->markClean();
}

void makeDirty(MainWindow& window) {
    Document& doc = *window.document();
    const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
    doc.setSelection({arm});
    doc.setMode(Mode::Animate);
    doc.setFrame(0);
    QVERIFY(doc.insertKeys({arm}));
}

}  // namespace

class ProjectTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
        qputenv("RIGGER_AUTOSAVE_DIR", QDir(dir_.path()).filePath("autosave").toLocal8Bit());
    }

    void saveThenOpenRestoresTheProject() {
        const QString file = QDir(dir_.path()).filePath("figure.rig");
        {
            MainWindow window;
            showFigure(window);
            makeDirty(window);
            QVERIFY(!window.document()->isClean());
            QVERIFY(bool(window.saveProjectTo(file)));
            QVERIFY(window.document()->isClean());
            QCOMPARE(window.projectPath(), file);
        }
        MainWindow other;
        QVERIFY(bool(other.openProject(file)));
        QVERIFY(other.document()->hasCharacter());
        QCOMPARE(other.document()->skeleton()->boneCount(),
                 loadCharacter("RiggedFigure.glb")->skeleton.boneCount());
        QVERIFY(other.document()->clip() != nullptr);
        QVERIFY(other.document()->isClean());
        QCOMPARE(other.projectPath(), file);
        QVERIFY(other.windowTitle().contains("figure"));
        QVERIFY(other.isWorkspaceVisible());
        QVERIFY(other.document()->skeleton()->find("arm_joint_L_1") != kNoBone);
    }

    void unsavedChangesAreConfirmedBeforeDiscarding() {
        MainWindow window;
        showFigure(window);
        makeDirty(window);
        int asked = 0;
        window.setUnsavedPrompt([&] {
            ++asked;
            return MainWindow::UnsavedChoice::Cancel;
        });
        QVERIFY(!window.confirmDiscard());
        QCOMPARE(asked, 1);
        QVERIFY(window.document()->hasCharacter());

        window.setUnsavedPrompt([&] { return MainWindow::UnsavedChoice::Discard; });
        QVERIFY(window.confirmDiscard());

        // Un projet propre ne demande rien.
        window.document()->markClean();
        window.setUnsavedPrompt([&]() -> MainWindow::UnsavedChoice {
            ++asked;
            return MainWindow::UnsavedChoice::Cancel;
        });
        QVERIFY(window.confirmDiscard());
        QCOMPARE(asked, 1);
    }

    void saveChoiceWritesTheFileThenContinues() {
        const QString file = QDir(dir_.path()).filePath("confirm.rig");
        MainWindow window;
        showFigure(window);
        makeDirty(window);
        QVERIFY(bool(window.saveProjectTo(file)));  // fixe le chemin du projet
        window.document()->setFrame(10);
        QVERIFY(window.document()->insertKeys({window.document()->skeleton()->find("arm_joint_L_1")}));
        QVERIFY(!window.document()->isClean());
        window.setUnsavedPrompt([] { return MainWindow::UnsavedChoice::Save; });
        QVERIFY(window.confirmDiscard());
        QVERIFY(window.document()->isClean());
    }

    void newSceneIsCancelledByTheUser() {
        MainWindow window;
        showFigure(window);
        makeDirty(window);
        window.setUnsavedPrompt([] { return MainWindow::UnsavedChoice::Cancel; });
        window.actions()->action(ActionId::FileNew)->trigger();
        QVERIFY(window.document()->hasCharacter());
        window.setUnsavedPrompt([] { return MainWindow::UnsavedChoice::Discard; });
        window.actions()->action(ActionId::FileNew)->trigger();
        QVERIFY(!window.document()->hasCharacter());
    }

    void autosaveWritesOnlyModifiedProjectsAndCanBeRecovered() {
        MainWindow window;
        showFigure(window);
        window.autosaveNow();
        QVERIFY(!window.hasRecoverableAutosave());  // projet propre : rien à écrire
        makeDirty(window);
        window.autosaveNow();
        QVERIFY(window.hasRecoverableAutosave());

        MainWindow restarted;  // « relancement » après un plantage
        QVERIFY(restarted.hasRecoverableAutosave());
        QVERIFY(bool(restarted.recoverAutosave()));
        QVERIFY(restarted.document()->hasCharacter());
        QVERIFY(restarted.document()->clip() != nullptr);
        QVERIFY(!restarted.document()->isClean());  // à enregistrer
        restarted.discardAutosave();
        QVERIFY(!restarted.hasRecoverableAutosave());
    }

    void savingRemovesTheAutosave() {
        MainWindow window;
        showFigure(window);
        makeDirty(window);
        window.autosaveNow();
        QVERIFY(window.hasRecoverableAutosave());
        QVERIFY(bool(window.saveProjectTo(QDir(dir_.path()).filePath("s.rig"))));
        QVERIFY(!window.hasRecoverableAutosave());
    }

    void corruptedProjectIsRefusedAndTheCurrentSceneKept() {
        const QString file = QDir(dir_.path()).filePath("broken.rig");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"format\":\"rigger-project\",\"version\":1,\"skeleton\":{\"bones\":[");  // tronqué
        f.close();
        MainWindow window;
        showFigure(window);
        const auto before = window.document()->skeleton()->boneCount();
        const auto result = window.openProject(file);
        QVERIFY(!result);
        QVERIFY(!result.error().message.empty());
        QCOMPARE(window.document()->skeleton()->boneCount(), before);
        QVERIFY(!bool(window.openProject(QDir(dir_.path()).filePath("missing.rig"))));
    }

    void exportedGltfReimports() {
        MainWindow window;
        showFigure(window);
        const QString file = QDir(dir_.path()).filePath("out.glb");
        QVERIFY(bool(window.exportGltfTo(file, {})));
        const auto back = io::importGltf(std::filesystem::path(file.toStdWString()));
        QVERIFY(back.ok());
        QCOMPARE(back.value().skeleton.boneCount(), window.document()->skeleton()->boneCount());
    }

    void imageExportNeedsTheViewport() {
        MainWindow window;
        showFigure(window);
        // Plateforme offscreen : pas de viewport OpenGL, l'export d'image doit échouer proprement.
        QVERIFY(!bool(window.exportImage(QDir(dir_.path()).filePath("x.png"), true, true)));
    }
};

RIGGER_REGISTER_QTEST(ProjectTest);
#include "test_project.moc"
