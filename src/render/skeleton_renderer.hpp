// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace rigger::render {

class SkeletonRenderer {
public:
    SkeletonRenderer();
    SkeletonRenderer(const SkeletonRenderer&) = delete;
    SkeletonRenderer& operator=(const SkeletonRenderer&) = delete;
    SkeletonRenderer(SkeletonRenderer&& other) noexcept;
    SkeletonRenderer& operator=(SkeletonRenderer&& other) noexcept;
    ~SkeletonRenderer();

    struct ColoredSegment {
        glm::vec3 a;
        glm::vec3 b;
        glm::vec3 color;
    };

    // Une couleur par segment (couleur d'os, surbrillance de sélection…).
    void setColoredSegments(const std::vector<ColoredSegment>& segments);
    void setSegments(const std::vector<std::pair<glm::vec3, glm::vec3>>& segments,
                     const glm::vec3& color = glm::vec3(0.15f, 0.6f, 1.0f));
    void draw() const;

private:
    void ensureStorage();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertexCount_ = 0;
};

}  // namespace rigger::render
