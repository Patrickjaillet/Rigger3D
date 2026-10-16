// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <cstddef>

#include "core/mesh/mesh.hpp"
#include "core/result.hpp"

namespace rigger {

Result<void> normalizeWeights(SkinWeight& skin);
Result<void> limitInfluences(SkinWeight& skin, std::size_t maxInfluences = 4);
Result<void> pruneWeights(SkinWeight& skin, float threshold = 0.01f);

}  // namespace rigger
