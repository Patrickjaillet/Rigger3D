// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>

#include "core/skeleton/fk.hpp"
#include "core/skeleton/symmetry.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Preferences.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

glm::vec3 worldPos(const Document& doc, BoneId id) {
    return glm::vec3(computeGlobals(*doc.skeleton(), doc.pose())[id][3]);
}

WorldDelta translation(const glm::vec3& t) {
    WorldDelta d;
    d.translation = t;
    return d;
}

// Personnage de test : RiggedFigure (19 os, noms _L_ / _R_, un maillage skinné).
std::shared_ptr<Character> figure() { return loadCharacter("RiggedFigure.glb"); }

}  // namespace

class RiggingTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    // --- Sélection, visibilité --------------------------------------------------------------

    void selectAllNoneToggleAndRange() {
        Document doc;
        doc.setCharacter(figure());
        doc.selectAll();
        QCOMPARE(doc.selection().size(), doc.skeleton()->boneCount());
        doc.clearSelection();
        QVERIFY(doc.selection().empty());

        const auto& order = doc.skeleton()->order();
        doc.setSelection({order[2]});
        doc.toggleInSelection(order[4]);
        QCOMPARE(doc.selection().size(), std::size_t{2});
        doc.toggleInSelection(order[4]);
        QCOMPARE(doc.selection().size(), std::size_t{1});

        doc.setSelection({order[2]});
        doc.selectRangeTo(order[6]);  // plage dans l'ordre hiérarchique, de l'ancre à la cible
        QCOMPARE(doc.selection().size(), std::size_t{5});
        QVERIFY(doc.isSelected(order[2]) && doc.isSelected(order[6]));
        doc.selectRangeTo(order[3]);  // l'ancre reste order[2]
        QCOMPARE(doc.selection().size(), std::size_t{2});
    }

    void hiddenLockedAndIsolatedBonesAreNotPickable() {
        Document doc;
        doc.setCharacter(figure());
        const BoneId a = doc.skeleton()->order()[1];
        const BoneId b = doc.skeleton()->order()[2];
        QVERIFY(doc.isBonePickable(a));
        doc.setBoneFlags(a, BoneFlags::Hidden);
        QVERIFY(!doc.isBoneVisible(a));
        QVERIFY(!doc.isBonePickable(a));
        doc.setBoneFlags(a, BoneFlags::Locked);
        QVERIFY(doc.isBoneVisible(a));
        QVERIFY(!doc.isBonePickable(a));  // verrouillé : visible mais pas sélectionnable
        doc.setBoneFlags(a, BoneFlags::None);

        doc.selectAll();
        QVERIFY(doc.isSelected(a));
        doc.setBoneFlags(b, BoneFlags::Hidden);
        doc.selectAll();
        QVERIFY(!doc.isSelected(b));  // « tout sélectionner » ignore les os masqués

        doc.setBoneFlags(b, BoneFlags::None);
        doc.setSelection({a});
        QSignalSpy spy(&doc, &Document::visibilityChanged);
        doc.setIsolated(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(doc.isBoneVisible(a));
        QVERIFY(!doc.isBoneVisible(b));
        QVERIFY(!doc.isBonePickable(b));
        doc.setIsolated(false);
        QVERIFY(doc.isBoneVisible(b));
        doc.clearSelection();
        doc.setIsolated(true);  // sélection vide : sans effet
        QVERIFY(!doc.isIsolated());
    }

    void hideShowLockUnlockCommands() {
        Document doc;
        doc.setCharacter(figure());
        const BoneId a = doc.skeleton()->order()[1];
        const BoneId b = doc.skeleton()->order()[2];
        doc.setSelection({a, b});
        doc.hideSelected();
        QVERIFY(!doc.isBoneVisible(a) && !doc.isBoneVisible(b));
        doc.showAll();
        QVERIFY(doc.isBoneVisible(a) && doc.isBoneVisible(b));
        doc.lockSelected();
        QVERIFY(hasFlag(doc.skeleton()->bone(a).flags, BoneFlags::Locked));
        doc.unlockAll();
        QVERIFY(!hasFlag(doc.skeleton()->bone(a).flags, BoneFlags::Locked));
    }

    // --- Gizmo : repère et manipulation -----------------------------------------------------

    void gizmoFrameFollowsSelectionAndSpace() {
        Document doc;
        doc.setCharacter(figure());
        glm::vec3 origin;
        glm::quat orientation;
        QVERIFY(!doc.gizmoFrame(origin, orientation));  // rien de sélectionné

        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        doc.setSelection({arm});
        QVERIFY(doc.gizmoFrame(origin, orientation));
        QVERIFY(glm::length(origin - worldPos(doc, arm)) < 1e-5f);

        QCOMPARE(doc.gizmoSpace(), GizmoSpace::Local);
        QSignalSpy spy(&doc, &Document::gizmoSpaceChanged);
        doc.cycleGizmoSpace();
        QCOMPARE(doc.gizmoSpace(), GizmoSpace::Global);
        QVERIFY(doc.gizmoFrame(origin, orientation));
        QVERIFY(std::abs(glm::dot(orientation, glm::quat(1, 0, 0, 0))) > 0.9999f);
        doc.cycleGizmoSpace();
        QCOMPARE(doc.gizmoSpace(), GizmoSpace::Parent);
        doc.cycleGizmoSpace();
        QCOMPARE(doc.gizmoSpace(), GizmoSpace::Local);
        QCOMPARE(spy.count(), 3);

        // Les os verrouillés ne sont pas manipulables.
        doc.setBoneFlags(arm, BoneFlags::Locked);
        QVERIFY(!doc.gizmoFrame(origin, orientation));
        QVERIFY(!doc.beginManipulation());
    }

    void manipulationMovesBonesAbsolutelyAndOnlyTopLevelOnes() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const BoneId forearm = doc.skeleton()->children(arm).front();
        const glm::vec3 armBefore = worldPos(doc, arm);
        const glm::vec3 forearmBefore = worldPos(doc, forearm);

        doc.setSelection({arm, forearm});  // le second suit déjà le premier
        QSignalSpy active(&doc, &Document::manipulationChanged);
        QVERIFY(doc.beginManipulation());
        QVERIFY(doc.isManipulating());
        QVERIFY(!doc.beginManipulation());  // déjà en cours
        doc.updateManipulation(translation({0.5f, 0.0f, 0.25f}));
        doc.updateManipulation(translation({0.5f, 0.0f, 0.25f}));  // delta TOTAL : pas de cumul
        QVERIFY(glm::length(worldPos(doc, arm) - (armBefore + glm::vec3(0.5f, 0, 0.25f))) < 1e-4f);
        QVERIFY(glm::length(worldPos(doc, forearm) - (forearmBefore + glm::vec3(0.5f, 0, 0.25f))) < 1e-4f);
        doc.updateManipulation(translation({0, 0, 0}));  // retour au départ
        QVERIFY(glm::length(worldPos(doc, arm) - armBefore) < 1e-4f);
        doc.endManipulation();
        QVERIFY(!doc.isManipulating());
        QCOMPARE(active.count(), 2);  // début puis fin
    }

    void rotationKeepsTheOriginAndRotatesChildren() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const BoneId forearm = doc.skeleton()->children(arm).front();
        const glm::vec3 armBefore = worldPos(doc, arm);
        const glm::vec3 forearmBefore = worldPos(doc, forearm);
        doc.setSelection({arm});
        WorldDelta delta;
        delta.rotation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0, 0, 1));
        QVERIFY(doc.beginManipulation());
        doc.updateManipulation(delta);
        doc.endManipulation();
        QVERIFY(glm::length(worldPos(doc, arm) - armBefore) < 1e-4f);
        const glm::vec3 expected = armBefore + delta.rotation * (forearmBefore - armBefore);
        QVERIFY(glm::length(worldPos(doc, forearm) - expected) < 1e-3f);
    }

    void riggingEditsWriteTheBindPoseInRigModeAndWarnOnce() {
        Document doc;
        doc.setCharacter(figure());
        doc.setMode(Mode::Rig);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const glm::vec3 before(doc.skeleton()->bindGlobals()[arm][3]);
        doc.setSelection({arm});
        QSignalSpy warnings(&doc, &Document::warning);
        QVERIFY(doc.beginManipulation());
        doc.updateManipulation(translation({0, 0.3f, 0}));
        doc.endManipulation();
        QVERIFY(glm::length(glm::vec3(doc.skeleton()->bindGlobals()[arm][3]) -
                            (before + glm::vec3(0, 0.3f, 0))) < 1e-4f);
        QCOMPARE(warnings.count(), 1);  // personnage skinné : avertissement
        QVERIFY(doc.beginManipulation());
        doc.endManipulation();
        QCOMPARE(warnings.count(), 1);  // une seule fois par personnage
    }

    void symmetryMirrorsTheEditToTheOppositeBone() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        const BoneId left = doc.skeleton()->find("arm_joint_L_1");
        const BoneId right = findMirrorBone(*doc.skeleton(), left);
        QVERIFY(right != kNoBone);
        doc.setSymmetryX(true);
        doc.setSelection({left});
        WorldDelta delta;
        delta.rotation = glm::angleAxis(glm::radians(35.0f), glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f)));
        QVERIFY(doc.beginManipulation());
        doc.updateManipulation(delta);
        doc.endManipulation();
        const auto globals = computeGlobals(*doc.skeleton(), doc.pose());
        const glm::mat4 mirrored = mirrorWorldX(globals[left]);
        for (int c = 0; c < 4; ++c) {
            QVERIFY(glm::length(glm::vec3(globals[right][c]) - glm::vec3(mirrored[c])) < 1e-3f);
        }
        // Sans symétrie, l'os opposé ne bouge pas.
        doc.resetPose(false);
        doc.setSymmetryX(false);
        const glm::vec3 rightBefore = worldPos(doc, right);
        QVERIFY(doc.beginManipulation());
        doc.updateManipulation(translation({0, 0.2f, 0}));
        doc.endManipulation();
        QVERIFY(glm::length(worldPos(doc, right) - rightBefore) < 1e-5f);
    }

    // --- Édition du rig ---------------------------------------------------------------------

    void addBoneCreatesAChildAtTheTipAndSelectsIt() {
        Document doc;
        doc.setCharacter(figure());
        QVERIFY(!doc.addBone().ok());  // pas en mode Rig
        doc.setMode(Mode::Rig);
        const std::size_t count = doc.skeleton()->boneCount();
        const BoneId parent = doc.skeleton()->find("neck_joint_1");
        doc.setSelection({parent});
        QSignalSpy hierarchy(&doc, &Document::hierarchyChanged);
        QVERIFY(doc.addBone().ok());
        QCOMPARE(doc.skeleton()->boneCount(), count + 1);
        QCOMPARE(hierarchy.count(), 1);
        QCOMPARE(doc.selection().size(), std::size_t{1});
        const BoneId created = doc.selection().front();
        QCOMPARE(doc.skeleton()->bone(created).parent, parent);
        QVERIFY(doc.skeleton()->bone(created).bindLocal.t.y > 0.0f);  // à l'extrémité du parent

        doc.clearSelection();
        QVERIFY(doc.addBone().ok());  // sans sélection : os racine
        QCOMPARE(doc.skeleton()->bone(doc.selection().front()).parent, kNoBone);
        QCOMPARE(doc.pose().size(), doc.skeleton()->slotCount());
    }

    void addBoneWithSymmetryCreatesTheTwin() {
        Document doc;
        doc.setCharacter(figure());
        doc.setMode(Mode::Rig);
        const std::size_t count = doc.skeleton()->boneCount();
        doc.setSymmetryX(true);
        doc.setSelection({doc.skeleton()->find("arm_joint_L_1")});
        QVERIFY(doc.addBone().ok());
        QCOMPARE(doc.skeleton()->boneCount(), count + 2);
        QVERIFY(doc.skeleton()->find("arm_joint_L_1.child") != kNoBone);
        const BoneId twin = doc.skeleton()->find("arm_joint_R_1.child");
        QVERIFY(twin != kNoBone);
        QCOMPARE(doc.skeleton()->bone(twin).parent, doc.skeleton()->find("arm_joint_R_1"));
    }

    void subdivideAndDeleteKeepTheSkinningValid() {
        Document doc;
        doc.setCharacter(figure());
        doc.setMode(Mode::Rig);
        const BoneId leg = doc.skeleton()->find("leg_joint_L_2");
        doc.setSelection({leg});
        const std::size_t count = doc.skeleton()->boneCount();
        QVERIFY(doc.subdivideSelected().ok());
        QCOMPARE(doc.skeleton()->boneCount(), count + 1);
        QVERIFY(doc.skeleton()->find("leg_joint_L_2.sub") != kNoBone);

        doc.setSelection({leg});
        QSignalSpy meshes(&doc, &Document::meshDataChanged);
        QVERIFY(doc.deleteSelected(false).ok());
        QVERIFY(!doc.skeleton()->contains(leg));
        QCOMPARE(meshes.count(), 1);
        QVERIFY(doc.selection().empty());
        for (const Mesh& mesh : doc.character()->meshes) {
            QVERIFY(mesh.validate().ok());  // poids renormalisés, os existants
        }
        QCOMPARE(doc.pose().size(), doc.skeleton()->slotCount());

        const BoneId arm = doc.skeleton()->find("arm_joint_R_1");
        doc.setSelection({arm});
        QVERIFY(doc.deleteSelected(true).ok());  // avec ses descendants
        QVERIFY(doc.skeleton()->find("arm_joint_R_2") == kNoBone);
        for (const Mesh& mesh : doc.character()->meshes) {
            QVERIFY(mesh.validate().ok());
        }
        doc.setMode(Mode::Animate);
        QVERIFY(!doc.deleteSelected(false).ok());  // seulement en mode Rig
    }

    // --- Pose : reset, copier / coller ------------------------------------------------------

    void resetPoseSelectionAndAll() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setClipIndex(-1);
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        const BoneId b = doc.skeleton()->find("arm_joint_R_1");
        Transform t = doc.boneLocal(a);
        t.r = glm::angleAxis(glm::radians(50.0f), glm::vec3(0, 0, 1));
        doc.setBoneLocal(a, t);
        doc.setBoneLocal(b, t);
        doc.setSelection({a});
        doc.resetPose(true);
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, doc.skeleton()->bone(a).bindLocal.r)) > 0.9999f);
        QVERIFY(std::abs(glm::dot(doc.pose()[b].r, doc.skeleton()->bone(b).bindLocal.r)) <
                0.999f);  // inchangé
        doc.resetPose(false);
        QVERIFY(std::abs(glm::dot(doc.pose()[b].r, doc.skeleton()->bone(b).bindLocal.r)) > 0.9999f);
        doc.setMode(Mode::Rig);
        doc.setBoneLocal(a, t);
        doc.resetPose(false);  // sans effet en mode Rig (on ne touche pas au bind)
        QVERIFY(std::abs(glm::dot(doc.skeleton()->bone(a).bindLocal.r, t.r)) > 0.9999f);
    }

    void copyAndPasteRestoresAPose() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setClipIndex(-1);
        QVERIFY(!doc.hasPoseClipboard());
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        Transform t = doc.boneLocal(a);
        t.r = glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 1, 0));
        doc.setBoneLocal(a, t);
        doc.setSelection({a});
        doc.copyPose(true);
        QVERIFY(doc.hasPoseClipboard());

        doc.resetPose(false);
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, t.r)) < 0.999f);
        doc.pastePose(false);
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, t.r)) > 0.9999f);
    }

    void pasteMirroredFlipsLeftAndRight() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setClipIndex(-1);
        const BoneId left = doc.skeleton()->find("arm_joint_L_1");
        const BoneId right = findMirrorBone(*doc.skeleton(), left);
        Transform t = doc.boneLocal(left);
        t.r = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3(0.2f, 0.4f, 1.0f)));
        doc.setBoneLocal(left, t);
        const auto sourceGlobals = computeGlobals(*doc.skeleton(), doc.pose());
        doc.setSelection({left});
        doc.copyPose(true);
        doc.resetPose(false);
        doc.pastePose(true);  // l'entrée « L » est collée sur l'os « R »
        const auto globals = computeGlobals(*doc.skeleton(), doc.pose());
        const glm::mat4 expected = mirrorWorldX(sourceGlobals[left]);
        for (int c = 0; c < 4; ++c) {
            QVERIFY(glm::length(glm::vec3(globals[right][c]) - glm::vec3(expected[c])) < 1e-3f);
        }
    }

    // --- Actions de la fenêtre ---------------------------------------------------------------

    void actionEnablementFollowsModeAndSelection() {
        MainWindow w;
        const auto act = [&](const QString& id) { return w.actions()->action(id); };
        QVERIFY(!act(ActionId::SelectAll)->isEnabled());  // pas de personnage
        QVERIFY(w.importFile(rigger::testing::dataFile("RiggedFigure.glb")).ok());
        QVERIFY(act(ActionId::SelectAll)->isEnabled());
        QVERIFY(!act(ActionId::RigAddBone)->isEnabled());  // mode Animate
        QVERIFY(!act(ActionId::PoseReset)->isEnabled());   // rien de sélectionné

        w.document()->setSelection({w.document()->skeleton()->find("arm_joint_L_1")});
        QVERIFY(act(ActionId::PoseReset)->isEnabled());
        QVERIFY(act(ActionId::SelectNone)->isEnabled());
        QVERIFY(!act(ActionId::RigDelete)->isEnabled());

        act(ActionId::ModeRig)->trigger();
        QVERIFY(act(ActionId::RigAddBone)->isEnabled());
        QVERIFY(act(ActionId::RigDelete)->isEnabled());
        QVERIFY(act(ActionId::RigSubdivide)->isEnabled());
        QVERIFY(!act(ActionId::PoseReset)->isEnabled());
        QVERIFY(act(ActionId::PoseSymmetry)->isEnabled());
    }

    void editActionsDriveTheDocument() {
        MainWindow w;
        const auto act = [&](const QString& id) { return w.actions()->action(id); };
        QVERIFY(w.importFile(rigger::testing::dataFile("RiggedFigure.glb")).ok());
        w.document()->setPlaying(false);

        act(ActionId::SelectAll)->trigger();
        QCOMPARE(w.document()->selection().size(), w.document()->skeleton()->boneCount());
        act(ActionId::SelectNone)->trigger();
        QVERIFY(w.document()->selection().empty());

        w.document()->setSelection({w.document()->skeleton()->find("arm_joint_L_1")});
        act(ActionId::BoneHide)->trigger();
        QVERIFY(!w.document()->isBoneVisible(w.document()->skeleton()->find("arm_joint_L_1")));
        act(ActionId::BoneShowAll)->trigger();
        QVERIFY(w.document()->isBoneVisible(w.document()->skeleton()->find("arm_joint_L_1")));

        act(ActionId::BoneIsolate)->setChecked(true);
        QVERIFY(w.document()->isIsolated());
        act(ActionId::BoneIsolate)->setChecked(false);
        QVERIFY(!w.document()->isIsolated());

        act(ActionId::PoseSymmetry)->setChecked(true);
        QVERIFY(w.document()->symmetryX());
        w.document()->setSymmetryX(false);
        QVERIFY(!act(ActionId::PoseSymmetry)->isChecked());  // l'action suit le Document

        // Les valeurs de statut reflètent l'espace du gizmo et la symétrie.
        w.document()->setTool(Tool::Move);
        w.document()->cycleGizmoSpace();
        QVERIFY2(w.findChild<QLabel*>("statusTool")->text().contains(QStringLiteral("Global")),
                 qPrintable(w.findChild<QLabel*>("statusTool")->text()));
        w.document()->setSymmetryX(true);
        QVERIFY(w.findChild<QLabel*>("statusTool")->text().contains(QStringLiteral("Mirror X")));
    }

    void riggingActionsEditTheSkeleton() {
        MainWindow w;
        const auto act = [&](const QString& id) { return w.actions()->action(id); };
        QVERIFY(w.importFile(rigger::testing::dataFile("RiggedFigure.glb")).ok());
        act(ActionId::ModeRig)->trigger();
        const std::size_t count = w.document()->skeleton()->boneCount();
        w.document()->setSelection({w.document()->skeleton()->find("neck_joint_1")});
        act(ActionId::RigAddBone)->trigger();
        QCOMPARE(w.document()->skeleton()->boneCount(), count + 1);
        act(ActionId::RigDelete)->trigger();  // supprime le nouvel os (sélectionné)
        QCOMPARE(w.document()->skeleton()->boneCount(), count);

        // Extrusion : comme « ajouter », puis l'outil Déplacer est activé pour positionner l'os.
        w.document()->setSelection({w.document()->skeleton()->find("neck_joint_1")});
        w.document()->setTool(Tool::Select);
        act(ActionId::RigExtrude)->trigger();
        QCOMPARE(w.document()->skeleton()->boneCount(), count + 1);
        QCOMPARE(w.document()->tool(), Tool::Move);
    }
};

RIGGER_REGISTER_QTEST(RiggingTest);
#include "test_rigging.moc"
