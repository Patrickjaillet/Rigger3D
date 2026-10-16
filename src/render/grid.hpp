// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include "render/glad_include.hpp"

namespace rigger::render {

class Grid {
public:
    Grid();
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;
    Grid(Grid&& other) noexcept;
    Grid& operator=(Grid&& other) noexcept;
    ~Grid();

    void rebuild(float halfExtent, int subdivisions);
    void draw() const;

private:
    void ensureStorage();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertexCount_ = 0;
};

}  // namespace rigger::render
