// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/ik/ik.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"

namespace rigger {

namespace {

constexpr float kEps = 1e-6f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float l = glm::length(v);
    return l > kEps ? v / l : fallback;
}

glm::vec3 anyPerpendicular(const glm::vec3& u) {
    const glm::vec3 helper = std::abs(u.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(u, helper));
}

// Comme fromTo, mais sans arrondir les très petits angles à l'identité (indispensable à la convergence fine).
glm::quat aimRotation(const glm::vec3& a, const glm::vec3& b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 u = a / la, v = b / lb;
    const float d = glm::dot(u, v);
    if (d < -0.999999f) return fromTo(a, b);
    const glm::vec3 c = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::quat rotationOf(const glm::mat4& m) { return glm::normalize(Transform::fromMat4(m).r); }

// FK limitée à la chaîne : globals[i] pour bones[i] (i < n) et globals[n] pour l'effecteur.
struct ChainFk {
    const Skeleton& skeleton;
    const IKChain& chain;
    glm::mat4 parentGlobal{1.0f};
    std::vector<glm::mat4> g;

    ChainFk(const Skeleton& s, const IKChain& c, const Pose& pose)
        : skeleton(s), chain(c), g(c.bones.size() + 1) {
        const BoneId parent = s.bone(c.bones.front()).parent;
        if (parent != kNoBone) {
            parentGlobal = computeGlobals(s, pose)[parent];
        }
        update(pose);
    }

    int n() const { return static_cast<int>(chain.bones.size()); }
    BoneId id(int i) const { return i < n() ? chain.bones[static_cast<std::size_t>(i)] : chain.effector; }

    void update(const Pose& pose) {
        glm::mat4 parent = parentGlobal;
        for (int i = 0; i <= n(); ++i) {
            g[static_cast<std::size_t>(i)] = parent * pose[id(i)].toMat4();
            parent = g[static_cast<std::size_t>(i)];
        }
    }
    glm::vec3 pos(int i) const { return glm::vec3(g[static_cast<std::size_t>(i)][3]); }
    glm::quat parentRotation(int i) const {
        return rotationOf(i == 0 ? parentGlobal : g[static_cast<std::size_t>(i - 1)]);
    }
};

// Applique une rotation monde `delta` à l'os i de la chaîne (puis sa limite), et met la FK à jour.
void rotateBone(ChainFk& fk, Pose& pose, int i, const glm::quat& delta) {
    const BoneId id = fk.id(i);
    const glm::quat globalRot = rotationOf(fk.g[static_cast<std::size_t>(i)]);
    glm::quat local = glm::inverse(fk.parentRotation(i)) * delta * globalRot;
    local = glm::normalize(local);
    if (static_cast<std::size_t>(i) < fk.chain.limits.size()) {
        local = applyJointLimit(fk.chain.limits[static_cast<std::size_t>(i)], fk.skeleton.bone(id).bindLocal,
                                local);
    }
    pose[id].r = local;
    fk.update(pose);
}

// Convertit des positions d'articulations cibles en rotations locales (racine -> bout).
void applyPositions(ChainFk& fk, Pose& pose, const std::vector<glm::vec3>& want) {
    for (int i = 0; i < fk.n(); ++i) {
        const glm::vec3 cur = fk.pos(i + 1) - fk.pos(i);
        const glm::vec3 target = want[static_cast<std::size_t>(i + 1)] - fk.pos(i);
        rotateBone(fk, pose, i, aimRotation(cur, target));
    }
}

void solveTwoBone(ChainFk& fk, Pose& pose, const IKTarget& target) {
    const glm::vec3 p0 = fk.pos(0), p1 = fk.pos(1), p2 = fk.pos(2);
    const float a = glm::length(p1 - p0), b = glm::length(p2 - p1);
    const glm::vec3 toTarget = target.world.t - p0;
    const float dist = glm::length(toTarget);
    const glm::vec3 u = safeNormalize(toTarget, safeNormalize(p2 - p0, glm::vec3(0, 1, 0)));
    if (a < kEps || b < kEps) {  // segment nul : on se contente de viser
        applyPositions(fk, pose, {p0, p0 + u * a, p0 + u * (a + b)});
        return;
    }
    const float d = std::clamp(dist, std::abs(a - b) + 1e-5f, a + b);
    const float cosA = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));

