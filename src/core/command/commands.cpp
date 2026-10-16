// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/command/commands.hpp"

#include <algorithm>

namespace rigger {

namespace {

bool sameIds(const std::vector<SetBoneTransformsCommand::Entry>& a,
             const std::vector<SetBoneTransformsCommand::Entry>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const SetBoneTransformsCommand::Entry& x, const SetBoneTransformsCommand::Entry& y) {
                          return x.id == y.id;
                      });
}

}  // namespace

// --- SetBoneTransformsCommand ----------------------------------------------------------------

SetBoneTransformsCommand::SetBoneTransformsCommand(Target target, std::vector<Entry> entries,
                                                   std::string name, std::string mergeKey)
    : target_(target), entries_(std::move(entries)), name_(std::move(name)), mergeKey_(std::move(mergeKey)) {}

void SetBoneTransformsCommand::apply(EditContext& context, bool forward) const {
    Character& character = *context.character;
    for (const Entry& e : entries_) {
        if (!character.skeleton.contains(e.id)) {
            continue;
        }
        const Transform& value = forward ? e.after : e.before;
        if (target_ == Target::Bind) {
            character.skeleton.setBindLocal(e.id, value);
        } else if (context.pose != nullptr) {
            if (e.id >= context.pose->size()) {
                *context.pose = character.skeleton.bindPose();
            }
            (*context.pose)[e.id] = value;
        }
    }
}

bool SetBoneTransformsCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const SetBoneTransformsCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_ || other->target_ != target_ ||
        !sameIds(entries_, other->entries_)) {
        return false;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].after = other->entries_[i].after;  // l'état de départ reste celui de la première commande
    }
    return true;
}

std::vector<BoneId> SetBoneTransformsCommand::affectedBones() const {
    std::vector<BoneId> ids;
    for (const Entry& e : entries_) {
        ids.push_back(e.id);
    }
    return ids;
}

// --- SetBonePropertiesCommand ----------------------------------------------------------------

SetBonePropertiesCommand::SetBonePropertiesCommand(std::vector<Entry> entries, std::string name,
                                                   std::string mergeKey)
    : entries_(std::move(entries)), name_(std::move(name)), mergeKey_(std::move(mergeKey)) {}

void SetBonePropertiesCommand::apply(EditContext& context, bool forward) const {
    Skeleton& skeleton = context.character->skeleton;
    for (const Entry& e : entries_) {
        if (!skeleton.contains(e.id)) {
            continue;
        }
        const BoneProps& props = forward ? e.after : e.before;
        if (skeleton.bone(e.id).name != props.name) {
            skeleton.rename(e.id, props.name);
        }
        skeleton.setLength(e.id, props.length);
        skeleton.setColor(e.id, props.color);
        skeleton.setFlags(e.id, props.flags);
    }
}

bool SetBonePropertiesCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const SetBonePropertiesCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_ ||
        entries_.size() != other->entries_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id != other->entries_[i].id) {
            return false;
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].after = other->entries_[i].after;
    }
    return true;
}

std::string SetBonePropertiesCommand::detail() const {
    return entries_.size() == 1 ? entries_.front().after.name : std::string();
}

std::vector<BoneId> SetBonePropertiesCommand::affectedBones() const {
    std::vector<BoneId> ids;
    for (const Entry& e : entries_) {
        ids.push_back(e.id);
    }
    return ids;
}

// --- StructureState / StructureCommand -------------------------------------------------------

StructureState StructureState::capture(const Character& character) {
    StructureState state;
    state.skeleton = character.skeleton;
    state.weights.reserve(character.meshes.size());
    for (const Mesh& mesh : character.meshes) {
        state.weights.push_back(mesh.skinWeights);
    }
    state.clips = character.clips;
    state.ikControls = character.ikControls;
    state.mix = character.mix;
    state.deform = character.deform;
    for (const Mesh& mesh : character.meshes) {
        std::vector<BoneId> bones;
        for (const MorphTarget& t : mesh.morphTargets) bones.push_back(t.weightBone);
        state.morphBones.push_back(std::move(bones));
    }
    return state;
}

void StructureState::applyTo(Character& character) const {
    character.skeleton.restoreFrom(skeleton);
    for (std::size_t i = 0; i < character.meshes.size() && i < weights.size(); ++i) {
        character.meshes[i].skinWeights = weights[i];
    }
    character.clips = clips;
    character.ikControls = ikControls;
    character.mix = mix;
    character.deform = deform;
    for (std::size_t m = 0; m < character.meshes.size() && m < morphBones.size(); ++m) {
        auto& targets = character.meshes[m].morphTargets;
        for (std::size_t t = 0; t < targets.size() && t < morphBones[m].size(); ++t)
            targets[t].weightBone = morphBones[m][t];
    }
}

