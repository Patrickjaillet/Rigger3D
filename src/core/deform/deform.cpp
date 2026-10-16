// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/deform/deform.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "core/util/profiler.hpp"

namespace rigger {

namespace {

glm::quat aim(const glm::vec3& a, const glm::vec3& b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return glm::quat(1, 0, 0, 0);
    const glm::vec3 u = a / la, v = b / lb;
    const float d = glm::dot(u, v);
    if (d < -0.999999f) return fromTo(a, b);
    const glm::vec3 c = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::quat rotationOf(const glm::mat4& m) { return glm::normalize(Transform::fromMat4(m).r); }

void ensurePose(const Skeleton& skeleton, Pose& pose) {
    if (pose.size() >= skeleton.slotCount()) return;
    const std::size_t old = pose.size();
    pose.resize(skeleton.slotCount());
    for (const BoneId id : skeleton.order()) {
        if (id >= old) pose[id] = skeleton.bone(id).bindLocal;
    }
}

bool consecutive(const Skeleton& skeleton, const std::vector<BoneId>& bones) {
    if (bones.empty()) return false;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (!skeleton.contains(bones[i])) return false;
        if (i > 0 && skeleton.bone(bones[i]).parent != bones[i - 1]) return false;
    }
    return true;
}

}  // namespace

// --- Twist
// ---------------------------------------------------------------------------------------------------------------

TwistChain makeTwistChain(BoneId source, const std::vector<BoneId>& twistBones, const glm::vec3& axis) {
    TwistChain chain;
    chain.source = source;
    chain.bones = twistBones;
    chain.axis = axis;
    for (std::size_t i = 0; i < twistBones.size(); ++i) {
        chain.factors.push_back(static_cast<float>(i + 1) / static_cast<float>(twistBones.size() + 1));
    }
    return chain;
}

void applyTwistChains(const Skeleton& skeleton, Pose& pose, const std::vector<TwistChain>& chains) {
    ensurePose(skeleton, pose);
    for (const TwistChain& chain : chains) {
        if (!chain.enabled || !skeleton.contains(chain.source) || glm::length(chain.axis) < 1e-6f) continue;
        glm::quat rel =
            glm::normalize(glm::inverse(skeleton.bone(chain.source).bindLocal.r) * pose[chain.source].r);
        if (rel.w < 0.0f) rel = -rel;
        const glm::quat twist = swingTwist(rel, glm::normalize(chain.axis)).twist;
        for (std::size_t i = 0; i < chain.bones.size(); ++i) {
            const BoneId id = chain.bones[i];
            if (!skeleton.contains(id)) continue;
            const float f = i < chain.factors.size() ? chain.factors[i] : 0.0f;
            pose[id].r = glm::normalize(skeleton.bone(id).bindLocal.r *
                                        slerpShortest(glm::quat(1, 0, 0, 0), twist, f));
        }
    }
}

// --- Contraintes
// ------------------------------------------------------------------------------------------------------------

Constraint makeChildOf(const Skeleton& skeleton, const Pose& pose, BoneId bone, BoneId target) {
    Constraint c;
    c.kind = ConstraintKind::ChildOf;
    c.bone = bone;
    c.target = target;
    Pose p = pose;
    ensurePose(skeleton, p);
    c.inverse = glm::inverse(computeGlobals(skeleton, p)[target]);
    c.name = "Child Of";
    return c;
}

void applyConstraints(const Skeleton& skeleton, Pose& pose, const std::vector<Constraint>& constraints) {
    ensurePose(skeleton, pose);
    for (const Constraint& c : constraints) {
        if (!c.enabled || c.influence <= 0.0f || !skeleton.contains(c.bone)) continue;
        const bool needsTarget = c.kind != ConstraintKind::Limit;
        if (needsTarget && !skeleton.contains(c.target)) continue;
        if (c.kind == ConstraintKind::Limit) {
            Transform t = pose[c.bone];
            Transform limited = t;
            if (c.limitLocation) limited.t = glm::clamp(t.t, c.locationMin, c.locationMax);
            if (c.limitScale) limited.s = glm::clamp(t.s, c.scaleMin, c.scaleMax);
            if (c.limitRotation) {
                limited.r = fromEulerXYZ(glm::clamp(toEulerXYZ(t.r), c.rotationMin, c.rotationMax));
            }
            pose[c.bone] = lerp(t, limited, std::min(c.influence, 1.0f));
            continue;
        }
        const std::vector<glm::mat4> g = computeGlobals(skeleton, pose);
        const BoneId parent = skeleton.bone(c.bone).parent;
        const glm::mat4 parentWorld = parent == kNoBone ? glm::mat4(1.0f) : g[parent];
        const Transform bone = Transform::fromMat4(g[c.bone]);
        const Transform target = Transform::fromMat4(g[c.target]);
        glm::mat4 desired(1.0f);
        switch (c.kind) {
            case ConstraintKind::CopyTransform: {
                Transform d = bone;
                if (c.copyLocation) d.t = target.t;
                if (c.copyRotation) d.r = target.r;
                if (c.copyScale) d.s = target.s;
                desired = d.toMat4();
                break;
            }
            case ConstraintKind::ChildOf:
                desired = g[c.target] * c.inverse * g[c.bone];
                break;
            case ConstraintKind::TrackTo: {
                Transform d = bone;
                const glm::vec3 axis =
                    glm::length(c.aimAxis) > 1e-6f ? glm::normalize(c.aimAxis) : glm::vec3(0, 1, 0);
                d.r = glm::normalize(aim(bone.r * axis, target.t - bone.t) * bone.r);
                desired = d.toMat4();
                break;
            }
            case ConstraintKind::Limit:
                break;
        }
        const Transform local = Transform::fromMat4(glm::inverse(parentWorld) * desired);
        pose[c.bone] = lerp(pose[c.bone], local, std::min(c.influence, 1.0f));
    }
}

// --- Spring bones
// ----------------------------------------------------------------------------------------------------------------

namespace {

struct ChainGeometry {
    std::vector<glm::vec3> head;  // origine de chaque os
    std::vector<glm::vec3> tip;   // pointe de chaque os
    std::vector<float> length;
};

ChainGeometry measure(const Skeleton& skeleton, const std::vector<glm::mat4>& g,
                      const std::vector<BoneId>& bones) {
    ChainGeometry geo;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const glm::vec3 head(g[bones[i]][3]);
        const glm::vec3 tip =
            i + 1 < bones.size()
                ? glm::vec3(g[bones[i + 1]][3])
                : glm::vec3(g[bones[i]] *
                            glm::vec4(0, std::max(skeleton.bone(bones[i]).length, 0.01f), 0, 1));
        geo.head.push_back(head);
        geo.tip.push_back(tip);
        geo.length.push_back(glm::length(tip - head));
    }
    return geo;
}

}  // namespace

