// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>

#include "core/animation/mix.hpp"
#include "core/animation/retarget.hpp"
#include "core/skeleton/fk.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

// Mini humanoïde : hips -> spine -> head ; spine -> arm.L -> hand.L ; hips -> upleg.L -> leg.L -> foot.L.
// `scale` multiplie toutes les longueurs ; `style` choisit la convention de noms.
struct Humanoid {
    Skeleton sk;
    BoneId hips, spine, head, arm, hand, upleg, leg, foot;
    explicit Humanoid(float scale = 1.0f, bool mixamo = false) {
        const auto name = [&](const char* plain, const char* mix) {
            return std::string(mixamo ? mix : plain);
        };
        const auto add = [&](const std::string& n, BoneId parent, glm::vec3 t) {
            Transform bind;
            bind.t = t * scale;
            return sk.addBone(n, parent, bind).value();
        };
        hips = add(name("hips", "mixamorig:Hips"), kNoBone, {0, 1, 0});
        spine = add(name("spine", "mixamorig:Spine"), hips, {0, 0.3f, 0});
        head = add(name("head", "mixamorig:Head"), spine, {0, 0.4f, 0});
        arm = add(name("upperarm.L", "mixamorig:LeftArm"), spine, {0.2f, 0.3f, 0});
        hand = add(name("hand.L", "mixamorig:LeftHand"), arm, {0.4f, 0, 0});
        upleg = add(name("thigh.L", "mixamorig:LeftUpLeg"), hips, {0.1f, -0.1f, 0});
        leg = add(name("shin.L", "mixamorig:LeftLeg"), upleg, {0, -0.45f, 0});
        foot = add(name("foot.L", "mixamorig:LeftFoot"), leg, {0, -0.45f, 0});
    }
    // Clip : chaque os listé tourne autour de `axis` de `angle` à t = duration (linéaire depuis le repos).
    AnimationClip clip(const char* label, const std::vector<BoneId>& bones, float angle,
                       double duration = 1.0) const {
        AnimationClip c;
        c.name = label;
        c.duration = duration;
        c.fps = 30.0f;
        for (const BoneId b : bones) {
            for (const double time : {0.0, duration}) {
                Keyframe<glm::quat> k;
                k.time = time;
                k.value =
                    glm::angleAxis(time == 0.0 ? 0.0f : angle, glm::vec3(0, 0, 1)) * sk.bone(b).bindLocal.r;
                k.mode = Interpolation::Linear;
                c.tracks[b].rotation.keys.push_back(k);
            }
        }
        return c;
    }
    glm::vec3 world(const Pose& p, BoneId id) const { return glm::vec3(computeGlobals(sk, p)[id][3]); }
};

bool samePose(const Pose& a, const Pose& b, float eps = 1e-5f) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!near(a[i].t, b[i].t, eps) || !testutil::sameRot(a[i].r, b[i].r, 1e-6f) ||
            !near(a[i].s, b[i].s, eps))
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("couches : poids 0 = pose inchangée, poids 1 = clip, additif ajoute la différence") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("wave", {h.arm}, 1.0f)};
    const Pose bind = h.sk.bindPose();

    AnimationLayer layer;
    layer.clip = 0;
    layer.weight = 0.0f;
    layer.mode = LayerMode::Additive;
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), bind));
    layer.mode = LayerMode::Override;
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), bind));

    layer.weight = 1.0f;
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), samplePose(clips[0], h.sk, 0.5)));
    layer.mode = LayerMode::Additive;  // clip depuis le repos : additif plein poids = clip
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), samplePose(clips[0], h.sk, 0.5)));
    layer.weight = 0.5f;  // demi-poids additif = demi-rotation (angle 0,5 à t = 0,5 -> 0,25)
    const Pose half = evaluateLayers(h.sk, clips, {layer}, 0.5);
    CHECK(testutil::sameRot(half[h.arm].r,
                            glm::angleAxis(0.25f, glm::vec3(0, 0, 1)) * h.sk.bone(h.arm).bindLocal.r, 1e-5f));
    layer.enabled = false;
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), bind));
    layer.enabled = true;
    layer.clip = 7;  // clip inexistant : ignoré
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 0.5), bind));
}

