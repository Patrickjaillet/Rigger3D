// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>

#include "core/character/edit.hpp"
#include "core/command/commands.hpp"
#include "core/ik/ik.hpp"
#include "core/ik/rig.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

// Chaîne verticale : base (0,0,0) -> n os de longueur `len` empilés selon +Y -> effecteur au bout.
struct Rig {
    Skeleton sk;
    std::vector<BoneId> bones;
    BoneId effector = kNoBone;
    Pose pose;

    explicit Rig(int count, float len = 1.0f) {
        BoneId parent = kNoBone;
        for (int i = 0; i < count; ++i) {
            Transform bind;
            bind.t = i == 0 ? glm::vec3(0.0f) : glm::vec3(0, len, 0);
            parent = sk.addBone("b" + std::to_string(i), parent, bind).value();
            bones.push_back(parent);
        }
        Transform bind;
        bind.t = glm::vec3(0, len, 0);
        effector = sk.addBone("tip", parent, bind).value();
        for (BoneId id = 0; id < sk.slotCount(); ++id) pose.push_back(sk.bone(id).bindLocal);
    }
    glm::vec3 joint(int i) const {
        const auto g = computeGlobals(sk, pose);
        return glm::vec3(
            g[i < static_cast<int>(bones.size()) ? bones[static_cast<std::size_t>(i)] : effector][3]);
    }
    IKChain chain(IKSolver solver) const {
        IKChain c;
        c.bones = bones;
        c.effector = effector;
        c.solver = solver;
        c.maxIterations = solver == IKSolver::CCD ? 400 : 60;
        return c;
    }
};

IKTarget at(const glm::vec3& p) {
    IKTarget t;
    t.world.t = p;
    return t;
}

bool finite(const Pose& pose) {
    for (const Transform& t : pose) {
        if (!std::isfinite(t.r.w + t.r.x + t.r.y + t.r.z + t.t.x + t.t.y + t.t.z)) return false;
    }
    return true;
}

void checkLengths(const Rig& rig, float len) {
    for (int i = 0; i < static_cast<int>(rig.bones.size()); ++i) {
        CHECK(std::abs(glm::length(rig.joint(i + 1) - rig.joint(i)) - len) < 1e-4f);
    }
}

}  // namespace

TEST_CASE("IK : chaîne invalide ou poids nul -> aucun changement") {
    Rig rig(2);
    IKChain bad = rig.chain(IKSolver::FABRIK);
    bad.effector = rig.bones[0];  // pas un enfant de la fin de chaîne
    const Pose before = rig.pose;
    CHECK_FALSE(solveIK(rig.sk, rig.pose, bad, at({1, 1, 0})).valid);
    IKTarget zero = at({1, 1, 0});
    zero.weight = 0.0f;
    CHECK_FALSE(solveIK(rig.sk, rig.pose, rig.chain(IKSolver::FABRIK), zero).valid);
    for (std::size_t i = 0; i < before.size(); ++i) CHECK(testutil::sameRot(before[i].r, rig.pose[i].r));
}

TEST_CASE("IK : makeChain remonte depuis le parent de l'effecteur") {
    Rig rig(3);
    const IKChain c = makeChain(rig.sk, rig.effector, 2);
    REQUIRE(c.bones.size() == 2);
    CHECK(c.bones[0] == rig.bones[1]);
    CHECK(c.bones[1] == rig.bones[2]);
    CHECK(isValidChain(rig.sk, c));
    CHECK(makeChain(rig.sk, rig.effector, 9).bones.empty());
}

TEST_CASE("Two-bone : cible atteignable -> erreur < 1e-3, longueurs conservées") {
    testutil::Rng rng(11);
    for (int i = 0; i < 200; ++i) {
        Rig rig(2);
        const glm::vec3 target = rng.unit() * rng.uniform(0.2f, 1.95f);
        const IKResult r = solveIK(rig.sk, rig.pose, rig.chain(IKSolver::TwoBone), at(target));
        CHECK(r.reached);
        CHECK(near(rig.joint(2), target, 1e-3f));
        checkLengths(rig, 1.0f);
        CHECK(finite(rig.pose));
    }
}

