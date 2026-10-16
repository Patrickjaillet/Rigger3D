// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/animation/clip.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Rétro-ciblage : applique un clip d'un squelette source à un squelette cible de proportions et
// d'orientations de repos différentes, via une table de correspondance d'os.

struct BoneMap {
    std::vector<std::pair<BoneId, BoneId>> pairs;  // (os source, os cible)

    BoneId target(BoneId source) const {
        for (const auto& [s, t] : pairs) {
            if (s == source) return t;
        }
        return kNoBone;
    }
    bool operator==(const BoneMap&) const = default;
};

// Nom canonique d'un os humanoïde : ignore préfixes (mixamorig:, Bip01...), casse, séparateurs, côté (L / R /
// Left / Right) et synonymes (thigh = upleg, calf = shin = leg, wrist = hand, clavicle = shoulder...). `side`
// reçoit 'L', 'R' ou 0. Exemple : « mixamorig:LeftUpLeg » et « thigh.L » donnent ("upleg", 'L').
std::string canonicalBoneName(const std::string& name, char& side);

// Correspondance automatique par noms canoniques (un os cible n'est utilisé qu'une fois).
BoneMap autoMapBones(const Skeleton& source, const Skeleton& target);

struct RetargetOptions {
    float fps = 0.0f;              // fréquence du clip produit (0 : celle du clip source)
    bool scaleTranslation = true;  // met à l'échelle les translations (rapport des hauteurs de hanches)
};

// Produit un clip pour le squelette cible : pour chaque os apparié animé dans la source, rotation compensée
// des différences d'orientation de repos (calculée en repère monde de repos), translation mise à l'échelle
// des proportions. Le clip est échantillonné image par image (clés linéaires).
AnimationClip retargetClip(const AnimationClip& clip, const Skeleton& source, const Skeleton& target,
                           const BoneMap& map, const RetargetOptions& options = {});

}  // namespace rigger
