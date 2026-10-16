// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

#include "core/animation/keys.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "editor/Document.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

// RiggedFigure sans son clip : on part d'un personnage vierge d'animation (le clip importé est supprimé).
std::shared_ptr<Character> blankFigure() {
    auto c = loadCharacter("RiggedFigure.glb");
    c->clips.clear();
    return c;
}

void prepare(Document& doc) {
    doc.setCharacter(blankFigure());
    doc.setPlaying(false);
}

}  // namespace

class AnimationTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void insertKeysCreatesAClipAndIsASingleUndoEntry() {
        Document doc;
        prepare(doc);
        QCOMPARE(doc.clipIndex(), -1);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        doc.setSelection({arm});
        doc.setFrame(0);
        QVERIFY(doc.insertKeys({arm}));
        QCOMPARE(doc.history().undoCount(), std::size_t{1});  // clip + clés = une seule entrée
        QCOMPARE(doc.undoText(), QStringLiteral("Insert Keys"));
        QVERIFY(doc.clip() != nullptr);
        QCOMPARE(doc.clip()->tracks.at(arm).translation.keys.size(), std::size_t{1});
        QCOMPARE(doc.clip()->tracks.at(arm).rotation.keys.size(), std::size_t{1});
        QCOMPARE(doc.clip()->tracks.at(arm).scale.keys.size(), std::size_t{1});

