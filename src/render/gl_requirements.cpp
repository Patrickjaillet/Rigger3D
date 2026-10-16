// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gl_requirements.hpp"

namespace rigger::render {

std::string describeRequirements() {
    return "OpenGL " + std::to_string(kGlMajor) + "." + std::to_string(kGlMinor) + " core, MSAA " +
           std::to_string(kMsaaSamples) + "x";
}

}  // namespace rigger::render
