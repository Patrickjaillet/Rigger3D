// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTest>
#include <cmath>
#include <filesystem>

#include "core/deform/morph.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "editor/DeformPanel.hpp"
#include "editor/Document.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"
#include "io/gltf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/project.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

std::shared_ptr<Character> figure() {
    auto c = loadCharacter("RiggedFigure.glb");
    c->clips.clear();
    return c;
}

// Personnage minimal avec blend shapes : un carré skinné sur un os, deux formes.
std::shared_ptr<Character> morphCharacter() {
    auto c = std::make_shared<Character>();
    Skeleton& sk = c->skeleton;
    const BoneId root = sk.addBone("root", kNoBone).value();
    Mesh mesh;
    for (const glm::vec3& p :
         {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)}) {
        Vertex v;
        v.position = p;
        mesh.vertices.push_back(v);
        SkinWeight w;
        w.joints[0] = root;
        w.weights[0] = 1.0f;
        mesh.skinWeights.push_back(w);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    Submesh sub;
    sub.indexCount = 6;
    mesh.submeshes.push_back(sub);
    for (const char* name : {"smile", "blink"}) {
        Transform bind;
        const BoneId bone = sk.addBone(std::string("Morph_") + name, kNoBone, bind).value();
        sk.setFlags(bone, BoneFlags::Control | BoneFlags::Morph);
        MorphTarget t;
        t.name = name;
        t.positionDeltas = {{0, 0, 0}, {0, 0, 0}, {0, 0.5f, 0}, {0, 0.5f, 0}};
        t.weightBone = bone;
        mesh.morphTargets.push_back(t);
    }
    c->meshes.push_back(std::move(mesh));
    return c;
}

}  // namespace

class DeformTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void twistSetupCreatesBonesAndSharesTheTwist() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const Skeleton& sk = *doc.skeleton();
        const BoneId forearm = sk.find("arm_joint_L_2");
        const BoneId hand = sk.find("arm_joint_L_3");
        const std::size_t bones = sk.boneCount();
        const std::size_t undo = doc.history().undoCount();
        const auto made = doc.createTwistSetup(forearm, hand, 2);
        QVERIFY(bool(made));
        QCOMPARE(doc.history().undoCount(), undo + 1);
        QCOMPARE(doc.skeleton()->boneCount(), bones + 2);
        QCOMPARE(doc.deform().twists.size(), std::size_t{1});
        const BoneId t1 = doc.deform().twists[0].bones[0];
        // Torsion de la main autour de l'axe de l'avant-bras : les os de twist en reçoivent une fraction.
        Transform h = doc.boneLocal(hand);
        h.r = h.r * glm::angleAxis(0.9f, doc.deform().twists[0].axis);
        doc.setBoneLocal(hand, h);
        const glm::quat rel = glm::inverse(doc.skeleton()->bone(t1).bindLocal.r) * doc.pose()[t1].r;
        QVERIFY(std::abs(2.0f * std::acos(std::min(1.0f, std::abs(rel.w))) - 0.3f) < 1e-3f);  // 0,9 / 3
        doc.undo();  // la rotation de la main
        doc.undo();  // la création des os de twist
        QCOMPARE(doc.skeleton()->boneCount(), bones);
        QVERIFY(doc.deform().twists.empty());
        QVERIFY(!bool(doc.createTwistSetup(forearm, forearm, 2)));  // pas une paire avant-bras / main
    }

    void constraintsAreAppliedAndUndoable() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        Constraint limit;
        limit.kind = ConstraintKind::Limit;
        limit.bone = arm;
        limit.rotationMin = glm::vec3(-0.1f);
        limit.rotationMax = glm::vec3(0.1f);
        QVERIFY(bool(doc.addConstraint(limit)));
        Transform t = doc.boneLocal(arm);
        t.r = fromEulerXYZ({1.0f, 0.0f, 0.0f});
        doc.setBoneLocal(arm, t);
        QVERIFY(std::abs(toEulerXYZ(doc.pose()[arm].r).x - 0.1f) < 1e-3f);    // limitée à 0,1 rad
        QVERIFY(std::abs(toEulerXYZ(doc.fkPose()[arm].r).x - 1.0f) < 1e-3f);  // la pose FK reste éditable
        doc.undo();                                                           // la rotation
        doc.undo();                                                           // la contrainte
        QVERIFY(doc.deform().constraints.empty());
        Constraint noTarget;
        noTarget.kind = ConstraintKind::CopyTransform;
        noTarget.bone = arm;
        QVERIFY(!bool(doc.addConstraint(noTarget)));  // cible manquante
    }

    void bakedSpringsBecomeRotationKeysInOneStep() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        QVERIFY(bool(doc.createSpringChain(arm, 3)));
        doc.editDeform([](DeformRig& d) {
            d.springs[0].stiffness = 0.0f;
            d.springs[0].gravity = 1.0f;
        });
        QVERIFY(doc.springsActive());
        doc.newClip();
        QVERIFY(doc.setClipDuration(2.0));
        const std::size_t undo = doc.history().undoCount();
        QVERIFY(bool(doc.bakeSprings(0, 30, true)));
        QCOMPARE(doc.history().undoCount(), undo + 1);
        QVERIFY(!doc.deform().springsEnabled);  // simulation désactivée après le bake
        const auto& rotation = doc.clip()->tracks.at(arm).rotation.keys;
        QVERIFY(rotation.size() >= 30);
        // La chaîne a bougé par rapport à la pose de repos (gravité sans raideur).
        QVERIFY(std::abs(glm::dot(rotation.back().value, rotation.front().value)) < 0.99999f);
        doc.undo();
        QVERIFY(doc.deform().springsEnabled);
        QVERIFY(doc.clip()->tracks.find(arm) == doc.clip()->tracks.end() ||
                doc.clip()->tracks.at(arm).rotation.keys.empty());
        QVERIFY(!bool(doc.bakeSprings(10, 5, false)));
    }

    void morphWeightsDriveTheMeshAndCanBeKeyed() {
        Document doc;
        doc.setCharacter(morphCharacter());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        QVERIFY(doc.setMorphWeight(0, 0, 0.5f));
        const auto verts = doc.skinnedVertices(0);
        QVERIFY(std::abs(verts[2].position.y - 1.25f) < 1e-5f);  // 1 + 0,5 * 0,5
        QVERIFY(std::abs(verts[0].position.y) < 1e-6f);
        QVERIFY(!doc.setMorphWeight(0, 9, 1.0f));
        // Avec l'auto-key, le poids est enregistré comme clé de la translation de l'os de poids.
        doc.newClip();
        doc.setAutoKey(true);
        QVERIFY(doc.setMorphWeight(0, 1, 0.8f));
        const BoneId bone = doc.character()->meshes[0].morphTargets[1].weightBone;
        QVERIFY(doc.clip()->tracks.count(bone) == 1);
        QVERIFY(std::abs(doc.clip()->tracks.at(bone).translation.keys.front().value.x - 0.8f) < 1e-5f);
        doc.setAutoKey(false);
        // Les os de poids ne se suppriment pas.
        doc.setMode(Mode::Rig);
        doc.setSelection({bone});
        QVERIFY(!bool(doc.deleteSelected(true)));
    }

    void correctiveDriversAreUndoable() {
        Document doc;
        doc.setCharacter(morphCharacter());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        MorphDriver d;
        d.bone = doc.skeleton()->find("root");
        d.axis = {0, 0, 1};
        d.angle0 = 0.0f;
        d.angle1 = 1.0f;
        QVERIFY(doc.editMorphDrivers(0, 0, {d}));
        QCOMPARE(doc.character()->meshes[0].morphTargets[0].drivers.size(), std::size_t{1});
        Transform t = doc.boneLocal(d.bone);
        t.r = glm::angleAxis(0.5f, glm::vec3(0, 0, 1));
        doc.setBoneLocal(d.bone, t);
        const auto weights = morphWeights(doc.character()->meshes[0], *doc.skeleton(), doc.pose());
        QVERIFY(std::abs(weights[0] - 0.5f) < 1e-4f);
        doc.undo();  // annule la rotation
        doc.undo();  // annule le pilote
        QVERIFY(doc.character()->meshes[0].morphTargets[0].drivers.empty());
        QVERIFY(!doc.editMorphDrivers(0, 0, {}));  // rien n'a changé
    }

    void autoRigBuildsAHumanoidWithSkinAndIkInOneUndoStep() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const std::size_t bones = doc.skeleton()->boneCount();
        const auto originalWeights = doc.character()->meshes[0].skinWeights;
        const std::size_t undo = doc.history().undoCount();
        const Landmarks landmarks = doc.guessCharacterLandmarks();
        QVERIFY(bool(doc.autoRigHumanoid(landmarks)));
        QCOMPARE(doc.history().undoCount(), undo + 1);
        QVERIFY(doc.skeleton()->find("hand.L") != kNoBone);
        QVERIFY(doc.skeleton()->find("thigh.R") != kNoBone);
        QVERIFY(doc.ikControls().size() >= 4);  // bras et jambes (au moins)
        for (const SkinWeight& w : doc.character()->meshes[0].skinWeights) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += w.weights[k];
                if (w.weights[k] > 0.0f) QVERIFY(doc.skeleton()->contains(w.joints[k]));
            }
            QVERIFY(std::abs(sum - 1.0f) < 1e-4f);
        }
        doc.undo();
        QCOMPARE(doc.skeleton()->boneCount(), bones);
        QVERIFY(doc.skeleton()->find("arm_joint_L_1") != kNoBone);
        QVERIFY(doc.ikControls().empty());
        const auto& restored = doc.character()->meshes[0].skinWeights;
        QCOMPARE(restored.size(), originalWeights.size());
        QVERIFY(restored[10].joints == originalWeights[10].joints &&
                restored[10].weights == originalWeights[10].weights);
        // Repères incohérents : refusés sans toucher au personnage.
        Landmarks bad = landmarks;
        bad[static_cast<int>(Landmark::HeadTop)].y = -5.0f;
        QVERIFY(!bool(doc.autoRigHumanoid(bad)));
        QCOMPARE(doc.skeleton()->boneCount(), bones);
    }

    void autoRigKeepsTheBlendShapeWeightBones() {
        Document doc;
        doc.setCharacter(morphCharacter());
        doc.setPlaying(false);
        // Maillage : deux carrés suffisent pour deviner les repères.
        Landmarks l = guessLandmarks({{-0.9f, 0, -0.1f}, {0.9f, 1.8f, 0.1f}});
        QVERIFY(bool(doc.autoRigHumanoid(l, {}, false)));
        const Mesh& mesh = doc.character()->meshes[0];
        for (const MorphTarget& t : mesh.morphTargets) {
            QVERIFY(doc.skeleton()->contains(t.weightBone));
            QVERIFY(hasFlag(doc.skeleton()->bone(t.weightBone).flags, BoneFlags::Morph));
        }
        doc.undo();
        for (const MorphTarget& t : doc.character()->meshes[0].morphTargets) {
            QVERIFY(doc.skeleton()->contains(t.weightBone));
        }
    }

    void deformSetupSurvivesProjectSerialization() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        QVERIFY(bool(doc.createSpringChain(arm, 3)));
        QVERIFY(doc.addSpringCollider(doc.skeleton()->find("neck_joint_1"), {0, 0.1f, 0}, 0.2f));
        Constraint childOf;
        childOf.kind = ConstraintKind::ChildOf;
        childOf.bone = doc.skeleton()->find("arm_joint_R_1");
        childOf.target = doc.skeleton()->find("torso_joint_1");
        QVERIFY(bool(doc.addConstraint(childOf)));
        QVERIFY(bool(doc.createTwistSetup(doc.skeleton()->find("arm_joint_L_2"),
                                          doc.skeleton()->find("arm_joint_L_3"), 2)));
        io::Project project;
        project.character = *doc.character();
        const auto loaded = io::parseProject(io::serializeProject(project));
        QVERIFY(loaded.ok());
        QVERIFY(loaded.value().character.deform == doc.character()->deform);
    }

    void exportBakesSpringDynamicsIntoAnimationChannels() {
        Document doc;
        doc.setCharacter(figure());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        QVERIFY(bool(doc.createSpringChain(arm, 3)));
        doc.editDeform([](DeformRig& d) { d.springs[0].stiffness = 0.0f; });
        doc.newClip();
        QVERIFY(doc.setClipDuration(2.0));
        const BoneId root = doc.skeleton()->find("torso_joint_1");
        doc.setSelection({root});
        QVERIFY(doc.insertKeys({root}));  // le clip anime au moins un os
        const auto path = std::filesystem::temp_directory_path() / "rigger_springs.glb";
        io::GltfExportOptions options;
        QVERIFY(bool(io::exportGltf(*doc.character(), path, options)));
        const auto back = io::importGltf(path);
        QVERIFY(back.ok());
        const Character& c = back.value();
        QVERIFY(!c.clips.empty());
        const BoneId springBone = c.skeleton.find("arm_joint_L_1");
        QVERIFY(springBone != kNoBone);
        QVERIFY(c.clips[0].tracks.count(springBone) == 1);
        QVERIFY(c.clips[0].tracks.at(springBone).rotation.keys.size() >= 30);  // une clé par image
        // Sans « bake deform », les os simulés ne sont pas exportés comme animés.
        options.bakeDeform = false;
        QVERIFY(bool(io::exportGltf(*doc.character(), path, options)));
        const auto plain = io::importGltf(path);
        QVERIFY(plain.ok());
        QVERIFY(plain.value().clips[0].tracks.count(plain.value().skeleton.find("arm_joint_L_1")) == 0);
        std::filesystem::remove(path);
    }

    void panelCreatesSpringChainsAndMorphSliders() {
        Document doc;
        doc.setCharacter(morphCharacter());
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
        DeformPanel panel(&doc);
        auto* slider = panel.findChild<QSlider*>("morphSlider_0_0");
        QVERIFY(slider != nullptr);
        slider->setValue(60);
        QVERIFY(std::abs(doc.pose()[doc.character()->meshes[0].morphTargets[0].weightBone].t.x - 0.6f) <
                1e-5f);
        doc.setSelection({doc.skeleton()->find("root")});
        auto* create = panel.findChild<QPushButton*>("springCreate");
        QVERIFY(create != nullptr);
        create->click();
        QCOMPARE(doc.deform().springs.size(), std::size_t{1});
        auto* list = panel.findChild<QListWidget*>("springList");
        QCOMPARE(list->count(), 1);
        panel.findChild<QCheckBox*>("springsGlobal")->click();
        QVERIFY(!doc.deform().springsEnabled);
        doc.undo();
        QVERIFY(doc.deform().springsEnabled);
    }
};

RIGGER_REGISTER_QTEST(DeformTest);
#include "test_deform.moc"
