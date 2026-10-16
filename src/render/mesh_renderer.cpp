// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/mesh_renderer.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include "core/skinning/dqs.hpp"
#include "core/skinning/lbs.hpp"
#include "render/shader.hpp"

namespace rigger::render {

MeshRenderer::MeshRenderer() = default;

void MeshRenderer::ensureStorage() {
    if (vao_ != 0 && vbo_ != 0 && ebo_ != 0 && boneBuffer_ != 0) {
        return;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &boneBuffer_);

    std::array<glm::mat4, kMaxBones> identityMatrices;
    identityMatrices.fill(glm::mat4(1.0f));
    glBindBuffer(GL_UNIFORM_BUFFER, boneBuffer_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(identityMatrices), identityMatrices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

MeshRenderer::MeshRenderer(MeshRenderer&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)),
      boneBuffer_(std::exchange(other.boneBuffer_, 0)),
      vertices_(std::move(other.vertices_)),
      indices_(std::move(other.indices_)),
      boneMatrices_(std::move(other.boneMatrices_)),
      indexCount_(std::exchange(other.indexCount_, 0)),
      primitive_(std::exchange(other.primitive_, GL_TRIANGLES)),
      cpuDataUploaded_(std::exchange(other.cpuDataUploaded_, false)),
      useDqs_(other.useDqs_) {}

MeshRenderer& MeshRenderer::operator=(MeshRenderer&& other) noexcept {
    if (this != &other) {
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (ebo_ != 0) {
            glDeleteBuffers(1, &ebo_);
        }
        if (boneBuffer_ != 0) {
            glDeleteBuffers(1, &boneBuffer_);
        }
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        boneBuffer_ = std::exchange(other.boneBuffer_, 0);
        vertices_ = std::move(other.vertices_);
        indices_ = std::move(other.indices_);
        boneMatrices_ = std::move(other.boneMatrices_);
        indexCount_ = std::exchange(other.indexCount_, 0);
        primitive_ = std::exchange(other.primitive_, GL_TRIANGLES);
        cpuDataUploaded_ = std::exchange(other.cpuDataUploaded_, false);
        useDqs_ = other.useDqs_;
    }
    return *this;
}

MeshRenderer::~MeshRenderer() {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
    }
    if (boneBuffer_ != 0) {
        glDeleteBuffers(1, &boneBuffer_);
    }
}

void MeshRenderer::setMesh(const std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices,
                           GLenum primitive) {
    ensureStorage();
    vertices_ = vertices;
    indices_ = indices;
    cpuDataUploaded_ = false;
    primitive_ = primitive;
    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)),
                 vertices.empty() ? nullptr : vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.empty() ? nullptr : indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT, sizeof(MeshVertex),
                           reinterpret_cast<void*>(offsetof(MeshVertex, joints)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, weights)));

    glBindVertexArray(0);
    indexCount_ = static_cast<GLsizei>(indices.size());
}

bool MeshRenderer::setBoneMatrices(const std::vector<glm::mat4>& matrices) {
    if (matrices.size() > kMaxBones) {
        return false;
    }
    for (const glm::mat4& matrix : matrices) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    return false;
                }
            }
        }
    }
    ensureStorage();
    boneMatrices_ = matrices;

    std::array<glm::mat4, kMaxBones> palette;
    palette.fill(glm::mat4(1.0f));
    std::copy(matrices.begin(), matrices.end(), palette.begin());
    glBindBuffer(GL_UNIFORM_BUFFER, boneBuffer_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(palette), palette.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return true;
}

void MeshRenderer::draw(const Shader& shader, bool gpuSkinning, std::uint32_t selectedBone) {
    if (vao_ == 0 || indexCount_ == 0) {
        return;
    }

    shader.setUniform("uSkinningEnabled", gpuSkinning ? 1 : 0);
    shader.setUniform("uShowInfluence", selectedBone == kNoBone ? 0 : 1);
    shader.setUniform("uSelectedBone", selectedBone == kNoBone ? -1 : static_cast<int>(selectedBone));
    if (gpuSkinning) {
        if (cpuDataUploaded_) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(vertices_.size() * sizeof(MeshVertex)),
                            vertices_.empty() ? nullptr : vertices_.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            cpuDataUploaded_ = false;
        }
        const GLuint blockIndex = glGetUniformBlockIndex(shader.id(), "SkinMatrices");
        if (blockIndex != GL_INVALID_INDEX) {
            glUniformBlockBinding(shader.id(), blockIndex, 0);
        }
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, boneBuffer_);
    } else if (!boneMatrices_.empty()) {
        rigger::Mesh mesh;
        mesh.vertices.resize(vertices_.size());
        mesh.skinWeights.resize(vertices_.size());
        for (std::size_t i = 0; i < vertices_.size(); ++i) {
            mesh.vertices[i].position = vertices_[i].position;
            mesh.vertices[i].normal = vertices_[i].normal;
            mesh.skinWeights[i].joints = {vertices_[i].joints.x, vertices_[i].joints.y, vertices_[i].joints.z,
                                          vertices_[i].joints.w};
            mesh.skinWeights[i].weights = {vertices_[i].weights.x, vertices_[i].weights.y,
                                           vertices_[i].weights.z, vertices_[i].weights.w};
        }
        auto cpuVertices =
            useDqs_ ? rigger::skinDqs(mesh, boneMatrices_) : rigger::skinLbs(mesh, boneMatrices_);
        if (!cpuVertices) {
            spdlog::error("CPU skinning failed in MeshRenderer: {}", cpuVertices.error().message);
            return;
        }
        std::vector<MeshVertex> transformed = vertices_;
        for (std::size_t i = 0; i < transformed.size(); ++i) {
            transformed[i].position = cpuVertices.value()[i].position;
            transformed[i].normal = cpuVertices.value()[i].normal;
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(transformed.size() * sizeof(MeshVertex)),
                        transformed.empty() ? nullptr : transformed.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        cpuDataUploaded_ = true;
    } else if (cpuDataUploaded_) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices_.size() * sizeof(MeshVertex)),
                        vertices_.empty() ? nullptr : vertices_.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        cpuDataUploaded_ = false;
    }

    glBindVertexArray(vao_);
    glDrawElements(primitive_, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void MeshRenderer::draw() const {
    if (vao_ == 0 || indexCount_ == 0) {
        return;
    }
    glBindVertexArray(vao_);
    glDrawElements(primitive_, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

}  // namespace rigger::render
