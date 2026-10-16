// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>

#include "core/character/edit.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skeleton/symmetry.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;
using testutil::sameRot;

TEST_CASE("mirrorName : .L/.R, _L/_R, minuscules, suffixes numériques, os centraux") {
    CHECK(*mirrorName("arm.L") == "arm.R");
    CHECK(*mirrorName("arm.R") == "arm.L");
    CHECK(*mirrorName("leg_L_2") == "leg_R_2");
    CHECK(*mirrorName("leg_joint_R_1") == "leg_joint_L_1");
    CHECK(*mirrorName("hand_l.001") == "hand_r.001");
    CHECK(*mirrorName("arm.L.002") == "arm.R.002");
    CHECK(*mirrorName("a_L_b_R") == "a_L_b_L");  // dernier jeton
    CHECK_FALSE(mirrorName("spine").has_value());
    CHECK_FALSE(mirrorName("Left").has_value());       // pas de séparateur : pas un suffixe
    CHECK_FALSE(mirrorName("shoulderL").has_value());  // « L » collé au mot
    CHECK_FALSE(mirrorName("torso_joint_3").has_value());
    CHECK_FALSE(mirrorName("").has_value());
    CHECK(*mirrorName(*mirrorName("foot.L")) == "foot.L");  // involution
}

TEST_CASE("findMirrorBone : retrouve le symétrique, kNoBone sinon") {
    Skeleton sk;
    const BoneId root = sk.addBone("root", kNoBone).value();
    const BoneId l = sk.addBone("arm.L", root).value();
    const BoneId r = sk.addBone("arm.R", root).value();
    const BoneId lonely = sk.addBone("leg.L", root).value();
    CHECK(findMirrorBone(sk, l) == r);
    CHECK(findMirrorBone(sk, r) == l);
    CHECK(findMirrorBone(sk, root) == kNoBone);
    CHECK(findMirrorBone(sk, lonely) == kNoBone);  // pas de leg.R
    CHECK(findMirrorBone(sk, 99) == kNoBone);
}

TEST_CASE("miroir monde : S W S reste une rotation directe et inverse x") {
    Transform t;
    t.t = {1, 2, 3};
    t.r = glm::angleAxis(glm::radians(40.0f), glm::normalize(glm::vec3(1, 2, 3)));
    const glm::mat4 world = t.toMat4();
    const glm::mat4 mirrored = mirrorWorldX(world);
    CHECK(near(glm::vec3(mirrored[3]), {-1, 2, 3}));
    CHECK(glm::determinant(glm::mat3(mirrored)) == doctest::Approx(1.0f));  // pas de miroir résiduel
    CHECK(near(mirrorWorldX(mirrored), world));                             // involution
}

TEST_CASE("mirroredLocal : le symétrique prend la transformation miroir malgré un parent différent") {
    Skeleton sk;
    const BoneId root = sk.addBone("root", kNoBone).value();
    Transform left;
    left.t = {1, 1, 0};
    Transform right;
    right.t = {-1, 1, 0};
    const BoneId l = sk.addBone("arm.L", root, left).value();
    const BoneId r = sk.addBone("arm.R", root, right).value();

    Pose pose = sk.bindPose();
    pose[l].r = glm::angleAxis(glm::radians(50.0f), glm::vec3(0, 0, 1));  // le bras gauche tourne autour de Z
    const auto g = computeGlobals(sk, pose);
    pose[r] = mirroredLocal(sk, g, r, g[l]);
    const auto g2 = computeGlobals(sk, pose);
    CHECK(near(g2[r], mirrorWorldX(g2[l]), 1e-4f));
    // Rotation autour de Z : miroir en X => angle opposé.
    CHECK(sameRot(pose[r].r, glm::angleAxis(glm::radians(-50.0f), glm::vec3(0, 0, 1))));
}

namespace {
// root -> A -> B -> C, deux maillages simples skinnés sur B et C.
Character makeCharacter() {
    Character c;
    Transform up;
    up.t = {0, 1, 0};
    const BoneId root = c.skeleton.addBone("root", kNoBone).value();
    const BoneId a = c.skeleton.addBone("A", root, up).value();
    const BoneId b = c.skeleton.addBone("B", a, up).value();
    c.skeleton.addBone("C", b, up);
    Mesh mesh;
    mesh.vertices.resize(3);
    mesh.skinWeights.resize(3);
    mesh.skinWeights[0].joints = {b, kNoBone, kNoBone, kNoBone};
    mesh.skinWeights[0].weights = {1, 0, 0, 0};
    mesh.skinWeights[1].joints = {a, b, kNoBone, kNoBone};
    mesh.skinWeights[1].weights = {0.25f, 0.75f, 0, 0};
    mesh.skinWeights[2].joints = {c.skeleton.find("C"), kNoBone, kNoBone, kNoBone};
    mesh.skinWeights[2].weights = {1, 0, 0, 0};
    c.meshes.push_back(mesh);
    AnimationClip clip;
    clip.tracks[b];
    clip.tracks[a];
    c.clips.push_back(clip);
    return c;
}
}  // namespace

