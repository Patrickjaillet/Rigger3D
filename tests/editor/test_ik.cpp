// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTest>
#include <cmath>

#include "core/skeleton/fk.hpp"
#include "editor/Document.hpp"
#include "editor/IKPanel.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

struct IkScene {
    Document doc;
    BoneId shoulder = kNoBone, elbow = kNoBone, wrist = kNoBone;
    IkScene() {
        auto c = loadCharacter("RiggedFigure.glb");
        c->clips.clear();
        doc.setCharacter(c);
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const Skeleton& sk = *doc.skeleton();
        shoulder = sk.find("arm_joint_L_1");
        elbow = sk.children(shoulder).front();
        wrist = sk.children(elbow).front();
    }
    glm::vec3 worldPos(BoneId id) const {
        return glm::vec3(computeGlobals(*doc.skeleton(), doc.pose())[id][3]);
    }
    BoneId target() const { return doc.ikControls().front().targetBone; }
};

}  // namespace

class IkTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void createIkAddsControlBonesAsOneUndoStep() {
        IkScene s;
        const std::size_t bones = s.doc.skeleton()->boneCount();
        const std::size_t undo = s.doc.history().undoCount();
        const auto made = s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, true);
        QVERIFY(bool(made));
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QCOMPARE(s.doc.skeleton()->boneCount(), bones + 2);  // cible + pole
        QCOMPARE(s.doc.ikControls().size(), std::size_t{1});
        QVERIFY(hasFlag(s.doc.skeleton()->bone(s.target()).flags, BoneFlags::Control));
        // Au repos, la chaîne ne bouge pas : la cible est exactement sur l'effecteur.
        const glm::vec3 rest = s.worldPos(s.wrist);
        QVERIFY(glm::length(rest - s.worldPos(s.target())) < 1e-4f);
        s.doc.undo();
        QCOMPARE(s.doc.skeleton()->boneCount(), bones);
        QVERIFY(s.doc.ikControls().empty());
        s.doc.redo();
        QCOMPARE(s.doc.ikControls().size(), std::size_t{1});
        QCOMPARE(s.doc.skeleton()->boneCount(), bones + 2);
    }

    void invalidChainsAreRefused() {
        IkScene s;
        QVERIFY(!bool(s.doc.createIK(s.wrist, 40, IKSolver::FABRIK, false)));
        QVERIFY(!bool(s.doc.createIK(s.wrist, 3, IKSolver::TwoBone, false)));
        QVERIFY(s.doc.ikControls().empty());
    }

    void movingTheTargetBoneMovesTheEffector() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, true)));
        const BoneId target = s.target();
        const glm::vec3 shoulderPos = s.worldPos(s.shoulder);
        const glm::vec3 wanted =
            s.worldPos(s.wrist) + (shoulderPos - s.worldPos(s.wrist)) * 0.2f + glm::vec3(0, 0.05f, 0);
        Transform t = s.doc.boneLocal(target);
        t.t = wanted;  // l'os cible est une racine : local = monde
        s.doc.setBoneLocal(target, t);
        QVERIFY(glm::length(s.worldPos(s.wrist) - wanted) < 1e-3f);
        // La longueur des os est conservée.
        const float upper = glm::length(s.worldPos(s.elbow) - s.worldPos(s.shoulder));
        const float lower = glm::length(s.worldPos(s.wrist) - s.worldPos(s.elbow));
        QVERIFY(upper > 0.01f && lower > 0.01f);
        // La pose FK n'est pas touchée par l'IK (elle reste éditable en dessous).
        QVERIFY(glm::length(computeGlobals(*s.doc.skeleton(), s.doc.fkPose())[s.wrist][3] -
                            glm::vec4(wanted, 1.0f)) > 1e-3f);
    }

    void toggleIkBlendsToFkAndIsUndoable() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, false)));
        Transform t = s.doc.boneLocal(s.target());
        t.t.y += 0.1f;
        s.doc.setBoneLocal(s.target(), t);
        const glm::vec3 ikPos = s.worldPos(s.wrist);
        const std::size_t undo = s.doc.history().undoCount();
        s.doc.toggleIK(0, false);
        QCOMPARE(s.doc.ikControls()[0].weight, 0.0f);
        QVERIFY(glm::length(s.worldPos(s.wrist) - ikPos) > 1e-3f);  // retour à la pose FK
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        s.doc.undo();
        QCOMPARE(s.doc.ikControls()[0].weight, 1.0f);
        QVERIFY(glm::length(s.worldPos(s.wrist) - ikPos) < 1e-4f);
    }

    void animatedToggleEndsWithOneHistoryEntry() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, false)));
        const std::size_t undo = s.doc.history().undoCount();
        s.doc.toggleIK(0, true);
        QVERIFY(s.doc.isIKBlending());
        QTRY_VERIFY_WITH_TIMEOUT(!s.doc.isIKBlending(), 3000);
        QCOMPARE(s.doc.ikControls()[0].weight, 0.0f);
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        s.doc.undo();
        QCOMPARE(s.doc.ikControls()[0].weight, 1.0f);
    }

    void bakeReproducesTheIkPoseOnEveryFrame() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, true)));
        const BoneId target = s.target();
        s.doc.newClip();
        QVERIFY(s.doc.setClipDuration(1.5));
        const Transform home = s.doc.boneLocal(target);
        s.doc.setSelection({target});
        s.doc.setFrame(0);
        QVERIFY(s.doc.insertKeys({target}));
        Transform moved = home;
        moved.t += (s.worldPos(s.shoulder) - home.t) * 0.3f;
        s.doc.setFrame(30);
        s.doc.setBoneLocal(target, moved);
        QVERIFY(s.doc.insertKeys({target}));

        std::vector<glm::vec3> expected;
        const std::vector<int> frames{0, 4, 9, 15, 22, 30};
        for (const int f : frames) {
            s.doc.setFrame(f);
            expected.push_back(s.worldPos(s.wrist));
        }
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(bool(s.doc.bakeIK(0, 0, 30, true)));
        QCOMPARE(s.doc.history().undoCount(), undo + 1);  // bake + désactivation = une seule entrée
        QVERIFY(!s.doc.ikControls()[0].enabled);
        for (std::size_t i = 0; i < frames.size(); ++i) {
            s.doc.setFrame(frames[i]);
            QVERIFY2(glm::length(s.worldPos(s.wrist) - expected[i]) < 1e-3f,
                     qPrintable(QStringLiteral("frame %1 differs").arg(frames[i])));
        }
        s.doc.undo();
        QVERIFY(s.doc.ikControls()[0].enabled);
        QVERIFY(s.doc.clip()->tracks.find(s.shoulder) == s.doc.clip()->tracks.end() ||
                s.doc.clip()->tracks.at(s.shoulder).rotation.keys.empty());
    }

    void footLockPinsTheTargetDuringItsRange() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, false)));
        const BoneId target = s.target();
        s.doc.newClip();
        QVERIFY(s.doc.setClipDuration(2.0));
        s.doc.setSelection({target});
        const Transform home = s.doc.boneLocal(target);
        s.doc.setFrame(0);
        QVERIFY(s.doc.insertKeys({target}));
        Transform later = home;
        later.t.y += 0.05f;
        s.doc.setFrame(60);
        s.doc.setBoneLocal(target, later);
        QVERIFY(s.doc.insertKeys({target}));
        s.doc.setFrame(0);
        const glm::vec3 pinned = s.worldPos(s.wrist);
        QVERIFY(bool(s.doc.addFootLock(0, 0, 40)));
        s.doc.setFrame(30);  // dans le verrou : le poignet ne bouge pas malgré la cible animée
        QVERIFY(glm::length(s.worldPos(s.wrist) - pinned) < 1e-3f);
        s.doc.setFrame(60);  // hors verrou : la cible reprend la main
        QVERIFY(glm::length(s.worldPos(s.wrist) - pinned) > 1e-3f);
        QVERIFY(s.doc.removeFootLock(0, 0));
        s.doc.setFrame(30);
        QVERIFY(glm::length(s.worldPos(s.wrist) - pinned) > 1e-4f);
    }

    void lookAtAimsTheBoneAtItsTarget() {
        IkScene s;
        QVERIFY(bool(s.doc.createLookAt(s.shoulder, 1.0f)));
        const BoneId target = s.target();
        Transform t = s.doc.boneLocal(target);
        t.t = s.worldPos(s.shoulder) + glm::vec3(0.0f, 0.0f, 2.0f);
        s.doc.setBoneLocal(target, t);
        const auto g = computeGlobals(*s.doc.skeleton(), s.doc.pose());
        const glm::vec3 axis = glm::normalize(glm::vec3(g[s.shoulder] * glm::vec4(0, 1, 0, 0)));
        QVERIFY(glm::length(axis - glm::vec3(0, 0, 1)) < 1e-3f);
    }

    void humanoidPresetIsASingleUndoStep() {
        IkScene s;
        const std::size_t bones = s.doc.skeleton()->boneCount();
        const std::size_t undo = s.doc.history().undoCount();
        const auto made = s.doc.createHumanoidIK();
        QVERIFY(bool(made));
        QVERIFY(made.value() >= 2);
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QCOMPARE(s.doc.ikControls().size(), static_cast<std::size_t>(made.value()));
        s.doc.undo();
        QCOMPARE(s.doc.skeleton()->boneCount(), bones);
        QVERIFY(s.doc.ikControls().empty());
    }

    void panelCreatesAndEditsControls() {
        IkScene s;
        IKPanel panel(&s.doc);
        auto* create = panel.findChild<QPushButton*>("ikCreate");
        auto* weight = panel.findChild<QSlider*>("ikWeight");
        auto* remove = panel.findChild<QPushButton*>("ikRemove");
        QVERIFY(create && weight && remove);
        QVERIFY(!weight->isEnabled() || panel.controlList()->count() == 0);
        s.doc.setSelection({s.wrist});
        create->click();
        QCOMPARE(panel.controlList()->count(), 1);
        QCOMPARE(panel.currentControl(), 0);
        QVERIFY(weight->parentWidget()->isEnabled());
        weight->setValue(40);
        QVERIFY(std::abs(s.doc.ikControls()[0].weight - 0.4f) < 1e-4f);
        weight->setValue(60);  // glissé continu : une seule entrée d'historique pour le poids
        const std::size_t undo = s.doc.history().undoCount();
        weight->setValue(70);
        QCOMPARE(s.doc.history().undoCount(), undo);
        remove->click();
        QCOMPARE(panel.controlList()->count(), 0);
        s.doc.undo();
        QCOMPARE(panel.controlList()->count(), 1);
    }

    void deletingTheTargetBoneRemovesTheControl() {
        IkScene s;
        QVERIFY(bool(s.doc.createIK(s.wrist, 2, IKSolver::TwoBone, false)));
        s.doc.setMode(Mode::Rig);
        s.doc.setSelection({s.target()});
        QVERIFY(bool(s.doc.deleteSelected(true)));
        QVERIFY(s.doc.ikControls().empty());
        s.doc.undo();
        QCOMPARE(s.doc.ikControls().size(), std::size_t{1});
    }
};

RIGGER_REGISTER_QTEST(IkTest);
#include "test_ik.moc"