StructureCommand::StructureCommand(std::string name, std::string detail, StructureState before,
                                   StructureState after)
    : name_(std::move(name)),
      detail_(std::move(detail)),
      before_(std::move(before)),
      after_(std::move(after)) {}

namespace {
// La pose suit le nombre d'emplacements du squelette ; les emplacements créés prennent leur bind pose.
void syncPose(EditContext& context) {
    if (context.pose == nullptr) {
        return;
    }
    const Skeleton& skeleton = context.character->skeleton;
    const std::size_t old = context.pose->size();
    context.pose->resize(skeleton.slotCount());
    for (BoneId id = static_cast<BoneId>(old); id < skeleton.slotCount(); ++id) {
        if (skeleton.contains(id)) {
            (*context.pose)[id] = skeleton.bone(id).bindLocal;
        }
    }
}
}  // namespace

void StructureCommand::execute(EditContext& context) {
    after_.applyTo(*context.character);
    syncPose(context);
}

void StructureCommand::undo(EditContext& context) {
    before_.applyTo(*context.character);
    syncPose(context);
}

// --- EditTracksCommand ------------------------------------------------------------------------------

EditTracksCommand::EditTracksCommand(std::size_t clipIndex, std::vector<Entry> entries, double durationBefore,
                                     double durationAfter, std::string name, std::string mergeKey)
    : clipIndex_(clipIndex),
      entries_(std::move(entries)),
      durationBefore_(durationBefore),
      durationAfter_(durationAfter),
      name_(std::move(name)),
      mergeKey_(std::move(mergeKey)) {}

void EditTracksCommand::apply(EditContext& context, bool forward) const {
    auto& clips = context.character->clips;
    if (clipIndex_ >= clips.size()) {
        return;
    }
    AnimationClip& clip = clips[clipIndex_];
    for (const Entry& e : entries_) {
        const BoneTracks& tracks = forward ? e.after : e.before;
        if (tracks.empty()) {
            clip.tracks.erase(e.id);
        } else {
            clip.tracks[e.id] = tracks;
        }
    }
    clip.duration = forward ? durationAfter_ : durationBefore_;
}

bool EditTracksCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const EditTracksCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_ ||
        other->clipIndex_ != clipIndex_ || other->entries_.size() != entries_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id != other->entries_[i].id) {
            return false;
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].after = other->entries_[i].after;
    }
    durationAfter_ = other->durationAfter_;
    return true;
}

std::vector<BoneId> EditTracksCommand::affectedBones() const {
    std::vector<BoneId> ids;
    for (const Entry& e : entries_) {
        ids.push_back(e.id);
    }
    return ids;
}

// --- Réglages de clip / liste de clips / poses -------------------------------------------------------

ClipMeta ClipMeta::of(const AnimationClip& clip) {
    return {clip.name, clip.duration, clip.fps, clip.loop, clip.markers, clip.rootMotion, clip.rootBone};
}

void ClipMeta::applyTo(AnimationClip& clip) const {
    clip.name = name;
    clip.duration = duration;
    clip.fps = fps;
    clip.loop = loop;
    clip.markers = markers;
    clip.rootMotion = rootMotion;
    clip.rootBone = rootBone;
}

EditClipMetaCommand::EditClipMetaCommand(std::size_t clipIndex, ClipMeta before, ClipMeta after,
                                         std::string name)
    : clipIndex_(clipIndex), before_(std::move(before)), after_(std::move(after)), name_(std::move(name)) {}

void EditClipMetaCommand::execute(EditContext& context) {
    if (clipIndex_ < context.character->clips.size()) {
        after_.applyTo(context.character->clips[clipIndex_]);
    }
}

void EditClipMetaCommand::undo(EditContext& context) {
    if (clipIndex_ < context.character->clips.size()) {
        before_.applyTo(context.character->clips[clipIndex_]);
    }
}

EditClipListCommand::EditClipListCommand(std::vector<AnimationClip> before, std::vector<AnimationClip> after,
                                         std::string name, std::string detail)
    : before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)) {}

void EditClipListCommand::execute(EditContext& context) { context.character->clips = after_; }
void EditClipListCommand::undo(EditContext& context) { context.character->clips = before_; }

