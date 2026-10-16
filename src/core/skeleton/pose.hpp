// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <vector>

#include "core/math/transform.hpp"

namespace rigger {

// Une transformation LOCALE par os, indexée par BoneId (les emplacements d'os supprimés sont ignorés).
// Si la pose est plus courte que le squelette (os ajoutés après sa création), les os manquants
// prennent leur bind pose.
using Pose = std::vector<Transform>;

}  // namespace rigger
