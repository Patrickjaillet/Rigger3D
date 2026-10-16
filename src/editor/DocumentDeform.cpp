// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Document : déformations avancées (Phase 14) : pose évaluée (IK, twist, contraintes, springs), blend shapes,
// spring bones (simulation + bake), auto-rig humanoïde.
#include <algorithm>
#include <cmath>

#include "core/deform/morph.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skinning/paint.hpp"
#include "core/util/profiler.hpp"
#include "editor/Document.hpp"

namespace rigger::editor {

namespace {
const DeformRig kNoDeform;
}

const DeformRig& Document::deform() const { return character_ ? character_->deform : kNoDeform; }

bool Document::springsActive() const {
    if (!character_ || mode_ != Mode::Animate) return false;
    const DeformRig& d = character_->deform;
    return d.springsEnabled &&
           std::any_of(d.springs.begin(), d.springs.end(), [](const SpringChain& s) { return s.enabled; });
}

const Pose& Document::pose() const {
    RIGGER_PROFILE_SCOPE("Document::pose");
    if (mode_ == Mode::Animate && character_) {
        const DeformRig& d = character_->deform;
        const bool ik = anyIKActive(character_->ikControls);
        const bool stateless = !d.twists.empty() || !d.constraints.empty();
        const bool springs = d.springsEnabled && !springRotations_.empty();
        if (ik || stateless || springs) {
            evaluated_ = pose_;
            applyIKControls(character_->skeleton, evaluated_, character_->ikControls, time_);
            applyStatelessDeform(character_->skeleton, evaluated_, d);
            if (springs) {
                for (const auto& [id, q] : springRotations_) {
                    if (id < evaluated_.size()) evaluated_[id].r = q;
                }
            }
            return evaluated_;
        }
    }
    return pose_;
}

void Document::resetSprings() {
    springState_.reset();
    springRotations_.clear();
}

void Document::stepSimulation(double dt) {
    if (!springsActive()) {
        springRotations_.clear();
        return;
    }
    Pose animated = pose_;
    applyIKControls(character_->skeleton, animated, character_->ikControls, time_);
    applyStatelessDeform(character_->skeleton, animated, character_->deform);
    stepSprings(character_->skeleton, animated, character_->deform, springState_, dt);
    springRotations_.clear();
    for (const SpringChain& chain : character_->deform.springs) {
        if (!chain.enabled) continue;
        for (const BoneId id : chain.bones) {
            if (id < animated.size()) springRotations_[id] = animated[id].r;
        }
    }
}

bool Document::editDeform(const std::function<void(DeformRig&)>& modify, const char* name,
                          const std::string& mergeKey) {
    if (!character_) return false;
    DeformRig after = character_->deform;
    modify(after);
    if (after == character_->deform) return false;
    runCommand(std::make_unique<EditDeformCommand>(character_->deform, std::move(after), name, std::string(),
                                                   mergeKey));
    resetSprings();
    emit deformChanged();
    emit poseChanged();
    return true;
}

Result<int> Document::createTwistSetup(BoneId forearm, BoneId hand, int count) {
    if (!character_ || !character_->skeleton.contains(forearm) || !character_->skeleton.contains(hand) ||
        character_->skeleton.bone(hand).parent != forearm) {
        return Error{"choose a forearm bone and its child (the hand)"};
    }
    if (count < 1 || count > 8) return Error{"the number of twist bones must be between 1 and 8"};
    int created = -1;
    history_.beginGroup("Create Twist Bones");
    const auto result = editStructure("Create Twist Bones", [&]() -> Result<void> {
        Skeleton& sk = character_->skeleton;
        const glm::vec3 offset = sk.bone(hand).bindLocal.t;  // de l'avant-bras vers la main, dans son repère
        if (glm::length(offset) < 1e-5f) return Error{"the hand bone is at the same place as the forearm"};
        std::vector<BoneId> twistBones;
        for (int i = 0; i < count; ++i) {
            Transform bind;
            bind.t = offset * (static_cast<float>(i + 1) / static_cast<float>(count + 1));
            const auto id =
                sk.addBone(sk.bone(forearm).name + "_twist" + std::to_string(i + 1), forearm, bind);
            if (!id) return id.error();
            sk.setLength(id.value(), glm::length(offset) / static_cast<float>(count + 1));
            sk.setColor(id.value(), sk.bone(forearm).color);
            twistBones.push_back(id.value());
        }
        TwistChain chain = makeTwistChain(hand, twistBones, glm::normalize(offset));
        chain.name = sk.bone(hand).name;
        character_->deform.twists.push_back(chain);
        created = static_cast<int>(character_->deform.twists.size()) - 1;
        recomputePose();
        emit hierarchyChanged();
        emit deformChanged();
        emit poseChanged();
        return {};
    });
    history_.endGroup();
    if (!result) return result.error();
    return created;
}

Result<int> Document::createSpringChain(BoneId root, int boneCount) {
    if (!character_ || !character_->skeleton.contains(root)) return Error{"unknown bone"};
    SpringChain chain;
    chain.name = character_->skeleton.bone(root).name;
    BoneId cur = root;
    for (int i = 0; i < boneCount && cur != kNoBone; ++i) {
        chain.bones.push_back(cur);
        const auto children = character_->skeleton.children(cur);
        cur = children.empty() ? kNoBone : children.front();
    }
    if (chain.bones.empty()) return Error{"no bone to simulate"};
    if (!editDeform([&](DeformRig& d) { d.springs.push_back(chain); }, "Create Spring Chain"))
        return Error{"nothing changed"};
    return static_cast<int>(character_->deform.springs.size()) - 1;
}

bool Document::addSpringCollider(BoneId bone, const glm::vec3& offset, float radius) {
    SpringCollider c;
    c.bone = bone;
    c.offset = offset;
    c.radius = std::max(radius, 0.001f);
    return editDeform([&](DeformRig& d) { d.colliders.push_back(c); }, "Add Collider");
}

Result<int> Document::addConstraint(Constraint constraint) {
    if (!character_ || !character_->skeleton.contains(constraint.bone))
        return Error{"unknown constrained bone"};
    if (constraint.kind != ConstraintKind::Limit && !character_->skeleton.contains(constraint.target)) {
        return Error{"this constraint needs a target bone"};
    }
    if (constraint.kind == ConstraintKind::ChildOf) {
        constraint.inverse =
            makeChildOf(character_->skeleton, pose_, constraint.bone, constraint.target).inverse;
    }
    if (!editDeform([&](DeformRig& d) { d.constraints.push_back(constraint); }, "Add Constraint"))
        return Error{"nothing changed"};
    return static_cast<int>(character_->deform.constraints.size()) - 1;
}

bool Document::setMorphWeight(std::size_t meshIndex, std::size_t targetIndex, float value) {
    if (!character_ || meshIndex >= character_->meshes.size() ||
        targetIndex >= character_->meshes[meshIndex].morphTargets.size()) {
        return false;
    }
    const BoneId bone = character_->meshes[meshIndex].morphTargets[targetIndex].weightBone;
    if (!character_->skeleton.contains(bone)) return false;
    Transform t = boneLocal(bone);
    t.t.x = value;
    return setBonesLocal({{bone, t}},
                         "morph:" + std::to_string(bone));  // auto-key et undo comme n'importe quel os
}

bool Document::editMorphDrivers(std::size_t meshIndex, std::size_t targetIndex,
                                std::vector<MorphDriver> drivers) {
    if (!character_ || meshIndex >= character_->meshes.size() ||
        targetIndex >= character_->meshes[meshIndex].morphTargets.size()) {
        return false;
    }
    const auto before = character_->meshes[meshIndex].morphTargets[targetIndex].drivers;
    if (before == drivers) return false;
    runCommand(std::make_unique<EditMorphDriversCommand>(
        meshIndex, targetIndex, before, std::move(drivers), "Edit Corrective Shape",
        character_->meshes[meshIndex].morphTargets[targetIndex].name));
    emit meshDataChanged();
    emit deformChanged();
    emit poseChanged();
    return true;
}

Result<void> Document::bakeSprings(int firstFrame, int lastFrame, bool disableAfter) {
    AnimationClip* live = mutableClip();
    if (!character_ || live == nullptr) return Error{"there is no clip to bake into"};
    if (lastFrame < firstFrame) return Error{"invalid frame range"};
    std::vector<BoneId> bones;
    for (const SpringChain& chain : character_->deform.springs) {
        if (!chain.enabled) continue;
        for (const BoneId id : chain.bones) bones.push_back(id);
    }
    if (bones.empty() || !character_->deform.springsEnabled) return Error{"there is no active spring chain"};
    const Skeleton& skeleton = character_->skeleton;
    const DeformRig rig = character_->deform;
    const std::vector<IKControl> controls = character_->ikControls;
    const double fps = live->fps;
    history_.beginGroup("Bake Springs");
    editClipTracks(
        "Bake Springs", {}, bones,
        [&](AnimationClip& work) {
            struct Sample {
                BoneId id;
                double time;
                glm::quat rotation;
            };
            std::vector<Sample> samples;
            SpringState state;  // la simulation repart de la pose animée de la première image
            for (int frame = firstFrame; frame <= lastFrame; ++frame) {
                const double t = frame / fps;
                Pose p = samplePose(*live, skeleton, t);
                applyIKControls(skeleton, p, controls, t);
                applyStatelessDeform(skeleton, p, rig);
                stepSprings(skeleton, p, rig, state, 1.0 / fps, 4);
                for (const BoneId id : bones) samples.push_back({id, t, p[id].r});
            }
            for (const Sample& s : samples) {
                Keyframe<glm::quat> key;
                key.time = s.time;
                key.value = s.rotation;
                key.mode = Interpolation::Linear;
                insertKey(work.tracks[s.id].rotation, key);
            }
            extendDuration(work);
        },
        true);
    if (disableAfter) editDeform([](DeformRig& d) { d.springsEnabled = false; }, "Bake Springs");
    history_.endGroup();
    return {};
}

Landmarks Document::guessCharacterLandmarks() const {
    std::vector<glm::vec3> points;
    if (character_) {
        for (std::size_t m = 0; m < character_->meshes.size(); ++m) {
            for (const Vertex& v : skinnedVertices(m)) points.push_back(v.position);
        }
    }
    return guessLandmarks(points);
}

Result<void> Document::autoRigHumanoid(const Landmarks& landmarks, const AutoSkinOptions& skinning,
                                       bool withIK) {
    if (!character_ || character_->meshes.empty()) return Error{"there is no mesh to rig"};
    if (const std::string problem = validateLandmarks(landmarks); !problem.empty()) return Error{problem};
    history_.beginGroup("Auto-Rig");
    auto result = editStructure("Auto-Rig", [&]() -> Result<void> {
        Character& c = *character_;
        Skeleton fresh = buildHumanoidSkeleton(landmarks);
        // Os de poids des blend shapes : conservés (nouvelle numérotation).
        struct Keep {
            std::size_t mesh, target;
            std::string name;
            Transform bind;
        };
        std::vector<Keep> keep;
        for (std::size_t m = 0; m < c.meshes.size(); ++m) {
            for (std::size_t t = 0; t < c.meshes[m].morphTargets.size(); ++t) {
                const BoneId old = c.meshes[m].morphTargets[t].weightBone;
                if (c.skeleton.contains(old))
                    keep.push_back({m, t, c.skeleton.bone(old).name, c.skeleton.bone(old).bindLocal});
            }
        }
        for (const Keep& k : keep) {
            const auto id = fresh.addBone(k.name, kNoBone, k.bind);
            if (!id) return id.error();
            fresh.setFlags(id.value(), BoneFlags::Control | BoneFlags::Morph);
            fresh.setLength(id.value(), 0.05f);
            c.meshes[k.mesh].morphTargets[k.target].weightBone = id.value();
        }
        c.skeleton.restoreFrom(fresh);
        c.clips.clear();
        c.ikControls.clear();
        c.mix = AnimationMix{};
        c.deform = DeformRig{};
        for (Mesh& mesh : c.meshes) rigger::autoSkin(mesh, c.skeleton, skinning);
        clipIndex_ = -1;
        selection_.clear();
        time_ = 0.0;
        recomputePose();
        syncPlayer();
        emit hierarchyChanged();
        emit meshDataChanged();
        emit selectionChanged();
        emit clipChanged();
        emit animationChanged();
        emit deformChanged();
        emit poseChanged();
        return {};
    });
    if (result && withIK) createHumanoidIK();
    history_.endGroup();
    return result;
}

}  // namespace rigger::editor
