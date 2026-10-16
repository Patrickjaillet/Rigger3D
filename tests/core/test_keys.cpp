// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>
#include <tuple>

#include "core/animation/keys.hpp"
#include "core/command/commands.hpp"
#include "core/command/history.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;
using testutil::Rng;
using testutil::sameRot;

namespace {

Transform xf(const glm::vec3& t, float angleDeg = 0.0f, const glm::vec3& s = {1, 1, 1}) {
    Transform out;
    out.t = t;
    out.r = glm::angleAxis(glm::radians(angleDeg), glm::vec3(0, 0, 1));
    out.s = s;
    return out;
}

}  // namespace

TEST_CASE("keyTransform : une clé par canal demandé, masque respecté, clé existante conservée") {
    BoneTracks tracks;
    keyTransform(tracks, 1.0, xf({1, 2, 3}, 30), kAllChannels);
    CHECK(tracks.translation.keys.size() == 1);
    CHECK(tracks.rotation.keys.size() == 1);
    CHECK(tracks.scale.keys.size() == 1);
    CHECK(keyedChannels(tracks, 1.0) == 0b111);

    keyTransform(tracks, 2.0, xf({4, 5, 6}), 1u << ChannelTranslation);  // seulement T
    CHECK(tracks.translation.keys.size() == 2);
    CHECK(tracks.rotation.keys.size() == 1);
    CHECK(keyedChannels(tracks, 2.0) == 0b001);
    CHECK(hasKey(tracks, ChannelTranslation, 2.0004));
    CHECK_FALSE(hasKey(tracks, ChannelRotation, 2.0));

    // Une clé existante garde son mode et ses tangentes quand on la ré-enregistre.
    tracks.translation.keys[0].mode = Interpolation::Bezier;
    tracks.translation.keys[0].outTangent = {9, 9, 9};
    keyTransform(tracks, 1.0, xf({7, 7, 7}), kAllChannels, Interpolation::Constant);
    CHECK(tracks.translation.keys[0].value == glm::vec3(7, 7, 7));
    CHECK(tracks.translation.keys[0].mode == Interpolation::Bezier);
    CHECK(tracks.translation.keys[0].outTangent == glm::vec3(9, 9, 9));
    // Une nouvelle clé reçoit le mode demandé.
    keyTransform(tracks, 5.0, xf({0, 0, 0}), kAllChannels, Interpolation::Constant);
    CHECK(tracks.rotation.keys.back().mode == Interpolation::Constant);
}

TEST_CASE("suppression, énumération et comptage de clés") {
    AnimationClip clip;
    keyTransform(clip.tracks[3], 0.0, xf({0, 0, 0}), kAllChannels);
    keyTransform(clip.tracks[3], 1.0, xf({1, 0, 0}), kAllChannels);
    keyTransform(clip.tracks[5], 1.0, xf({2, 0, 0}), 1u << ChannelScale);
    CHECK(keyCount(clip) == 7);
    CHECK(allKeys(clip).size() == 7);
    CHECK(keysOfBone(clip, 5).size() == 1);
    CHECK(keysOfBone(clip, 99).empty());

    CHECK(eraseKeys(clip, {{3, ChannelRotation, 1.0}, {3, ChannelRotation, 1.0}, {42, 0, 0.0}}) ==
          1);  // doublon / inconnu
    CHECK(keyCount(clip) == 6);
    CHECK(eraseKeys(clip, keysOfBone(clip, 5)) == 1);
    CHECK(clip.tracks.count(5) == 0);  // piste vide retirée
    CHECK(eraseKeys(clip, allKeys(clip)) == 5);
    CHECK(clip.tracks.empty());
}

