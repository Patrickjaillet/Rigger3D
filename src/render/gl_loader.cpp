// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gl_loader.hpp"

#include <glad/glad.h>

#if !defined(NDEBUG)
#include <iostream>
#endif

namespace rigger::render {

namespace {
#if !defined(NDEBUG)
void APIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                            const GLchar* message, const void* /*userParam*/) {
    std::cerr << "[OpenGL] " << "source=" << source << " type=" << type << " id=" << id
              << " severity=" << severity << " message=" << message << '\n';
    (void)length;
}
#endif
}  // namespace

bool loadOpenGL(GlGetProcAddress getProcAddress) {
    if (getProcAddress == nullptr) {
        return false;
    }

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(getProcAddress)) == 0) {
        return false;
    }

#if !defined(NDEBUG)
#if defined(GLAD_GL_VERSION_4_3) || defined(GLAD_GL_ARB_debug_output)
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugOutput, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#endif
#endif

    return true;
}

}  // namespace rigger::render