TEST_CASE("Two-bone : cible hors de portée -> chaîne tendue vers la cible, sans NaN") {
    Rig rig(2);
    const IKResult r = solveIK(rig.sk, rig.pose, rig.chain(IKSolver::TwoBone), at({0, 10, 0}));
    CHECK_FALSE(r.reached);
    CHECK(finite(rig.pose));
    CHECK(near(rig.joint(2), {0, 2, 0}, 1e-4f));

    Rig side(2);
    solveIK(side.sk, side.pose, side.chain(IKSolver::TwoBone), at({6, 0, 0}));
    CHECK(near(side.joint(2), {2, 0, 0}, 1e-4f));
    CHECK(near(side.joint(1), {1, 0, 0}, 1e-4f));
    checkLengths(side, 1.0f);
}

TEST_CASE("Two-bone : le pole oriente le coude") {
    for (const float sign : {1.0f, -1.0f}) {
        Rig rig(2);
        IKTarget t = at({1.2f, 0.0f, 0.0f});
        t.hasPole = true;
        t.pole = {0.6f, 0.0f, 5.0f * sign};
        solveIK(rig.sk, rig.pose, rig.chain(IKSolver::TwoBone), t);
        CHECK(near(rig.joint(2), {1.2f, 0, 0}, 1e-3f));
        CHECK(rig.joint(1).z * sign > 0.1f);
    }
    // Twist : un quart de tour du plan de flexion amène le coude de +Z vers -Y ou +Y.
    Rig rig(2);
    IKTarget t = at({1.2f, 0.0f, 0.0f});
    t.hasPole = true;
    t.pole = {0.6f, 0.0f, 5.0f};
    t.poleTwist = glm::half_pi<float>();
    solveIK(rig.sk, rig.pose, rig.chain(IKSolver::TwoBone), t);
    CHECK(std::abs(rig.joint(1).z) < 1e-3f);
    CHECK(std::abs(rig.joint(1).y) > 0.1f);
}

TEST_CASE("IK : cible à l'origine de la chaîne (dégénéré) sans NaN") {
    for (const IKSolver solver : {IKSolver::TwoBone, IKSolver::FABRIK, IKSolver::CCD}) {
        Rig rig(2);
        solveIK(rig.sk, rig.pose, rig.chain(solver), at({0, 0, 0}));
        CHECK(finite(rig.pose));
        checkLengths(rig, 1.0f);
        Rig same(3);
        solveIK(same.sk, same.pose, same.chain(solver), at(same.joint(0)));
        CHECK(finite(same.pose));
    }
}

TEST_CASE("FABRIK / CCD : chaînes longues, cible atteignable") {
    testutil::Rng rng(5);
    for (const IKSolver solver : {IKSolver::FABRIK, IKSolver::CCD}) {
        for (int i = 0; i < 100; ++i) {
            Rig rig(6, 0.5f);  // portée 3 m
            const glm::vec3 target =
                rng.unit() * rng.uniform(0.5f, solver == IKSolver::CCD
                                                   ? 2.0f
                                                   : 2.8f);  // CCD converge lentement près de la portée max
            const IKResult r = solveIK(rig.sk, rig.pose, rig.chain(solver), at(target));
            CHECK_MESSAGE(r.error < 1e-3f, "error=", r.error, " solver=", static_cast<int>(solver));
            checkLengths(rig, 0.5f);
            CHECK(finite(rig.pose));
        }
    }
}

TEST_CASE("FABRIK / CCD : hors de portée -> chaîne alignée vers la cible") {
    for (const IKSolver solver : {IKSolver::FABRIK, IKSolver::CCD}) {
        Rig rig(4);
        const IKResult r = solveIK(rig.sk, rig.pose, rig.chain(solver), at({0, 0, 20}));
        CHECK_FALSE(r.reached);
        CHECK(finite(rig.pose));
        CHECK(near(rig.joint(4), {0, 0, 4}, 5e-2f));
        checkLengths(rig, 1.0f);
    }
}

