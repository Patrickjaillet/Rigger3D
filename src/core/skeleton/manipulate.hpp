// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "core/math/transform.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Repère d'orientation d'un gizmo : axes locaux de l'os, axes du monde, ou axes de son parent.
enum class GizmoSpace { Local, Global, Parent };

// Orientation MONDE (quaternion unitaire) du repère choisi pour l'os `id`, d'après ses matrices monde.
glm::quat spaceOrientation(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId id,
                           GizmoSpace space);

// Moyenne des origines monde des os (pivot du gizmo d'une multi-sélection).
glm::vec3 selectionPivot(const std::vector<glm::mat4>& globals, const std::vector<BoneId>& ids);

// Retire de la sélection les os dont un ancêtre est aussi sélectionné : ils suivent déjà leur parent (FK),
// leur appliquer le même déplacement le doublerait.
std::vector<BoneId> topLevelBones(const Skeleton& skeleton, const std::vector<BoneId>& ids);

// Déplacement exprimé dans le repère MONDE, tel que produit par un gizmo pendant un glissé.
//  - translation : décalage monde ;
//  - rotation    : rotation monde appliquée à l'orientation de l'os (autour de son origine) ;
//  - scale       : facteurs par axe de `scaleBasis` (repère du gizmo).
struct WorldDelta {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::quat scaleBasis{1.0f, 0.0f, 0.0f, 0.0f};

    bool isIdentity() const;
};

// Nouvelle transformation LOCALE de l'os après application du déplacement, calculée à partir de l'état de
// DÉPART du glissé (`startGlobals`, `startLocal`) : le résultat est absolu, donc stable quel que soit le
// nombre de mises à jour. Hypothèse : le parent n'a pas d'échelle non uniforme (sinon un cisaillement non
// représentable apparaît). Échelle : exacte pour un facteur uniforme ou dans le repère local ; sinon projetée
// sur les axes locaux.
Transform applyWorldDelta(const Skeleton& skeleton, const std::vector<glm::mat4>& startGlobals,
                          const Transform& startLocal, BoneId id, const WorldDelta& delta);

}  // namespace rigger
