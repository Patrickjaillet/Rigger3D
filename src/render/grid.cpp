// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/grid.hpp"

#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace rigger::render {

namespace {

struct Vertex {
    GLfloat position[3];
    GLfloat color[3];
};

}  // namespace

Grid::Grid() = default;

void Grid::ensureStorage() {
    if (vao_ != 0 && vbo_ != 0) {
        return;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
}

Grid::Grid(Grid&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      vertexCount_(std::exchange(other.vertexCount_, 0)) {}

Grid& Grid::operator=(Grid&& other) noexcept {
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

Grid::~Grid() {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
}

void Grid::rebuild(float halfExtent, int subdivisions) {
    ensureStorage();

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(subdivisions * 8 + 8));

    const float step = (2.0f * halfExtent) / static_cast<float>(subdivisions);
    const glm::vec3 axisRed(0.9f, 0.2f, 0.2f);
    const glm::vec3 axisBlue(0.2f, 0.35f, 0.9f);
    const glm::vec3 neutral(0.7f, 0.7f, 0.7f);

    for (int i = -subdivisions; i <= subdivisions; ++i) {
        const float f = static_cast<float>(i) * step;
        const glm::vec3 color = (i == 0) ? axisRed : neutral;
        vertices.push_back({{f, 0.0f, -halfExtent}, {color.r, color.g, color.b}});
        vertices.push_back({{f, 0.0f, halfExtent}, {color.r, color.g, color.b}});

        const glm::vec3 zColor = (i == 0) ? axisBlue : neutral;
        vertices.push_back({{-halfExtent, 0.0f, f}, {zColor.r, zColor.g, zColor.b}});
        vertices.push_back({{halfExtent, 0.0f, f}, {zColor.r, zColor.g, zColor.b}});
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(3 * sizeof(GLfloat)));

    glBindVertexArray(0);
    vertexCount_ = static_cast<GLsizei>(vertices.size());
}

void Grid::draw() const {
    if (vao_ == 0) {
        return;
    }
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}

}  // namespace rigger::render
