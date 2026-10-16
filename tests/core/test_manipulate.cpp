// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>

#include "core/skeleton/fk.hpp"
#include "core/skeleton/manipulate.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;
using testutil::Rng;
using testutil::sameRot;

namespace {

// Parent P tourné de 90° autour de Z, enfant C à 1 m de P le long de son axe X local.
struct Pair {
    Skeleton sk;
    BoneId p, c;
    Pair() {
        Transform tp;
        tp.t = {1, 2, 3};
        tp.r = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        Transform tc;
        tc.t = {1, 0, 0};
        p = sk.addBone("P", kNoBone, tp).value();
        c = sk.addBone("C", p, tc).value();
    }
};

}  // namespace

TEST_CASE("delta identité : la transformation locale ne change pas") {
    Pair pair;
    const Pose pose = pair.sk.bindPose();
    const auto g = computeGlobals(pair.sk, pose);
    for (const BoneId id : {pair.p, pair.c}) {
        const Transform out = applyWorldDelta(pair.sk, g, pose[id], id, WorldDelta{});
        CHECK(near(out.t, pose[id].t));
        CHECK(sameRot(out.r, pose[id].r));
        CHECK(near(out.s, pose[id].s));
    }
    CHECK(WorldDelta{}.isIdentity());
}

TEST_CASE("translation monde : l'os se déplace exactement du delta, même sous un parent tourné") {
    Pair pair;
    const Pose pose = pair.sk.bindPose();
    const auto g = computeGlobals(pair.sk, pose);
    WorldDelta delta;
    delta.translation = {0.5f, -1.0f, 2.0f};

    Pose moved = pose;
    moved[pair.c] = applyWorldDelta(pair.sk, g, pose[pair.c], pair.c, delta);
    const auto g2 = computeGlobals(pair.sk, moved);
    CHECK(near(glm::vec3(g2[pair.c][3]), glm::vec3(g[pair.c][3]) + delta.translation));
    // L'orientation monde n'a pas changé.
    CHECK(sameRot(Transform::fromMat4(g2[pair.c]).r, Transform::fromMat4(g[pair.c]).r));
}

TEST_CASE("rotation monde : l'orientation tourne autour de l'origine de l'os, ses enfants suivent") {
    Pair pair;
    const Pose pose = pair.sk.bindPose();
    const auto g = computeGlobals(pair.sk, pose);
    WorldDelta delta;
    delta.rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(1, 0, 0));  // 30° autour de X MONDE

    Pose moved = pose;
    moved[pair.p] = applyWorldDelta(pair.sk, g, pose[pair.p], pair.p, delta);
    const auto g2 = computeGlobals(pair.sk, moved);
    CHECK(near(glm::vec3(g2[pair.p][3]), glm::vec3(g[pair.p][3])));  // l'origine ne bouge pas
    const glm::quat expected = delta.rotation * Transform::fromMat4(g[pair.p]).r;
    CHECK(sameRot(Transform::fromMat4(g2[pair.p]).r, expected));
    // L'enfant (à 1 m le long de l'axe X local de P) tourne avec lui autour de l'origine de P.
    const glm::vec3 originP(g[pair.p][3]);
    const glm::vec3 before = glm::vec3(g[pair.c][3]) - originP;
    CHECK(near(glm::vec3(g2[pair.c][3]) - originP, delta.rotation * before));
}

TEST_CASE("échelle : facteurs dans le repère local, uniforme exacte, repère monde projeté") {
    Pair pair;
    Pose pose = pair.sk.bindPose();
    pose[pair.c].s = {2, 3, 4};
    const auto g = computeGlobals(pair.sk, pose);

    WorldDelta uniform;
    uniform.scale = glm::vec3(1.5f);
    const Transform u = applyWorldDelta(pair.sk, g, pose[pair.c], pair.c, uniform);
    CHECK(near(u.s, {3, 4.5f, 6}));
    CHECK(near(u.t, pose[pair.c].t));  // la position ne bouge pas

    // Facteur sur l'axe X du repère LOCAL de l'os : seule l'échelle locale X change.
    WorldDelta local;
    local.scale = {2, 1, 1};
    local.scaleBasis = spaceOrientation(pair.sk, g, pair.c, GizmoSpace::Local);
    CHECK(near(applyWorldDelta(pair.sk, g, pose[pair.c], pair.c, local).s, {4, 3, 4}));

    // Même facteur sur l'axe X MONDE : le parent tourne de 90° autour de Z, donc l'axe X monde est l'axe Y
    // local.
    WorldDelta global;
    global.scale = {2, 1, 1};
    CHECK(near(applyWorldDelta(pair.sk, g, pose[pair.c], pair.c, global).s, {2, 6, 4}));
}

TEST_CASE("repères d'orientation : local, parent, global") {
    Pair pair;
    const auto g = computeGlobals(pair.sk, pair.sk.bindPose());
    CHECK(sameRot(spaceOrientation(pair.sk, g, pair.c, GizmoSpace::Global), glm::quat(1, 0, 0, 0)));
    const glm::quat parentRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    CHECK(sameRot(spaceOrientation(pair.sk, g, pair.p, GizmoSpace::Local), parentRot));
    CHECK(sameRot(spaceOrientation(pair.sk, g, pair.c, GizmoSpace::Parent), parentRot));
    CHECK(
        sameRot(spaceOrientation(pair.sk, g, pair.p, GizmoSpace::Parent), glm::quat(1, 0, 0, 0)));  // racine
}

TEST_CASE("sélection : pivot moyen et os de plus haut niveau") {
    Pair pair;
    const auto g = computeGlobals(pair.sk, pair.sk.bindPose());
    CHECK(near(selectionPivot(g, {pair.p, pair.c}),
               (glm::vec3(g[pair.p][3]) + glm::vec3(g[pair.c][3])) * 0.5f));
    CHECK(near(selectionPivot(g, {}), {0, 0, 0}));

    CHECK(topLevelBones(pair.sk, {pair.p, pair.c}) == std::vector<BoneId>{pair.p});  // C suit P
    CHECK(topLevelBones(pair.sk, {pair.c}) == std::vector<BoneId>{pair.c});
    CHECK(topLevelBones(pair.sk, {}).empty());
}

TEST_CASE("translation aléatoire : résultat absolu, indépendant de l'ordre des mises à jour") {
    Rng rng(99);
    Pair pair;
    const Pose start = pair.sk.bindPose();
    const auto g = computeGlobals(pair.sk, start);
    for (int i = 0; i < 200; ++i) {
        WorldDelta d;
        d.translation = rng.vec(3.0f);
        d.rotation = glm::angleAxis(rng.uniform(-3.0f, 3.0f), rng.unit());
        // Appliquer deux fois le même delta depuis l'état de départ donne le même résultat (pas de dérive).
        const Transform a = applyWorldDelta(pair.sk, g, start[pair.c], pair.c, d);
        const Transform b = applyWorldDelta(pair.sk, g, start[pair.c], pair.c, d);
        CHECK(near(a.t, b.t));
        CHECK(sameRot(a.r, b.r));
        Pose moved = start;
        moved[pair.c] = a;
        const auto g2 = computeGlobals(pair.sk, moved);
        CHECK(near(glm::vec3(g2[pair.c][3]), glm::vec3(g[pair.c][3]) + d.translation, 1e-3f));
        CHECK(
            sameRot(Transform::fromMat4(g2[pair.c]).r, d.rotation * Transform::fromMat4(g[pair.c]).r, 1e-4f));
    }
}
