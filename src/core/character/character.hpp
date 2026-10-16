// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/animation/clip.hpp"
#include "core/animation/mix.hpp"
#include "core/deform/deform.hpp"
#include "core/ik/rig.hpp"
#include "core/mesh/mesh.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Un personnage : squelette + maillages (skinnés sur ce squelette) + clips d'animation.
// `warnings` : problèmes non bloquants rencontrés à l'import (à afficher dans l'UI / le journal).
// Pose nommée de la bibliothèque : transformations locales par NOM d'os (applicable à tout squelette de mêmes
// noms).
struct NamedPose {
    std::string name;
    std::vector<std::pair<std::string, Transform>> bones;
};

struct Character {
    std::string name;
    Skeleton skeleton;
    std::vector<Mesh> meshes;
    std::vector<AnimationClip> clips;
    std::vector<NamedPose> poses;       // bibliothèque de poses
    DeformRig deform;                   // os de twist, contraintes, spring bones
    AnimationMix mix;                   // couches, séquenceur NLA, blend tree
    std::vector<IKControl> ikControls;  // contrôles IK (cibles / poles = os de contrôle)
    std::vector<std::string> warnings;
};

}  // namespace rigger
