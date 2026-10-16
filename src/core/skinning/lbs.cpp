// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skinning/lbs.hpp"

#include <atomic>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>

#include "core/util/parallel.hpp"
#include "core/util/profiler.hpp"

namespace rigger {
namespace {

bool finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

Result<std::vector<Vertex>> skinLbs(const Mesh& mesh, const std::vector<glm::mat4>& skinMatrices) {
    RIGGER_PROFILE_SCOPE("skinLbs");
    const Result<void> validation = mesh.validate();
    if (!validation) {
        return validation.error();
    }

    std::vector<Vertex> skinned = mesh.vertices;
    if (mesh.skinWeights.empty()) {
        return skinned;
    }

    // Les matrices sont validées une seule fois (et non à chaque sommet).
    std::vector<std::uint8_t> matrixFinite(skinMatrices.size());
    for (std::size_t m = 0; m < skinMatrices.size(); ++m) matrixFinite[m] = finite(skinMatrices[m]) ? 1 : 0;

    // Première erreur rencontrée par n'importe quel fil (messages statiques : aucune allocation dans la
    // boucle).
    std::atomic<const char*> failure{nullptr};
    const auto fail = [&failure](const char* message) {
        const char* expected = nullptr;
        failure.compare_exchange_strong(expected, message);
    };

    parallelFor(mesh.vertices.size(), 16384, [&](std::size_t begin, std::size_t end) {
        for (std::size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
            const SkinWeight& skin = mesh.skinWeights[vertexIndex];
            glm::mat4 blended(0.0f);
            for (std::size_t influence = 0; influence < skin.weights.size(); ++influence) {
                const float weight = skin.weights[influence];
                if (weight == 0.0f) {
                    continue;
                }
                const BoneId joint = skin.joints[influence];
                if (joint >= skinMatrices.size()) {
                    fail("skin joint index exceeds skin matrix count");
                    return;
                }
                if (matrixFinite[joint] == 0) {
                    fail("skin matrix contains a non-finite value");
                    return;
                }
                blended += skinMatrices[joint] * weight;
            }

            const glm::vec4 position = blended * glm::vec4(mesh.vertices[vertexIndex].position, 1.0f);
            if (!std::isfinite(position.w) || std::fabs(position.w) <= 1.0e-8f) {
                fail("skinning produced an invalid position");
                return;
            }
            const glm::vec3 p = glm::vec3(position) / position.w;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                fail("skinning produced a non-finite position");
                return;
            }
            skinned[vertexIndex].position = p;

            // Normale : transposée de l'inverse = matrice des cofacteurs / déterminant (sans inversion de
            // matrice).
            const glm::vec3 c0(blended[0]), c1(blended[1]), c2(blended[2]);
            const glm::vec3 k0 = glm::cross(c1, c2), k1 = glm::cross(c2, c0), k2 = glm::cross(c0, c1);
            const float determinant = glm::dot(c0, k0);
            if (!std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-8f) {
                fail("skinning matrix is singular; cannot transform vertex normal");
                return;
            }
            const glm::vec3 n = mesh.vertices[vertexIndex].normal;
            const glm::vec3 normal = (k0 * n.x + k1 * n.y + k2 * n.z) / determinant;
            const float normalLength = glm::length(normal);
            if (!std::isfinite(normalLength) || normalLength <= 1.0e-8f) {
                fail("skinning produced an invalid vertex normal");
                return;
            }
            skinned[vertexIndex].normal = normal / normalLength;
        }
    });
    if (const char* message = failure.load()) {
        return Error{message};
    }
    return skinned;
}

}  // namespace rigger
