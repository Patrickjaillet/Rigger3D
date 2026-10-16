// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skinning/dqs.hpp"

#include <array>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace rigger {
namespace {

struct DualQuaternion {
    glm::quat real{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat dual{0.0f, 0.0f, 0.0f, 0.0f};
};

bool rigidTransform(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    if (std::fabs(matrix[0][3]) > 1.0e-4f || std::fabs(matrix[1][3]) > 1.0e-4f ||
        std::fabs(matrix[2][3]) > 1.0e-4f || std::fabs(matrix[3][3] - 1.0f) > 1.0e-4f) {
        return false;
    }
    const glm::mat3 linear(matrix);
    const glm::vec3 x = linear[0];
    const glm::vec3 y = linear[1];
    const glm::vec3 z = linear[2];
    return std::fabs(glm::dot(x, x) - 1.0f) <= 1.0e-3f && std::fabs(glm::dot(y, y) - 1.0f) <= 1.0e-3f &&
           std::fabs(glm::dot(z, z) - 1.0f) <= 1.0e-3f && std::fabs(glm::dot(x, y)) <= 1.0e-3f &&
           std::fabs(glm::dot(x, z)) <= 1.0e-3f && std::fabs(glm::dot(y, z)) <= 1.0e-3f &&
           std::fabs(glm::determinant(linear) - 1.0f) <= 1.0e-3f;
}

DualQuaternion toDualQuaternion(const glm::mat4& matrix) {
    DualQuaternion result;
    result.real = glm::normalize(glm::quat_cast(glm::mat3(matrix)));
    const glm::vec3 translation = glm::vec3(matrix[3]);
    result.dual = 0.5f * glm::quat(0.0f, translation.x, translation.y, translation.z) * result.real;
    bool flip = result.real.w < -1.0e-6f;
    if (std::fabs(result.real.w) <= 1.0e-6f) {
        const std::array<float, 3> vectorPart{result.real.x, result.real.y, result.real.z};
        for (const float component : vectorPart) {
            if (std::fabs(component) > 1.0e-6f) {
                flip = component < 0.0f;
                break;
            }
        }
    }
    // Pick a deterministic representative at 180 degrees, where quaternion signs are ambiguous.
    if (flip) {
        result.real = -result.real;
        result.dual = -result.dual;
    }
    return result;
}

glm::vec3 transformPoint(const DualQuaternion& transform, const glm::vec3& point) {
    const glm::quat translated = 2.0f * transform.dual * glm::conjugate(transform.real);
    const glm::quat rotated =
        transform.real * glm::quat(0.0f, point.x, point.y, point.z) * glm::conjugate(transform.real);
    return glm::vec3(rotated.x + translated.x, rotated.y + translated.y, rotated.z + translated.z);
}

}  // namespace

Result<std::vector<Vertex>> skinDqs(const Mesh& mesh, const std::vector<glm::mat4>& skinMatrices) {
    const Result<void> validation = mesh.validate();
    if (!validation) {
        return validation.error();
    }

    std::vector<Vertex> skinned = mesh.vertices;
    if (mesh.skinWeights.empty()) {
        return skinned;
    }

    for (std::size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
        const SkinWeight& skin = mesh.skinWeights[vertexIndex];
        DualQuaternion blended{glm::quat(0.0f, 0.0f, 0.0f, 0.0f), glm::quat(0.0f, 0.0f, 0.0f, 0.0f)};
        glm::quat reference(1.0f, 0.0f, 0.0f, 0.0f);
        bool hasReference = false;

        for (std::size_t influence = 0; influence < skin.weights.size(); ++influence) {
            const float weight = skin.weights[influence];
            if (weight == 0.0f) {
                continue;
            }
            const BoneId joint = skin.joints[influence];
            if (joint >= skinMatrices.size()) {
                return Error{"skin joint index exceeds skin matrix count"};
            }
            const glm::mat4& matrix = skinMatrices[joint];
            if (!rigidTransform(matrix)) {
                return Error{"dual quaternion skinning requires rigid skin matrices"};
            }
            DualQuaternion transform = toDualQuaternion(matrix);
            if (!hasReference) {
                reference = transform.real;
                hasReference = true;
            } else if (glm::dot(reference, transform.real) < -1.0e-6f) {
                transform.real = -transform.real;
                transform.dual = -transform.dual;
            }
            blended.real += transform.real * weight;
            blended.dual += transform.dual * weight;
        }

        const float realLength = glm::length(blended.real);
        if (!std::isfinite(realLength) || realLength <= 1.0e-8f) {
            return Error{"dual quaternion blend is degenerate"};
        }
        blended.real /= realLength;
        blended.dual /= realLength;
        blended.dual -= blended.real * glm::dot(blended.real, blended.dual);

        skinned[vertexIndex].position = transformPoint(blended, mesh.vertices[vertexIndex].position);
        if (!std::isfinite(skinned[vertexIndex].position.x) ||
            !std::isfinite(skinned[vertexIndex].position.y) ||
            !std::isfinite(skinned[vertexIndex].position.z)) {
            return Error{"dual quaternion skinning produced a non-finite position"};
        }
        const glm::vec3 normal = blended.real * mesh.vertices[vertexIndex].normal;
        const float normalLength = glm::length(normal);
        if (!std::isfinite(normalLength) || normalLength <= 1.0e-8f) {
            return Error{"dual quaternion skinning produced an invalid vertex normal"};
        }
        skinned[vertexIndex].normal = normal / normalLength;
    }

    return skinned;
}

}  // namespace rigger