TEST_CASE("Limites : charnière et cône jamais dépassés") {
    testutil::Rng rng(21);
    IKChain base;
    {
        Rig rig(2);
        base = rig.chain(IKSolver::CCD);
    }
    for (const IKSolver solver : {IKSolver::CCD, IKSolver::FABRIK, IKSolver::TwoBone}) {
        for (int i = 0; i < 100; ++i) {
            Rig rig(2);
            IKChain c = rig.chain(solver);
            c.limits.resize(2);
            c.limits[1] = {JointLimitType::Hinge, {1, 0, 0}, 0.0f, 2.2f, 0.0f};  // coude : une charnière
            c.limits[0] = {JointLimitType::Cone, {0, 1, 0}, -0.3f, 0.3f, 1.2f};  // épaule : cône + torsion
            solveIK(rig.sk, rig.pose, c, at(rng.unit() * rng.uniform(0.3f, 2.5f)));
            CHECK(finite(rig.pose));
            // Charnière : rotation relative autour de X uniquement, dans [0, 2.2].
            glm::quat rel = glm::inverse(rig.sk.bone(rig.bones[1]).bindLocal.r) * rig.pose[rig.bones[1]].r;
            if (rel.w < 0) rel = -rel;
            const float angle = 2.0f * std::atan2(rel.x, rel.w);
            CHECK(angle >= -1e-4f);
            CHECK(angle <= 2.2f + 1e-4f);
            CHECK(std::abs(rel.y) < 1e-4f);
            CHECK(std::abs(rel.z) < 1e-4f);
            // Cône : écart à l'axe <= 1.2 rad, torsion dans [-0.3, 0.3].
            glm::quat rel0 = glm::inverse(rig.sk.bone(rig.bones[0]).bindLocal.r) * rig.pose[rig.bones[0]].r;
            if (rel0.w < 0) rel0 = -rel0;
            const SwingTwist st = swingTwist(rel0, {0, 1, 0});
            glm::quat sw = st.swing;
            if (sw.w < 0) sw = -sw;
            CHECK(2.0f * std::acos(std::min(sw.w, 1.0f)) <= 1.2f + 1e-3f);
            glm::quat tw = st.twist;
            if (tw.w < 0) tw = -tw;
            CHECK(std::abs(2.0f * std::atan2(tw.y, tw.w)) <= 0.3f + 1e-3f);
        }
    }
}

TEST_CASE("Mélange IK <-> FK : poids 0.5 = rotations à mi-chemin") {
    Rig full(2), half(2);
    solveIK(full.sk, full.pose, full.chain(IKSolver::TwoBone), at({1.5f, 0.5f, 0.0f}));
    IKTarget t = at({1.5f, 0.5f, 0.0f});
    t.weight = 0.5f;
    solveIK(half.sk, half.pose, half.chain(IKSolver::TwoBone), t);
    for (const BoneId id : half.bones) {
        const glm::quat expected = slerpShortest(half.sk.bone(id).bindLocal.r, full.pose[id].r, 0.5f);
        CHECK(testutil::sameRot(expected, half.pose[id].r, 1e-5f));
    }
}

TEST_CASE("Orientation de l'effecteur : KeepWorld et MatchTarget") {
    Rig rig(2);
    IKChain c = rig.chain(IKSolver::TwoBone);
    c.effectorOrientation = EffectorOrientation::KeepWorld;
    const glm::quat before = Transform::fromMat4(computeGlobals(rig.sk, rig.pose)[rig.effector]).r;
    solveIK(rig.sk, rig.pose, c, at({1.0f, 1.0f, 0.5f}));
    const glm::quat after = Transform::fromMat4(computeGlobals(rig.sk, rig.pose)[rig.effector]).r;
    CHECK(testutil::sameRot(before, after, 1e-4f));

    Rig match(2);
    IKChain m = match.chain(IKSolver::TwoBone);
    m.effectorOrientation = EffectorOrientation::MatchTarget;
    IKTarget t = at({1.0f, 1.0f, 0.5f});
    t.world.r = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 2, 3)));
    solveIK(match.sk, match.pose, m, t);
    const glm::quat got = Transform::fromMat4(computeGlobals(match.sk, match.pose)[match.effector]).r;
    CHECK(testutil::sameRot(t.world.r, got, 1e-4f));
}

