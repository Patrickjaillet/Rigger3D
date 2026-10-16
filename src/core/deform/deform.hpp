// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Déformations avancées appliquées par-dessus la pose (après l'IK) : os de twist, contraintes, physique
// secondaire. Ordre d'évaluation : clip / FK -> IK -> twist -> contraintes -> spring bones -> skinning.

// --- Os de twist
// --------------------------------------------------------------------------------------------------- La
// torsion de l'os `source` (autour de `axis`, repère de repos de l'os) est répartie sur les os de twist :
// chaque os reçoit la fraction `factors[i]` de la torsion (avant-bras : 1/3, 2/3 vers le poignet). La source
// garde sa torsion complète ; les os de twist sont des os frères / enfants de l'avant-bras qui portent une
// partie du skinning.
struct TwistChain {
    std::string name;
    BoneId source = kNoBone;
    std::vector<BoneId> bones;
    std::vector<float> factors;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    bool enabled = true;
    bool operator==(const TwistChain&) const = default;
};

// Chaîne de twist avec fractions régulières (i + 1) / (n + 1).
TwistChain makeTwistChain(BoneId source, const std::vector<BoneId>& twistBones,
                          const glm::vec3& axis = {0.0f, 1.0f, 0.0f});
void applyTwistChains(const Skeleton& skeleton, Pose& pose, const std::vector<TwistChain>& chains);

// --- Contraintes
// -------------------------------------------------------------------------------------------------------
enum class ConstraintKind { CopyTransform, Limit, ChildOf, TrackTo };

struct Constraint {
    std::string name;
    ConstraintKind kind = ConstraintKind::CopyTransform;
    BoneId bone = kNoBone;    // os contraint
    BoneId target = kNoBone;  // cible (inutile pour Limit)
    float influence = 1.0f;
    bool enabled = true;
    // CopyTransform : composantes copiées (repère monde).
    bool copyLocation = true, copyRotation = true, copyScale = false;
    // Limit (repère local) : bornes de position, d'angles d'Euler XYZ (radians) et d'échelle.
    bool limitLocation = false, limitRotation = true, limitScale = false;
    glm::vec3 locationMin{-1e9f}, locationMax{1e9f};
    glm::vec3 rotationMin{-3.14159265f}, rotationMax{3.14159265f};
    glm::vec3 scaleMin{0.0f}, scaleMax{1e9f};
    // ChildOf : transformation inverse de la cible capturée à la création (le contraint suit les mouvements
    // de la cible).
    glm::mat4 inverse{1.0f};
    // TrackTo : axe local de l'os qui doit viser la cible.
    glm::vec3 aimAxis{0.0f, 1.0f, 0.0f};
    bool operator==(const Constraint&) const = default;
};

// Crée un ChildOf avec la transformation inverse actuelle de la cible (le contraint ne bouge pas à la
// création).
Constraint makeChildOf(const Skeleton& skeleton, const Pose& pose, BoneId bone, BoneId target);
void applyConstraints(const Skeleton& skeleton, Pose& pose, const std::vector<Constraint>& constraints);

// --- Physique secondaire (spring bones)
// ------------------------------------------------------------------------------ Chaîne d'os (parent ->
// enfant) simulée par Verlet : chaque pointe d'os est tirée vers sa position animée (raideur), amortie,
// soumise à la gravité, à longueur d'os constante et poussée hors des colliders sphériques.
struct SpringChain {
    std::string name;
    std::vector<BoneId> bones;
    float stiffness = 0.2f;  // 0 : libre, 1 : rigide (suit l'animation)
    float damping = 0.1f;    // 0 : sans frottement, 1 : figé
    float gravity = 1.0f;    // multiplicateur de la gravité (9,81 m/s^2 vers -Y)
    bool enabled = true;
    bool operator==(const SpringChain&) const = default;
};

struct SpringCollider {
    BoneId bone = kNoBone;   // os qui porte la sphère (kNoBone : fixe dans le monde)
    glm::vec3 offset{0.0f};  // centre dans le repère de l'os
    float radius = 0.1f;
    bool operator==(const SpringCollider&) const = default;
};

// État de simulation (positions monde des pointes). `valid` faux : sera initialisé à la pose animée au
// prochain pas.
struct SpringState {
    std::vector<std::vector<glm::vec3>> position;
    std::vector<std::vector<glm::vec3>> previous;
    bool valid = false;
    void reset() { *this = SpringState{}; }
};

struct DeformRig {
    std::vector<TwistChain> twists;
    std::vector<Constraint> constraints;
    std::vector<SpringChain> springs;
    std::vector<SpringCollider> colliders;
    bool springsEnabled = true;
    bool operator==(const DeformRig&) const = default;

    bool empty() const { return twists.empty() && constraints.empty() && springs.empty(); }
};

// Avance la simulation de `dt` secondes sur la pose animée `pose` (modifiée : rotations des os de chaque
// chaîne). `substeps` sous-pas de Verlet pour la stabilité. Sans chaîne active : aucun effet.
void stepSprings(const Skeleton& skeleton, Pose& pose, const DeformRig& rig, SpringState& state, double dt,
                 int substeps = 2);

// Applique twist puis contraintes (tout ce qui est sans état).
void applyStatelessDeform(const Skeleton& skeleton, Pose& pose, const DeformRig& rig);

// Retire de la configuration tout ce qui référence un os absent (retourne vrai si quelque chose a changé).
bool pruneDeformRig(const Skeleton& skeleton, DeformRig& rig);

}  // namespace rigger
