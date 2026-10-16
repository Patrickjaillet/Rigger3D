// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>
#include <functional>
#include <set>

#include "core/skeleton/fk.hpp"
#include "core/skeleton/skeleton.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::Rng;

namespace {
Transform at(glm::vec3 t) {
    Transform x;
    x.t = t;
    return x;
}

// Bras : épaule -> coude -> poignet -> main, os de 1 m le long de X
struct Arm {
    Skeleton sk;
    BoneId shoulder, elbow, wrist, hand;
    Arm() {
        shoulder = sk.addBone("shoulder", kNoBone, at({0, 0, 0})).value();
        elbow = sk.addBone("elbow", shoulder, at({1, 0, 0})).value();
        wrist = sk.addBone("wrist", elbow, at({1, 0, 0})).value();
        hand = sk.addBone("hand", wrist, at({1, 0, 0})).value();
    }
};

// Vérifie tous les invariants documentés du squelette.
void checkInvariants(const Skeleton& sk) {
    const auto& order = sk.order();
    REQUIRE(order.size() == sk.boneCount());
    std::set<BoneId> seen;
    std::set<std::string> names;
    for (BoneId id : order) {
        REQUIRE(sk.contains(id));
        const Bone& b = sk.bone(id);
        CHECK(!b.name.empty());
        CHECK(names.insert(b.name).second);                         // noms uniques
        CHECK(sk.find(b.name) == id);                               // index de noms cohérent
        if (b.parent != kNoBone) CHECK(seen.count(b.parent) == 1);  // parent AVANT enfant
        CHECK(seen.insert(id).second);                              // pas de doublon
    }
}
}  // namespace

TEST_CASE("squelette vide") {
    Skeleton sk;
    CHECK(sk.boneCount() == 0);
    CHECK(sk.order().empty());
    CHECK(sk.rootBones().empty());
    CHECK(sk.find("x") == kNoBone);
    CHECK_FALSE(sk.contains(0));
}

TEST_CASE("addBone : ids stables et ordre topologique") {
    Arm a;
    CHECK(a.shoulder == 0);
    CHECK(a.hand == 3);
    CHECK(a.sk.boneCount() == 4);
    CHECK(a.sk.order() == std::vector<BoneId>{0, 1, 2, 3});
    CHECK(a.sk.bone(a.elbow).parent == a.shoulder);
    checkInvariants(a.sk);
}

TEST_CASE("addBone : erreurs") {
    Skeleton sk;
    CHECK_FALSE(sk.addBone("", kNoBone).ok());
    CHECK_FALSE(sk.addBone("x", 42).ok());
    CHECK(sk.boneCount() == 0);
}

TEST_CASE("noms uniques à l'ajout") {
    Skeleton sk;
    BoneId a = sk.addBone("Bone", kNoBone).value();
    BoneId b = sk.addBone("Bone", kNoBone).value();
    BoneId c = sk.addBone("Bone", kNoBone).value();
    CHECK(sk.bone(a).name == "Bone");
    CHECK(sk.bone(b).name == "Bone.001");
    CHECK(sk.bone(c).name == "Bone.002");
    checkInvariants(sk);
}

TEST_CASE("find, children, rootBones") {
    Arm a;
    BoneId other = a.sk.addBone("other_root", kNoBone).value();
    CHECK(a.sk.find("wrist") == a.wrist);
    CHECK(a.sk.find("nope") == kNoBone);
    CHECK(a.sk.children(a.elbow) == std::vector<BoneId>{a.wrist});
    CHECK(a.sk.children(a.hand).empty());
    CHECK(a.sk.rootBones() == std::vector<BoneId>{a.shoulder, other});
}

TEST_CASE("isDescendantOf") {
    Arm a;
    CHECK(a.sk.isDescendantOf(a.hand, a.shoulder));
    CHECK(a.sk.isDescendantOf(a.hand, a.elbow));
    CHECK_FALSE(a.sk.isDescendantOf(a.shoulder, a.hand));
    CHECK_FALSE(a.sk.isDescendantOf(a.elbow, a.elbow));
}