TEST_CASE("solveIKLayer : la chaîne enfant est résolue après son parent") {
    // Deux chaînes en série : bras A (b0,b1,tip) ; le "tip" porte une seconde chaîne (c0, c1, tip2).
    Skeleton sk;
    Transform up;
    up.t = {0, 1, 0};
    const BoneId a0 = sk.addBone("a0", kNoBone).value();
    const BoneId a1 = sk.addBone("a1", a0, up).value();
    const BoneId aTip = sk.addBone("aTip", a1, up).value();
    const BoneId c0 = sk.addBone("c0", aTip, up).value();
    const BoneId cTip = sk.addBone("cTip", c0, up).value();
    Pose pose;
    for (BoneId id = 0; id < sk.slotCount(); ++id) pose.push_back(sk.bone(id).bindLocal);

    IKEntry childEntry;
    childEntry.chain.bones = {c0};
    childEntry.chain.effector = cTip;
    childEntry.chain.solver = IKSolver::FABRIK;
    childEntry.target.world.t = {1, 3, 0};
    IKEntry parentEntry;
    parentEntry.chain.bones = {a0, a1};
    parentEntry.chain.effector = aTip;
    parentEntry.chain.solver = IKSolver::TwoBone;
    parentEntry.target.world.t = {1.5f, 1.0f, 0.0f};

    const auto results = solveIKLayer(sk, pose, {childEntry, parentEntry});  // ordre inversé exprès
    REQUIRE(results.size() == 2);
    CHECK(results[1].reached);
    const auto g = computeGlobals(sk, pose);
    CHECK(near(glm::vec3(g[aTip][3]), {1.5f, 1.0f, 0.0f}, 1e-3f));
    // La chaîne enfant (1 os) pivote autour de SA racine c0, déjà déplacée par la chaîne parent, et vise la
    // cible.
    const glm::vec3 base = glm::vec3(g[c0][3]);
    CHECK(near(base, glm::vec3(g[aTip][3]) + glm::normalize(glm::vec3(g[aTip] * glm::vec4(0, 1, 0, 0))),
               1e-3f));
    CHECK(std::abs(glm::length(glm::vec3(g[cTip][3]) - base) - 1.0f) < 1e-4f);
    CHECK(
        near(glm::normalize(glm::vec3(g[cTip][3]) - base), glm::normalize(glm::vec3(1, 3, 0) - base), 1e-3f));
}

namespace {
// Rig à deux os + un os de contrôle « target » (racine, hors chaîne) et un os « pole ».
struct ControlRig {
    Character character;
    BoneId target = kNoBone, pole = kNoBone;
    Pose pose;
    ControlRig() {
        Rig rig(2);
        character.skeleton = rig.sk;
        Transform t;
        t.t = {1.0f, 1.0f, 0.0f};
        target = character.skeleton.addBone("IK_target", kNoBone, t).value();
        t.t = {0.5f, 0.0f, 3.0f};
        pole = character.skeleton.addBone("IK_pole", kNoBone, t).value();
        character.skeleton.setFlags(target, BoneFlags::Control);
        IKControl c;
        c.name = "arm";
        c.chain = makeChain(character.skeleton, character.skeleton.find("tip"), 2, IKSolver::TwoBone);
        c.targetBone = target;
        c.poleBone = pole;
        character.ikControls.push_back(c);
        pose = character.skeleton.bindPose();
    }
    glm::vec3 tip() const {
        return glm::vec3(computeGlobals(character.skeleton, pose)[character.skeleton.find("tip")][3]);
    }
};
}  // namespace