void stepSprings(const Skeleton& skeleton, Pose& pose, const DeformRig& rig, SpringState& state, double dt,
                 int substeps) {
    RIGGER_PROFILE_SCOPE("stepSprings");
    if (!rig.springsEnabled || rig.springs.empty()) return;
    ensurePose(skeleton, pose);
    const std::vector<glm::mat4> animated = computeGlobals(skeleton, pose);
    std::vector<glm::vec3> colliderCenter;
    for (const SpringCollider& c : rig.colliders) {
        colliderCenter.push_back(
            skeleton.contains(c.bone) ? glm::vec3(animated[c.bone] * glm::vec4(c.offset, 1.0f)) : c.offset);
    }
    if (!state.valid || state.position.size() != rig.springs.size()) {
        state.position.assign(rig.springs.size(), {});
        state.previous.assign(rig.springs.size(), {});
        for (std::size_t s = 0; s < rig.springs.size(); ++s) {
            if (!consecutive(skeleton, rig.springs[s].bones)) continue;
            state.position[s] = measure(skeleton, animated, rig.springs[s].bones).tip;
            state.previous[s] = state.position[s];
        }
        state.valid = true;
    }
    dt = std::clamp(dt, 0.0, 0.1);
    substeps = std::max(substeps, 1);
    const double h = dt / substeps;
    for (std::size_t s = 0; s < rig.springs.size(); ++s) {
        const SpringChain& chain = rig.springs[s];
        if (!chain.enabled || !consecutive(skeleton, chain.bones)) continue;
        const ChainGeometry rest = measure(skeleton, animated, chain.bones);
        auto& p = state.position[s];
        auto& prev = state.previous[s];
        if (p.size() != chain.bones.size()) {  // chaîne modifiée entre deux pas
            p = rest.tip;
            prev = rest.tip;
        }
        const float stiff =
            1.0f - std::pow(1.0f - std::clamp(chain.stiffness, 0.0f, 0.999f), static_cast<float>(h * 60.0));
        const float keep =
            std::pow(1.0f - std::clamp(chain.damping, 0.0f, 0.999f), static_cast<float>(h * 60.0));
        const glm::vec3 gravity(0.0f, -9.81f * chain.gravity, 0.0f);
        for (int sub = 0; sub < substeps; ++sub) {
            for (std::size_t i = 0; i < p.size(); ++i) {
                const glm::vec3 velocity = (p[i] - prev[i]) * keep;
                prev[i] = p[i];
                p[i] += velocity + gravity * static_cast<float>(h * h) + (rest.tip[i] - p[i]) * stiff;
            }
            for (int iteration = 0; iteration < 4; ++iteration) {
                for (std::size_t i = 0; i < p.size(); ++i) {
                    const glm::vec3 head = i == 0 ? rest.head[0] : p[i - 1];
                    glm::vec3 dir = p[i] - head;
                    const float len = glm::length(dir);
                    dir = len > 1e-8f ? dir / len : glm::normalize(rest.tip[i] - rest.head[i]);
                    p[i] = head + dir * rest.length[i];
                    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
                        const glm::vec3 d = p[i] - colliderCenter[c];
                        const float dl = glm::length(d);
                        if (dl < rig.colliders[c].radius && dl > 1e-8f)
                            p[i] = colliderCenter[c] + d / dl * rig.colliders[c].radius;
                    }
                }
            }
        }
        // Positions simulées -> rotations locales (racine vers le bout, FK recalculée à chaque os).
        for (std::size_t i = 0; i < chain.bones.size(); ++i) {
            const std::vector<glm::mat4> g = computeGlobals(skeleton, pose);
            const BoneId id = chain.bones[i];
            const ChainGeometry now = measure(skeleton, g, chain.bones);
            const glm::quat delta = aim(now.tip[i] - now.head[i], p[i] - now.head[i]);
            const BoneId parent = skeleton.bone(id).parent;
            const glm::quat parentRot = parent == kNoBone ? glm::quat(1, 0, 0, 0) : rotationOf(g[parent]);
            pose[id].r = glm::normalize(glm::inverse(parentRot) * delta * rotationOf(g[id]));
        }
    }
}