EditPoseLibraryCommand::EditPoseLibraryCommand(std::vector<NamedPose> before, std::vector<NamedPose> after,
                                               std::string name, std::string detail)
    : before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)) {}

void EditPoseLibraryCommand::execute(EditContext& context) { context.character->poses = after_; }
void EditPoseLibraryCommand::undo(EditContext& context) { context.character->poses = before_; }

EditIKControlsCommand::EditIKControlsCommand(std::vector<IKControl> before, std::vector<IKControl> after,
                                             std::string name, std::string detail, std::string mergeKey)
    : before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)),
      mergeKey_(std::move(mergeKey)) {}

void EditIKControlsCommand::execute(EditContext& context) { context.character->ikControls = after_; }
void EditIKControlsCommand::undo(EditContext& context) { context.character->ikControls = before_; }

bool EditIKControlsCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const EditIKControlsCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_) {
        return false;
    }
    after_ = other->after_;
    return true;
}

EditWeightsCommand::EditWeightsCommand(std::vector<MeshWeightDelta> deltas, std::string name,
                                       std::string detail)
    : deltas_(std::move(deltas)), name_(std::move(name)), detail_(std::move(detail)) {}

std::size_t EditWeightsCommand::vertexCount() const {
    std::size_t total = 0;
    for (const MeshWeightDelta& d : deltas_) total += d.vertices.size();
    return total;
}

void EditWeightsCommand::apply(EditContext& context, bool forward) const {
    for (const MeshWeightDelta& delta : deltas_) {
        if (delta.mesh >= context.character->meshes.size()) continue;
        Mesh& mesh = context.character->meshes[delta.mesh];
        for (const VertexWeightDelta& v : delta.vertices) {
            if (v.vertex < mesh.skinWeights.size()) mesh.skinWeights[v.vertex] = forward ? v.after : v.before;
        }
    }
}

std::vector<MeshWeightDelta> diffWeights(const Character& character,
                                         const std::vector<std::vector<SkinWeight>>& before) {
    std::vector<MeshWeightDelta> out;
    for (std::size_t m = 0; m < character.meshes.size() && m < before.size(); ++m) {
        const auto& now = character.meshes[m].skinWeights;
        MeshWeightDelta delta;
        delta.mesh = m;
        for (std::size_t v = 0; v < now.size() && v < before[m].size(); ++v) {
            if (now[v].joints != before[m][v].joints || now[v].weights != before[m][v].weights) {
                delta.vertices.push_back({static_cast<std::uint32_t>(v), before[m][v], now[v]});
            }
        }
        if (!delta.vertices.empty()) out.push_back(std::move(delta));
    }
    return out;
}

EditMixCommand::EditMixCommand(AnimationMix before, AnimationMix after, std::string name, std::string detail,
                               std::string mergeKey)
    : before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)),
      mergeKey_(std::move(mergeKey)) {}

void EditMixCommand::execute(EditContext& context) { context.character->mix = after_; }
void EditMixCommand::undo(EditContext& context) { context.character->mix = before_; }

bool EditMixCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const EditMixCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_) return false;
    after_ = other->after_;
    return true;
}

EditDeformCommand::EditDeformCommand(DeformRig before, DeformRig after, std::string name, std::string detail,
                                     std::string mergeKey)
    : before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)),
      mergeKey_(std::move(mergeKey)) {}

void EditDeformCommand::execute(EditContext& context) { context.character->deform = after_; }
void EditDeformCommand::undo(EditContext& context) { context.character->deform = before_; }

bool EditDeformCommand::merge(const Command& next) {
    const auto* other = dynamic_cast<const EditDeformCommand*>(&next);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_) return false;
    after_ = other->after_;
    return true;
}

EditMorphDriversCommand::EditMorphDriversCommand(std::size_t mesh, std::size_t target,
                                                 std::vector<MorphDriver> before,
                                                 std::vector<MorphDriver> after, std::string name,
                                                 std::string detail)
    : mesh_(mesh),
      target_(target),
      before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)),
      detail_(std::move(detail)) {}

void EditMorphDriversCommand::set(EditContext& context, const std::vector<MorphDriver>& drivers) const {
    if (mesh_ < context.character->meshes.size() &&
        target_ < context.character->meshes[mesh_].morphTargets.size()) {
        context.character->meshes[mesh_].morphTargets[target_].drivers = drivers;
    }
}
void EditMorphDriversCommand::execute(EditContext& context) { set(context, after_); }
void EditMorphDriversCommand::undo(EditContext& context) { set(context, before_); }

}  // namespace rigger