    glm::vec3 pv = (target.hasPole ? target.pole - p0 : p1 - p0);
    pv -= u * glm::dot(pv, u);
    if (glm::length(pv) < 1e-5f) {  // pole aligné avec l'axe : on garde le coude actuel
        pv = (p1 - p0) - u * glm::dot(p1 - p0, u);
    }
    pv = glm::length(pv) < 1e-5f ? anyPerpendicular(u) : glm::normalize(pv);
    if (target.poleTwist != 0.0f) {
        pv = glm::angleAxis(target.poleTwist, u) * pv;
    }
    const glm::vec3 elbow = p0 + u * (a * cosA) + pv * (a * sinA);
    const glm::vec3 wrist = elbow + safeNormalize(p0 + u * d - elbow, u) * b;
    applyPositions(fk, pose, {p0, elbow, wrist});
}

void solveFabrik(ChainFk& fk, Pose& pose, const IKChain& chain, const IKTarget& target) {
    const int n = fk.n();
    std::vector<glm::vec3> p(static_cast<std::size_t>(n + 1));
    std::vector<float> len(static_cast<std::size_t>(n));
    float total = 0.0f;
    for (int i = 0; i <= n; ++i) p[static_cast<std::size_t>(i)] = fk.pos(i);
    for (int i = 0; i < n; ++i) {
        len[static_cast<std::size_t>(i)] =
            glm::length(p[static_cast<std::size_t>(i + 1)] - p[static_cast<std::size_t>(i)]);
        total += len[static_cast<std::size_t>(i)];
    }
    const glm::vec3 root = p[0], t = target.world.t;
    const glm::vec3 fallbackDir = safeNormalize(p.back() - root, glm::vec3(0, 1, 0));
    if (glm::length(t - root) >= total) {  // hors de portée : chaîne tendue vers la cible
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            p[k + 1] = p[k] + safeNormalize(t - p[k], fallbackDir) * len[k];
        }
    } else {
        for (int it = 0; it < chain.maxIterations; ++it) {
            if (glm::length(p[static_cast<std::size_t>(n)] - t) < chain.tolerance * 0.25f) break;
            p[static_cast<std::size_t>(n)] = t;
            for (int i = n - 1; i >= 0; --i) {
                const auto k = static_cast<std::size_t>(i);
                p[k] = p[k + 1] + safeNormalize(p[k] - p[k + 1], fallbackDir) * len[k];
            }
            p[0] = root;
            for (int i = 0; i < n; ++i) {
                const auto k = static_cast<std::size_t>(i);
                p[k + 1] = p[k] + safeNormalize(p[k + 1] - p[k], fallbackDir) * len[k];
            }
        }
    }
    applyPositions(fk, pose, p);
    // Avec des limites, la conversion peut s'écarter de la cible : quelques passes de CCD amortissent
    // l'écart.
}

void solveCcd(ChainFk& fk, Pose& pose, const IKChain& chain, const IKTarget& target) {
    const float damping = std::clamp(chain.damping, 0.01f, 1.0f);
    for (int it = 0; it < chain.maxIterations; ++it) {
        if (glm::length(fk.pos(fk.n()) - target.world.t) < chain.tolerance * 0.25f) break;
        for (int i = fk.n() - 1; i >= 0; --i) {
            const glm::vec3 joint = fk.pos(i);
            const glm::quat full = aimRotation(fk.pos(fk.n()) - joint, target.world.t - joint);
            const glm::quat step =
                damping >= 1.0f ? full : slerpShortest(glm::quat(1, 0, 0, 0), full, damping);
            rotateBone(fk, pose, i, step);
        }
    }
}