TEST_CASE("Contrôles IK : la cible est un os, le pole oriente le coude") {
    ControlRig rig;
    const auto results = applyIKControls(rig.character.skeleton, rig.pose, rig.character.ikControls);
    REQUIRE(results.size() == 1);
    CHECK(results[0].reached);
    CHECK(near(rig.tip(), {1.0f, 1.0f, 0.0f}, 1e-3f));
    const auto g = computeGlobals(rig.character.skeleton, rig.pose);
    CHECK(g[rig.character.skeleton.find("b1")][3].z > 0.05f);  // coude du côté du pole (+Z)
    // L'os de contrôle n'est jamais modifié par la résolution.
    CHECK(near(rig.pose[rig.target].t, {1.0f, 1.0f, 0.0f}));
}

TEST_CASE("Contrôles IK : désactivé, poids nul, cible disparue -> aucun effet") {
    ControlRig rig;
    const Pose bind = rig.pose;
    rig.character.ikControls[0].enabled = false;
    applyIKControls(rig.character.skeleton, rig.pose, rig.character.ikControls);
    rig.character.ikControls[0].enabled = true;
    rig.character.ikControls[0].weight = 0.0f;
    applyIKControls(rig.character.skeleton, rig.pose, rig.character.ikControls);
    for (std::size_t i = 0; i < bind.size(); ++i) CHECK(testutil::sameRot(bind[i].r, rig.pose[i].r));
    CHECK_FALSE(anyIKActive(rig.character.ikControls));
    rig.character.ikControls[0].weight = 1.0f;
    CHECK(anyIKActive(rig.character.ikControls));
    CHECK(isIKDriven(rig.character.ikControls, rig.character.skeleton.find("b0")));
    CHECK_FALSE(isIKDriven(rig.character.ikControls, rig.target));
}

TEST_CASE("Contrôles IK : supprimer l'os cible ou la chaîne retire le contrôle (pole : oublié)") {
    ControlRig rig;
    REQUIRE(removeBoneClean(rig.character, rig.pole, RemoveMode::WithDescendants));
    REQUIRE(rig.character.ikControls.size() == 1);
    CHECK(rig.character.ikControls[0].poleBone == kNoBone);
    REQUIRE(removeBoneClean(rig.character, rig.target, RemoveMode::WithDescendants));
    CHECK(rig.character.ikControls.empty());
}

TEST_CASE("EditIKControlsCommand : execute / undo / fusion") {
    ControlRig rig;
    const auto before = rig.character.ikControls;
    auto after = before;
    after[0].weight = 0.5f;
    EditContext ctx{&rig.character, &rig.pose};
    EditIKControlsCommand a(before, after, "Edit IK", {}, "weight");
    a.execute(ctx);
    CHECK(rig.character.ikControls[0].weight == 0.5f);
    auto later = after;
    later[0].weight = 0.25f;
    EditIKControlsCommand b(after, later, "Edit IK", {}, "weight");
    CHECK(a.merge(b));
    a.execute(ctx);
    CHECK(rig.character.ikControls[0].weight == 0.25f);
    a.undo(ctx);
    CHECK(rig.character.ikControls == before);
    EditIKControlsCommand other(after, later, "Edit IK", {}, "other");
    CHECK_FALSE(a.merge(other));
}

TEST_CASE("Look-at : l'axe de l'os pointe vers la cible, limite de cône respectée") {
    Rig rig(1);
    const glm::vec3 target(2.0f, 1.0f, 3.0f);
    solveLookAt(rig.sk, rig.pose, rig.bones[0], {0, 1, 0}, target, 1.0f);
    const auto g = computeGlobals(rig.sk, rig.pose);
    const glm::vec3 axis = glm::normalize(glm::vec3(g[rig.bones[0]] * glm::vec4(0, 1, 0, 0)));
    CHECK(near(axis, glm::normalize(target), 1e-4f));

    Rig limited(1);
    JointLimit cone{JointLimitType::Cone, {0, 1, 0}, -0.5f, 0.5f, 0.4f};
    solveLookAt(limited.sk, limited.pose, limited.bones[0], {0, 1, 0}, {5, 0, 0}, 1.0f, cone);
    const auto g2 = computeGlobals(limited.sk, limited.pose);
    const glm::vec3 axis2 = glm::normalize(glm::vec3(g2[limited.bones[0]] * glm::vec4(0, 1, 0, 0)));
    CHECK(std::acos(std::min(1.0f, axis2.y)) <= 0.4f + 1e-3f);

    Rig half(1);
    solveLookAt(half.sk, half.pose, half.bones[0], {0, 1, 0}, {5, 0, 0}, 0.5f);
    const auto g3 = computeGlobals(half.sk, half.pose);
    const glm::vec3 axis3 = glm::normalize(glm::vec3(g3[half.bones[0]] * glm::vec4(0, 1, 0, 0)));
    CHECK(std::acos(axis3.y) == doctest::Approx(glm::half_pi<float>() * 0.5f).epsilon(1e-3));
}

