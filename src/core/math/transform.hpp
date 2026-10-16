// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
// Transformation TRS (translation, rotation, échelle). Repère main droite, Y haut, radians.
//
// Convention : apply(p) = t + r * (s * p)   (l'échelle est appliquée en premier, puis la rotation)
// Limite connue : la composition et l'inverse de TRS sont EXACTES quand l'échelle du parent est
// uniforme, ou quand l'enfant n'a pas de rotation. Un parent à échelle non uniforme avec un enfant
// tourné produit du cisaillement, non représentable en TRS : pour les poses d'animation, le calcul
// monde passe donc par des mat4 (voir computeGlobals). L'inverse() est exact pour une échelle uniforme.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace rigger {

struct Transform {
    glm::vec3 t{0.0f};
    glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z
    glm::vec3 s{1.0f};

    bool operator==(const Transform&) const = default;

    glm::mat4 toMat4() const;
    // Décompose une matrice affine (sans perspective). Un déterminant négatif (miroir) est porté par s.x.
    static Transform fromMat4(const glm::mat4& m);

    glm::vec3 apply(const glm::vec3& p) const { return t + r * (s * p); }
    glm::vec3 applyDir(const glm::vec3& v) const { return r * (s * v); }

    Transform inverse() const;
    Transform operator*(const Transform& child) const;  // (*this) puis child : parent * enfant
};

// Interpolation : lerp de t et s, slerp (chemin le plus court) de r.
Transform lerp(const Transform& a, const Transform& b, float u);

}  // namespace rigger
