// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skeleton/manipulate.hpp"

#include <algorithm>
#include <cmath>

namespace rigger {

namespace {
glm::quat worldRotation(const glm::mat4& world) { return glm::normalize(Transform::fromMat4(world).r); }
}  // namespace

glm::quat spaceOrientation(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId id,
                           GizmoSpace space) {
    switch (space) {
        case GizmoSpace::Global:
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        case GizmoSpace::Parent: {
            const BoneId parent = skeleton.bone(id).parent;
            return parent == kNoBone ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : worldRotation(globals[parent]);
        }
        case GizmoSpace::Local:
            break;
    }
    return worldRotation(globals[id]);
}

glm::vec3 selectionPivot(const std::vector<glm::mat4>& globals, const std::vector<BoneId>& ids) {
    if (ids.empty()) {
        return glm::vec3(0.0f);
    }
    glm::vec3 sum(0.0f);
    for (const BoneId id : ids) {
        sum += glm::vec3(globals[id][3]);
    }
    return sum / static_cast<float>(ids.size());
}

std::vector<BoneId> topLevelBones(const Skeleton& skeleton, const std::vector<BoneId>& ids) {
    std::vector<BoneId> out;
    for (const BoneId id : ids) {
        const bool followsParent = std::any_of(ids.begin(), ids.end(), [&](BoneId other) {
            return other != id && skeleton.isDescendantOf(id, other);
        });
        if (!followsParent) {
            out.push_back(id);
        }
    }
    return out;
}

bool WorldDelta::isIdentity() const {
    return glm::all(glm::lessThan(glm::abs(translation), glm::vec3(1e-9f))) &&
           std::abs(glm::dot(rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))) > 1.0f - 1e-7f &&
           glm::all(glm::lessThan(glm::abs(scale - glm::vec3(1.0f)), glm::vec3(1e-9f)));
}

Transform applyWorldDelta(const Skeleton& skeleton, const std::vector<glm::mat4>& startGlobals,
                          const Transform& startLocal, BoneId id, const WorldDelta& delta) {
    const BoneId parent = skeleton.bone(id).parent;
    const glm::mat4 parentWorld = parent == kNoBone ? glm::mat4(1.0f) : startGlobals[parent];
    const Transform world = Transform::fromMat4(startGlobals[id]);

    Transform moved = world;
    moved.t = world.t + delta.translation;
    moved.r = glm::normalize(delta.rotation * world.r);
    Transform local = Transform::fromMat4(glm::inverse(parentWorld) * moved.toMat4());

    // Échelle : appliquée à l'échelle locale de départ (le décodage monde -> local ci-dessus la conserve déjà
    // approximativement ; on la remet exacte). Les axes du repère du gizmo sont projetés sur les axes locaux.
    const glm::mat3 relative = glm::mat3_cast(glm::inverse(world.r) * delta.scaleBasis);
    const glm::mat3 absRelative(glm::abs(relative[0]), glm::abs(relative[1]), glm::abs(relative[2]));
    const glm::vec3 localFactors = glm::vec3(1.0f) + absRelative * (delta.scale - glm::vec3(1.0f));
    local.s = startLocal.s * localFactors;
    return local;
}

}  // namespace rigger
