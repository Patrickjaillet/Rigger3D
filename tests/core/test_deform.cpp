// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>

#include "core/deform/deform.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

Transform at(const glm::vec3& t) {
    Transform tr;
    tr.t = t;
    return tr;
}

// Bras : root -> upper -> forearm -> hand ; deux os de twist enfants de l'avant-bras ; cible libre.
struct Arm {
    Skeleton sk;
    BoneId root, upper, forearm, hand, twist1, twist2, target;
    Arm() {
        root = sk.addBone("root", kNoBone, at({0, 0, 0})).value();
        upper = sk.addBone("upper", root, at({0, 1, 0})).value();
        forearm = sk.addBone("forearm", upper, at({0, 1, 0})).value();
        hand = sk.addBone("hand", forearm, at({0, 1, 0})).value();
        twist1 = sk.addBone("twist1", forearm, at({0, 0.33f, 0})).value();
        twist2 = sk.addBone("twist2", forearm, at({0, 0.66f, 0})).value();
        target = sk.addBone("target", kNoBone, at({2, 1, 0})).value();
    }
    glm::vec3 world(const Pose& p, BoneId id) const { return glm::vec3(computeGlobals(sk, p)[id][3]); }
    glm::quat worldRot(const Pose& p, BoneId id) const {
        return Transform::fromMat4(computeGlobals(sk, p)[id]).r;
    }
};

float angleOf(const glm::quat& q) {
    glm::quat n = glm::normalize(q);
    if (n.w < 0) n = -n;
    return 2.0f * std::acos(std::min(1.0f, n.w));
}

}  // namespace

TEST_CASE("twist : la torsion de la main est répartie sur les os de twist") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.hand].r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    const std::vector<TwistChain> chains = {makeTwistChain(a.hand, {a.twist1, a.twist2})};
    CHECK(chains[0].factors[0] == doctest::Approx(1.0f / 3.0f));
    CHECK(chains[0].factors[1] == doctest::Approx(2.0f / 3.0f));
    applyTwistChains(a.sk, pose, chains);
    CHECK(angleOf(pose[a.twist1].r) == doctest::Approx(glm::half_pi<float>() / 3.0f).epsilon(1e-4));
    CHECK(angleOf(pose[a.twist2].r) == doctest::Approx(glm::half_pi<float>() * 2.0f / 3.0f).epsilon(1e-4));
    CHECK(angleOf(pose[a.hand].r) ==
          doctest::Approx(glm::half_pi<float>()).epsilon(1e-4));  // la source garde tout
}

TEST_CASE("twist : une flexion (pas une torsion) n'active pas les os de twist ; désactivable") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.hand].r = glm::angleAxis(0.8f, glm::vec3(1, 0, 0));  // flexion du poignet
    auto chains = std::vector<TwistChain>{makeTwistChain(a.hand, {a.twist1, a.twist2})};
    applyTwistChains(a.sk, pose, chains);
    CHECK(angleOf(pose[a.twist1].r) < 1e-4f);
    CHECK(angleOf(pose[a.twist2].r) < 1e-4f);
    pose[a.hand].r = glm::angleAxis(1.0f, glm::vec3(0, 1, 0));
    chains[0].enabled = false;
    applyTwistChains(a.sk, pose, chains);
    CHECK(angleOf(pose[a.twist1].r) < 1e-4f);
}

