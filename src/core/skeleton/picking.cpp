// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skeleton/picking.hpp"

#include <algorithm>

namespace rigger {

BoneSegments boneSegments(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId id) {
    BoneSegments out;
    const Bone& bone = skeleton.bone(id);
    out.origin = glm::vec3(globals[id][3]);
    if (bone.parent != kNoBone) {
        out.hasLink = true;
        out.linkFrom = glm::vec3(globals[bone.parent][3]);
        out.linkTo = out.origin;
    }
    if (skeleton.children(id).empty()) {
        out.hasStub = true;
        out.stubTo = glm::vec3(globals[id] * glm::vec4(0.0f, bone.length, 0.0f, 1.0f));
    }
    return out;
}

std::vector<PickHit> pickBones(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                               const Ray& ray, const PickParams& params,
                               const std::function<bool(BoneId)>& pickable) {
    std::vector<PickHit> hits;
    const auto radiusAt = [&](const glm::vec3& p) {
        const float distance = glm::length(p - params.cameraPosition);
        return (params.worldPerPixel + distance * params.anglePerPixel) * params.tolerancePixels;
    };
    for (const BoneId id : skeleton.order()) {
        if (pickable && !pickable(id)) {
            continue;
        }
        const BoneSegments seg = boneSegments(skeleton, globals, id);
        std::optional<float> best;
        const auto consider = [&best](std::optional<float> t) {
            if (t && (!best || *t < *best)) {
                best = t;
            }
        };
        // Articulation : sphère un peu plus grosse que le lien pour qu'un os court reste cliquable.
        consider(intersectSphere(ray, seg.origin, radiusAt(seg.origin) * 1.25f));
        if (seg.hasLink) {
            consider(intersectCapsule(ray, seg.linkFrom, seg.linkTo,
                                      radiusAt((seg.linkFrom + seg.linkTo) * 0.5f)));
        }
        if (seg.hasStub) {
            consider(
                intersectCapsule(ray, seg.origin, seg.stubTo, radiusAt((seg.origin + seg.stubTo) * 0.5f)));
        }
        if (best) {
            hits.push_back({id, *best});
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const PickHit& a, const PickHit& b) { return a.t != b.t ? a.t < b.t : a.id < b.id; });
    return hits;
}

std::vector<BoneId> bonesInRect(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                                const glm::mat4& viewProjection, const glm::vec2& ndcMin,
                                const glm::vec2& ndcMax, const std::function<bool(BoneId)>& pickable) {
    const glm::vec2 lo = glm::min(ndcMin, ndcMax);
    const glm::vec2 hi = glm::max(ndcMin, ndcMax);
    std::vector<BoneId> out;
    for (const BoneId id : skeleton.order()) {
        if (pickable && !pickable(id)) {
            continue;
        }
        const glm::vec4 clip = viewProjection * glm::vec4(glm::vec3(globals[id][3]), 1.0f);
        if (clip.w <= 1e-6f) {
            continue;  // derrière la caméra
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x >= lo.x && ndc.x <= hi.x && ndc.y >= lo.y && ndc.y <= hi.y && ndc.z >= -1.0f &&
            ndc.z <= 1.0f) {
            out.push_back(id);
        }
    }
    return out;
}

BoneId cyclePick(const std::vector<PickHit>& hits, BoneId currentlySelected) {
    if (hits.empty()) {
        return kNoBone;
    }
    const auto it =
        std::find_if(hits.begin(), hits.end(), [&](const PickHit& h) { return h.id == currentlySelected; });
    if (it == hits.end()) {
        return hits.front().id;
    }
    const std::size_t next = (static_cast<std::size_t>(it - hits.begin()) + 1) % hits.size();
    return hits[next].id;
}

}  // namespace rigger
