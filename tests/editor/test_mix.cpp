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
#include "editor/MixPanel.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"
#include "io/project.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

AnimationClip rotateClip(const char* name, BoneId bone, const Skeleton& sk, float angle, double duration) {
    AnimationClip c;
    c.name = name;
    c.duration = duration;
    c.fps = 30.0f;
    for (const double t : {0.0, duration}) {
        Keyframe<glm::quat> k;
        k.time = t;
        k.value = glm::angleAxis(t == 0.0 ? 0.0f : angle, glm::vec3(0, 0, 1)) * sk.bone(bone).bindLocal.r;
        k.mode = Interpolation::Linear;
        c.tracks[bone].rotation.keys.push_back(k);
    }
    return c;
}

struct MixScene {
    Document doc;
    BoneId arm = kNoBone, leg = kNoBone;
    MixScene() {
        auto c = loadCharacter("RiggedFigure.glb");
        c->clips.clear();
        arm = c->skeleton.find("arm_joint_L_1");
        leg = c->skeleton.find("leg_joint_L_1");
        c->clips.push_back(rotateClip("walk", leg, c->skeleton, 0.6f, 1.0));
        c->clips.push_back(rotateClip("wave", arm, c->skeleton, -1.0f, 2.0));
        doc.setCharacter(c);
        doc.setPlaying(false);
        doc.setMode(Mode::Animate);
    }
    glm::quat rotation(BoneId id) const { return doc.pose()[id].r; }
};

}  // namespace

class MixTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void layersWithMaskSuperimposeTwoClips() {
        MixScene s;
        QVERIFY(!s.doc.mixActive());
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(s.doc.editMix([&](AnimationMix& m) {
            m.mode = MixMode::Layers;
            AnimationLayer legs;
            legs.name = "legs";
            legs.clip = 0;
            AnimationLayer arms;
            arms.name = "arms";
            arms.clip = 1;
            arms.mask = {s.arm};
            m.layers = {legs, arms};
        }));
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QVERIFY(s.doc.mixActive());
        QVERIFY(std::abs(s.doc.timelineDuration() - 2.0) < 1e-9);  // le plus long des clips
        s.doc.setTime(0.5);
        const auto bind = s.doc.skeleton()->bindPose();
        const glm::quat legExpected = glm::angleAxis(0.3f, glm::vec3(0, 0, 1)) * bind[s.leg].r;
        const glm::quat armExpected = glm::angleAxis(-0.25f, glm::vec3(0, 0, 1)) * bind[s.arm].r;
        QVERIFY(std::abs(glm::dot(s.rotation(s.leg), legExpected)) > 0.99999f);
        QVERIFY(std::abs(glm::dot(s.rotation(s.arm), armExpected)) > 0.99999f);
        s.doc.undo();
        QVERIFY(!s.doc.mixActive());
        s.doc.redo();
        QVERIFY(s.doc.mixActive());
    }

    void weightEditsMergeIntoOneUndoStep() {
        MixScene s;
        QVERIFY(s.doc.editMix([](AnimationMix& m) {
            m.mode = MixMode::Layers;
            m.layers = {AnimationLayer{}};
        }));
        const std::size_t undo = s.doc.history().undoCount();
        for (const float w : {0.9f, 0.7f, 0.5f}) {
            QVERIFY(s.doc.editMix([w](AnimationMix& m) { m.layers[0].weight = w; }, "Edit Mix", "w"));
        }
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QCOMPARE(s.doc.mix().layers[0].weight, 0.5f);
        s.doc.undo();
        QCOMPARE(s.doc.mix().layers[0].weight, 1.0f);
    }

    void crossfadeBuildsOverlappingStrips() {
        MixScene s;
        QVERIFY(s.doc.addCrossfade(0, 1, 0.5));
        QCOMPARE(static_cast<int>(s.doc.mix().mode), static_cast<int>(MixMode::Nla));
        QCOMPARE(s.doc.mix().strips.size(), std::size_t{2});
        QVERIFY(std::abs(s.doc.mix().strips[1].start - 0.5) < 1e-9);  // 1 s - 0,5 s de fondu
        QVERIFY(std::abs(s.doc.mix().strips[1].blendIn - 0.5) < 1e-9);
        QVERIFY(std::abs(s.doc.timelineDuration() - 2.5) < 1e-9);
        QVERIFY(!s.doc.addCrossfade(0, 9, 0.5));
        s.doc.undo();
        QVERIFY(s.doc.mix().strips.empty());
    }

    void deletingAClipFixesLayerReferencesAsOneStep() {
        MixScene s;
        QVERIFY(s.doc.editMix([](AnimationMix& m) {
            m.mode = MixMode::Layers;
            AnimationLayer a, b;
            a.clip = 0;
            b.clip = 1;
            m.layers = {a, b};
            m.tree.entries = {{0, 0.0f}, {1, 1.0f}};
        }));
        s.doc.setClipIndex(0);
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(bool(s.doc.deleteClip()));
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QCOMPARE(s.doc.mix().layers.size(), std::size_t{1});
        QCOMPARE(s.doc.mix().layers[0].clip, 0);  // l'ancien clip 1 devient le clip 0
        QCOMPARE(s.doc.mix().tree.entries.size(), std::size_t{1});
        s.doc.undo();
        QCOMPARE(s.doc.mix().layers.size(), std::size_t{2});
        QCOMPARE(s.doc.character()->clips.size(), std::size_t{2});
    }

    void retargetAddsAClipOnTheTargetSkeleton() {
        MixScene s;
        auto source = loadCharacter("RiggedFigure.glb");  // même squelette, avec son clip importé
        QVERIFY(!source->clips.empty());
        const BoneMap map = autoMapBones(source->skeleton, s.doc.character()->skeleton);
        QVERIFY(map.pairs.size() >= 5);
        const std::size_t clips = s.doc.character()->clips.size();
        const auto result = s.doc.retargetClipFrom(*source, 0, map);
        QVERIFY(bool(result));
        QCOMPARE(s.doc.character()->clips.size(), clips + 1);
        QCOMPARE(s.doc.clipIndex(), result.value());
        QVERIFY(s.doc.clip()->name.find("retargeted") != std::string::npos);
        s.doc.undo();
        QCOMPARE(s.doc.character()->clips.size(), clips);
        QVERIFY(!bool(s.doc.retargetClipFrom(*source, 99, map)));
        QVERIFY(!bool(s.doc.retargetClipFrom(*source, 0, BoneMap{})));
    }

    void mixSurvivesProjectSerialization() {
        MixScene s;
        QVERIFY(s.doc.addCrossfade(0, 1, 0.5));
        QVERIFY(s.doc.editMix([&](AnimationMix& m) {
            AnimationLayer l;
            l.clip = 1;
            l.mask = {s.arm};
            l.mode = LayerMode::Additive;
            l.weight = 0.4f;
            m.layers = {l};
            m.tree.entries = {{0, 0.0f}, {1, 2.0f}};
            m.tree.parameter = 1.5f;
        }));
        io::Project project;
        project.character = *s.doc.character();
        const auto loaded = io::parseProject(io::serializeProject(project));
        QVERIFY(loaded.ok());
        QVERIFY(loaded.value().character.mix == s.doc.character()->mix);
    }

    void panelAddsLayersAndEditsThem() {
        MixScene s;
        MixPanel panel(&s.doc);
        auto* add = panel.findChild<QPushButton*>("mixAddLayer");
        auto* weight = panel.findChild<QSlider*>("mixLayerWeight");
        auto* list = panel.findChild<QListWidget*>("mixLayerList");
        auto* mask = panel.findChild<QPushButton*>("mixMaskFromSelection");
        QVERIFY(add && weight && list && mask);
        add->click();
        QCOMPARE(list->count(), 1);
        QVERIFY(s.doc.mixActive());  // ajouter une couche active le mode Couches
        weight->setValue(30);
        QVERIFY(std::abs(s.doc.mix().layers[0].weight - 0.3f) < 1e-5f);
        s.doc.setSelection({s.arm});
        mask->click();
        QCOMPARE(s.doc.mix().layers[0].mask.size(), std::size_t{1});
        panel.findChild<QPushButton*>("mixRemoveLayer")->click();
        QCOMPARE(list->count(), 0);
        s.doc.undo();
        QCOMPARE(list->count(), 1);
    }
};

RIGGER_REGISTER_QTEST(MixTest);
#include "test_mix.moc"
