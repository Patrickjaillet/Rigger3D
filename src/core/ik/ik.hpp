// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include "core/math/transform.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Cinématique inverse (IK). Une chaîne est un chemin parent -> enfant d'os :
//   bones[0] (racine de la chaîne) -> bones[1] -> ... -> bones[n-1], puis `effector`, enfant direct de
//   bones[n-1].
// Les articulations sont les ORIGINES des os : J_i = origine de bones[i] (i < n) et J_n = origine de
// l'effecteur (poignet, cheville, bout de la queue...). Les solveurs ne modifient que les rotations locales
// de bones[0..n-1] (et, sur demande, celle de l'effecteur) : les longueurs d'os sont donc toujours
// conservées. Exemple bras : bones = {bras, avant-bras}, effector = main  =>  épaule, coude, poignet.

enum class IKSolver { TwoBone, FABRIK, CCD };

// Limite d'articulation, exprimée dans le repère de repos de l'os : rel = inverse(bind.r) * local.r.
enum class JointLimitType { None, Hinge, Cone };
struct JointLimit {
    JointLimitType type = JointLimitType::None;
    glm::vec3 axis{1.0f, 0.0f,
                   0.0f};  // charnière : axe de rotation ; cône : axe de torsion (dans le repère de repos)
    float min = -glm::pi<float>();  // charnière : angle min ; cône : torsion min (radians)
    float max = glm::pi<float>();
    float swing = glm::pi<float>();  // cône : écart maximal à l'axe (radians)

    bool operator==(const JointLimit&) const = default;
};

enum class EffectorOrientation {
    Follow,     // l'effecteur suit simplement son parent
    KeepWorld,  // l'effecteur garde son orientation monde d'avant la résolution (poignet qui ne bascule pas)
    MatchTarget,  // l'effecteur prend l'orientation de la cible
};

struct IKChain {
    std::vector<BoneId> bones;
    BoneId effector = kNoBone;
    IKSolver solver = IKSolver::FABRIK;
    std::vector<JointLimit> limits;  // optionnel : une limite par os de `bones` (peut être plus court)
    int maxIterations = 20;
    float tolerance = 1e-3f;  // mètres
    float damping = 1.0f;     // CCD : fraction de la rotation appliquée à chaque pas (0..1]
    EffectorOrientation effectorOrientation = EffectorOrientation::Follow;

    bool operator==(const IKChain&) const = default;
};

struct IKTarget {
    Transform world;   // position (et orientation, pour MatchTarget) monde de l'effecteur
    float weight = 1;  // 0 : FK pure, 1 : IK pure (mélange par slerp des rotations locales)
    bool hasPole = false;
    glm::vec3 pole{0.0f};  // position monde du pole (TwoBone) : le coude / genou se place de son côté
    float poleTwist =
        0.0f;  // rotation supplémentaire du plan de flexion autour de l'axe racine -> cible (radians)
};

struct IKResult {
    bool valid = false;    // chaîne valide et poids > 0
    bool reached = false;  // l'effecteur est à moins de `tolerance` de la cible
    float error = 0.0f;    // distance finale effecteur - cible (mètres)
};

struct IKEntry {
    IKChain chain;
    IKTarget target;
};

// Vérifie la chaîne : os vivants, bones[i+1] enfant de bones[i], effector enfant de bones.back().
bool isValidChain(const Skeleton& skeleton, const IKChain& chain);

// Chaîne de `count` os remontant depuis le PARENT de `effector` (ex. effector = main, count = 2 : bras +
// avant-bras). Retourne une chaîne vide (bones.empty()) si l'effecteur n'a pas assez d'ancêtres.
IKChain makeChain(const Skeleton& skeleton, BoneId effector, int count, IKSolver solver = IKSolver::FABRIK);

// Applique une limite à une rotation locale (retourne la rotation corrigée).
glm::quat applyJointLimit(const JointLimit& limit, const Transform& bindLocal, const glm::quat& local);

// Oriente un os pour que son axe local `aimAxis` pointe vers `target` (monde). `limit` s'applique à la
// rotation locale obtenue ; `weight` mélange avec la pose d'origine.
void solveLookAt(const Skeleton& skeleton, Pose& pose, BoneId bone, const glm::vec3& aimAxis,
                 const glm::vec3& target, float weight, const JointLimit& limit = {});

// Résout une chaîne : modifie `pose` (indexée par BoneId, complétée par la bind pose si trop courte).
IKResult solveIK(const Skeleton& skeleton, Pose& pose, const IKChain& chain, const IKTarget& target);

// Résout plusieurs chaînes dans l'ordre de dépendance (une chaîne enfant après celle dont elle dépend).
std::vector<IKResult> solveIKLayer(const Skeleton& skeleton, Pose& pose, const std::vector<IKEntry>& entries);

}  // namespace rigger
