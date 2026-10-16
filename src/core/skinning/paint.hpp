// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

#include "core/mesh/mesh.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Édition des poids de skinning (weight painting). Tout est pur (sans Qt, sans OpenGL) : le viewport fournit
// le centre du pinceau et les positions des sommets ; ce module décide des nouveaux poids. Invariants
// garantis après chaque opération : poids finis, dans [0, 1], au plus 4 influences par sommet, os verrouillés
// jamais modifiés ; avec la normalisation automatique la somme vaut exactement 1.

// Os verrouillés (leurs poids ne bougent pas), indexés par BoneId.
struct WeightLocks {
    std::vector<bool> locked;
    bool isLocked(BoneId id) const { return id < locked.size() && locked[id]; }
    void set(BoneId id, bool value) {
        if (id >= locked.size()) locked.resize(static_cast<std::size_t>(id) + 1, false);
        locked[id] = value;
    }
};

// Poids de `bone` sur ce sommet (0 s'il n'influence pas).
float influenceOf(const SkinWeight& skin, BoneId bone);

// Fixe le poids de `bone`. Avec autoNormalize, la masse restante est redistribuée sur les autres os NON
// verrouillés (proportionnellement), la somme reste 1 et le plus petit os non verrouillé est éjecté si les 4
// emplacements sont pris. Sans autoNormalize seul l'os visé change. Retourne vrai si le sommet a changé.
bool setBoneWeight(SkinWeight& skin, BoneId bone, float value, const WeightLocks& locks, bool autoNormalize);

enum class BrushMode { Add, Subtract, Replace, Smooth };
enum class Falloff { Smooth, Linear, Constant };

struct Brush {
    float radius = 0.1f;    // mètres
    float strength = 0.5f;  // 0..1 par coup de pinceau
    Falloff falloff = Falloff::Smooth;
    BrushMode mode = BrushMode::Add;
    float value = 1.0f;  // Replace : poids visé
};

// Intensité 0..1 du pinceau à la distance donnée du centre.
float falloffWeight(Falloff falloff, float distance, float radius);

// Voisinage des sommets (arêtes des triangles) pour le lissage.
struct MeshTopology {
    std::vector<std::vector<std::uint32_t>> neighbors;
    static MeshTopology build(const Mesh& mesh);
};

// Sommets touchés par un coup de pinceau : (indice, intensité du falloff > 0). `allowed` (optionnel, un octet
// par sommet) limite les sommets éligibles (visibilité, sélection).
using BrushHits = std::vector<std::pair<std::uint32_t, float>>;
BrushHits collectBrushHits(const std::vector<glm::vec3>& positions, const glm::vec3& center,
                           const Brush& brush, const std::vector<std::uint8_t>* allowed = nullptr);

// Applique un coup de pinceau à `bone` ; retourne les sommets effectivement modifiés (triés, sans doublon).
std::vector<std::uint32_t> applyBrushHits(Mesh& mesh, const MeshTopology& topology, const BrushHits& hits,
                                          const Brush& brush, BoneId bone, const WeightLocks& locks,
                                          bool autoNormalize);

// Symétrie X : pour chaque sommet, le sommet le plus proche de sa position miroir (x -> -x), ou UINT32_MAX
// s'il n'y en a aucun à moins de `tolerance`.
std::vector<std::uint32_t> mirrorVertexMap(const std::vector<glm::vec3>& restPositions, float tolerance);
inline constexpr std::uint32_t kNoVertex = 0xFFFFFFFFu;

// Sommets visibles depuis `eye` : face à l'œil (normale) et non masqués par un autre triangle du maillage.
std::vector<std::uint8_t> visibleVertices(const std::vector<glm::vec3>& positions,
                                          const std::vector<glm::vec3>& normals,
                                          const std::vector<std::uint32_t>& indices, const glm::vec3& eye,
                                          const std::vector<std::uint32_t>& candidates);

// --- Skinning automatique ----------------------------------------------------------------------

enum class AutoSkinMethod {
    Distance,       // proximité des segments d'os, puis lissage topologique
    HeatDiffusion,  // diffusion de chaleur sur le maillage (poids lisses qui respectent la topologie)
};

struct AutoSkinOptions {
    AutoSkinMethod method = AutoSkinMethod::Distance;
    float power = 3.0f;             // Distance : pente de la décroissance 1 / distance^power
    int smoothIterations = 3;       // Distance : lissage des poids sur la topologie
    int diffusionIterations = 150;  // HeatDiffusion : itérations de Gauss-Seidel par os
    float diffusionStrength =
        1.0f;  // HeatDiffusion : poids de l'ancrage à l'os le plus proche (plus grand = plus local)
    std::size_t maxInfluences = 4;
};

// Poids par proximité des segments d'os (pose de repos, os de contrôle ignorés), lissés puis normalisés.
void autoSkin(Mesh& mesh, const Skeleton& skeleton, const AutoSkinOptions& options = {});

// Polygone (lasso) : vrai si le point est à l'intérieur (règle pair-impair).
bool pointInPolygon(const glm::vec2& point, const std::vector<glm::vec2>& polygon);

// --- Outils globaux ----------------------------------------------------------------------------

std::size_t normalizeAllWeights(Mesh& mesh);  // retourne le nombre de sommets corrigés
std::size_t limitAllInfluences(Mesh& mesh, std::size_t maxInfluences = 4);
std::size_t pruneAllWeights(Mesh& mesh, float threshold = 0.01f);
// Copie les poids du sommet le plus proche de `source` (mêmes identifiants d'os) ; retourne le nombre de
// sommets.
std::size_t copyWeightsNearest(Mesh& destination, const Mesh& source);

}  // namespace rigger