TEST_CASE("Contrôle look-at et verrou de pied via applyIKControls") {
    ControlRig rig;
    IKControl look;
    look.kind = IKKind::LookAt;
    look.chain.bones = {rig.character.skeleton.find("b0")};
    look.targetBone = rig.target;
    rig.character.ikControls = {look};
    applyIKControls(rig.character.skeleton, rig.pose, rig.character.ikControls);
    const auto g = computeGlobals(rig.character.skeleton, rig.pose);
    const glm::vec3 axis = glm::normalize(glm::vec3(g[look.chain.bones[0]] * glm::vec4(0, 1, 0, 0)));
    CHECK(near(axis, glm::normalize(glm::vec3(1, 1, 0)), 1e-3f));

    // Verrou : la cible est figée à son monde capturé, même si l'os de contrôle bouge.
    ControlRig foot;
    FootLock lock;
    lock.start = 1.0;
    lock.end = 2.0;
    lock.world.t = {0.0f, 1.5f, 0.5f};
    foot.character.ikControls[0].locks = {lock};
    CHECK(activeLock(foot.character.ikControls[0], 1.5) != nullptr);
    CHECK(activeLock(foot.character.ikControls[0], 0.5) == nullptr);
    CHECK(activeLock(foot.character.ikControls[0], -1.0) == nullptr);
    Pose locked = foot.pose;
    applyIKControls(foot.character.skeleton, locked, foot.character.ikControls, 1.5);
    foot.pose = locked;
    CHECK(near(foot.tip(), {0.0f, 1.5f, 0.5f}, 1e-3f));
    Pose free = foot.character.skeleton.bindPose();
    applyIKControls(foot.character.skeleton, free, foot.character.ikControls, 0.5);
    foot.pose = free;
    CHECK(near(foot.tip(), {1.0f, 1.0f, 0.0f}, 1e-3f));
}

TEST_CASE("Two-bone : balayer la cible ne fait jamais sauter le coude (pas de tremblement ni d'inversion)") {
    Rig rig(2);
    IKChain chain = rig.chain(IKSolver::TwoBone);
    glm::vec3 previousElbow(0.0f);
    bool first = true;
    for (int i = 0; i <= 400; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / 400.0f;
        IKTarget t = at({1.4f * std::cos(a), 0.6f * std::sin(2.0f * a), 1.2f * std::sin(a)});
        t.hasPole = true;
        t.pole = {0.0f, 4.0f,
                  0.0f};  // le trajet de la cible ne croise jamais l'axe du pole (singularité inévitable)
        solveIK(rig.sk, rig.pose, chain, t);
        const glm::vec3 elbow = rig.joint(1);
        if (!first) CHECK(glm::length(elbow - previousElbow) < 0.12f);  // mouvement continu
        // Le coude reste du côté du pole : leurs composantes perpendiculaires à l'axe épaule -> cible sont de
        // même sens.
        const glm::vec3 u = glm::normalize(t.world.t);
        CHECK(glm::dot(elbow - u * glm::dot(elbow, u), t.pole - u * glm::dot(t.pole, u)) > -1e-4f);
        previousElbow = elbow;
        first = false;
    }
}
