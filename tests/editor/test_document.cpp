// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QSignalSpy>
#include <QTest>
#include <cmath>

#include "core/skeleton/fk.hpp"
#include "editor/Document.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

class DocumentTest : public QObject {
    Q_OBJECT
private slots:
    void emptyDocumentIsSafe() {
        Document doc;
        QVERIFY(!doc.hasCharacter());
        QVERIFY(doc.skeleton() == nullptr);
        QCOMPARE(doc.frameCount(), 0);
        doc.setSelection({0, 1, 2});  // os inconnus : ignorés
        QVERIFY(doc.selection().empty());
        doc.setPlaying(true);  // rien à lire
        QVERIFY(!doc.isPlaying());
        QVERIFY(!doc.setBoneLength(0, 1.0f));
    }

    void settingCharacterResetsStateAndStartsPlayback() {
        Document doc;
        QSignalSpy changed(&doc, &Document::characterChanged);
        doc.setCharacter(loadCharacter("arm.gltf"), QStringLiteral("arm.gltf"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(doc.hasCharacter());
        QCOMPARE(doc.sourcePath(), QStringLiteral("arm.gltf"));
        QCOMPARE(doc.mode(), Mode::Animate);
        QVERIFY(doc.isPlaying());
        QCOMPARE(doc.clipIndex(), 0);
        QCOMPARE(doc.vertexCount(), std::size_t{30});
        doc.setCharacter(nullptr);
        QVERIFY(!doc.hasCharacter());
        QVERIFY(!doc.isPlaying());
        QVERIFY(doc.pose().empty());
    }

    void playbackAdvancesTheClipAndWraps() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        const BoneId elbow = doc.skeleton()->find("elbow");
        QSignalSpy poses(&doc, &Document::poseChanged);
        QTest::qWait(120);
        QVERIFY(poses.count() >= 2);
        QVERIFY(doc.time() > 0.0);
        QVERIFY(doc.time() < 1.0);
        // La pose est celle du clip : le coude a tourné (rotation animée de 0 à 90°).
        QVERIFY(doc.pose()[elbow].r.z > 0.0f);
        doc.setPlaying(false);
        const double frozen = doc.time();
        QTest::qWait(60);
        QCOMPARE(doc.time(), frozen);
        doc.setTime(2.5);  // clip en boucle (durée 1 s) : 2.5 s -> 0.5 s
        QVERIFY(std::abs(doc.time() - 0.5) < 1e-9);
    }

    void frameApiClampsToTheClip() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setPlaying(false);
        QCOMPARE(doc.fps(), 30.0);
        QCOMPARE(doc.frameCount(), 31);  // 1 s à 30 images/s : images 0..30
        doc.setFrame(12);
        QCOMPARE(doc.frame(), 12);
        doc.setFrame(999);
        QCOMPARE(doc.frame(), 30);
        doc.setFrame(-5);
        QCOMPARE(doc.frame(), 0);
    }

