// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <array>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Auto-rig humanoïde : ~22 repères (articulations) placés sur le mesh -> squelette humanoïde. Le skinning
// automatique (core/skinning/paint) et l'IK humanoïde (éditeur) se font ensuite sur ce squelette.

enum class Landmark : int {
    Pelvis,
    Spine,
    Chest,
    Neck,
    Head,
    HeadTop,
    ShoulderL,
    ElbowL,
    WristL,
    HandTipL,
    ShoulderR,
    ElbowR,
    WristR,
    HandTipR,
    HipL,
    KneeL,
    AnkleL,
    ToeL,
    HipR,
    KneeR,
    AnkleR,
    ToeR,
    Count
};
inline constexpr int kLandmarkCount = static_cast<int>(Landmark::Count);

using Landmarks = std::array<glm::vec3, kLandmarkCount>;

// Nom lisible (anglais, traduit par l'interface) d'un repère.
const char* landmarkName(Landmark landmark);

// Placement automatique d'après les points du mesh : proportions humaines standard ajustées à la boîte
// englobante (personnage debout, Y haut, face à +Z ; bras en T si le mesh est large, le long du corps sinon).
Landmarks guessLandmarks(const std::vector<glm::vec3>& points);

// Les repères ont-ils un ordre anatomique cohérent ? (tête au-dessus du cou, genou sous la hanche, côté L à
// +X...). Retourne un message d'erreur, vide si tout va bien.
std::string validateLandmarks(const Landmarks& landmarks);

// Os créés (identifiants dans le squelette produit).
struct HumanoidBones {
    BoneId pelvis = kNoBone, spine = kNoBone, chest = kNoBone, neck = kNoBone, head = kNoBone;
    BoneId upperArm[2] = {kNoBone, kNoBone}, foreArm[2] = {kNoBone, kNoBone}, hand[2] = {kNoBone, kNoBone};
    BoneId thigh[2] = {kNoBone, kNoBone}, shin[2] = {kNoBone, kNoBone}, foot[2] = {kNoBone, kNoBone},
           toe[2] = {kNoBone, kNoBone};
};

// Construit le squelette (rotations de repos identité : les os sont définis par leurs origines). `side` 0 =
// gauche (+X).
Skeleton buildHumanoidSkeleton(const Landmarks& landmarks, HumanoidBones* bones = nullptr);

}  // namespace rigger