float twistAngle(const glm::quat& twist, const glm::vec3& axis) {
    glm::quat q = twist;
    if (q.w < 0.0f) q = -q;
    return 2.0f * std::atan2(glm::dot(glm::vec3(q.x, q.y, q.z), axis), q.w);
}

}  // namespace

bool isValidChain(const Skeleton& skeleton, const IKChain& chain) {
    if (chain.bones.empty() || chain.effector == kNoBone || !skeleton.contains(chain.effector)) {
        return false;
    }
    for (std::size_t i = 0; i < chain.bones.size(); ++i) {
        if (!skeleton.contains(chain.bones[i])) return false;
        if (i > 0 && skeleton.bone(chain.bones[i]).parent != chain.bones[i - 1]) return false;
    }
    return skeleton.bone(chain.effector).parent == chain.bones.back();
}

IKChain makeChain(const Skeleton& skeleton, BoneId effector, int count, IKSolver solver) {
    IKChain chain;
    if (!skeleton.contains(effector) || count < 1) {
        return chain;
    }
    BoneId cur = skeleton.bone(effector).parent;
    for (int i = 0; i < count; ++i) {
        if (cur == kNoBone) {
            return IKChain{};
        }
        chain.bones.insert(chain.bones.begin(), cur);
        cur = skeleton.bone(cur).parent;
    }
    chain.effector = effector;
    chain.solver = (solver == IKSolver::TwoBone && count != 2) ? IKSolver::FABRIK : solver;
    return chain;
}

glm::quat applyJointLimit(const JointLimit& limit, const Transform& bindLocal, const glm::quat& local) {
    if (limit.type == JointLimitType::None) {
        return local;
    }
    glm::quat rel = glm::normalize(glm::inverse(bindLocal.r) * local);
    if (rel.w < 0.0f) rel = -rel;
    const glm::vec3 axis = safeNormalize(limit.axis, glm::vec3(1, 0, 0));
    const SwingTwist st = swingTwist(rel, axis);
    const float lo = std::min(limit.min, limit.max), hi = std::max(limit.min, limit.max);
    const float angle = std::clamp(twistAngle(st.twist, axis), lo, hi);
    const glm::quat twist = glm::angleAxis(angle, axis);
    if (limit.type == JointLimitType::Hinge) {
        return glm::normalize(bindLocal.r * twist);
    }
    glm::quat swing = st.swing;
    if (swing.w < 0.0f) swing = -swing;
    const float swingAngle = 2.0f * std::acos(std::clamp(swing.w, -1.0f, 1.0f));
    if (swingAngle > limit.swing) {
        const glm::vec3 v(swing.x, swing.y, swing.z);
        swing = glm::angleAxis(std::max(limit.swing, 0.0f), safeNormalize(v, anyPerpendicular(axis)));
    }
    return glm::normalize(bindLocal.r * swing * twist);
}

void solveLookAt(const Skeleton& skeleton, Pose& pose, BoneId bone, const glm::vec3& aimAxis,
                 const glm::vec3& target, float weight, const JointLimit& limit) {
    if (!skeleton.contains(bone) || weight <= 0.0f) return;
    if (pose.size() < skeleton.slotCount()) {
        const std::size_t old = pose.size();
        pose.resize(skeleton.slotCount());
        for (BoneId id : skeleton.order()) {
            if (id >= old) pose[id] = skeleton.bone(id).bindLocal;
        }
    }
    const std::vector<glm::mat4> g = computeGlobals(skeleton, pose);
    const BoneId parent = skeleton.bone(bone).parent;
    const glm::quat parentRot = parent == kNoBone ? glm::quat(1, 0, 0, 0) : rotationOf(g[parent]);
    const glm::quat globalRot = rotationOf(g[bone]);
    const glm::vec3 origin = glm::vec3(g[bone][3]);
    const glm::vec3 current = globalRot * safeNormalize(aimAxis, glm::vec3(0, 1, 0));
    glm::quat local =
        glm::normalize(glm::inverse(parentRot) * aimRotation(current, target - origin) * globalRot);
    local = applyJointLimit(limit, skeleton.bone(bone).bindLocal, local);
    pose[bone].r = slerpShortest(pose[bone].r, local, std::clamp(weight, 0.0f, 1.0f));
}

