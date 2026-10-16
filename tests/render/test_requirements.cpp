// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include "render/gl_requirements.hpp"

TEST_CASE("exigences OpenGL") {
    CHECK(rigger::render::kGlMajor == 4);
    CHECK(rigger::render::kGlMinor == 1);
    CHECK(rigger::render::describeRequirements() == "OpenGL 4.1 core, MSAA 4x");
}
