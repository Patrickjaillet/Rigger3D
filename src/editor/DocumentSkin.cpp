// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Document : weight painting (Phase 11). Pinceau, sélection de sommets, outils globaux. La logique des poids
// vit dans core/skinning/paint.*, les deltas annulables dans core/command (EditWeightsCommand).
#include <algorithm>
#include <cmath>

#include "core/deform/morph.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skeleton/symmetry.hpp"
#include "core/skinning/lbs.hpp"
#include "editor/Document.hpp"

namespace rigger::editor {

void Document::setBrush(const Brush& brush) {
    brush_ = brush;
    brush_.radius = std::clamp(brush_.radius, 0.001f, 10.0f);
    brush_.strength = std::clamp(brush_.strength, 0.0f, 1.0f);
    brush_.value = std::clamp(brush_.value, 0.0f, 1.0f);
    emit paintSettingsChanged();
}

void Document::setAutoNormalizeWeights(bool enabled) {
    autoNormalize_ = enabled;
    emit paintSettingsChanged();
}

void Document::setPaintOnlyVisible(bool enabled) {
    paintVisibleOnly_ = enabled;
    emit paintSettingsChanged();
}

void Document::setPaintActive(bool active) {
    if (active) vertexSelectMode_ = false;
    paintActive_ = active;
    emit paintSettingsChanged();
}

void Document::setVertexSelectMode(bool enabled) {
    if (enabled) paintActive_ = false;
    vertexSelectMode_ = enabled;
    emit paintSettingsChanged();
}

void Document::setVertexSelectShape(VertexSelectShape shape) {
    vertexSelectShape_ = shape;
    emit paintSettingsChanged();
}

BoneId Document::activeBone() const {
    if (!character_) return kNoBone;
    for (const BoneId id : selection_) {
        if (character_->skeleton.contains(id) &&
            !hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Control)) {
            return id;
        }
    }
    return kNoBone;
}

void Document::setWeightLocked(BoneId id, bool locked) {
    weightLocks_.set(id, locked);
    emit weightLocksChanged();
}

std::vector<Vertex> Document::skinnedVertices(std::size_t meshIndex) const {
    if (!character_ || meshIndex >= character_->meshes.size()) return {};
    const Mesh& mesh = character_->meshes[meshIndex];
    const Pose& posed = pose();
    const std::vector<float> weights = morphWeights(mesh, character_->skeleton, posed);
    if (anyMorphActive(weights)) {  // blend shapes : forme de repos déformée avant le skinning
        Mesh morphed = mesh;
        morphed.vertices = applyMorphs(mesh, weights);
        if (!mesh.hasSkinning()) return morphed.vertices;
        auto result = skinLbs(morphed, computeSkinMatrices(character_->skeleton, posed));
        return result ? std::move(result.value()) : morphed.vertices;
    }
    if (!mesh.hasSkinning()) return mesh.vertices;
    auto result = skinLbs(mesh, computeSkinMatrices(character_->skeleton, posed));
    return result ? std::move(result.value()) : mesh.vertices;
}

std::optional<glm::vec3> Document::raycastSurface(const Ray& ray) const {
    if (!character_) return std::nullopt;
    std::optional<float> best;
    for (std::size_t m = 0; m < character_->meshes.size(); ++m) {
        const Mesh& mesh = character_->meshes[m];
        const std::vector<Vertex> verts = skinnedVertices(m);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto hit =
                intersectTriangle(ray, verts[mesh.indices[i]].position, verts[mesh.indices[i + 1]].position,
                                  verts[mesh.indices[i + 2]].position, false);
            if (hit && hit->t > 0.0f && (!best || hit->t < *best)) best = hit->t;
        }
    }
    return best ? std::optional<glm::vec3>(ray.at(*best)) : std::nullopt;
}

// --- Coup de pinceau ---------------------------------------------------------------------------