TEST_CASE("removeBoneClean : les poids passent au parent, fusionnés et normalisés") {
    Character c = makeCharacter();
    const BoneId a = c.skeleton.find("A");
    const BoneId b = c.skeleton.find("B");
    REQUIRE(removeBoneClean(c, b, RemoveMode::ReparentChildren).ok());
    CHECK_FALSE(c.skeleton.contains(b));
    CHECK(c.skeleton.bone(c.skeleton.find("C")).parent == a);  // enfant rattaché au grand-parent
    // Sommet 0 : 100 % B -> 100 % A. Sommet 1 : 25 % A + 75 % B -> 100 % A (doublon fusionné).
    const auto& w = c.meshes[0].skinWeights;
    CHECK(w[0].joints[0] == a);
    CHECK(w[0].weights[0] == doctest::Approx(1.0f));
    CHECK(w[1].joints[0] == a);
    CHECK(w[1].weights[0] == doctest::Approx(1.0f));
    CHECK(w[1].joints[1] == kNoBone);
    CHECK(c.meshes[0].validate().ok());
    // Les pistes d'animation de l'os supprimé disparaissent, les autres restent.
    CHECK(c.clips[0].tracks.count(b) == 0);
    CHECK(c.clips[0].tracks.count(a) == 1);
}

TEST_CASE("removeBoneClean avec descendants : toute la branche part, le skinning reste valide") {
    Character c = makeCharacter();
    const BoneId a = c.skeleton.find("A");
    const BoneId root = c.skeleton.find("root");
    REQUIRE(removeBoneClean(c, a, RemoveMode::WithDescendants).ok());
    CHECK(c.skeleton.boneCount() == 1);
    CHECK(c.meshes[0].validate().ok());
    for (const SkinWeight& sk : c.meshes[0].skinWeights) {
        CHECK(sk.joints[0] == root);  // tout est replié sur le seul os restant
        CHECK(sk.weights[0] == doctest::Approx(1.0f));
    }
    CHECK(c.clips[0].tracks.empty());
}

TEST_CASE("removeBoneClean sur une racine sans ancêtre : poids reportés sur un os conservé") {
    Character c = makeCharacter();
    const BoneId root = c.skeleton.find("root");
    REQUIRE(removeBoneClean(c, root, RemoveMode::ReparentChildren).ok());
    CHECK(c.meshes[0].validate().ok());
    CHECK_FALSE(removeBoneClean(c, 99, RemoveMode::ReparentChildren).ok());
}

TEST_CASE("subdivideBone : nouvel os au milieu, enfants rattachés, positions monde conservées") {
    Character c = makeCharacter();
    const BoneId a = c.skeleton.find("A");
    const BoneId b = c.skeleton.find("B");
    const auto before = c.skeleton.bindGlobals();
    const glm::vec3 bWorld(before[b][3]);
    const auto mid = subdivideBone(c, a);
    REQUIRE(mid.ok());
    const BoneId m = mid.value();
    CHECK(c.skeleton.bone(m).parent == a);
    CHECK(c.skeleton.bone(b).parent == m);
    CHECK(c.skeleton.boneCount() == 5);
    const auto& after = c.skeleton.bindGlobals();
    CHECK(near(glm::vec3(after[m][3]), (glm::vec3(before[a][3]) + bWorld) * 0.5f));  // au milieu de A et B
    CHECK(near(glm::vec3(after[b][3]), bWorld));                                     // B n'a pas bougé
    CHECK(c.skeleton.bone(m).name == "A.sub");
    // Un os feuille se subdivise le long de sa longueur.
    const BoneId leaf = c.skeleton.find("C");
    const float length = c.skeleton.bone(leaf).length;
    const auto leafMid = subdivideBone(c, leaf);
    REQUIRE(leafMid.ok());
    CHECK(c.skeleton.bone(leaf).length == doctest::Approx(length * 0.5f));
    CHECK_FALSE(subdivideBone(c, 99).ok());
}