TEST_CASE("contraintes : Copy Transform (position, rotation, influence)") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.target].t = {2, 3, 0};
    pose[a.target].r = glm::angleAxis(0.7f, glm::vec3(0, 0, 1));
    Constraint c;
    c.kind = ConstraintKind::CopyTransform;
    c.bone = a.hand;
    c.target = a.target;
    c.copyRotation = false;
    Pose copy = pose;
    applyConstraints(a.sk, copy, {c});
    CHECK(near(a.world(copy, a.hand), {2, 3, 0}, 1e-4f));
    c.influence = 0.5f;
    copy = pose;
    const glm::vec3 before = a.world(pose, a.hand);
    applyConstraints(a.sk, copy, {c});
    CHECK(near(a.world(copy, a.hand), (before + glm::vec3(2, 3, 0)) * 0.5f, 1e-3f));
    c.influence = 1.0f;
    c.copyLocation = false;
    c.copyRotation = true;
    copy = pose;
    applyConstraints(a.sk, copy, {c});
    CHECK(testutil::sameRot(a.worldRot(copy, a.hand), a.worldRot(pose, a.target), 1e-4f));
    c.enabled = false;
    copy = pose;
    applyConstraints(a.sk, copy, {c});
    CHECK(testutil::sameRot(a.worldRot(copy, a.hand), a.worldRot(pose, a.hand), 1e-6f));
}

TEST_CASE("contraintes : Limit (rotation, position, échelle)") {
    Arm a;
    Pose pose = a.sk.bindPose();
    Constraint c;
    c.kind = ConstraintKind::Limit;
    c.bone = a.forearm;
    c.rotationMin = {-0.2f, -0.2f, -0.2f};
    c.rotationMax = {0.2f, 0.2f, 0.2f};
    pose[a.forearm].r = fromEulerXYZ({1.0f, -1.0f, 0.1f});
    applyConstraints(a.sk, pose, {c});
    const glm::vec3 e = toEulerXYZ(pose[a.forearm].r);
    CHECK(e.x == doctest::Approx(0.2f).epsilon(1e-3));
    CHECK(e.y == doctest::Approx(-0.2f).epsilon(1e-3));
    CHECK(e.z == doctest::Approx(0.1f).epsilon(1e-3));
    Constraint loc;
    loc.kind = ConstraintKind::Limit;
    loc.bone = a.forearm;
    loc.limitRotation = false;
    loc.limitLocation = true;
    loc.locationMin = {-0.5f, 0.0f, -0.5f};
    loc.locationMax = {0.5f, 2.0f, 0.5f};
    pose[a.forearm].t = {3, 5, -4};
    applyConstraints(a.sk, pose, {loc});
    CHECK(near(pose[a.forearm].t, {0.5f, 2.0f, -0.5f}));
    Constraint scl;
    scl.kind = ConstraintKind::Limit;
    scl.bone = a.forearm;
    scl.limitRotation = false;
    scl.limitScale = true;
    scl.scaleMin = glm::vec3(0.5f);
    scl.scaleMax = glm::vec3(1.5f);
    pose[a.forearm].s = {3, 0.1f, 1};
    applyConstraints(a.sk, pose, {scl});
    CHECK(near(pose[a.forearm].s, {1.5f, 0.5f, 1.0f}));
}

TEST_CASE("contraintes : Track To vise la cible") {
    Arm a;
    Pose pose = a.sk.bindPose();
    pose[a.target].t = {3, 2, 1};
    Constraint c;
    c.kind = ConstraintKind::TrackTo;
    c.bone = a.upper;
    c.target = a.target;
    applyConstraints(a.sk, pose, {c});
    const glm::vec3 axis = a.worldRot(pose, a.upper) * glm::vec3(0, 1, 0);
    const glm::vec3 want = glm::normalize(glm::vec3(3, 2, 1) - a.world(pose, a.upper));
    CHECK(near(axis, want, 1e-4f));
}

TEST_CASE("contraintes : Child Of suit la cible (translation et rotation)") {
    Arm a;
    Pose pose = a.sk.bindPose();
    Constraint c = makeChildOf(a.sk, pose, a.hand, a.target);
    Pose rest = pose;
    applyConstraints(a.sk, rest, {c});
    CHECK(near(a.world(rest, a.hand), a.world(pose, a.hand), 1e-4f));  // à la création : aucun mouvement
    Pose moved = pose;
    moved[a.target].t += glm::vec3(0.5f, 0.0f, 1.0f);
    applyConstraints(a.sk, moved, {c});
    CHECK(near(a.world(moved, a.hand), a.world(pose, a.hand) + glm::vec3(0.5f, 0.0f, 1.0f), 1e-4f));
    Pose turned = pose;
    turned[a.target].r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    applyConstraints(a.sk, turned, {c});
    const glm::vec3 pivot = a.world(pose, a.target);
    const glm::vec3 expected =
        pivot + glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1)) * (a.world(pose, a.hand) - pivot);
    CHECK(near(a.world(turned, a.hand), expected, 1e-4f));
    c.influence = 0.0f;
    Pose none = moved;
    applyConstraints(a.sk, none, {c});
    CHECK(near(a.world(none, a.hand), a.world(moved, a.hand),
               1e-5f));  // influence 0 : la pose n'est pas touchée
}