bool Document::beginPaintStroke() {
    if (!character_ || strokeActive_ || manipulating_ || activeBone() == kNoBone) return false;
    strokeActive_ = true;
    strokeBefore_.clear();
    strokeTopology_.clear();
    strokeMirror_.clear();
    strokeSkinned_.clear();
    for (std::size_t m = 0; m < character_->meshes.size(); ++m) {
        const Mesh& mesh = character_->meshes[m];
        strokeBefore_.push_back(mesh.skinWeights);
        strokeTopology_.push_back(MeshTopology::build(mesh));
        std::vector<glm::vec3> rest;
        rest.reserve(mesh.vertices.size());
        for (const Vertex& v : mesh.vertices) rest.push_back(v.position);
        strokeMirror_.push_back(
            symmetryX_ ? mirrorVertexMap(rest, std::max(mesh.computeBounds().radius * 1e-3f, 1e-4f))
                       : std::vector<std::uint32_t>{});
        strokeSkinned_.push_back(skinnedVertices(m));
    }
    return true;
}

void Document::paintAt(const glm::vec3& center, const glm::vec3& eye) {
    if (!strokeActive_ || !character_) return;
    const BoneId bone = activeBone();
    if (bone == kNoBone) return;
    BoneId mirrorBone = kNoBone;
    if (symmetryX_) {
        mirrorBone = findMirrorBone(character_->skeleton, bone);
        if (mirrorBone == kNoBone) mirrorBone = bone;  // os central : le miroir agit sur le même os
    }
    bool changedAny = false;
    for (std::size_t m = 0; m < character_->meshes.size(); ++m) {
        Mesh& mesh = character_->meshes[m];
        if (!mesh.hasSkinning()) continue;
        std::vector<glm::vec3> positions(strokeSkinned_[m].size());
        std::vector<glm::vec3> normals(strokeSkinned_[m].size());
        for (std::size_t v = 0; v < positions.size(); ++v) {
            positions[v] = strokeSkinned_[m][v].position;
            normals[v] = strokeSkinned_[m][v].normal;
        }
        BrushHits hits = collectBrushHits(positions, center, brush_);
        if (paintVisibleOnly_ && !hits.empty()) {
            std::vector<std::uint32_t> candidates;
            for (const auto& h : hits) candidates.push_back(h.first);
            const auto visible = visibleVertices(positions, normals, mesh.indices, eye, candidates);
            hits.erase(std::remove_if(hits.begin(), hits.end(),
                                      [&](const auto& h) { return visible[h.first] == 0; }),
                       hits.end());
        }
        if (!applyBrushHits(mesh, strokeTopology_[m], hits, brush_, bone, weightLocks_, autoNormalize_)
                 .empty()) {
            changedAny = true;
        }
        if (symmetryX_ && !strokeMirror_[m].empty()) {
            BrushHits mirrored;
            for (const auto& [v, f] : hits) {
                const std::uint32_t twin = strokeMirror_[m][v];
                if (twin != kNoVertex) mirrored.emplace_back(twin, f);
            }
            if (!applyBrushHits(mesh, strokeTopology_[m], mirrored, brush_, mirrorBone, weightLocks_,
                                autoNormalize_)
                     .empty()) {
                changedAny = true;
            }
        }
    }
    if (changedAny) {
        emit meshDataChanged();
        emit poseChanged();
    }
}

void Document::endPaintStroke() {
    if (!strokeActive_) return;
    strokeActive_ = false;
    auto deltas = diffWeights(*character_, strokeBefore_);
    strokeBefore_.clear();
    strokeTopology_.clear();
    strokeMirror_.clear();
    strokeSkinned_.clear();
    if (deltas.empty()) return;
    const BoneId bone = activeBone();
    history_.record(std::make_unique<EditWeightsCommand>(
        std::move(deltas), "Paint Weights",
        bone != kNoBone ? character_->skeleton.bone(bone).name : std::string()));
    history_.breakMerge();
    emit historyChanged();
}

// --- Sélection de sommets ------------------------------------------------------------------------

void Document::setVertexSelection(std::vector<VertexRef> vertices) {
    if (vertices == vertexSelection_) return;
    vertexSelection_ = std::move(vertices);
    emit vertexSelectionChanged();
    emit poseChanged();
}