TEST_CASE("couches : masque d'os respecté (os + descendants seulement)") {
    Humanoid h;
    std::vector<AnimationClip> clips = {
        h.clip("all", {h.hips, h.spine, h.arm, h.hand, h.upleg, h.leg}, 0.8f)};
    AnimationLayer layer;
    layer.clip = 0;
    layer.mask = {h.arm};
    const Pose pose = evaluateLayers(h.sk, clips, {layer}, 0.5);
    const Pose bind = h.sk.bindPose();
    const Pose full = samplePose(clips[0], h.sk, 0.5);
    for (const BoneId id : {h.arm, h.hand}) CHECK(testutil::sameRot(pose[id].r, full[id].r, 1e-6f));
    for (const BoneId id : {h.hips, h.spine, h.head, h.upleg, h.leg, h.foot}) {
        CHECK(testutil::sameRot(pose[id].r, bind[id].r, 1e-6f));
    }
    const auto mask = boneMask(h.sk, {h.arm});
    CHECK(mask[h.arm]);
    CHECK(mask[h.hand]);
    CHECK_FALSE(mask[h.spine]);
    CHECK(boneMask(h.sk, {}).empty());
}

TEST_CASE("couches : marche (jambes) + salut (bras) superposés avec masques") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("walk", {h.upleg, h.leg, h.arm}, 0.6f),
                                        h.clip("wave", {h.arm, h.hand}, -1.2f)};
    AnimationLayer legs;
    legs.name = "legs";
    legs.clip = 0;
    legs.mask = {h.hips};  // tout le corps
    AnimationLayer arms;
    arms.name = "arms";
    arms.clip = 1;
    arms.mask = {h.arm};
    const Pose pose = evaluateLayers(h.sk, clips, {legs, arms}, 0.5);
    CHECK(testutil::sameRot(pose[h.upleg].r, samplePose(clips[0], h.sk, 0.5)[h.upleg].r, 1e-6f));
    CHECK(testutil::sameRot(pose[h.arm].r, samplePose(clips[1], h.sk, 0.5)[h.arm].r,
                            1e-6f));  // le salut recouvre la marche
    CHECK(mixDuration(clips, AnimationMix{MixMode::Layers, {legs, arms}, {}, {}}) == doctest::Approx(1.0));
}

TEST_CASE("couches : boucle et vitesse") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("c", {h.arm}, 1.0f, 2.0)};
    AnimationLayer layer;
    layer.clip = 0;
    layer.speed = 2.0f;
    // t = 1.5 s à vitesse 2 -> 3 s de clip -> boucle -> 1 s de clip.
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 1.5), samplePose(clips[0], h.sk, 1.0)));
    layer.loop = false;  // sans boucle : bloqué à la fin
    CHECK(samePose(evaluateLayers(h.sk, clips, {layer}, 1.5), samplePose(clips[0], h.sk, 2.0)));
}

