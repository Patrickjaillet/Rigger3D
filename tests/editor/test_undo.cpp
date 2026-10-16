// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <DockWidget.h>

#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>
#include <QUndoView>
#include <algorithm>
#include <array>
#include <random>

#include "core/skeleton/fk.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/HistoryPanel.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Preferences.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

// Générateur déterministe (graine fixe) : séquences reproductibles.
class Rng {
public:
    explicit Rng(unsigned seed) : gen_(seed) {}
    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen_); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen_); }
    glm::vec3 vec(float range) {
        return {uniform(-range, range), uniform(-range, range), uniform(-range, range)};
    }

private:
    std::mt19937 gen_;
};

// Comparaison structurelle de l'état éditable d'un Document.
struct DocState {
    struct B {
        BoneId id;
        std::string name;
        BoneId parent;
        glm::vec3 t, s;
        glm::quat r;
        float length;
        BoneFlags flags;
        glm::vec3 color;
        bool operator==(const B& o) const {
            return id == o.id && name == o.name && parent == o.parent && t == o.t && s == o.s && r == o.r &&
                   length == o.length && flags == o.flags && color == o.color;
        }
    };
    std::vector<B> bones;
    std::vector<std::vector<std::pair<std::array<BoneId, 4>, std::array<float, 4>>>> weights;
    std::vector<std::size_t> poseHash;
    std::vector<glm::vec3> poseT;
    bool operator==(const DocState& o) const {
        // La pose n'est pas comparée : en mode Rig elle est dérivée de la bind pose, en Animate elle est
        // transitoire (le fuzzing du core couvre les commandes de pose).
        return bones == o.bones && weights == o.weights;
    }
};

DocState stateOf(const Document& doc) {
    DocState s;
    const Skeleton& sk = *doc.skeleton();
    for (const BoneId id : sk.order()) {
        const Bone& b = sk.bone(id);
        s.bones.push_back(
            {id, b.name, b.parent, b.bindLocal.t, b.bindLocal.s, b.bindLocal.r, b.length, b.flags, b.color});
    }
    for (const Mesh& mesh : doc.character()->meshes) {
        auto& out = s.weights.emplace_back();
        for (const SkinWeight& w : mesh.skinWeights) out.push_back({w.joints, w.weights});
    }
    for (const BoneId id : sk.order()) {
        if (id < doc.pose().size()) s.poseT.push_back(doc.pose()[id].t);
    }
    return s;
}

WorldDelta moveDelta(const glm::vec3& t) {
    WorldDelta d;
    d.translation = t;
    return d;
}

std::shared_ptr<Character> staticFigure() { return loadCharacter("RiggedFigure.glb"); }

}  // namespace

class UndoTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void emptyHistoryAtStart() {
        Document doc;
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.canRedo());
        QVERIFY(doc.undoText().isEmpty());
        QVERIFY(doc.isClean());
        doc.setCharacter(staticFigure());
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.isClean());
        doc.undo();  // sans effet
        doc.redo();
    }

    void gizmoDragIsASingleEntry() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setPlaying(false);
        doc.setClipIndex(-1);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const glm::vec3 before(computeGlobals(*doc.skeleton(), doc.pose())[arm][3]);
        doc.setSelection({arm});
        QVERIFY(doc.beginManipulation());
        QVERIFY(!doc.canUndo());  // pendant le glissé
        for (int i = 1; i <= 30; ++i)
            doc.updateManipulation(moveDelta({0.01f * static_cast<float>(i), 0, 0}));
        doc.endManipulation();
        QCOMPARE(doc.history().undoCount(), std::size_t{1});
        QVERIFY(doc.canUndo());
        QCOMPARE(doc.undoText(), QStringLiteral("Transform Bones"));
        QVERIFY(!doc.isClean());

        const glm::vec3 after(computeGlobals(*doc.skeleton(), doc.pose())[arm][3]);
        QVERIFY(glm::length(after - (before + glm::vec3(0.3f, 0, 0))) < 1e-4f);
        doc.undo();
        QVERIFY(glm::length(glm::vec3(computeGlobals(*doc.skeleton(), doc.pose())[arm][3]) - before) < 1e-5f);
        QVERIFY(doc.isClean());
        QCOMPARE(doc.redoText(), QStringLiteral("Transform Bones"));
        doc.redo();
        QVERIFY(glm::length(glm::vec3(computeGlobals(*doc.skeleton(), doc.pose())[arm][3]) - after) < 1e-5f);

        // Un glissé sans effet n'ajoute rien.
        doc.setSelection({arm});
        QVERIFY(doc.beginManipulation());
        doc.endManipulation();
        QCOMPARE(doc.history().undoCount(), std::size_t{1});
    }

    void inspectorScrubMergesUntilTheCursorIsReleased() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setPlaying(false);
        doc.setMode(Mode::Rig);
        const BoneId id = doc.skeleton()->find("neck_joint_1");
        const Transform original = doc.skeleton()->bone(id).bindLocal;
        doc.setSelection({id});
        for (int i = 1; i <= 40; ++i) {
            Transform t = original;
            t.t.x = 0.01f * static_cast<float>(i);
            doc.setBoneLocal(id, t);
        }
        QCOMPARE(doc.history().undoCount(), std::size_t{1});
        doc.endContinuousEdit();  // curseur relâché
        Transform t = original;
        t.t.x = 5.0f;
        doc.setBoneLocal(id, t);
        QCOMPARE(doc.history().undoCount(), std::size_t{2});
        doc.undo();
        QCOMPARE(doc.skeleton()->bone(id).bindLocal.t.x, 0.4f);  // fin du premier glissé
        doc.undo();
        QCOMPARE(doc.skeleton()->bone(id).bindLocal.t.x, original.t.x);  // état AVANT le premier glissé
    }

    void propertyEditsAreUndoable() {
        Document doc;
        doc.setCharacter(staticFigure());
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        const std::string oldName = doc.skeleton()->bone(a).name;
        const float oldLength = doc.skeleton()->bone(a).length;

        QVERIFY(doc.renameBone(a, QStringLiteral("shoulder")).ok());
        QCOMPARE(doc.undoText(), QStringLiteral("Rename Bone (shoulder)"));
        QVERIFY(doc.setBoneLength(a, 0.9f));
        QVERIFY(doc.setBoneColor(a, {1, 0, 0}));
        QVERIFY(doc.setBoneFlags(a, BoneFlags::Hidden));
        doc.undo();
        QVERIFY(!hasFlag(doc.skeleton()->bone(a).flags, BoneFlags::Hidden));
        doc.undo();
        doc.undo();
        QCOMPARE(doc.skeleton()->bone(a).length, oldLength);
        doc.undo();
        QCOMPARE(doc.skeleton()->bone(a).name, oldName);
        QVERIFY(doc.skeleton()->find("shoulder") == kNoBone);
        doc.redo();
        QCOMPARE(doc.skeleton()->bone(a).name, std::string("shoulder"));
        QVERIFY(!doc.renameBone(a, QStringLiteral(" ")).ok());  // erreur : rien n'est ajouté
        QCOMPARE(doc.history().undoCount(), std::size_t{1});
    }

    void visibilityAndLockCommandsAreUndoable() {
        Document doc;
        doc.setCharacter(staticFigure());
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        const BoneId b = doc.skeleton()->find("arm_joint_R_1");
        doc.setSelection({a, b});
        doc.hideSelected();
        QVERIFY(!doc.isBoneVisible(a) && !doc.isBoneVisible(b));
        doc.lockSelected();
        doc.undo();
        QVERIFY(!hasFlag(doc.skeleton()->bone(a).flags, BoneFlags::Locked));
        doc.undo();
        QVERIFY(doc.isBoneVisible(a) && doc.isBoneVisible(b));
        doc.redo();
        doc.showAll();
        QVERIFY(doc.isBoneVisible(a));
        doc.undo();
        QVERIFY(!doc.isBoneVisible(a));  // « tout afficher » annulé : de nouveau masqué
    }

    void structureEditsAreUndoableWithWeightsAndSelection() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setPlaying(false);
        doc.setMode(Mode::Rig);
        const DocState initial = stateOf(doc);
        const std::size_t count = doc.skeleton()->boneCount();

        doc.setSelection({doc.skeleton()->find("neck_joint_1")});
        QVERIFY(doc.addBone().ok());
        QCOMPARE(doc.undoText(), QStringLiteral("Add Bone"));
        const BoneId created = doc.selection().front();
        doc.setSelection({doc.skeleton()->find("leg_joint_L_2")});
        QVERIFY(doc.subdivideSelected().ok());
        doc.setSelection({doc.skeleton()->find("arm_joint_R_1")});
        QVERIFY(doc.deleteSelected(false).ok());
        const DocState finalState = stateOf(doc);
        QVERIFY(doc.skeleton()->find("arm_joint_R_1") == kNoBone);

        QSignalSpy hierarchy(&doc, &Document::hierarchyChanged);
        doc.undo();  // suppression annulée : l'os revient avec son identifiant et ses poids
        QVERIFY(doc.skeleton()->find("arm_joint_R_1") != kNoBone);
        QVERIFY(hierarchy.count() >= 1);
        doc.undo();
        doc.undo();
        QCOMPARE(doc.skeleton()->boneCount(), count);
        QVERIFY(doc.skeleton()->find("arm_joint_L_1") != kNoBone);
        QVERIFY(
            !doc.skeleton()->contains(created));  // l'os ajouté a disparu, la sélection ne le référence plus
        QVERIFY(std::find(doc.selection().begin(), doc.selection().end(), created) == doc.selection().end());
        QVERIFY(stateOf(doc) == initial);
        for (const Mesh& mesh : doc.character()->meshes) QVERIFY(mesh.validate().ok());

        doc.redo();
        doc.redo();
        doc.redo();
        QVERIFY(stateOf(doc) == finalState);
    }

    void reparentFromTheTreeIsUndoable() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setMode(Mode::Rig);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const BoneId oldParent = doc.skeleton()->bone(arm).parent;
        const glm::vec3 before(doc.skeleton()->bindGlobals()[arm][3]);
        QVERIFY(doc.reparentBone(arm, doc.skeleton()->find("leg_joint_L_1"), true).ok());
        QCOMPARE(doc.undoText(), QStringLiteral("Reparent Bone"));
        QVERIFY(!doc.reparentBone(arm, arm, true).ok());  // cycle refusé : pas d'entrée
        QCOMPARE(doc.history().undoCount(), std::size_t{1});
        doc.undo();
        QCOMPARE(doc.skeleton()->bone(arm).parent, oldParent);
        QVERIFY(glm::length(glm::vec3(doc.skeleton()->bindGlobals()[arm][3]) - before) < 1e-5f);
    }

    void poseCommandsAreUndoable() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setPlaying(false);
        doc.setClipIndex(-1);
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        Transform t = doc.boneLocal(a);
        t.r = glm::angleAxis(glm::radians(50.0f), glm::vec3(0, 0, 1));
        const Transform rest = doc.boneLocal(a);
        doc.setBoneLocal(a, t);
        doc.setSelection({a});
        doc.copyPose(true);
        doc.resetPose(false);
        QCOMPARE(doc.undoText(), QStringLiteral("Reset Pose"));
        doc.pastePose(false);
        QCOMPARE(doc.undoText(), QStringLiteral("Paste Pose"));
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, t.r)) > 0.9999f);
        doc.undo();  // coller annulé
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, rest.r)) > 0.9999f);
        doc.undo();  // reset annulé : la pose retouchée revient
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, t.r)) > 0.9999f);
        doc.undo();
        QVERIFY(std::abs(glm::dot(doc.pose()[a].r, rest.r)) > 0.9999f);
    }

    void newCharacterClearsTheHistory() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.renameBone(0, QStringLiteral("x"));
        QVERIFY(doc.canUndo());
        doc.setCharacter(staticFigure());
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.isClean());
        QCOMPARE(doc.undoStack()->count(), 0);
    }

    // --- Adaptateur QUndoStack ---------------------------------------------------------------

    void undoStackMirrorsTheHistoryAndRelaysUserNavigation() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setPlaying(false);
        doc.setMode(Mode::Rig);
        const BoneId a = doc.skeleton()->find("neck_joint_1");
        const DocState initial = stateOf(doc);
        doc.renameBone(a, QStringLiteral("head"));
        doc.setBoneLength(a, 0.5f);
        doc.endContinuousEdit();
        doc.setBoneFlags(a, BoneFlags::Locked);
        const DocState finalState = stateOf(doc);

        QUndoStack* stack = doc.undoStack();
        QCOMPARE(stack->count(), 3);
        QCOMPARE(stack->index(), 3);
        QCOMPARE(stack->text(0), QStringLiteral("Rename Bone (head)"));
        QCOMPARE(stack->text(2), QStringLiteral("Change Bone Flags (head)"));

        // L'utilisateur clique la première ligne de la vue : tout est annulé jusqu'à l'état initial.
        stack->setIndex(0);
        QCOMPARE(doc.history().undoCount(), std::size_t{0});
        QCOMPARE(doc.history().redoCount(), std::size_t{3});
        QVERIFY(stateOf(doc) == initial);
        QCOMPARE(stack->count(), 3);
        stack->setIndex(2);
        QCOMPARE(doc.history().undoCount(), std::size_t{2});
        stack->setIndex(3);
        QVERIFY(stateOf(doc) == finalState);

        // Une action annulée depuis le Document met la pile à jour.
        doc.undo();
        QCOMPARE(stack->index(), 2);
        doc.setBoneColor(a, {0, 1, 0});  // écarte l'avenir
        QCOMPARE(stack->count(), 3);
        QCOMPARE(stack->index(), 3);
        QCOMPARE(stack->text(2), QStringLiteral("Edit Bone (head)"));
    }

    void undoStackMirrorFollowsMergedCommandsAndTheLimit() {
        Document doc;
        doc.setCharacter(staticFigure());
        doc.setMode(Mode::Rig);
        const BoneId a = doc.skeleton()->find("neck_joint_1");
        Transform t = doc.skeleton()->bone(a).bindLocal;
        for (int i = 0; i < 25; ++i) {
            t.t.x = static_cast<float>(i);
            doc.setBoneLocal(a, t);
        }
        QCOMPARE(doc.undoStack()->count(), 1);  // les commandes fusionnées ne créent pas de lignes
        doc.history().setLimit(3);
        for (int i = 0; i < 6; ++i) {
            doc.endContinuousEdit();
            t.t.y = static_cast<float>(i);
            doc.setBoneLocal(a, t);
        }
        QCOMPARE(doc.history().undoCount(), std::size_t{3});
        QCOMPARE(doc.undoStack()->count(), 3);
    }

    void historyPanelShowsTheStack() {
        Document doc;
        doc.setCharacter(staticFigure());
        HistoryPanel panel(&doc);
        QCOMPARE(panel.view()->stack(), doc.undoStack());
        doc.renameBone(0, QStringLiteral("root2"));
        QCOMPARE(panel.view()->stack()->count(), 1);
    }

    // --- Fenêtre : actions, titre ------------------------------------------------------------

    void undoRedoActionsShowTheCommandNameAndTheTitleShowsTheModifiedMarker() {
        MainWindow w;
        const auto act = [&](const QString& id) { return w.actions()->action(id); };
        QVERIFY(w.importFile(rigger::testing::dataFile("RiggedFigure.glb")).ok());
        QVERIFY(!w.isWindowModified());
        QVERIFY(!act(ActionId::EditUndo)->isEnabled());
        QCOMPARE(act(ActionId::EditUndo)->text(), QStringLiteral("&Undo"));

        const BoneId a = w.document()->skeleton()->find("arm_joint_L_1");
        w.document()->renameBone(a, QStringLiteral("shoulder"));
        QVERIFY(act(ActionId::EditUndo)->isEnabled());
        QCOMPARE(act(ActionId::EditUndo)->text(), QStringLiteral("&Undo Rename Bone (shoulder)"));
        QVERIFY(w.isWindowModified());  // « * » dans le titre
        QVERIFY(w.windowTitle().contains(QStringLiteral("[*]")));

        act(ActionId::EditUndo)->trigger();
        QVERIFY(!w.isWindowModified());  // revenu à l'état propre : plus de marqueur
        QVERIFY(!act(ActionId::EditUndo)->isEnabled());
        QVERIFY(act(ActionId::EditRedo)->isEnabled());
        QCOMPARE(act(ActionId::EditRedo)->text(), QStringLiteral("&Redo Rename Bone (shoulder)"));
        act(ActionId::EditRedo)->trigger();
        QVERIFY(w.isWindowModified());
        QCOMPARE(w.document()->skeleton()->bone(a).name, std::string("shoulder"));

        w.document()->markClean();
        QVERIFY(!w.isWindowModified());
        QCOMPARE(w.actions()->shortcut(ActionId::EditUndo), QKeySequence(QStringLiteral("Ctrl+Z")));
        QCOMPARE(w.actions()->shortcut(ActionId::EditRedo), QKeySequence(QStringLiteral("Ctrl+Y")));
    }

    void historyDockExistsAndActionNamesAreTranslated() {
        MainWindow w;
        QVERIFY(w.dock("dockHistory") != nullptr);
        QVERIFY(w.historyPanel() != nullptr);
        QVERIFY(w.importFile(rigger::testing::dataFile("RiggedFigure.glb")).ok());
        w.document()->renameBone(w.document()->skeleton()->find("arm_joint_L_1"), QStringLiteral("epaule"));
        w.preferences()->setLanguage(QStringLiteral("fr"));
        QVERIFY2(w.actions()->action(ActionId::EditUndo)->text().contains(QStringLiteral("Renommer")),
                 qPrintable(w.actions()->action(ActionId::EditUndo)->text()));
        QCOMPARE(w.dock("dockHistory")->windowTitle(), QStringLiteral("Historique"));
        w.preferences()->setLanguage(QStringLiteral("en"));
    }

    // --- Fuzzing au niveau du Document -------------------------------------------------------

    void fuzzingTheEditorOperations() {
        for (const unsigned seed : {11u, 12u}) {
            Rng rng(seed);
            Document doc;
            doc.setCharacter(staticFigure());
            doc.setPlaying(false);
            doc.setClipIndex(-1);
            doc.history().setLimit(5000);
            const DocState initial = stateOf(doc);

            const auto anyBone = [&]() {
                const auto& order = doc.skeleton()->order();
                return order[static_cast<std::size_t>(rng.integer(0, static_cast<int>(order.size()) - 1))];
            };
            for (int i = 0; i < 400; ++i) {
                const int op = rng.integer(0, 16);
                const bool rig = doc.mode() == Mode::Rig;
                switch (op) {
                    case 0:
                        doc.setMode(rig ? Mode::Animate : Mode::Rig);
                        break;
                    case 1:
                    case 2: {
                        const BoneId id = anyBone();
                        Transform t = doc.boneLocal(id);
                        t.t += rng.vec(0.3f);
                        doc.setBoneLocal(id, t);
                        break;
                    }
                    case 3:
                        doc.setBoneLength(anyBone(), rng.uniform(0.05f, 1.0f));
                        break;
                    case 4:
                        doc.setBoneColor(anyBone(), glm::abs(rng.vec(1.0f)));
                        break;
                    case 5:
                        doc.setBoneFlags(anyBone(),
                                         rng.integer(0, 2) == 0 ? BoneFlags::Locked : BoneFlags::None);
                        break;
                    case 6:
                        doc.renameBone(anyBone(), QStringLiteral("b%1").arg(rng.integer(0, 9999)));
                        break;
                    case 7: {
                        doc.setSelection({anyBone(), anyBone()});
                        if (doc.beginManipulation()) {
                            doc.updateManipulation(moveDelta(rng.vec(0.5f)));
                            doc.updateManipulation(moveDelta(rng.vec(0.5f)));
                            doc.endManipulation();
                        }
                        break;
                    }
                    case 8:
                        doc.setSelection({anyBone()});
                        doc.addBone();
                        break;
                    case 9:
                        doc.setSelection({anyBone()});
                        if (doc.skeleton()->boneCount() > 3) doc.deleteSelected(rng.integer(0, 1) != 0);
                        break;
                    case 10:
                        doc.setSelection({anyBone()});
                        doc.subdivideSelected();
                        break;
                    case 11:
                        doc.reparentBone(anyBone(), rng.integer(0, 3) == 0 ? kNoBone : anyBone(),
                                         rng.integer(0, 1) != 0);
                        break;
                    case 12:
                        doc.setSelection({anyBone()});
                        doc.resetPose(rng.integer(0, 1) != 0);
                        break;
                    case 13:
                        doc.copyPose(rng.integer(0, 1) != 0);
                        doc.pastePose(rng.integer(0, 1) != 0);
                        break;
                    case 14:
                        doc.setSelection({anyBone(), anyBone()});
                        if (rng.integer(0, 1) != 0)
                            doc.hideSelected();
                        else
                            doc.showAll();
                        break;
                    case 15:
                        doc.endContinuousEdit();
                        break;
                    default:
                        for (int k = rng.integer(1, 3); k > 0; --k) {
                            if (rng.integer(0, 1) != 0)
                                doc.undo();
                            else
                                doc.redo();
                        }
                        break;
                }
            }
            // Rejoue tout l'avenir, note l'état final de la pile, annule tout, puis rétablit tout.
            while (doc.canRedo()) doc.redo();
            const DocState top = stateOf(doc);
            const std::size_t entries = doc.history().undoCount();
            QVERIFY(entries > 20);
            QCOMPARE(doc.undoStack()->count(), static_cast<int>(doc.history().size()));
            while (doc.canUndo()) doc.undo();
            // La pose n'est comparée qu'en mode Animate : en Rig elle est dérivée de la bind pose.
            doc.setMode(Mode::Animate);
            doc.setMode(Mode::Rig);
            QVERIFY(stateOf(doc) == initial);
            QVERIFY(doc.isClean());
            while (doc.canRedo()) doc.redo();
            QVERIFY(stateOf(doc) == top);
            for (const Mesh& mesh : doc.character()->meshes) QVERIFY(mesh.validate().ok());
        }
    }
};

RIGGER_REGISTER_QTEST(UndoTest);
#include "test_undo.moc"