bool Document::setSelectedVertexWeight(float value) {
    const BoneId bone = activeBone();
    if (!character_ || bone == kNoBone || vertexSelection_.empty()) return false;
    return editWeights(
        "Set Weights",
        [&](Character& c) {
            for (const VertexRef& ref : vertexSelection_) {
                if (ref.mesh < c.meshes.size() && ref.vertex < c.meshes[ref.mesh].skinWeights.size()) {
                    setBoneWeight(c.meshes[ref.mesh].skinWeights[ref.vertex], bone, value, weightLocks_,
                                  autoNormalize_);
                }
            }
        },
        character_->skeleton.bone(bone).name);
}

// --- Outils globaux --------------------------------------------------------------------------------

bool Document::editWeights(const char* name, const std::function<void(Character&)>& edit,
                           const std::string& detail) {
    if (!character_ || strokeActive_) return false;
    std::vector<std::vector<SkinWeight>> before;
    for (const Mesh& mesh : character_->meshes) before.push_back(mesh.skinWeights);
    edit(*character_);
    auto deltas = diffWeights(*character_, before);
    if (deltas.empty()) return false;
    history_.record(std::make_unique<EditWeightsCommand>(std::move(deltas), name, detail));
    history_.breakMerge();
    emit meshDataChanged();
    emit poseChanged();
    emit historyChanged();
    return true;
}

namespace {
std::size_t countVertices(const Character& before, const std::vector<std::vector<SkinWeight>>& snapshot) {
    std::size_t total = 0;
    for (const MeshWeightDelta& d : diffWeights(before, snapshot)) total += d.vertices.size();
    return total;
}
}  // namespace

Result<std::size_t> Document::autoSkin(const AutoSkinOptions& options) {
    if (!character_ || character_->meshes.empty()) return Error{"there is no mesh to skin"};
    std::size_t changed = 0;
    editWeights("Auto Skin", [&](Character& c) {
        std::vector<std::vector<SkinWeight>> snapshot;
        for (const Mesh& m : c.meshes) snapshot.push_back(m.skinWeights);
        for (Mesh& mesh : c.meshes) rigger::autoSkin(mesh, c.skeleton, options);
        changed = countVertices(c, snapshot);
    });
    return changed;
}

Result<std::size_t> Document::normalizeSkinWeights() {
    if (!character_) return Error{"no character"};
    std::size_t changed = 0;
    editWeights("Normalize Weights", [&](Character& c) {
        for (Mesh& m : c.meshes) changed += normalizeAllWeights(m);
    });
    return changed;
}

Result<std::size_t> Document::limitSkinInfluences(std::size_t maxInfluences) {
    if (!character_) return Error{"no character"};
    std::size_t changed = 0;
    editWeights("Limit Influences", [&](Character& c) {
        for (Mesh& m : c.meshes) changed += limitAllInfluences(m, maxInfluences);
    });
    return changed;
}

Result<std::size_t> Document::pruneSkinWeights(float threshold) {
    if (!character_) return Error{"no character"};
    std::size_t changed = 0;
    editWeights("Prune Weights", [&](Character& c) {
        for (Mesh& m : c.meshes) changed += pruneAllWeights(m, threshold);
    });
    return changed;
}

Result<std::size_t> Document::copySkinWeights(std::size_t fromMesh, std::size_t toMesh) {
    if (!character_ || fromMesh >= character_->meshes.size() || toMesh >= character_->meshes.size() ||
        fromMesh == toMesh) {
        return Error{"choose two different meshes"};
    }
    if (!character_->meshes[fromMesh].hasSkinning()) return Error{"the source mesh has no skin weights"};
    std::size_t changed = 0;
    editWeights("Copy Weights", [&](Character& c) {
        std::vector<std::vector<SkinWeight>> snapshot;
        for (const Mesh& m : c.meshes) snapshot.push_back(m.skinWeights);
        copyWeightsNearest(c.meshes[toMesh], c.meshes[fromMesh]);
        changed = countVertices(c, snapshot);
    });
    return changed;
}

}  // namespace rigger::editor
