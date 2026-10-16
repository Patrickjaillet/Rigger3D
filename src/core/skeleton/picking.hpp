// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <vector>

#include "core/math/ray.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Segments affichés (et donc sélectionnables) d'un os : le lien parent -> os, et, pour une feuille, un
// tronçon d'une longueur `Bone::length` le long de l'axe Y local. Le rendu et le picking partagent cette
// définition.
struct BoneSegments {
    bool hasLink = false;
    glm::vec3 linkFrom{0.0f};  // origine du parent
    glm::vec3 linkTo{0.0f};    // origine de l'os
    bool hasStub = false;
    glm::vec3 stubTo{0.0f};  // extrémité du tronçon de feuille (part de l'origine de l'os)
    glm::vec3 origin{0.0f};
};
BoneSegments boneSegments(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId id);

// Tolérance de sélection : rayon monde = worldPerPixel + distance_à_la_caméra * anglePerPixel, multiplié par
// tolerancePixels. Perspective : anglePerPixel = fovY / hauteur, worldPerPixel = 0. Orthographique :
// l'inverse.
struct PickParams {
    glm::vec3 cameraPosition{0.0f};
    float anglePerPixel = 0.0f;
    float worldPerPixel = 0.0f;
    float tolerancePixels = 8.0f;
};

struct PickHit {
    BoneId id = kNoBone;
    float t = 0.0f;  // distance le long du rayon
};

// Os touchés par le rayon, du plus proche au plus lointain (égalité : plus petit BoneId d'abord). Seuls les
// os pour lesquels `pickable` renvoie vrai sont considérés (masqués / verrouillés / hors isolation : à
// exclure). Un clic répété au même endroit parcourt cette liste (os superposés).
std::vector<PickHit> pickBones(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                               const Ray& ray, const PickParams& params,
                               const std::function<bool(BoneId)>& pickable);

// Os dont l'origine se projette dans le rectangle (coordonnées normalisées de l'écran, [-1, 1]).
std::vector<BoneId> bonesInRect(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                                const glm::mat4& viewProjection, const glm::vec2& ndcMin,
                                const glm::vec2& ndcMax, const std::function<bool(BoneId)>& pickable);

// Choisit l'os à sélectionner pour un clic : le premier de `hits` ; si le clic répète le précédent (mêmes
// candidats, os actuellement sélectionné dans la liste), passe au suivant (cycle).
BoneId cyclePick(const std::vector<PickHit>& hits, BoneId currentlySelected);

}  // namespace rigger
