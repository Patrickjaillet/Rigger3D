// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace rigger {

// Plus petite rotation amenant la direction a sur la direction b (a et b non nécessairement unitaires).
// Cas dégénérés : vecteurs nuls ou parallèles -> identité ; opposés -> demi-tour autour d'un axe
// perpendiculaire quelconque.
glm::quat fromTo(const glm::vec3& a, const glm::vec3& b);

// Interpolation sphérique en suivant TOUJOURS le chemin le plus court (corrige l'hémisphère q / -q).
// Résultat normalisé.
glm::quat slerpShortest(const glm::quat& a, const glm::quat& b, float u);

// Décomposition q = swing * twist, où twist est la rotation autour de `axis` et swing une rotation
// d'axe perpendiculaire à `axis`.
struct SwingTwist {
    glm::quat swing;
    glm::quat twist;
};
SwingTwist swingTwist(const glm::quat& q, const glm::vec3& axis);

// Angles d'Euler XYZ en radians, convention "extrinsèque" (comme Blender) :
//   R = Rz(z) * Ry(y) * Rx(x)   (rotation autour de X d'abord, puis Y, puis Z, axes du monde).
// toEulerXYZ renvoie y dans [-pi/2, pi/2]. En blocage de cardan (|y| = pi/2), z vaut 0.
// À n'utiliser que pour l'AFFICHAGE / la saisie dans l'interface : l'état stocke toujours des quaternions.
glm::vec3 toEulerXYZ(const glm::quat& q);
glm::quat fromEulerXYZ(const glm::vec3& euler);

// Vrai si a et b représentent la même rotation (q et -q sont équivalents).
bool sameRotation(const glm::quat& a, const glm::quat& b, float eps = 1e-5f);

}  // namespace rigger
