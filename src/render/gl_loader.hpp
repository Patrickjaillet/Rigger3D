// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

namespace rigger::render {

using GlGetProcAddress = void* (*)(const char* name);

bool loadOpenGL(GlGetProcAddress getProcAddress);

}  // namespace rigger::render
