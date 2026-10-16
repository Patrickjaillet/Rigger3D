// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string_view>
#include <vector>

namespace rigger::io {

// Extensions de fichiers 3D importables (Phase 5 : glTF 2.0).
std::vector<std::string_view> supportedImportExtensions();

}  // namespace rigger::io
