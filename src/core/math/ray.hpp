// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <optional>

namespace rigger {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};  // DOIT être unitaire (utiliser Ray::make)

    glm::vec3 at(float t) const { return origin + dir * t; }
    static Ray make(const glm::vec3& origin, const glm::vec3& direction);
};

struct TriangleHit {
    float t;  // distance le long du rayon
    float u;  // coordonnées barycentriques (poids de v1 et v2)
    float v;
};

// Les fonctions sphère/capsule renvoient la plus petite distance t >= 0 à la surface,
// ou 0 si l'origine du rayon est à l'intérieur du volume ; std::nullopt si pas d'intersection.
std::optional<float> intersectSphere(const Ray& ray, const glm::vec3& center, float radius);
std::optional<float> intersectCapsule(const Ray& ray, const glm::vec3& a, const glm::vec3& b, float radius);

// Möller-Trumbore. Avec cullBackface, les triangles vus par l'arrière (sens horaire) sont ignorés.
std::optional<TriangleHit> intersectTriangle(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1,
                                             const glm::vec3& v2, bool cullBackface = false);

// Rayon monde passant par le pixel (x, y) d'une vue de taille (width, height) (origine en haut à gauche, y
// vers le bas), pour la matrice vue-projection `viewProjection` (perspective ou orthographique).
Ray screenRay(const glm::mat4& viewProjection, float x, float y, float width, float height);

// Intersection avec le plan (point, normale) : t >= 0, std::nullopt si le rayon est parallèle ou si le plan
// est derrière. `normal` n'a pas besoin d'être unitaire.
std::optional<float> intersectPlane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal);

// Points les plus proches entre un rayon (t >= 0) et une droite infinie : paramètre sur le rayon, paramètre
// sur la droite (en unités de `lineDir`, unitaire) et distance minimale. Sert à glisser le long d'un axe de
// gizmo.
struct RayLineClosest {
    float tRay = 0.0f;
    float tLine = 0.0f;
    float distance = 0.0f;
};
std::optional<RayLineClosest> closestRayToLine(const Ray& ray, const glm::vec3& linePoint,
                                               const glm::vec3& lineDir);

// Anneau (cercle de rayon `radius` dans le plan (center, normal)) épaissi de `tolerance` : renvoie la
// distance t du point où le rayon traverse le plan si ce point est à moins de `tolerance` du cercle.
std::optional<float> intersectRing(const Ray& ray, const glm::vec3& center, const glm::vec3& normal,
                                   float radius, float tolerance);

}  // namespace rigger