TEST_CASE("reparent refuse les cycles") {
    Arm a;
    CHECK_FALSE(a.sk.reparent(a.shoulder, a.hand).ok());  // sous un descendant
    CHECK_FALSE(a.sk.reparent(a.elbow, a.elbow).ok());    // sous soi-même
    CHECK_FALSE(a.sk.reparent(a.elbow, 99).ok());         // parent inexistant
    CHECK(a.sk.bone(a.shoulder).parent == kNoBone);       // rien n'a changé
    checkInvariants(a.sk);
}

TEST_CASE("reparent : ordre topologique maintenu quand le parent a un id plus grand") {
    Skeleton sk;
    BoneId b0 = sk.addBone("b0", kNoBone).value();
    BoneId b1 = sk.addBone("b1", kNoBone).value();
    BoneId b2 = sk.addBone("b2", kNoBone).value();
    REQUIRE(sk.reparent(b1, b2).ok());  // b1 (id 1) devient enfant de b2 (id 2)
    checkInvariants(sk);
    auto pos = [&](BoneId id) {
        return std::find(sk.order().begin(), sk.order().end(), id) - sk.order().begin();
    };
    CHECK(pos(b2) < pos(b1));
    (void)b0;
}

TEST_CASE("reparent sans keepWorld conserve bindLocal ; avec keepWorld conserve la position monde") {
    Skeleton sk;
    BoneId p1 = sk.addBone("p1", kNoBone, at({10, 0, 0})).value();
    BoneId p2 = sk.addBone("p2", kNoBone, at({0, 5, 0})).value();
    BoneId c = sk.addBone("c", p1, at({1, 1, 1})).value();
    glm::vec3 worldBefore = glm::vec3(sk.bindGlobals()[c][3]);
    CHECK_VEC3(worldBefore, glm::vec3(11, 1, 1));

    REQUIRE(sk.reparent(c, p2, false).ok());
    CHECK_VEC3(sk.bone(c).bindLocal.t, glm::vec3(1, 1, 1));
    CHECK_VEC3(glm::vec3(sk.bindGlobals()[c][3]), glm::vec3(1, 6, 1));

    REQUIRE(sk.reparent(c, p1, false).ok());
    REQUIRE(sk.reparent(c, p2, true).ok());
    CHECK_VEC3(glm::vec3(sk.bindGlobals()[c][3]), worldBefore);
}

TEST_CASE("removeBone avec descendants : ids des autres os inchangés, ids jamais réutilisés") {
    Arm a;
    BoneId side = a.sk.addBone("side", a.shoulder).value();
    REQUIRE(a.sk.removeBone(a.elbow, RemoveMode::WithDescendants).ok());
    CHECK(a.sk.boneCount() == 2);
    CHECK_FALSE(a.sk.contains(a.elbow));
    CHECK_FALSE(a.sk.contains(a.wrist));
    CHECK_FALSE(a.sk.contains(a.hand));
    CHECK(a.sk.contains(side));
    CHECK(a.sk.bone(side).name == "side");
    CHECK(a.sk.find("elbow") == kNoBone);
    checkInvariants(a.sk);

    BoneId fresh = a.sk.addBone("fresh", kNoBone).value();
    CHECK(fresh > side);  // jamais de réutilisation d'id
}

TEST_CASE("removeBone en re-parentant les enfants conserve leur position monde") {
    Skeleton sk;
    BoneId a = sk.addBone("a", kNoBone, at({1, 0, 0})).value();
    Transform mid;
    mid.t = {0, 2, 0};
    mid.r = glm::angleAxis(0.9f, glm::vec3(0, 0, 1));
    BoneId b = sk.addBone("b", a, mid).value();
    BoneId c = sk.addBone("c", b, at({3, 0, 0})).value();
    glm::mat4 worldC = sk.bindGlobals()[c];

    REQUIRE(sk.removeBone(b, RemoveMode::ReparentChildren).ok());
    CHECK(sk.bone(c).parent == a);
    CHECK(testutil::near(sk.bindGlobals()[c], worldC, 1e-3f));
    checkInvariants(sk);
}

TEST_CASE("removeBone : erreur sur id inconnu") {
    Skeleton sk;
    CHECK_FALSE(sk.removeBone(7).ok());
}