TEST_CASE("crossfade et NLA : bandes, répétition, fondu enchaîné") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("A", {h.arm}, 1.0f), h.clip("B", {h.arm}, -1.0f)};
    const Pose a = samplePose(clips[0], h.sk, 0.5), b = samplePose(clips[1], h.sk, 0.5);
    CHECK(samePose(crossfade(a, b, 0.0f), a));
    CHECK(samePose(crossfade(a, b, 1.0f), b));
    CHECK(testutil::sameRot(crossfade(a, b, 0.5f)[h.arm].r, h.sk.bone(h.arm).bindLocal.r,
                            1e-5f));  // +0,5 et -0,5 se compensent

    NlaStrip sa;
    sa.name = "A";
    sa.clip = 0;
    sa.start = 1.0;
    sa.repeat = 2.0f;  // longueur 2 s
    CHECK(stripLength(sa, clips) == doctest::Approx(2.0));
    CHECK(samePose(evaluateNla(h.sk, clips, {sa}, 0.5), h.sk.bindPose()));  // avant la bande
    CHECK(samePose(evaluateNla(h.sk, clips, {sa}, 1.5), samplePose(clips[0], h.sk, 0.5)));
    CHECK(samePose(evaluateNla(h.sk, clips, {sa}, 2.5), samplePose(clips[0], h.sk, 0.5)));  // 2e répétition
    CHECK(samePose(evaluateNla(h.sk, clips, {sa}, 3.5), h.sk.bindPose()));                  // après la bande
    sa.speed = 2.0f;
    CHECK(stripLength(sa, clips) == doctest::Approx(1.0));
    sa.enabled = false;
    CHECK(samePose(evaluateNla(h.sk, clips, {sa}, 1.5), h.sk.bindPose()));

    // Fondu enchaîné : B démarre à 1,5 s avec blendIn 0,5 s par-dessus A (même piste).
    NlaStrip first;
    first.clip = 0;
    first.start = 0.0;
    NlaStrip second;
    second.clip = 1;
    second.start = 0.5;
    second.blendIn = 0.5;
    first.repeat = 1.0f;
    const double t = 0.75;  // u = 0,5
    const Pose mixed = evaluateNla(h.sk, clips, {first, second}, t);
    const Pose expected = crossfade(samplePose(clips[0], h.sk, t), samplePose(clips[1], h.sk, t - 0.5), 0.5f);
    CHECK(samePose(mixed, expected, 1e-5f));
    // blendOut de la dernière bande : retombe vers la pose de repos.
    NlaStrip solo;
    solo.clip = 0;
    solo.blendOut = 0.5;
    CHECK(samePose(evaluateNla(h.sk, clips, {solo}, 1.0), h.sk.bindPose()));
    CHECK(mixDuration(clips, AnimationMix{MixMode::Nla, {}, {first, second}, {}}) == doctest::Approx(1.5));
}

TEST_CASE("NLA : une piste haute recouvre une piste basse, additif possible") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("base", {h.arm, h.spine}, 1.0f),
                                        h.clip("top", {h.arm}, -0.5f)};
    NlaStrip base;
    base.clip = 0;
    NlaStrip top;
    top.clip = 1;
    top.track = 1;
    const Pose p = evaluateNla(h.sk, clips, {top, base}, 0.5);  // ordre du tableau sans importance
    CHECK(testutil::sameRot(p[h.arm].r, samplePose(clips[1], h.sk, 0.5)[h.arm].r, 1e-6f));
    CHECK(testutil::sameRot(p[h.spine].r, samplePose(clips[0], h.sk, 0.5)[h.spine].r, 1e-6f));
    top.mode = LayerMode::Additive;
    top.weight = 1.0f;
    const Pose q = evaluateNla(h.sk, clips, {top, base}, 0.5);
    // Additif : rotation base (0,5) puis delta de top (-0,25) autour du même axe -> 0,25.
    CHECK(testutil::sameRot(q[h.arm].r,
                            glm::angleAxis(0.25f, glm::vec3(0, 0, 1)) * h.sk.bone(h.arm).bindLocal.r, 1e-5f));
}

TEST_CASE("blend tree 1D : seuils, interpolation, bornes, synchronisation de phase") {
    Humanoid h;
    std::vector<AnimationClip> clips = {h.clip("walk", {h.leg}, 0.4f, 1.0),
                                        h.clip("run", {h.leg}, 1.2f, 0.5)};
    BlendTree1D tree;
    tree.entries = {{0, 1.0f}, {1, 3.0f}};
    tree.parameter = 1.0f;
    CHECK(samePose(evaluateBlendTree(h.sk, clips, tree, 0.3), samplePose(clips[0], h.sk, 0.3)));
    tree.parameter = 3.0f;
    CHECK(samePose(evaluateBlendTree(h.sk, clips, tree, 0.2), samplePose(clips[1], h.sk, 0.2)));
    tree.parameter = 99.0f;  // au-delà du dernier seuil : borné
    CHECK(samePose(evaluateBlendTree(h.sk, clips, tree, 0.2), samplePose(clips[1], h.sk, 0.2)));
    tree.parameter = -5.0f;
    CHECK(samePose(evaluateBlendTree(h.sk, clips, tree, 0.3), samplePose(clips[0], h.sk, 0.3)));
    tree.parameter = 2.0f;  // milieu : durée mélangée 0,75 s, phase 0,4 -> tA = 0,4 s, tB = 0,2 s
    const double time = 0.3;
    const double phase = time / 0.75;
    const Pose mid = evaluateBlendTree(h.sk, clips, tree, time);
    CHECK(samePose(
        mid,
        crossfade(samplePose(clips[0], h.sk, phase * 1.0), samplePose(clips[1], h.sk, phase * 0.5), 0.5f),
        1e-5f));
    tree.entries.clear();
    CHECK(samePose(evaluateBlendTree(h.sk, clips, tree, 0.3), h.sk.bindPose()));
    // Dispatch.
    AnimationMix mix;
    mix.mode = MixMode::Clip;
    CHECK(samePose(evaluateMix(h.sk, clips, mix, 0.1), h.sk.bindPose()));
}

