// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "io/formats.hpp"

namespace rigger::io {

std::vector<std::string_view> supportedImportExtensions() { return {".gltf", ".glb"}; }

}  // namespace rigger::io