TEST_CASE("rename : unicité, nom vide, même nom") {
    Arm a;
    REQUIRE(a.sk.rename(a.hand, "wrist").ok());
    CHECK(a.sk.bone(a.hand).name == "wrist.001");
    CHECK(a.sk.find("wrist") == a.wrist);
    REQUIRE(a.sk.rename(a.hand, "wrist.001").ok());  // son propre nom : inchangé
    CHECK(a.sk.bone(a.hand).name == "wrist.001");
    CHECK_FALSE(a.sk.rename(a.hand, "").ok());
    CHECK_FALSE(a.sk.rename(99, "x").ok());
    checkInvariants(a.sk);
}

TEST_CASE("restoreBone : annulation d'une suppression avec l'id d'origine") {
    Arm a;
    Bone saved = a.sk.bone(a.hand);
    REQUIRE(a.sk.removeBone(a.hand).ok());
    REQUIRE(a.sk.restoreBone(saved).ok());
    CHECK(a.sk.contains(a.hand));
    CHECK(a.sk.bone(a.hand).name == "hand");
    CHECK(a.sk.bone(a.hand).parent == a.wrist);
    checkInvariants(a.sk);
}

TEST_CASE("restoreBone : refus (id utilisé, parent absent, nom pris)") {
    Arm a;
    Bone b = a.sk.bone(a.hand);
    CHECK_FALSE(a.sk.restoreBone(b).ok());   // id déjà vivant
    REQUIRE(a.sk.removeBone(a.wrist).ok());  // supprime wrist + hand
    CHECK_FALSE(a.sk.restoreBone(b).ok());   // parent absent
    Bone c = b;
    c.id = 50;
    c.parent = kNoBone;
    c.name = "shoulder";
    CHECK_FALSE(a.sk.restoreBone(c).ok());  // nom pris
}

TEST_CASE("flags") {
    Arm a;
    a.sk.setFlags(a.hand, BoneFlags::Hidden | BoneFlags::Locked);
    CHECK(hasFlag(a.sk.bone(a.hand).flags, BoneFlags::Hidden));
    CHECK(hasFlag(a.sk.bone(a.hand).flags, BoneFlags::Locked));
    CHECK_FALSE(hasFlag(a.sk.bone(a.elbow).flags, BoneFlags::Hidden));
}

TEST_CASE("bind : globals en chaîne et inverseBind * globals = identité") {
    Arm a;
    const auto& g = a.sk.bindGlobals();
    CHECK_VEC3(glm::vec3(g[a.hand][3]), glm::vec3(3, 0, 0));
    for (BoneId id : a.sk.order()) CHECK(testutil::near(a.sk.inverseBind()[id] * g[id], glm::mat4(1.0f)));
}

TEST_CASE("cache invalidé par les modifications ; revision incrémentée") {
    Arm a;
    auto rev = a.sk.revision();
    CHECK_VEC3(glm::vec3(a.sk.bindGlobals()[a.hand][3]), glm::vec3(3, 0, 0));
    a.sk.setBindLocal(a.elbow, at({5, 0, 0}));
    CHECK(a.sk.revision() > rev);
    CHECK_VEC3(glm::vec3(a.sk.bindGlobals()[a.hand][3]), glm::vec3(7, 0, 0));
}

TEST_CASE("FK : coude plié à 90° autour de Z -> la main monte") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.elbow].r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    auto g = computeGlobals(a.sk, pose);
    CHECK_VEC3(glm::vec3(g[a.hand][3]), glm::vec3(1, 2, 0));
    CHECK_VEC3(glm::vec3(g[a.elbow][3]), glm::vec3(1, 0, 0));
}

TEST_CASE("FK : la rotation de l'épaule entraîne toute la chaîne") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.shoulder].r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    auto g = computeGlobals(a.sk, pose);
    CHECK_VEC3(glm::vec3(g[a.hand][3]), glm::vec3(0, 3, 0));
}

TEST_CASE("FK : pose plus courte que le squelette -> bind pose pour les os manquants") {
    Arm a;
    Pose pose = a.sk.bindPose();
    BoneId extra = a.sk.addBone("finger", a.hand, at({0.5f, 0, 0})).value();
    REQUIRE(pose.size() <= extra);
    auto g = computeGlobals(a.sk, pose);
    CHECK_VEC3(glm::vec3(g[extra][3]), glm::vec3(3.5f, 0, 0));
}

