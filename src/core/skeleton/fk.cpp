// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skeleton/fk.hpp"

#include "core/util/profiler.hpp"

namespace rigger {

std::vector<glm::mat4> computeGlobals(const Skeleton& skeleton, const Pose& pose) {
    RIGGER_PROFILE_SCOPE("computeGlobals");
    std::vector<glm::mat4> globals(skeleton.slotCount(), glm::mat4(1.0f));
    for (BoneId id : skeleton.order()) {
        const Bone& b = skeleton.bone(id);
        const Transform& local = id < pose.size() ? pose[id] : b.bindLocal;
        const glm::mat4 m = local.toMat4();
        globals[id] = b.parent == kNoBone ? m : globals[b.parent] * m;
    }
    return globals;
}

std::vector<glm::mat4> computeSkinMatrices(const Skeleton& skeleton, const Pose& pose) {
    std::vector<glm::mat4> skin = computeGlobals(skeleton, pose);
    const auto& inv = skeleton.inverseBind();
    for (BoneId id : skeleton.order()) skin[id] = skin[id] * inv[id];
    return skin;
}

}  // namespace rigger