TEST_CASE("remapKeys : déplacement, remplacement à l'arrivée, duplication, étirement, durée") {
    AnimationClip clip;
    clip.duration = 2.0;
    for (int i = 0; i <= 4; ++i) {
        keyTransform(clip.tracks[1], i * 0.5, xf({static_cast<float>(i), 0, 0}), 1u << ChannelTranslation);
    }
    // Décale les clés de 0,5 s à 1,0 s de +0,25 s.
    const auto moved = remapKeys(clip, {{1, 0, 0.5}, {1, 0, 1.0}}, [](double t) { return t + 0.25; });
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].time == doctest::Approx(0.75));
    CHECK(clip.tracks[1].translation.keys.size() == 5);
    CHECK_FALSE(hasKey(clip.tracks[1], 0, 0.5));
    CHECK(hasKey(clip.tracks[1], 0, 0.75));
    CHECK(hasKey(clip.tracks[1], 0, 1.25));
    const auto& keys = clip.tracks[1].translation.keys;
    CHECK(std::is_sorted(keys.begin(), keys.end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; }));

    // Une clé qui arrive sur une clé existante la remplace.
    remapKeys(clip, {{1, 0, 0.75}}, [](double) { return 1.5; });
    CHECK(clip.tracks[1].translation.keys.size() == 4);

    // Duplication : les originales restent, des copies apparaissent, la durée s'étend.
    const std::size_t before = keyCount(clip);
    const auto copies = remapKeys(clip, {{1, 0, 0.0}}, [](double t) { return t + 10.0; }, true);
    CHECK(keyCount(clip) == before + 1);
    CHECK(hasKey(clip.tracks[1], 0, 0.0));
    CHECK(hasKey(clip.tracks[1], 0, 10.0));
    CHECK(clip.duration == doctest::Approx(10.0));
    CHECK(copies[0].time == doctest::Approx(10.0));

    // Étirement autour d'un pivot, temps négatifs ramenés à 0.
    remapKeys(clip, {{1, 0, 10.0}}, [](double t) { return 1.0 + (t - 1.0) * -0.5; });
    CHECK(hasKey(clip.tracks[1], 0, 0.0));  // 1 + 9 * -0.5 = -3.5 -> 0
    // Référence inconnue : aucun effet.
    CHECK(remapKeys(clip, {{77, 0, 1.0}}, [](double t) { return t; }).empty());
}

TEST_CASE("setKeyInterpolation : mode par clé, tangentes auto en Bezier") {
    AnimationClip clip;
    for (int i = 0; i < 4; ++i) {
        keyTransform(clip.tracks[2], i, xf({static_cast<float>(i * i), 0, 0}), 1u << ChannelTranslation);
    }
    setKeyInterpolation(clip, {{2, 0, 1.0}}, Interpolation::Bezier);
    const auto& keys = clip.tracks[2].translation.keys;
    CHECK(keys[1].mode == Interpolation::Bezier);
    CHECK(keys[2].mode == Interpolation::Linear);          // les autres clés ne changent pas
    CHECK(keys[1].outTangent.x == doctest::Approx(2.0f));  // (4 - 0) / 2
    CHECK(keys[2].outTangent.x == 0.0f);

    // Tangente déjà éditée : conservée.
    clip.tracks[2].translation.keys[1].outTangent = {7, 0, 0};
    setKeyInterpolation(clip, {{2, 0, 1.0}}, Interpolation::Bezier);
    CHECK(clip.tracks[2].translation.keys[1].outTangent.x == 7.0f);
    setKeyInterpolation(clip, {{2, 0, 1.0}, {2, 0, 3.0}}, Interpolation::Constant);
    CHECK(clip.tracks[2].translation.keys[3].mode == Interpolation::Constant);
    setKeyInterpolation(clip, {{9, 0, 1.0}}, Interpolation::Linear);  // inconnu : sans effet

    // Rotation en Bezier : tangente calculée aussi.
    for (int i = 0; i < 3; ++i)
        keyTransform(clip.tracks[4], i, xf({0, 0, 0}, 40.0f * static_cast<float>(i)), 1u << ChannelRotation);
    setKeyInterpolation(clip, {{4, ChannelRotation, 1.0}}, Interpolation::Bezier);
    CHECK(clip.tracks[4].rotation.keys[1].mode == Interpolation::Bezier);
    CHECK(glm::length(sampleTrack(clip.tracks[4].rotation, 0.5)) == doctest::Approx(1.0f));
}

TEST_CASE("copier / coller de clés : temps relatifs, mode et tangentes copiés, os invalides ignorés") {
    AnimationClip clip;
    keyTransform(clip.tracks[1], 1.0, xf({1, 0, 0}, 10), kAllChannels, Interpolation::Bezier);
    keyTransform(clip.tracks[1], 2.0, xf({2, 0, 0}, 20), kAllChannels);
    keyTransform(clip.tracks[2], 1.5, xf({9, 9, 9}), 1u << ChannelTranslation);
    clip.tracks[1].translation.keys[0].outTangent = {5, 0, 0};

    const std::vector<KeyRef> picked = {{1, 0, 1.0}, {1, 1, 2.0}, {2, 0, 1.5}};
    const auto clipboard = copyKeys(clip, picked);
    REQUIRE(clipboard.size() == 3);
    CHECK(clipboard[0].relativeTime == 0.0);
    CHECK(clipboard[1].relativeTime == doctest::Approx(1.0));

    const auto pasted = pasteKeys(clip, clipboard, 10.0, [](BoneId id) { return id != 2; });
    REQUIRE(pasted.size() == 2);  // l'os 2 est refusé
    CHECK(hasKey(clip.tracks[1], 0, 10.0));
    CHECK(hasKey(clip.tracks[1], 1, 11.0));
    const auto idx = findKey(clip.tracks[1].translation, 10.0);
    CHECK(clip.tracks[1].translation.keys[*idx].mode == Interpolation::Bezier);
    CHECK(clip.tracks[1].translation.keys[*idx].outTangent.x == 5.0f);
    CHECK(clip.duration >= 11.0);
    CHECK(pasteKeys(clip, {}, 0.0).empty());
}

