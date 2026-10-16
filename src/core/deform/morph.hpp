// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <vector>

#include "core/mesh/mesh.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Blend shapes (morph targets) et shapes correctives.

// Poids de chaque cible : translation X de l'os de poids (valeur de repos si l'os n'est pas dans la pose) +
// poids des pilotes correctifs (angles d'articulation).
std::vector<float> morphWeights(const Mesh& mesh, const Skeleton& skeleton, const Pose& pose);

// Poids piloté par un pilote correctif pour une pose donnée (0..1).
float driverWeight(const MorphDriver& driver, const Skeleton& skeleton, const Pose& pose);

// Vrai si au moins un poids est non nul.
bool anyMorphActive(const std::vector<float>& weights);

// Sommets de repos déformés : position += somme(poids * delta). Les normales sont recalculées si
// `recomputeNormals`.
std::vector<Vertex> applyMorphs(const Mesh& mesh, const std::vector<float>& weights,
                                bool recomputeNormals = true);

}  // namespace rigger