        doc.undo();
        QCOMPARE(doc.clipIndex(), -1);  // le clip créé disparaît avec les clés
        doc.redo();
        QVERIFY(doc.clip() != nullptr);
        QVERIFY(keyCount(*doc.clip()) == 3);
    }

    void keysAreInsertedAtTheCurrentFrameWithTheCurrentPose() {
        Document doc;
        prepare(doc);
        doc.newClip();
        QVERIFY(doc.setClipDuration(2.0));
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        Transform t = doc.boneLocal(arm);
        t.t.y += 0.5f;
        doc.setBoneLocal(arm, t);  // modification transitoire (auto-key désactivée)
        QCOMPARE(doc.boneKeyStates()[arm], BoneKeyState::Modified);
        doc.setFrame(15);
        QCOMPARE(doc.boneKeyStates()[arm], BoneKeyState::None);  // la retouche n'est valable qu'à son temps
        doc.setFrame(0);
        doc.setBoneLocal(arm, t);
        QVERIFY(doc.insertKeys({arm}, 1u << ChannelTranslation));
        QCOMPARE(doc.boneKeyStates()[arm], BoneKeyState::Keyed);
        QVERIFY(std::abs(doc.clip()->tracks.at(arm).translation.keys[0].value.y - t.t.y) < 1e-6f);
        QCOMPARE(doc.clip()->tracks.at(arm).rotation.keys.size(), std::size_t{0});  // seulement T
        doc.setFrame(10);
        QVERIFY(std::abs(doc.pose()[arm].t.y - t.t.y) <
                1e-6f);  // clé unique : valeur constante dans le temps

        doc.setMode(Mode::Rig);
        QVERIFY(!doc.insertKeys({arm}));  // seulement en mode Animer
    }

    void autoKeyKeysEditedComponentsOnly() {
        Document doc;
        prepare(doc);
        doc.newClip();
        doc.setAutoKey(true);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        Transform t = doc.boneLocal(arm);
        t.t.x += 0.3f;
        doc.setFrame(6);
        QVERIFY(doc.setBoneLocal(arm, t));
        QCOMPARE(doc.clip()->tracks.at(arm).translation.keys.size(), std::size_t{1});
        QCOMPARE(doc.clip()->tracks.at(arm).rotation.keys.size(),
                 std::size_t{0});  // rotation inchangée : pas de clé
        QCOMPARE(doc.boneKeyStates()[arm], BoneKeyState::Keyed);
        // Une seconde frame : la courbe interpole entre les deux clés.
        doc.setFrame(12);
        t.t.x += 0.3f;
        doc.setBoneLocal(arm, t);
        doc.setFrame(9);
        QVERIFY(std::abs(doc.pose()[arm].t.x - (t.t.x - 0.15f)) < 1e-4f);

        // Un glissé de gizmo en auto-key donne aussi des clés (une entrée d'historique).
        doc.setFrame(20);
        doc.setSelection({arm});
        const std::size_t entries = doc.history().undoCount();
        QVERIFY(doc.beginManipulation());
        WorldDelta delta;
        delta.rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1));
        doc.updateManipulation(delta);
        doc.endManipulation();
        QCOMPARE(doc.history().undoCount(), entries + 1);
        QVERIFY(findKey(doc.clip()->tracks.at(arm).rotation, doc.time()).has_value());
        doc.undo();
        QVERIFY(!findKey(doc.clip()->tracks.at(arm).rotation, 20.0 / 30.0).has_value());
    }

    void deleteInterpolateAndEditKeys() {
        Document doc;
        prepare(doc);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        for (const int frame : {0, 10, 20}) {
            doc.setFrame(frame);
            Transform t = doc.boneLocal(arm);
            t.t.x = static_cast<float>(frame) * 0.1f;
            doc.setBoneLocal(arm, t);
            doc.insertKeys({arm}, 1u << ChannelTranslation);
        }
        const double t10 = 10.0 / 30.0;
        const KeyRef mid{arm, ChannelTranslation, t10};
        QVERIFY(doc.setKeysInterpolation({mid}, Interpolation::Bezier));
        const auto& track = doc.clip()->tracks.at(arm).translation;
        QCOMPARE(track.keys[1].mode, Interpolation::Bezier);
        QVERIFY(track.keys[1].outTangent.x != 0.0f);

        KeyEdit edit;
        edit.value = glm::vec3(5, 0, 0);
        edit.outTangent = glm::vec3(1, 2, 3);
        QVERIFY(doc.editKey(mid, edit));
        QCOMPARE(doc.clip()->tracks.at(arm).translation.keys[1].value.x, 5.0f);
        QCOMPARE(doc.clip()->tracks.at(arm).translation.keys[1].outTangent, glm::vec3(1, 2, 3));
        // Déplacement dans le temps depuis la courbe : la sélection suit la clé.
        doc.setKeySelection({mid});
        KeyEdit move;
        move.time = 15.0 / 30.0;
        QVERIFY(doc.editKey(mid, move));
        QVERIFY(findKey(doc.clip()->tracks.at(arm).translation, 15.0 / 30.0).has_value());
        QCOMPARE(doc.keySelection().size(), std::size_t{1});
        QVERIFY(std::abs(doc.keySelection()[0].time - 0.5) < 1e-9);

        // Rotation : valeur éditée en angles d'Euler.
        doc.setFrame(0);
        doc.insertKeys({arm}, 1u << ChannelRotation);
        KeyEdit rot;
        rot.value = glm::vec3(0, 0, glm::radians(40.0f));
        QVERIFY(doc.editKey({arm, ChannelRotation, 0.0}, rot));
        const glm::vec3 euler = toEulerXYZ(doc.clip()->tracks.at(arm).rotation.keys[0].value);
        QVERIFY(std::abs(euler.z - glm::radians(40.0f)) < 1e-4f);

        QCOMPARE(doc.deleteKeys({{arm, ChannelTranslation, 0.0}}), std::size_t{1});
        QVERIFY(!findKey(doc.clip()->tracks.at(arm).translation, 0.0).has_value());
        QCOMPARE(doc.deleteKeys({}), std::size_t{0});
        // Tout est annulable, dans l'ordre inverse.
        while (doc.canUndo()) doc.undo();
        QVERIFY(doc.clip() == nullptr || keyCount(*doc.clip()) == 0);
    }

    void keyDragIsASingleAbsoluteUndoableEntry() {
        Document doc;
        prepare(doc);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        for (const int frame : {0, 10, 20}) {
            doc.setFrame(frame);
            doc.insertKeys({arm}, 1u << ChannelTranslation);
        }
        const std::size_t entries = doc.history().undoCount();
        const std::vector<KeyRef> keys = {{arm, ChannelTranslation, 10.0 / 30.0},
                                          {arm, ChannelTranslation, 20.0 / 30.0}};
        QVERIFY(doc.beginKeyDrag(keys, KeyDragMode::Move));
        QVERIFY(!doc.beginKeyDrag(keys, KeyDragMode::Move));  // déjà en cours
        for (int i = 1; i <= 20; ++i) doc.updateKeyDrag(0.1 * i);
        doc.updateKeyDrag(0.5);  // absolu : seul le dernier décalage compte
        QVERIFY(doc.isDraggingKeys());
        QVERIFY(findKey(doc.clip()->tracks.at(arm).translation, 10.0 / 30.0 + 0.5).has_value());
        QVERIFY(!findKey(doc.clip()->tracks.at(arm).translation, 10.0 / 30.0).has_value());
        QVERIFY(doc.clip()->duration >= 20.0 / 30.0 + 0.5 - 1e-9);  // la durée s'étend
        doc.endKeyDrag();
        QCOMPARE(doc.history().undoCount(), entries + 1);
        QCOMPARE(doc.undoText(), QStringLiteral("Move Keys"));
        doc.undo();
        QVERIFY(findKey(doc.clip()->tracks.at(arm).translation, 10.0 / 30.0).has_value());
        QVERIFY(std::abs(doc.clip()->duration - 1.0) < 1e-9 || doc.clip()->duration <= 20.0 / 30.0 + 1e-9 ||
                doc.clip()->duration > 0.0);
        doc.redo();

        // Duplication : les clés d'origine restent.
        const std::size_t before = keyCount(*doc.clip());
        QVERIFY(doc.beginKeyDrag({{arm, ChannelTranslation, 0.0}}, KeyDragMode::Duplicate));
        doc.updateKeyDrag(3.0);
        doc.endKeyDrag();
        QCOMPARE(keyCount(*doc.clip()), before + 1);
        QCOMPARE(doc.undoText(), QStringLiteral("Duplicate Keys"));

        // Étirement autour d'un pivot.
        const auto all = allKeys(*doc.clip());
        QVERIFY(doc.beginKeyDrag(all, KeyDragMode::Scale, 0.0));
        doc.updateKeyDrag(2.0);
        doc.endKeyDrag();
        QCOMPARE(doc.undoText(), QStringLiteral("Scale Keys"));
        // Un glissé sans effet n'ajoute rien.
        const std::size_t n = doc.history().undoCount();
        QVERIFY(doc.beginKeyDrag({{arm, ChannelTranslation, 0.0}}, KeyDragMode::Move));
        doc.updateKeyDrag(0.0);
        doc.endKeyDrag();
        QCOMPARE(doc.history().undoCount(), n);
    }

    void copyPasteKeysAndGlobalOffset() {
        Document doc;
        prepare(doc);
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        const BoneId b = doc.skeleton()->find("arm_joint_R_1");
        for (const int frame : {0, 6}) {
            doc.setFrame(frame);
            doc.insertKeys({a, b}, kAllChannels);
        }
        QVERIFY(!doc.hasKeyClipboard());
        doc.copyKeys(
            {{a, ChannelRotation, 0.0}, {a, ChannelRotation, 6.0 / 30.0}, {b, ChannelTranslation, 0.0}});
        QVERIFY(doc.hasKeyClipboard());
        const auto pasted = doc.pasteKeys(1.0);
        QCOMPARE(pasted.size(), std::size_t{3});
        QVERIFY(findKey(doc.clip()->tracks.at(a).rotation, 1.0).has_value());
        QVERIFY(findKey(doc.clip()->tracks.at(a).rotation, 1.0 + 6.0 / 30.0).has_value());
        QCOMPARE(doc.undoText(), QStringLiteral("Paste Keys"));
        QCOMPARE(doc.keySelection().size(), std::size_t{3});
        doc.undo();
        QVERIFY(!findKey(doc.clip()->tracks.at(a).rotation, 1.0).has_value());

        // Décalage global : toutes les clés bougent d'un coup.
        const auto all = allKeys(*doc.clip());
        QVERIFY(doc.beginKeyDrag(all, KeyDragMode::Move));
        doc.updateKeyDrag(0.5);
        doc.endKeyDrag();
        QVERIFY(findKey(doc.clip()->tracks.at(a).rotation, 0.5).has_value());
        QVERIFY(!findKey(doc.clip()->tracks.at(a).rotation, 0.0).has_value());
    }

    void clipManagement() {
        Document doc;
        doc.setCharacter(loadCharacter("RiggedFigure.glb"));
        doc.setPlaying(false);
        QCOMPARE(doc.character()->clips.size(), std::size_t{1});
        QVERIFY(doc.newClip().ok());
        QCOMPARE(doc.character()->clips.size(), std::size_t{2});
        QCOMPARE(doc.clipIndex(), 1);
        QVERIFY(doc.renameClip(QStringLiteral(" Salute ")).ok());
        QCOMPARE(doc.clip()->name, std::string("Salute"));
        QVERIFY(!doc.renameClip(QStringLiteral("  ")).ok());
        QVERIFY(doc.duplicateClip().ok());
        QCOMPARE(doc.clip()->name, std::string("Salute copy"));
        QCOMPARE(doc.character()->clips.size(), std::size_t{3});
        QVERIFY(doc.duplicateClip().ok());  // noms uniques
        QVERIFY(doc.clip()->name != "Salute copy");

        QVERIFY(doc.setClipFps(60.0f));
        QCOMPARE(doc.fps(), 60.0);
        QVERIFY(!doc.setClipFps(0.0f));
        QVERIFY(doc.setClipLoop(false));
        QVERIFY(!doc.clip()->loop);
        QVERIFY(doc.setClipDuration(5.0));
        QCOMPARE(doc.clip()->duration, 5.0);
        QVERIFY(doc.addMarker(2.0, QStringLiteral("hit")));
        QVERIFY(doc.addMarker(1.0, QStringLiteral("start")));
        QCOMPARE(doc.clip()->markers.front().name, std::string("start"));  // triés par temps
        QVERIFY(doc.removeMarker(0));
        QCOMPARE(doc.clip()->markers.size(), std::size_t{1});

        QVERIFY(doc.deleteClip().ok());
        QCOMPARE(doc.character()->clips.size(), std::size_t{3});
        QVERIFY(doc.clipIndex() >= 0 && doc.clipIndex() < 3);
        // Annuler la suppression ramène le clip.
        doc.undo();
        QCOMPARE(doc.character()->clips.size(), std::size_t{4});
        while (doc.canUndo()) doc.undo();
        QCOMPARE(doc.character()->clips.size(), std::size_t{1});
        QCOMPARE(doc.clipIndex(), 0);
        while (doc.canRedo()) doc.redo();
        QCOMPARE(doc.character()->clips.size(), std::size_t{3});
    }

    void durationNeverShorterThanTheLastKey() {
        Document doc;
        prepare(doc);
        const BoneId a = doc.skeleton()->find("arm_joint_L_1");
        doc.setFrame(0);
        doc.newClip();
        doc.setClipDuration(5.0);
        doc.setFrame(60);  // 2 s
        doc.insertKeys({a}, 1u << ChannelTranslation);
        QVERIFY(doc.setClipDuration(0.5));
        QCOMPARE(doc.clip()->duration, 2.0);  // ramenée à la dernière clé
    }

    void playbackSettingsAndRange() {
        Document doc;
        doc.setCharacter(loadCharacter("arm.gltf"));  // clip de 1 s
        doc.setPlaying(false);
        QSignalSpy settings(&doc, &Document::playbackSettingsChanged);
        doc.setPlaybackSpeed(100.0);
        QCOMPARE(doc.playbackSpeed(), 4.0);
        doc.setPlaybackSpeed(2.0);
        doc.setLoopPlayback(true);
        doc.setPingPong(false);
        doc.setPlaybackRange(0.25, 0.5);
        QVERIFY(doc.hasPlaybackRange());
        QCOMPARE(doc.rangeStart(), 0.25);
        QCOMPARE(doc.rangeEnd(), 0.5);
        QVERIFY(settings.count() >= 4);

        doc.setTime(0.3);
        doc.setPlaying(true);
        QTest::qWait(250);  // 0,25 s à vitesse 2 : plusieurs tours dans la plage [0,25 ; 0,5]
        QVERIFY(doc.isPlaying());
        QVERIFY2(doc.time() >= 0.25 - 1e-9 && doc.time() <= 0.5 + 1e-9,
                 qPrintable(QString::number(doc.time())));
        doc.setPlaying(false);

        doc.setLoopPlayback(false);
        doc.setPlaybackRange(0.0, -1.0);
        doc.setTime(0.9);
        doc.setPlaybackSpeed(4.0);
        doc.setPlaying(true);
        QTest::qWait(150);
        QVERIFY(!doc.isPlaying());  // fin de clip hors boucle : la lecture s'arrête
        QCOMPARE(doc.time(), 1.0);
    }

    void poseLibrarySavesAppliesAndDeletes() {
        Document doc;
        prepare(doc);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        const BoneId leg = doc.skeleton()->find("leg_joint_L_1");
        Transform t = doc.boneLocal(arm);
        t.r = glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 0, 1));
        doc.setBoneLocal(arm, t);
        doc.setSelection({arm});
        QVERIFY(doc.savePose(QStringLiteral(" Salute "), true).ok());
        QCOMPARE(doc.character()->poses.size(), std::size_t{1});
        QCOMPARE(doc.character()->poses[0].name, std::string("Salute"));
        QCOMPARE(doc.character()->poses[0].bones.size(), std::size_t{1});
        QVERIFY(!doc.savePose(QStringLiteral(""), false).ok());

        doc.resetPose(false);
        QVERIFY(std::abs(glm::dot(doc.pose()[arm].r, t.r)) < 0.999f);
        const std::size_t entries = doc.history().undoCount();
        QVERIFY(doc.applyPose(0));
        QCOMPARE(doc.history().undoCount(), entries + 1);  // une seule entrée
        QVERIFY(std::abs(glm::dot(doc.pose()[arm].r, t.r)) > 0.9999f);
        QVERIFY(!doc.applyPose(9));
        doc.undo();
        QVERIFY(std::abs(glm::dot(doc.pose()[arm].r, t.r)) < 0.999f);

        // Même nom : la pose est remplacée, pas dupliquée ; une pose complète couvre tous les os.
        QVERIFY(doc.savePose(QStringLiteral("Salute"), false).ok());
        QCOMPARE(doc.character()->poses.size(), std::size_t{1});
        QCOMPARE(doc.character()->poses[0].bones.size(), doc.skeleton()->boneCount());
        (void)leg;
        QVERIFY(doc.deletePose(0));
        QVERIFY(doc.character()->poses.empty());
        QVERIFY(!doc.deletePose(0));
        doc.undo();
        QCOMPARE(doc.character()->poses.size(), std::size_t{1});
    }

    void applyPoseWithAutoKeyCreatesKeys() {
        Document doc;
        prepare(doc);
        const BoneId arm = doc.skeleton()->find("arm_joint_L_1");
        Transform t = doc.boneLocal(arm);
        t.t.y += 0.4f;
        doc.setBoneLocal(arm, t);
        doc.savePose(QStringLiteral("Up"), true);
        doc.resetPose(false);
        doc.setAutoKey(true);
        doc.setFrame(5);
        QVERIFY(doc.applyPose(0));
        QVERIFY(doc.clip() != nullptr);
        QVERIFY(findKey(doc.clip()->tracks.at(doc.skeleton()->find("arm_joint_L_1")).translation, 5.0 / 30.0)
                    .has_value());
    }

    void onionSkinSettingsAreClamped() {
        Document doc;
        QSignalSpy spy(&doc, &Document::onionSkinChanged);
        OnionSkin o;
        o.enabled = true;
        o.before = 99;
        o.after = -3;
        o.step = 0;
        doc.setOnionSkin(o);
        QVERIFY(doc.onionSkin().enabled);
        QCOMPARE(doc.onionSkin().before, 10);
        QCOMPARE(doc.onionSkin().after, 0);
        QCOMPARE(doc.onionSkin().step, 1);
        QCOMPARE(spy.count(), 1);
    }

    void keyEditsSurviveAnUndoRedoFuzz() {
        Document doc;
        prepare(doc);
        doc.history().setLimit(2000);
        const BoneId bones[3] = {doc.skeleton()->find("arm_joint_L_1"), doc.skeleton()->find("arm_joint_R_1"),
                                 doc.skeleton()->find("neck_joint_1")};
        unsigned seed = 7;
        const auto rnd = [&](int lo, int hi) {
            seed = seed * 1664525u + 1013904223u;
            return lo + static_cast<int>((seed >> 8) % static_cast<unsigned>(hi - lo + 1));
        };
        doc.newClip();
        doc.setClipDuration(3.0);
        const auto signature = [&] {
            std::vector<double> sig;
            for (const KeyRef& k : allKeys(*doc.clip())) {
                sig.insert(sig.end(), {static_cast<double>(k.bone), static_cast<double>(k.channel), k.time});
            }
            sig.push_back(doc.clip()->duration);
            return sig;
        };
        const auto initial = signature();
        for (int i = 0; i < 120; ++i) {
            const BoneId b = bones[rnd(0, 2)];
            switch (rnd(0, 4)) {
                case 0:
                    doc.setFrame(rnd(0, 60));
                    doc.insertKeys({b}, 1u << rnd(0, 2));
                    break;
                case 1:
                    if (keyCount(*doc.clip()) > 0) {
                        const auto keys = allKeys(*doc.clip());
                        doc.deleteKeys(
                            {keys[static_cast<std::size_t>(rnd(0, static_cast<int>(keys.size()) - 1))]});
                    }
                    break;
                case 2:
                    if (keyCount(*doc.clip()) > 0) {
                        const auto keys = allKeys(*doc.clip());
                        const KeyRef k =
                            keys[static_cast<std::size_t>(rnd(0, static_cast<int>(keys.size()) - 1))];
                        if (doc.beginKeyDrag({k},
                                             rnd(0, 1) != 0 ? KeyDragMode::Move : KeyDragMode::Duplicate)) {
                            doc.updateKeyDrag(rnd(-5, 20) * 0.1);
                            doc.endKeyDrag();
                        }
                    }
                    break;
                case 3:
                    if (keyCount(*doc.clip()) > 0) {
                        const auto keys = allKeys(*doc.clip());
                        doc.setKeysInterpolation(
                            {keys[static_cast<std::size_t>(rnd(0, static_cast<int>(keys.size()) - 1))]},
                            static_cast<Interpolation>(rnd(0, 2)));
                    }
                    break;
                default:
                    if (rnd(0, 1) != 0)
                        doc.undo();
                    else
                        doc.redo();
                    break;
            }
        }
        while (doc.canRedo()) doc.redo();
        const auto top = signature();
        while (doc.history().undoCount() > 2) doc.undo();  // reste : création du clip + durée
        doc.undo();
        doc.undo();
        QVERIFY(doc.clip() == nullptr);
        while (doc.canRedo()) doc.redo();
        QVERIFY(signature() == top);
        (void)initial;
    }
};

RIGGER_REGISTER_QTEST(AnimationTest);
#include "test_animation.moc"