TEST_CASE("spring bones : longueurs conservées, sans NaN, déterministe") {
    Arm a;
    DeformRig rig;
    SpringChain chain;
    chain.bones = {a.upper, a.forearm, a.hand};
    chain.stiffness = 0.0f;
    chain.damping = 0.05f;
    rig.springs = {chain};
    const auto run = [&] {
        Pose pose = a.sk.bindPose();
        SpringState state;
        for (int i = 0; i < 90; ++i) {
            Pose frame = pose;
            frame[a.root].t =
                glm::vec3(0.3f * std::sin(0.2f * static_cast<float>(i)), 0.0f, 0.0f);  // la racine bouge
            stepSprings(a.sk, frame, rig, state, 1.0 / 60.0);
            for (const Transform& t : frame) REQUIRE(std::isfinite(t.r.w + t.r.x + t.r.y + t.r.z));
            const auto g = computeGlobals(a.sk, frame);
            CHECK(glm::length(glm::vec3(g[a.forearm][3]) - glm::vec3(g[a.upper][3])) ==
                  doctest::Approx(1.0f).epsilon(1e-3));
            CHECK(glm::length(glm::vec3(g[a.hand][3]) - glm::vec3(g[a.forearm][3])) ==
                  doctest::Approx(1.0f).epsilon(1e-3));
            if (i == 89) return frame;
        }
        return pose;
    };
    const Pose first = run(), second = run();
    for (std::size_t i = 0; i < first.size(); ++i) CHECK(testutil::sameRot(first[i].r, second[i].r, 1e-7f));
}

TEST_CASE("spring bones : gravité sans raideur fait tomber la chaîne, raideur max suit l'animation") {
    Arm a;
    DeformRig rig;
    SpringChain chain;
    chain.bones = {a.upper, a.forearm, a.hand};
    chain.gravity = 1.0f;
    chain.stiffness = 0.0f;
    chain.damping = 0.2f;
    rig.springs = {chain};
    const glm::vec3 restHand = a.world(a.sk.bindPose(), a.hand);

    // Chaîne légèrement inclinée (un pendule exactement à la verticale reste en équilibre instable).
    const auto tilted = [&] {
        Pose p = a.sk.bindPose();
        p[a.upper].r = glm::angleAxis(0.3f, glm::vec3(0, 0, 1));
        return p;
    };
    const glm::vec3 tiltedHand = a.world(tilted(), a.hand);
    SpringState free;
    Pose pose = tilted();
    for (int i = 0; i < 300; ++i) {
        pose = tilted();
        stepSprings(a.sk, pose, rig, free, 1.0 / 60.0);
    }
    CHECK(a.world(pose, a.hand).y < tiltedHand.y - 0.5f);  // la chaîne retombe vers le bas

    rig.springs[0].stiffness = 0.98f;
    rig.springs[0].damping = 0.5f;
    SpringState stiff;
    for (int i = 0; i < 200; ++i) {
        pose = a.sk.bindPose();
        stepSprings(a.sk, pose, rig, stiff, 1.0 / 60.0);
    }
    CHECK(near(a.world(pose, a.hand), restHand, 0.15f));

    rig.springsEnabled = false;  // désactivable : la pose reste celle de l'animation
    SpringState off;
    pose = a.sk.bindPose();
    stepSprings(a.sk, pose, rig, off, 1.0 / 60.0);
    CHECK(near(a.world(pose, a.hand), restHand, 1e-5f));
}