    void modeControlsPoseSource() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setPlaying(false);
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setTime(0.5);
        QVERIFY(doc.pose()[elbow].r.z > 0.1f);  // Animate : pose du clip
        doc.setMode(Mode::Rig);
        QVERIFY(std::abs(doc.pose()[elbow].r.z) < 1e-6f);  // Rig : bind pose
        doc.setMode(Mode::Animate);
        QVERIFY(doc.pose()[elbow].r.z > 0.1f);
        // Passer en Rig ou Skin met la lecture en pause ; lancer la lecture revient en Animate.
        doc.setPlaying(true);
        doc.setMode(Mode::Skin);
        QVERIFY(!doc.isPlaying());
        doc.setPlaying(true);
        QCOMPARE(doc.mode(), Mode::Animate);
        QVERIFY(doc.isPlaying());
    }

    void selectionFiltersAndSignals() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        QSignalSpy spy(&doc, &Document::selectionChanged);
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({elbow, shoulder, elbow, 99});
        QCOMPARE(doc.selection(), (std::vector<BoneId>{elbow, shoulder}));
        QCOMPARE(spy.count(), 1);
        doc.setSelection({elbow, shoulder});  // identique : pas de signal
        QCOMPARE(spy.count(), 1);
        QVERIFY(doc.isSelected(shoulder));
        doc.clearSelection();
        QVERIFY(!doc.isSelected(shoulder));
    }

    void editsWriteBindInRigModeAndPoseOtherwise() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setPlaying(false);
        const BoneId elbow = doc.skeleton()->find("elbow");
        Transform t = doc.boneLocal(elbow);

        doc.setMode(Mode::Rig);
        t.t.x = 0.5f;
        QSignalSpy bone(&doc, &Document::boneChanged);
        QVERIFY(doc.setBoneLocal(elbow, t));
        QCOMPARE(bone.count(), 1);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.x, 0.5f);
        QCOMPARE(doc.pose()[elbow].t.x, 0.5f);  // la pose de repos suit

        doc.setMode(Mode::Animate);
        t.t.x = -0.25f;
        QVERIFY(doc.setBoneLocal(elbow, t));
        QCOMPARE(doc.pose()[elbow].t.x, -0.25f);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.x, 0.5f);  // le bind n'a pas bougé
        doc.setTime(0.0);  // changer de temps réévalue le clip : la retouche manuelle disparaît
        QVERIFY(doc.pose()[elbow].t.x != -0.25f);
    }

    void lockedBonesRefuseEdits() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        const BoneId elbow = doc.skeleton()->find("elbow");
        QVERIFY(doc.setBoneFlags(elbow, BoneFlags::Locked));
        QVERIFY(!doc.setBoneLocal(elbow, Transform{}));
        QVERIFY(!doc.setBoneLength(elbow, 2.0f));
        QVERIFY(!doc.setBoneColor(elbow, {1, 0, 0}));
        QVERIFY(doc.setBoneFlags(elbow, BoneFlags::None));  // le verrou reste modifiable
        QVERIFY(doc.setBoneLength(elbow, 2.0f));
        QCOMPARE(doc.skeleton()->bone(elbow).length, 2.0f);
    }

    void renameAndReparent() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        const BoneId elbow = doc.skeleton()->find("elbow");
        QVERIFY(!doc.renameBone(elbow, QStringLiteral("   ")).ok());
        QVERIFY(doc.renameBone(elbow, QStringLiteral(" forearm ")).ok());
        QCOMPARE(QString::fromStdString(doc.skeleton()->bone(elbow).name), QStringLiteral("forearm"));
        QVERIFY(doc.renameBone(elbow, QStringLiteral("shoulder")).ok());  // nom pris : suffixe automatique
        QVERIFY(doc.skeleton()->bone(elbow).name != "shoulder");

        QSignalSpy hierarchy(&doc, &Document::hierarchyChanged);
        const glm::mat4 before = doc.skeleton()->bindGlobals()[elbow];
        QVERIFY(doc.reparentBone(elbow, kNoBone, true).ok());  // devient racine, position monde conservée
        QCOMPARE(hierarchy.count(), 1);
        const glm::mat4 after = doc.skeleton()->bindGlobals()[elbow];
        QVERIFY(glm::length(glm::vec3(before[3]) - glm::vec3(after[3])) < 1e-5f);
        QVERIFY(!doc.reparentBone(shoulder, shoulder).ok());  // cycle refusé
    }

    void skinMethodSignal() {
        Document doc;
        QSignalSpy spy(&doc, &Document::skinMethodChanged);
        doc.setSkinMethod(SkinMethod::Dqs);
        doc.setSkinMethod(SkinMethod::Dqs);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(doc.skinMethod(), SkinMethod::Dqs);
    }
};

RIGGER_REGISTER_QTEST(DocumentTest);
#include "test_document.moc"
