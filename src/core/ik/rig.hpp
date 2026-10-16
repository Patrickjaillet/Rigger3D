// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string>
#include <vector>

#include "core/ik/ik.hpp"

namespace rigger {

// Verrou de position : pendant [start, end] (secondes) la cible est figée à `world` (pied planté au sol).
struct FootLock {
    double start = 0.0;
    double end = 0.0;
    Transform world;  // transformation monde de la cible capturée au début du verrou

    bool operator==(const FootLock&) const = default;
};

enum class IKKind {
    Chain,   // chaîne d'os qui atteint la cible (TwoBone / FABRIK / CCD)
    LookAt,  // un seul os (chain.bones[0]) dont l'axe `aimAxis` vise la cible (tête, yeux)
};

// Contrôle IK d'un personnage : une chaîne + les os qui servent de CIBLE et de POLE (des os du squelette,
// sans skinning, drapeau BoneFlags::Control). Comme la cible et le pole sont de vrais os, leurs clés,
// courbes, gizmos et annulations passent par le système existant. `weight` est le mélange IK <-> FK (0 = FK
// pure, 1 = IK pure).
struct IKControl {
    std::string name;
    IKKind kind = IKKind::Chain;
    glm::vec3 aimAxis{0.0f, 1.0f, 0.0f};  // LookAt : axe LOCAL de l'os qui doit viser la cible
    std::vector<FootLock> locks;
    IKChain chain;
    BoneId targetBone = kNoBone;
    BoneId poleBone = kNoBone;  // optionnel (TwoBone)
    float poleTwist = 0.0f;
    float weight = 1.0f;
    bool enabled = true;

    friend bool operator==(const IKControl&, const IKControl&) = default;
};

// Évalue la couche IK par-dessus une pose FK (clip ou retouches) : cibles et poles lus dans la pose FK
// courante (monde), chaînes résolues dans l'ordre de dépendance. Les contrôles désactivés, de poids nul ou
// dont un os a disparu sont ignorés. `time` (secondes) sert aux verrous de pied ; une valeur négative les
// ignore.
std::vector<IKResult> applyIKControls(const Skeleton& skeleton, Pose& pose,
                                      const std::vector<IKControl>& controls, double time = -1.0);

// Verrou actif d'un contrôle au temps donné (nullptr si aucun).
const FootLock* activeLock(const IKControl& control, double time);

// Vrai si un contrôle actif pilote cet os (os de la chaîne ou effecteur dont l'orientation est imposée).
bool isIKDriven(const std::vector<IKControl>& controls, BoneId id);

// Vrai si au moins un contrôle est actif (enabled et weight > 0).
bool anyIKActive(const std::vector<IKControl>& controls);

// Retire les contrôles dont un os a disparu (chaîne, effecteur, cible) ; le pole absent est simplement
// oublié. Retourne vrai si quelque chose a changé.
bool pruneIKControls(const Skeleton& skeleton, std::vector<IKControl>& controls);

}  // namespace rigger