TEST_CASE("retarget : noms canoniques et correspondance automatique") {
    char side = 0;
    CHECK(canonicalBoneName("mixamorig:LeftUpLeg", side) == "upleg");
    CHECK(side == 'L');
    CHECK(canonicalBoneName("thigh.L", side) == "upleg");
    CHECK(side == 'L');
    CHECK(canonicalBoneName("Bip01_R_Calf", side) == "leg");
    CHECK(side == 'R');
    CHECK(canonicalBoneName("Spine1", side) == "spine1");
    CHECK(side == 0);
    CHECK(canonicalBoneName("mixamorig:Hips", side) == "hips");
    CHECK(canonicalBoneName("pelvis", side) == "hips");

    Humanoid src(1.0f, false), dst(1.0f, true);
    const BoneMap map = autoMapBones(src.sk, dst.sk);
    CHECK(map.pairs.size() == 8);
    CHECK(map.target(src.hips) == dst.hips);
    CHECK(map.target(src.arm) == dst.arm);
    CHECK(map.target(src.upleg) == dst.upleg);
    CHECK(map.target(src.foot) == dst.foot);
}

TEST_CASE("retarget : identité = clip identique") {
    Humanoid h;
    AnimationClip clip = h.clip("c", {h.hips, h.arm, h.upleg}, 0.9f);
    Keyframe<glm::vec3> t0, t1;
    t0.time = 0.0;
    t0.value = h.sk.bone(h.hips).bindLocal.t;
    t1.time = 1.0;
    t1.value = t0.value + glm::vec3(0.5f, 0.0f, 0.2f);
    t0.mode = t1.mode = Interpolation::Linear;
    clip.tracks[h.hips].translation.keys = {t0, t1};
    BoneMap map;
    for (const BoneId id : h.sk.order()) map.pairs.emplace_back(id, id);
    const AnimationClip out = retargetClip(clip, h.sk, h.sk, map);
    for (const double t : {0.0, 0.2, 0.5, 1.0}) {
        const Pose a = samplePose(clip, h.sk, t), b = samplePose(out, h.sk, t);
        for (const BoneId id : {h.hips, h.arm, h.upleg}) {
            CHECK(testutil::sameRot(a[id].r, b[id].r, 1e-5f));
            CHECK(near(a[id].t, b[id].t, 1e-4f));
        }
    }
    CHECK(out.duration == doctest::Approx(clip.duration));
}

TEST_CASE("retarget : proportions différentes (squelette 2x) -> positions monde mises à l'échelle") {
    Humanoid small(1.0f, false), big(2.0f, true);
    AnimationClip clip = small.clip("move", {small.hips, small.arm, small.leg, small.spine}, 0.7f);
    Keyframe<glm::vec3> t0, t1;
    t0.time = 0.0;
    t0.value = small.sk.bone(small.hips).bindLocal.t;
    t1.time = 1.0;
    t1.value = t0.value + glm::vec3(0.4f, 0.0f, 0.1f);
    t0.mode = t1.mode = Interpolation::Linear;
    clip.tracks[small.hips].translation.keys = {t0, t1};
    const BoneMap map = autoMapBones(small.sk, big.sk);
    const AnimationClip out = retargetClip(clip, small.sk, big.sk, map);
    REQUIRE_FALSE(out.tracks.empty());
    for (const double t : {0.0, 0.3, 0.7, 1.0}) {
        const Pose a = samplePose(clip, small.sk, t), b = samplePose(out, big.sk, t);
        for (const BoneId id : {small.hand, small.foot, small.head, small.hips}) {
            const BoneId other = map.target(id);
            CHECK(near(big.world(b, other), small.world(a, id) * 2.0f, 2e-3f));
        }
    }
}
