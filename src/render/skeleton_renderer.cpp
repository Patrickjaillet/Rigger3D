// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/skeleton_renderer.hpp"

#include <utility>
#include <vector>

namespace rigger::render {

namespace {

struct Vertex {
    GLfloat position[3];
    GLfloat color[3];
};

}  // namespace

SkeletonRenderer::SkeletonRenderer() = default;

void SkeletonRenderer::ensureStorage() {
    if (vao_ != 0 && vbo_ != 0) {
        return;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
}

SkeletonRenderer::SkeletonRenderer(SkeletonRenderer&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      vertexCount_(std::exchange(other.vertexCount_, 0)) {}

SkeletonRenderer& SkeletonRenderer::operator=(SkeletonRenderer&& other) noexcept {
    if (this != &other) {
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        vertexCount_ = std::exchange(other.vertexCount_, 0);
    }
    return *this;
}

SkeletonRenderer::~SkeletonRenderer() {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
}

void SkeletonRenderer::setSegments(const std::vector<std::pair<glm::vec3, glm::vec3>>& segments,
                                   const glm::vec3& color) {
    std::vector<ColoredSegment> colored;
    colored.reserve(segments.size());
    for (const auto& [a, b] : segments) {
        colored.push_back({a, b, color});
    }
    setColoredSegments(colored);
}

void SkeletonRenderer::setColoredSegments(const std::vector<ColoredSegment>& segments) {
    ensureStorage();

    std::vector<Vertex> vertices;
    vertices.reserve(segments.size() * 2);

    for (const auto& [a, b, color] : segments) {
        vertices.push_back({{a.x, a.y, a.z}, {color.r, color.g, color.b}});
        vertices.push_back({{b.x, b.y, b.z}, {color.r, color.g, color.b}});
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    // Emplacement 2 = couleur (aColor du shader du viewport) ; l'emplacement 1 (normale) reste générique.
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(3 * sizeof(GLfloat)));

    glBindVertexArray(0);
    vertexCount_ = static_cast<GLsizei>(vertices.size());
}

void SkeletonRenderer::draw() const {
    if (vao_ == 0 || vertexCount_ == 0) {
        return;
    }
    // Les lignes n'ont pas de normale : valeur générique = direction de la lumière, pour un rendu plein (non
    // assombri).
    glVertexAttrib3f(1, 0.5812f, 0.7265f, 0.4359f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}

}  // namespace rigger::render
