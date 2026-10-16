// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <algorithm>

#include "io/formats.hpp"

TEST_CASE("formats importables") {
    const auto ext = rigger::io::supportedImportExtensions();
    CHECK(std::find(ext.begin(), ext.end(), ".glb") != ext.end());
    CHECK(std::find(ext.begin(), ext.end(), ".gltf") != ext.end());
}