TEST_CASE("spring bones : inertie (la pointe suit en retard) et collision avec une sphère") {
    Arm a;
    DeformRig rig;
    SpringChain chain;
    chain.bones = {a.upper, a.forearm, a.hand};
    chain.stiffness = 0.3f;
    chain.gravity = 0.0f;
    rig.springs = {chain};
    SpringState state;
    Pose pose = a.sk.bindPose();
    stepSprings(a.sk, pose, rig, state, 1.0 / 60.0);  // initialisation à la pose animée
    pose = a.sk.bindPose();
    pose[a.root].t = {1.5f, 0, 0};  // saut brusque de la racine
    stepSprings(a.sk, pose, rig, state, 1.0 / 60.0);
    const glm::vec3 animatedTip = glm::vec3(1.5f, 4.0f, 0.0f) * 1.0f;
    CHECK(a.world(pose, a.hand).x < 1.5f);  // la main n'a pas encore rattrapé l'animation
    (void)animatedTip;
    for (int i = 0; i < 300; ++i) {
        pose = a.sk.bindPose();
        pose[a.root].t = {1.5f, 0, 0};
        stepSprings(a.sk, pose, rig, state, 1.0 / 60.0);
    }
    CHECK(near(a.world(pose, a.hand), glm::vec3(1.5f, 3.0f, 0.0f), 0.1f));  // elle a fini par rattraper

    // Collider : une sphère posée sur le chemin de la pointe.
    DeformRig withCollider = rig;
    withCollider.springs[0].gravity = 1.0f;
    withCollider.springs[0].stiffness = 0.0f;
    SpringCollider ball;
    ball.offset = {0, 1.0f, 0};  // sphère fixe dans le monde, centre sous la pointe haute
    ball.radius = 0.6f;
    withCollider.colliders = {ball};
    SpringState s2;
    Pose p2;
    for (int i = 0; i < 240; ++i) {
        p2 = a.sk.bindPose();
        stepSprings(a.sk, p2, withCollider, s2, 1.0 / 60.0);
        for (const BoneId b : {a.forearm, a.hand}) {
            CHECK(glm::length(a.world(p2, b) - glm::vec3(0, 1, 0)) >= 0.6f - 0.05f);
        }
    }
}

TEST_CASE("deform : nettoyage des références à des os disparus") {
    Arm a;
    DeformRig rig;
    rig.twists = {makeTwistChain(a.hand, {a.twist1})};
    Constraint c;
    c.bone = a.hand;
    c.target = a.target;
    rig.constraints = {c};
    SpringChain s;
    s.bones = {a.upper, a.forearm};
    rig.springs = {s, SpringChain{"bad", {a.upper, a.hand}}};  // hand n'est pas l'enfant de upper
    rig.colliders = {SpringCollider{a.hand, {}, 0.1f}};
    CHECK(pruneDeformRig(a.sk, rig));
    CHECK(rig.springs.size() == 1);
    CHECK(rig.twists.size() == 1);
    REQUIRE(a.sk.removeBone(a.target, RemoveMode::WithDescendants).ok());
    CHECK(pruneDeformRig(a.sk, rig));
    CHECK(rig.constraints.empty());
    CHECK_FALSE(pruneDeformRig(a.sk, rig));
}

#include "core/deform/autorig.hpp"
#include "core/skinning/paint.hpp"