TEST_CASE("EditTracksCommand : annuler / rétablir, fusion, durée, clip inconnu") {
    Character c;
    c.skeleton.addBone("a", kNoBone);
    c.clips.emplace_back();
    c.clips[0].duration = 1.0;
    Pose pose = c.skeleton.bindPose();
    EditContext ctx{&c, &pose};
    History history;

    BoneTracks tracks;
    keyTransform(tracks, 3.0, xf({1, 2, 3}), kAllChannels);
    history.execute(
        std::make_unique<EditTracksCommand>(
            0, std::vector<EditTracksCommand::Entry>{{0, BoneTracks{}, tracks}}, 1.0, 3.0, "Insert Keys"),
        ctx);
    CHECK(c.clips[0].tracks.count(0) == 1);
    CHECK(c.clips[0].duration == 3.0);
    history.undo(ctx);
    CHECK(c.clips[0].tracks.empty());  // piste vide retirée
    CHECK(c.clips[0].duration == 1.0);
    history.redo(ctx);
    CHECK(c.clips[0].tracks[0].translation.keys.size() == 1);

    // Fusion : un glissé de clé = une entrée, l'état de départ reste le premier.
    for (int i = 1; i <= 5; ++i) {
        BoneTracks moving = tracks;
        moving.translation.keys[0].time = 3.0 + i;
        history.execute(
            std::make_unique<EditTracksCommand>(0, std::vector<EditTracksCommand::Entry>{{0, tracks, moving}},
                                                3.0, 3.0 + i, "Move Keys", "drag"),
            ctx);
    }
    CHECK(history.undoCount() == 2);
    CHECK(c.clips[0].tracks[0].translation.keys[0].time == 8.0);
    CHECK(c.clips[0].duration == 8.0);
    history.undo(ctx);
    CHECK(c.clips[0].tracks[0].translation.keys[0].time == 3.0);
    CHECK(c.clips[0].duration == 3.0);

    // Clip inexistant : sans effet, sans plantage.
    EditTracksCommand ghost(5, {{0, BoneTracks{}, tracks}}, 0.0, 1.0, "Ghost");
    ghost.execute(ctx);
    ghost.undo(ctx);
    CHECK(c.clips.size() == 1);
}

TEST_CASE("réglages de clip, liste de clips et bibliothèque de poses : annulables") {
    Character c;
    c.skeleton.addBone("a", kNoBone);
    AnimationClip clip;
    clip.name = "Walk";
    clip.duration = 2.0;
    c.clips = {clip};
    Pose pose = c.skeleton.bindPose();
    EditContext ctx{&c, &pose};
    History history;

    ClipMeta before = ClipMeta::of(c.clips[0]);
    ClipMeta after = before;
    after.name = "Run";
    after.fps = 60.0f;
    after.loop = false;
    after.markers.push_back({1.0, "step"});
    history.execute(std::make_unique<EditClipMetaCommand>(0, before, after, "Edit Clip"), ctx);
    CHECK(c.clips[0].name == "Run");
    CHECK(c.clips[0].fps == 60.0f);
    CHECK_FALSE(c.clips[0].loop);
    CHECK(c.clips[0].markers.size() == 1);
    history.undo(ctx);
    CHECK(c.clips[0].name == "Walk");
    CHECK(c.clips[0].markers.empty());

    const auto oldClips = c.clips;
    auto newClips = oldClips;
    newClips.push_back(AnimationClip{});
    newClips.back().name = "Idle";
    history.execute(std::make_unique<EditClipListCommand>(oldClips, newClips, "New Clip", "Idle"), ctx);
    CHECK(c.clips.size() == 2);
    CHECK(history.nextUndo()->detail() == "Idle");
    history.undo(ctx);
    CHECK(c.clips.size() == 1);

    NamedPose p;
    p.name = "Salute";
    p.bones.emplace_back("a", xf({1, 0, 0}));
    history.execute(std::make_unique<EditPoseLibraryCommand>(
                        std::vector<NamedPose>{}, std::vector<NamedPose>{p}, "Save Pose", "Salute"),
                    ctx);
    CHECK(c.poses.size() == 1);
    history.undo(ctx);
    CHECK(c.poses.empty());
}