void applyStatelessDeform(const Skeleton& skeleton, Pose& pose, const DeformRig& rig) {
    applyTwistChains(skeleton, pose, rig.twists);
    applyConstraints(skeleton, pose, rig.constraints);
}

bool pruneDeformRig(const Skeleton& skeleton, DeformRig& rig) {
    bool changed = false;
    const auto removeIf = [&](auto& list, auto pred) {
        const auto it = std::remove_if(list.begin(), list.end(), pred);
        changed = changed || it != list.end();
        list.erase(it, list.end());
    };
    removeIf(rig.twists, [&](const TwistChain& t) {
        return !skeleton.contains(t.source) || t.bones.empty() ||
               std::any_of(t.bones.begin(), t.bones.end(), [&](BoneId b) { return !skeleton.contains(b); });
    });
    removeIf(rig.constraints, [&](const Constraint& c) {
        return !skeleton.contains(c.bone) ||
               (c.kind != ConstraintKind::Limit && !skeleton.contains(c.target));
    });
    removeIf(rig.springs, [&](const SpringChain& s) { return !consecutive(skeleton, s.bones); });
    removeIf(rig.colliders,
             [&](const SpringCollider& c) { return c.bone != kNoBone && !skeleton.contains(c.bone); });
    return changed;
}

}  // namespace rigger