TEST_CASE("auto-rig : repères devinés, validation, squelette humanoïde cohérent") {
    // Mesh humanoïde minimal : boîte de 1,8 m de haut, envergure 1,8 m (T-pose).
    std::vector<glm::vec3> points = {{-0.9f, 0, -0.1f}, {0.9f, 1.8f, 0.1f}, {0, 0.9f, 0}};
    const Landmarks l = guessLandmarks(points);
    CHECK(validateLandmarks(l).empty());
    CHECK(l[static_cast<int>(Landmark::HeadTop)].y == doctest::Approx(1.8f));
    CHECK(l[static_cast<int>(Landmark::ShoulderL)].x > 0.0f);
    CHECK(l[static_cast<int>(Landmark::ShoulderR)].x < 0.0f);
    CHECK(l[static_cast<int>(Landmark::HandTipL)].x ==
          doctest::Approx(0.9f).epsilon(1e-3));  // bras en T jusqu'au bord

    HumanoidBones ids;
    const Skeleton sk = buildHumanoidSkeleton(l, &ids);
    CHECK(sk.boneCount() == 19);
    CHECK(sk.bone(ids.head).parent == ids.neck);
    CHECK(sk.bone(ids.upperArm[0]).parent == ids.chest);
    CHECK(sk.bone(ids.toe[1]).parent == ids.foot[1]);
    // Les positions monde de repos retrouvent exactement les repères.
    const auto& g = sk.bindGlobals();
    CHECK(near(glm::vec3(g[ids.foreArm[0]][3]), l[static_cast<int>(Landmark::ElbowL)], 1e-5f));
    CHECK(near(glm::vec3(g[ids.shin[1]][3]), l[static_cast<int>(Landmark::KneeR)], 1e-5f));
    CHECK(sk.find("hand.L") == ids.hand[0]);

    // Bras le long du corps si le mesh est étroit.
    const Landmarks a = guessLandmarks({{-0.3f, 0, -0.1f}, {0.3f, 1.8f, 0.1f}});
    CHECK(a[static_cast<int>(Landmark::WristL)].y < a[static_cast<int>(Landmark::ElbowL)].y);

    // Validation : tête sous le cou, côtés inversés.
    Landmarks bad = l;
    bad[static_cast<int>(Landmark::HeadTop)].y = 0.1f;
    CHECK_FALSE(validateLandmarks(bad).empty());
    bad = l;
    std::swap(bad[static_cast<int>(Landmark::ShoulderL)], bad[static_cast<int>(Landmark::ShoulderR)]);
    CHECK_FALSE(validateLandmarks(bad).empty());
    CHECK(std::string(landmarkName(Landmark::KneeR)) == "Knee R");
}

TEST_CASE(
    "auto-rig : le squelette généré peut être skinné automatiquement (poids valides, bon os dominant)") {
    const Landmarks l = guessLandmarks({{-0.9f, 0, -0.1f}, {0.9f, 1.8f, 0.1f}});
    HumanoidBones ids;
    Mesh mesh;
    // Nuage de sommets : une colonne le long du corps et un bras.
    for (float y = 0.0f; y <= 1.8f; y += 0.1f) {
        Vertex v;
        v.position = {0.0f, y, 0.0f};
        mesh.vertices.push_back(v);
    }
    for (float x = 0.2f; x <= 0.9f; x += 0.1f) {
        Vertex v;
        v.position = {x, 1.44f, 0.0f};
        mesh.vertices.push_back(v);
    }
    const Skeleton sk = buildHumanoidSkeleton(l, &ids);
    for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
        mesh.indices.insert(mesh.indices.end(),
                            {static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1),
                             static_cast<std::uint32_t>(i + 2)});
    autoSkin(mesh, sk);
    REQUIRE(mesh.skinWeights.size() == mesh.vertices.size());
    for (const SkinWeight& w : mesh.skinWeights) {
        float sum = 0;
        for (const float x : w.weights) sum += x;
        CHECK(sum == doctest::Approx(1.0f));
    }
    // Un sommet du bout du bras gauche est dominé par l'avant-bras ou la main gauche.
    const SkinWeight& tip = mesh.skinWeights.back();
    const BoneId dominant = tip.joints[0];
    CHECK((dominant == ids.hand[0] || dominant == ids.foreArm[0]));
    // Un sommet de la tête est dominé par la tête ou le cou.
    const SkinWeight& head = mesh.skinWeights[17];
    CHECK((head.joints[0] == ids.head || head.joints[0] == ids.neck));
}
