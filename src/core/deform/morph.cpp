// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/deform/morph.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"

namespace rigger {

float driverWeight(const MorphDriver& driver, const Skeleton& skeleton, const Pose& pose) {
    if (!skeleton.contains(driver.bone) || driver.bone >= pose.size()) return 0.0f;
    const glm::quat rel =
        glm::normalize(glm::inverse(skeleton.bone(driver.bone).bindLocal.r) * pose[driver.bone].r);
    const glm::vec3 axis =
        glm::length(driver.axis) > 1e-6f ? glm::normalize(driver.axis) : glm::vec3(1, 0, 0);
    glm::quat twist = swingTwist(rel.w < 0.0f ? -rel : rel, axis).twist;
    if (twist.w < 0.0f) twist = -twist;
    const float angle = 2.0f * std::atan2(glm::dot(glm::vec3(twist.x, twist.y, twist.z), axis), twist.w);
    const float span = driver.angle1 - driver.angle0;
    if (std::abs(span) < 1e-6f) return angle >= driver.angle1 ? 1.0f : 0.0f;
    return std::clamp((angle - driver.angle0) / span, 0.0f, 1.0f);
}

std::vector<float> morphWeights(const Mesh& mesh, const Skeleton& skeleton, const Pose& pose) {
    std::vector<float> weights(mesh.morphTargets.size(), 0.0f);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const MorphTarget& target = mesh.morphTargets[i];
        float w = 0.0f;
        if (target.weightBone != kNoBone && skeleton.contains(target.weightBone)) {
            w = target.weightBone < pose.size() ? pose[target.weightBone].t.x
                                                : skeleton.bone(target.weightBone).bindLocal.t.x;
        }
        for (const MorphDriver& d : target.drivers) w += driverWeight(d, skeleton, pose);
        weights[i] = std::isfinite(w) ? w : 0.0f;
    }
    return weights;
}

bool anyMorphActive(const std::vector<float>& weights) {
    return std::any_of(weights.begin(), weights.end(), [](float w) { return std::abs(w) > 1e-6f; });
}

std::vector<Vertex> applyMorphs(const Mesh& mesh, const std::vector<float>& weights, bool recompute) {
    std::vector<Vertex> out = mesh.vertices;
    bool changed = false;
    for (std::size_t t = 0; t < mesh.morphTargets.size() && t < weights.size(); ++t) {
        const float w = weights[t];
        if (std::abs(w) <= 1e-6f) continue;
        const auto& deltas = mesh.morphTargets[t].positionDeltas;
        for (std::size_t v = 0; v < out.size() && v < deltas.size(); ++v) out[v].position += deltas[v] * w;
        changed = true;
    }
    if (changed && recompute) {
        Mesh tmp;
        tmp.vertices = out;
        tmp.indices = mesh.indices;
        tmp.recomputeNormals();
        for (std::size_t v = 0; v < out.size(); ++v) out[v].normal = tmp.vertices[v].normal;
    }
    return out;
}

}  // namespace rigger
