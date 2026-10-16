// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/character/edit.hpp"

#include <algorithm>
#include <set>

#include "core/skinning/weights.hpp"

namespace rigger {

namespace {

// Plus proche ancêtre de `id` qui n'est pas dans `removed` (kNoBone s'il n'y en a pas).
BoneId survivingAncestor(const Skeleton& skeleton, BoneId id, const std::set<BoneId>& removed) {
    BoneId cur = skeleton.bone(id).parent;
    while (cur != kNoBone && removed.count(cur) != 0) {
        cur = skeleton.bone(cur).parent;
    }
    return cur;
}

void remapWeights(Character& character, const std::set<BoneId>& removed) {
    const Skeleton& skeleton = character.skeleton;
    // Os de repli quand un sommet perd toutes ses influences : le premier os conservé dans l'ordre
    // hiérarchique.
    BoneId fallback = kNoBone;
    for (const BoneId id : skeleton.order()) {
        if (removed.count(id) == 0) {
            fallback = id;
            break;
        }
    }
    for (Mesh& mesh : character.meshes) {
        for (SkinWeight& skin : mesh.skinWeights) {
            bool touched = false;
            for (std::size_t k = 0; k < 4; ++k) {
                if (skin.weights[k] > 0.0f && removed.count(skin.joints[k]) != 0) {
                    touched = true;
                    skin.joints[k] = survivingAncestor(skeleton, skin.joints[k], removed);
                    if (skin.joints[k] == kNoBone) {
                        skin.weights[k] = 0.0f;
                    }
                }
            }
            if (!touched) {
                continue;
            }
            // Fusionne les doublons créés par le remplacement, puis renormalise.
            for (std::size_t a = 0; a < 4; ++a) {
                for (std::size_t b = a + 1; b < 4; ++b) {
                    if (skin.weights[a] > 0.0f && skin.joints[a] == skin.joints[b] &&
                        skin.weights[b] > 0.0f) {
                        skin.weights[a] += skin.weights[b];
                        skin.weights[b] = 0.0f;
                        skin.joints[b] = kNoBone;
                    }
                }
            }
            float sum = 0.0f;
            for (const float w : skin.weights) {
                sum += w;
            }
            if (sum <= 0.0f) {
                skin = SkinWeight{};
                if (fallback != kNoBone) {
                    skin.joints[0] = fallback;
                    skin.weights[0] = 1.0f;
                }
            } else {
                for (std::size_t k = 0; k < 4; ++k) {
                    if (skin.weights[k] <= 0.0f) {
                        skin.weights[k] = 0.0f;
                        skin.joints[k] = kNoBone;
                    }
                }
                normalizeWeights(skin);
            }
        }
    }
}

}  // namespace

Result<void> removeBoneClean(Character& character, BoneId id, RemoveMode mode) {
    Skeleton& skeleton = character.skeleton;
    if (!skeleton.contains(id)) {
        return Error{"unknown bone"};
    }
    std::set<BoneId> removed{id};
    if (mode == RemoveMode::WithDescendants) {
        for (const BoneId other : skeleton.order()) {
            if (skeleton.isDescendantOf(other, id)) {
                removed.insert(other);
            }
        }
    }
    for (const BoneId gone : removed) {
        if (hasFlag(skeleton.bone(gone).flags, BoneFlags::Morph)) {
            return Error{"blend shape weight bones cannot be deleted"};
        }
    }
    if (removed.size() >= skeleton.boneCount()) {
        return Error{"cannot delete every bone of the skeleton"};
    }
    remapWeights(character, removed);
    auto result = skeleton.removeBone(id, mode);
    if (!result) {
        return result;
    }
    for (AnimationClip& clip : character.clips) {
        for (const BoneId gone : removed) {
            clip.tracks.erase(gone);
        }
    }
    pruneIKControls(character.skeleton, character.ikControls);
    pruneDeformRig(character.skeleton, character.deform);
    return {};
}

Result<BoneId> subdivideBone(Character& character, BoneId id) {
    Skeleton& skeleton = character.skeleton;
    if (!skeleton.contains(id)) {
        return Error{"unknown bone"};
    }
    const std::vector<BoneId> children = skeleton.children(id);
    const glm::vec3 tail = children.empty() ? glm::vec3(0.0f, skeleton.bone(id).length, 0.0f)
                                            : skeleton.bone(children.front()).bindLocal.t;
    Transform middle;
    middle.t = tail * 0.5f;
    const std::string name = skeleton.bone(id).name + ".sub";
    auto created = skeleton.addBone(name, id, middle);
    if (!created) {
        return created.error();
    }
    const BoneId mid = created.value();
    const float half = std::max(skeleton.bone(id).length * 0.5f, 1e-3f);
    skeleton.setLength(mid, half);
    skeleton.setLength(id, half);
    skeleton.setColor(mid, skeleton.bone(id).color);
    skeleton.setFlags(mid, skeleton.bone(id).flags);
    for (const BoneId child : children) {
        auto moved = skeleton.reparent(child, mid, true);
        if (!moved) {
            return moved.error();
        }
    }
    return mid;
}

}  // namespace rigger