TEST_CASE("fuzzing des clés : 500 éditions aléatoires puis tout annuler / rétablir") {
    Rng rng(5);
    Character c;
    for (int i = 0; i < 4; ++i) c.skeleton.addBone("b" + std::to_string(i), kNoBone);
    c.clips.emplace_back();
    c.clips[0].duration = 1.0;
    Pose pose = c.skeleton.bindPose();
    EditContext ctx{&c, &pose};
    History history(10000);

    const auto snapshot = [&] {
        std::vector<std::tuple<BoneId, int, double, float, int>> s;
        for (const KeyRef& k : allKeys(c.clips[0])) {
            const auto& t = c.clips[0].tracks.at(k.bone);
            float v = 0;
            int mode = 0;
            if (k.channel == ChannelRotation) {
                const auto i = findKey(t.rotation, k.time);
                v = t.rotation.keys[*i].value.w;
                mode = static_cast<int>(t.rotation.keys[*i].mode);
            } else {
                const auto& tr = k.channel == ChannelScale ? t.scale : t.translation;
                const auto i = findKey(tr, k.time);
                v = tr.keys[*i].value.x;
                mode = static_cast<int>(tr.keys[*i].mode);
            }
            s.emplace_back(k.bone, k.channel, k.time, v, mode);
        }
        s.emplace_back(0, 99, c.clips[0].duration, 0.0f, 0);
        return s;
    };
    const auto initial = snapshot();

    for (int i = 0; i < 500; ++i) {
        AnimationClip edited = c.clips[0];
        const double durationBefore = edited.duration;
        std::vector<BoneId> touched;
        const int op = rng.integer(0, 4);
        const BoneId bone = static_cast<BoneId>(rng.integer(0, 3));
        const double t = rng.integer(0, 40) * 0.1;
        if (op == 0) {
            keyTransform(edited.tracks[bone], t, xf(rng.vec(3.0f), rng.uniform(0, 90)),
                         1u << rng.integer(0, 2));
            extendDuration(edited);
        } else if (op == 1 && keyCount(edited) > 0) {
            const auto keys = allKeys(edited);
            eraseKeys(edited,
                      {keys[static_cast<std::size_t>(rng.integer(0, static_cast<int>(keys.size()) - 1))]});
        } else if (op == 2 && keyCount(edited) > 0) {
            const auto keys = allKeys(edited);
            const double dt = rng.integer(-5, 15) * 0.1;
            remapKeys(
                edited, {keys[static_cast<std::size_t>(rng.integer(0, static_cast<int>(keys.size()) - 1))]},
                [dt](double x) { return x + dt; }, rng.integer(0, 1) != 0);
        } else if (op == 3 && keyCount(edited) > 0) {
            const auto keys = allKeys(edited);
            setKeyInterpolation(
                edited, {keys[static_cast<std::size_t>(rng.integer(0, static_cast<int>(keys.size()) - 1))]},
                static_cast<Interpolation>(rng.integer(0, 2)));
        } else {
            continue;
        }
        std::vector<EditTracksCommand::Entry> entries;
        for (BoneId b = 0; b < 4; ++b) {
            const BoneTracks oldT = c.clips[0].tracks.count(b) ? c.clips[0].tracks.at(b) : BoneTracks{};
            const BoneTracks newT = edited.tracks.count(b) ? edited.tracks.at(b) : BoneTracks{};
            entries.push_back({b, oldT, newT});
        }
        history.execute(
            std::make_unique<EditTracksCommand>(0, entries, durationBefore, edited.duration, "Edit Keys"),
            ctx);
        if (rng.integer(0, 6) == 0) {
            history.undo(ctx);
            if (rng.integer(0, 1) != 0) history.redo(ctx);
        }
    }
    const auto top = snapshot();
    while (history.undo(ctx) != nullptr) {
    }
    CHECK(snapshot() == initial);
    while (history.redo(ctx) != nullptr) {
    }
    CHECK(snapshot() == top);
    CHECK(keyCount(c.clips[0]) > 0);
}
