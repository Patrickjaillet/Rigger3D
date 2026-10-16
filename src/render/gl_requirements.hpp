// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string>

namespace rigger::render {

// Contexte OpenGL exigé par le moteur de rendu (OpenGL 4.1 core : maximum supporté par macOS).
inline constexpr int kGlMajor = 4;
inline constexpr int kGlMinor = 1;
inline constexpr int kMsaaSamples = 4;
inline constexpr int kDepthBits = 24;

// Ex. : "OpenGL 4.1 core, MSAA 4x"
std::string describeRequirements();

}  // namespace rigger::render
