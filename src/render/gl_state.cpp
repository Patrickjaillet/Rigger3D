// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gl_state.hpp"

#include <glad/glad.h>

namespace rigger::render {

void initializeViewportState() {
    glClearColor(0.75f, 0.75f, 0.75f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
}

void resizeViewport(int width, int height) { glViewport(0, 0, width, height); }

void clearViewport() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

}  // namespace rigger::render