TEST_CASE("FK : emplacements supprimés = identité") {
    Arm a;
    REQUIRE(a.sk.removeBone(a.hand).ok());
    auto g = computeGlobals(a.sk, a.sk.bindPose());
    CHECK(g.size() == a.sk.slotCount());
    CHECK(testutil::near(g[a.hand], glm::mat4(1.0f)));
}

TEST_CASE("skinning : matrices = identité au repos") {
    Arm a;
    auto skin = computeSkinMatrices(a.sk, a.sk.bindPose());
    for (BoneId id : a.sk.order()) CHECK(testutil::near(skin[id], glm::mat4(1.0f), 1e-4f));
}

TEST_CASE("skinning : un point lié à l'avant-bras suit sa rotation") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.elbow].r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    auto skin = computeSkinMatrices(a.sk, pose);
    // sommet au repos en (2, 0, 0) (au poignet), lié à 100% au coude : tourne de 90° autour du coude (1,0,0)
    glm::vec3 v = glm::vec3(skin[a.elbow] * glm::vec4(2, 0, 0, 1));
    CHECK_VEC3(v, glm::vec3(1, 1, 0));
}

TEST_CASE("arbre aléatoire : invariants et FK cohérents après 400 opérations") {
    Rng rng(2024);
    Skeleton sk;
    auto aliveIds = [&] { return std::vector<BoneId>(sk.order().begin(), sk.order().end()); };

    for (int step = 0; step < 400; ++step) {
        auto ids = aliveIds();
        int op = rng.integer(0, 99);
        if (ids.empty() || op < 50) {
            BoneId parent =
                (ids.empty() || rng.integer(0, 4) == 0) ? kNoBone : ids[rng.integer(0, (int)ids.size() - 1)];
            Transform t;
            t.t = rng.vec(2);
            t.r = rng.quat();
            REQUIRE(sk.addBone("bone", parent, t).ok());
        } else if (op < 75) {
            BoneId id = ids[rng.integer(0, (int)ids.size() - 1)];
            BoneId np = rng.integer(0, 5) == 0 ? kNoBone : ids[rng.integer(0, (int)ids.size() - 1)];
            bool wouldCycle = np == id || (np != kNoBone && sk.isDescendantOf(np, id));
            auto r = sk.reparent(id, np, rng.integer(0, 1) == 1);
            CHECK(r.ok() == !wouldCycle);
        } else if (op < 90) {
            BoneId id = ids[rng.integer(0, (int)ids.size() - 1)];
            REQUIRE(sk.removeBone(
                          id, rng.integer(0, 1) ? RemoveMode::WithDescendants : RemoveMode::ReparentChildren)
                        .ok());
        } else {
            BoneId id = ids[rng.integer(0, (int)ids.size() - 1)];
            REQUIRE(sk.rename(id, "bone").ok());
        }
        checkInvariants(sk);
    }

    // FK = produit naïf des bindLocal le long de la chaîne de parents
    std::function<glm::mat4(BoneId)> naive = [&](BoneId id) -> glm::mat4 {
        const Bone& b = sk.bone(id);
        glm::mat4 m = b.bindLocal.toMat4();
        return b.parent == kNoBone ? m : naive(b.parent) * m;
    };
    auto g = computeGlobals(sk, sk.bindPose());
    for (BoneId id : sk.order()) CHECK(testutil::near(g[id], naive(id), 1e-2f));
}

TEST_CASE("color : valeur par défaut, modification, conservée par restoreBone") {
    Skeleton sk;
    const BoneId a = sk.addBone("a", kNoBone).value();
    CHECK(sk.bone(a).color.z == doctest::Approx(1.0f));
    const std::uint64_t before = sk.revision();
    sk.setColor(a, {1.0f, 0.5f, 0.0f});
    CHECK(sk.bone(a).color.y == doctest::Approx(0.5f));
    CHECK(sk.revision() > before);

    const Bone copy = sk.bone(a);
    REQUIRE(sk.removeBone(a).ok());
    REQUIRE(sk.restoreBone(copy).ok());
    CHECK(sk.bone(a).color.x == doctest::Approx(1.0f));
}