IKResult solveIK(const Skeleton& skeleton, Pose& pose, const IKChain& chain, const IKTarget& target) {
    IKResult result;
    if (!isValidChain(skeleton, chain) || target.weight <= 0.0f ||
        !(std::isfinite(target.world.t.x) && std::isfinite(target.world.t.y) &&
          std::isfinite(target.world.t.z))) {
        return result;
    }
    result.valid = true;
    // Pose complétée par la bind pose pour les os manquants.
    if (pose.size() < skeleton.slotCount()) {
        const std::size_t old = pose.size();
        pose.resize(skeleton.slotCount());
        for (BoneId id : skeleton.order()) {
            if (id >= old) pose[id] = skeleton.bone(id).bindLocal;
        }
    }
    const Pose original = pose;
    ChainFk fk(skeleton, chain, pose);
    const glm::quat effectorWorldBefore = rotationOf(fk.g.back());

    switch (chain.solver) {
        case IKSolver::TwoBone:
            if (chain.bones.size() == 2) {
                solveTwoBone(fk, pose, target);
            } else {
                solveFabrik(fk, pose, chain, target);
            }
            break;
        case IKSolver::FABRIK:
            solveFabrik(fk, pose, chain, target);
            break;
        case IKSolver::CCD:
            solveCcd(fk, pose, chain, target);
            break;
    }
    // Avec des limites, FABRIK / TwoBone peuvent rester loin de la cible : on affine par CCD (qui respecte
    // les limites).
    if (!chain.limits.empty() && chain.solver != IKSolver::CCD &&
        glm::length(fk.pos(fk.n()) - target.world.t) > chain.tolerance) {
        solveCcd(fk, pose, chain, target);
    }

    // Orientation de l'effecteur.
    if (chain.effectorOrientation != EffectorOrientation::Follow) {
        const glm::quat world = chain.effectorOrientation == EffectorOrientation::KeepWorld
                                    ? effectorWorldBefore
                                    : target.world.r;
        pose[chain.effector].r = glm::normalize(glm::inverse(fk.parentRotation(fk.n())) * world);
    }
    // Mélange IK <-> FK (les rotations locales des os concernés seulement).
    if (target.weight < 1.0f) {
        for (int i = 0; i <= fk.n(); ++i) {
            const BoneId id = fk.id(i);
            pose[id].r = slerpShortest(original[id].r, pose[id].r, std::clamp(target.weight, 0.0f, 1.0f));
        }
    }
    fk.update(pose);
    result.error = glm::length(fk.pos(fk.n()) - target.world.t);
    result.reached = result.error <= chain.tolerance;
    return result;
}

std::vector<IKResult> solveIKLayer(const Skeleton& skeleton, Pose& pose,
                                   const std::vector<IKEntry>& entries) {
    std::vector<std::size_t> rank(skeleton.slotCount(), 0);
    for (std::size_t i = 0; i < skeleton.order().size(); ++i) {
        rank[skeleton.order()[i]] = i;
    }
    std::vector<std::size_t> indices(entries.size());
    for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    const auto key = [&](std::size_t e) {
        const IKChain& c = entries[e].chain;
        return c.bones.empty() || c.bones.front() >= rank.size() ? std::size_t{0} : rank[c.bones.front()];
    };
    std::stable_sort(indices.begin(), indices.end(),
                     [&](std::size_t a, std::size_t b) { return key(a) < key(b); });
    std::vector<IKResult> results(entries.size());
    for (const std::size_t e : indices) {
        results[e] = solveIK(skeleton, pose, entries[e].chain, entries[e].target);
    }
    return results;
}

}  // namespace rigger
