// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

#include "core/math/transform.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Nom du côté opposé : « arm.L » <-> « arm.R », « leg_L_2 » <-> « leg_R_2 », « hand_l.001 » <-> « hand_r.001
// » (jeton L/R isolé par « . » ou « _ », en majuscule ou minuscule, suivi de la fin, d'un séparateur ou de
// chiffres). std::nullopt pour un os central (aucun jeton gauche/droite).
std::optional<std::string> mirrorName(const std::string& name);

// Os symétrique de `id` (même hiérarchie, nom miroir) ; kNoBone s'il n'existe pas ou si l'os est central.
BoneId findMirrorBone(const Skeleton& skeleton, BoneId id);

// Matrice de miroir par rapport au plan YZ (x -> -x).
glm::mat4 mirrorMatrixX();

// Transformation monde symétrique : S * W * S (reste une rotation directe, positions x -> -x).
glm::mat4 mirrorWorldX(const glm::mat4& world);

// Transformation LOCALE à donner à `target` pour que son monde soit le miroir de `sourceWorld`, compte tenu
// de la pose actuelle de son parent (`globals`).
Transform mirroredLocal(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId target,
                        const glm::mat4& sourceWorld);

}  // namespace rigger
