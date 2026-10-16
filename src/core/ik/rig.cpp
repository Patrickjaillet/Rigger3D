// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/ik/rig.hpp"

#include <algorithm>

#include "core/skeleton/fk.hpp"
#include "core/util/profiler.hpp"

namespace rigger {

namespace {

bool active(const IKControl& c) { return c.enabled && c.weight > 0.0f; }

bool validLookAt(const Skeleton& skeleton, const IKControl& c) {
    return c.chain.bones.size() == 1 && skeleton.contains(c.chain.bones[0]);
}

bool usable(const Skeleton& skeleton, const IKControl& c) {
    const bool shape = c.kind == IKKind::LookAt ? validLookAt(skeleton, c) : isValidChain(skeleton, c.chain);
    return shape && skeleton.contains(c.targetBone);
}

}  // namespace

bool anyIKActive(const std::vector<IKControl>& controls) {
    return std::any_of(controls.begin(), controls.end(), active);
}

bool isIKDriven(const std::vector<IKControl>& controls, BoneId id) {
    for (const IKControl& c : controls) {
        if (!active(c)) continue;
        if (std::find(c.chain.bones.begin(), c.chain.bones.end(), id) != c.chain.bones.end()) return true;
        if (c.kind == IKKind::Chain && id == c.chain.effector &&
            c.chain.effectorOrientation != EffectorOrientation::Follow)
            return true;
    }
    return false;
}

const FootLock* activeLock(const IKControl& control, double time) {
    if (time < 0.0) return nullptr;
    for (const FootLock& lock : control.locks) {
        if (time >= lock.start - 1e-9 && time <= lock.end + 1e-9) return &lock;
    }
    return nullptr;
}

std::vector<IKResult> applyIKControls(const Skeleton& skeleton, Pose& pose,
                                      const std::vector<IKControl>& controls, double time) {
    RIGGER_PROFILE_SCOPE("applyIKControls");
    std::vector<IKResult> results(controls.size());
    if (!anyIKActive(controls)) {
        return results;
    }
    std::vector<std::size_t> rank(skeleton.slotCount(), 0);
    for (std::size_t i = 0; i < skeleton.order().size(); ++i) {
        rank[skeleton.order()[i]] = i;
    }
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < controls.size(); ++i) {
        if (active(controls[i]) && usable(skeleton, controls[i])) indices.push_back(i);
    }
    // Une chaîne enfant d'une autre est résolue après elle (rang hiérarchique de sa racine).
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        return rank[controls[a].chain.bones.front()] < rank[controls[b].chain.bones.front()];
    });
    for (const std::size_t i : indices) {
        const IKControl& c = controls[i];
        // Cible et pole sont relus après chaque résolution : ils peuvent dépendre d'une chaîne déjà résolue.
        const std::vector<glm::mat4> globals = computeGlobals(skeleton, pose);
        IKTarget target;
        target.world = Transform::fromMat4(globals[c.targetBone]);
        if (const FootLock* lock = activeLock(c, time)) {
            target.world = lock->world;  // pied planté : la cible reste où elle a été verrouillée
        }
        if (c.kind == IKKind::LookAt) {
            solveLookAt(skeleton, pose, c.chain.bones[0], c.aimAxis, target.world.t, std::min(c.weight, 1.0f),
                        c.chain.limits.empty() ? JointLimit{} : c.chain.limits[0]);
            results[i].valid = true;
            results[i].reached = true;
            continue;
        }
        target.weight = std::min(c.weight, 1.0f);
        if (skeleton.contains(c.poleBone)) {
            target.hasPole = true;
            target.pole = glm::vec3(globals[c.poleBone][3]);
            target.poleTwist = c.poleTwist;
        }
        results[i] = solveIK(skeleton, pose, c.chain, target);
    }
    return results;
}

bool pruneIKControls(const Skeleton& skeleton, std::vector<IKControl>& controls) {
    bool changed = false;
    for (auto it = controls.begin(); it != controls.end();) {
        if (!usable(skeleton, *it)) {
            it = controls.erase(it);
            changed = true;
            continue;
        }
        if (it->poleBone != kNoBone && !skeleton.contains(it->poleBone)) {
            it->poleBone = kNoBone;
            changed = true;
        }
        ++it;
    }
    return changed;
}

}  // namespace rigger
