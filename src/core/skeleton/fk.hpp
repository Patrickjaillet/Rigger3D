// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <vector>

#include "core/skeleton/skeleton.hpp"

namespace rigger {

// FK (cinématique directe) : transformation MONDE de chaque os, indexée par BoneId (O(n), une passe).
// Les emplacements d'os supprimés valent l'identité. Calcul en mat4 (exact même avec échelle non uniforme).
std::vector<glm::mat4> computeGlobals(const Skeleton& skeleton, const Pose& pose);

// Matrices de skinning : monde(pose) * inverseBind. Identité au repos.
std::vector<glm::mat4> computeSkinMatrices(const Skeleton& skeleton, const Pose& pose);

}  // namespace rigger
