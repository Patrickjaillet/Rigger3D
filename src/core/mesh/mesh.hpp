// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec2 uv0{0.0f};
};

struct SkinWeight {
    std::array<BoneId, 4> joints{{kNoBone, kNoBone, kNoBone, kNoBone}};
    std::array<float, 4> weights{{0.0f, 0.0f, 0.0f, 0.0f}};

    bool empty() const {
        return std::all_of(weights.begin(), weights.end(), [](float weight) { return weight <= 0.0f; });
    }
};

// Image RGBA 8 bits, lignes de haut en bas (dÃ©jÃ  décodée : core ne fait pas d'I/O fichier).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Material {
    std::string name = "material";
    glm::vec3 baseColor{1.0f};
    std::shared_ptr<const Image> baseColorTexture;  // optionnelle, partagée entre matériaux
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct Submesh {
    std::string name;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    Material material;

    std::uint32_t endIndex() const { return firstIndex + indexCount; }
};

struct Bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

// Pilote correctif d'une blend shape : la forme est activée selon l'angle d'une articulation (épaule,
// coude...). Poids ajouté = clamp((angle - angle0) / (angle1 - angle0), 0, 1) ; l'angle est la torsion signée
// de la rotation de l'os, relative à sa pose de repos, autour de `axis` (repère de repos de l'os).
struct MorphDriver {
    BoneId bone = kNoBone;
    glm::vec3 axis{1.0f, 0.0f, 0.0f};
    float angle0 = 0.0f;  // radians : poids 0
    float angle1 = 1.0f;  // radians : poids 1
    bool operator==(const MorphDriver&) const = default;
};

// Blend shape (morph target) : déplacements de sommets ajoutés à la forme de repos, pondérés. Le poids vient
// de la translation X de l'os de contrôle `weightBone` (donc animable avec la timeline, l'auto-key et l'undo)
// ; les `drivers` ajoutent un poids piloté par des articulations (shapes correctives).
struct MorphTarget {
    std::string name;
    std::vector<glm::vec3> positionDeltas;  // un par sommet
    BoneId weightBone = kNoBone;
    std::vector<MorphDriver> drivers;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;
    std::vector<SkinWeight> skinWeights;
    std::vector<MorphTarget> morphTargets;

    bool hasSkinning() const { return !skinWeights.empty(); }

    Bounds computeBounds() const {
        Bounds bounds{};
        if (vertices.empty()) {
            return bounds;
        }

        bounds.min = vertices.front().position;
        bounds.max = vertices.front().position;
        for (const Vertex& vertex : vertices) {
            bounds.min = glm::min(bounds.min, vertex.position);
            bounds.max = glm::max(bounds.max, vertex.position);
        }
        bounds.center = (bounds.min + bounds.max) * 0.5f;

        float maxDistanceSq = 0.0f;
        for (const Vertex& vertex : vertices) {
            const glm::vec3 delta = vertex.position - bounds.center;
            const float distanceSq = glm::dot(delta, delta);
            if (distanceSq > maxDistanceSq) {
                maxDistanceSq = distanceSq;
            }
        }
        bounds.radius = std::sqrt(maxDistanceSq);
        return bounds;
    }

    void recomputeNormals() {
        if (vertices.empty() || indices.size() < 3) {
            return;
        }

        std::vector<glm::vec3> accum(vertices.size(), glm::vec3(0.0f));
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const std::uint32_t i0 = indices[i];
            const std::uint32_t i1 = indices[i + 1];
            const std::uint32_t i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const glm::vec3 a = vertices[i0].position;
            const glm::vec3 b = vertices[i1].position;
            const glm::vec3 c = vertices[i2].position;
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
            if (glm::dot(normal, normal) < 1e-8f) {
                continue;
            }

            accum[i0] += normal;
            accum[i1] += normal;
            accum[i2] += normal;
        }

        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const float length = glm::length(accum[i]);
            if (length > 1e-8f) {
                vertices[i].normal = glm::normalize(accum[i]);
            }
        }
    }

    Result<void> validate() const {
        if (vertices.empty() && indices.empty() && submeshes.empty() && skinWeights.empty()) {
            return {};
        }

        if (indices.size() % 3 != 0) {
            return Error{"triangle index count must be a multiple of 3"};
        }

        if (!skinWeights.empty() && skinWeights.size() != vertices.size()) {
            return Error{"skin weights count must match vertex count"};
        }

        for (const MorphTarget& target : morphTargets) {
            if (target.positionDeltas.size() != vertices.size()) {
                return Error{"morph target deltas count must match vertex count"};
            }
            for (const glm::vec3& d : target.positionDeltas) {
                if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) {
                    return Error{"morph target contains a non-finite value"};
                }
            }
        }

        for (const Submesh& submesh : submeshes) {
            if (submesh.firstIndex + submesh.indexCount > indices.size()) {
                return Error{"submesh index range exceeds mesh index buffer"};
            }
        }

        for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
            const Vertex& vertex = vertices[vertexIndex];
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                !std::isfinite(vertex.position.z)) {
                return Error{"vertex position contains a non-finite value"};
            }

            if (vertexIndex < skinWeights.size()) {
                const SkinWeight& skin = skinWeights[vertexIndex];
                float total = 0.0f;
                for (std::size_t weightIndex = 0; weightIndex < skin.weights.size(); ++weightIndex) {
                    const float weight = skin.weights[weightIndex];
                    if (!std::isfinite(weight) || weight < 0.0f) {
                        return Error{"skin weights must be finite and non-negative"};
                    }
                    total += weight;
                    if (weight > 0.0f && skin.joints[weightIndex] == kNoBone) {
                        return Error{"skin joint index cannot be kNoBone when weight is positive"};
                    }
                }
                if (total <= 0.0f) {
                    return Error{"skin weights must sum to a positive value"};
                }
                if (std::fabs(total - 1.0f) > 1.0e-4f) {
                    return Error{"skin weights must sum to 1 within tolerance"};
                }
            }
        }

        return {};
    }
};

}  // namespace rigger
