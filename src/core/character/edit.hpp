// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include "core/character/character.hpp"
#include "core/result.hpp"

namespace rigger {

// Suppression « propre » d'un os : les poids de skinning qui le référencent passent à son plus proche ancêtre
// conservé (renormalisés ; sans ancêtre, ils sont répartis sur les autres influences, ou sur l'os racine
// restant), ses pistes d'animation disparaissent. Avec WithDescendants, les descendants sont supprimés de la
// même façon ; avec ReparentChildren, les enfants passent au parent de l'os supprimé en gardant leur position
// monde (leurs pistes d'animation locales ne sont pas recalculées).
Result<void> removeBoneClean(Character& character, BoneId id, RemoveMode mode);

// Coupe un os en deux : un nouvel os est inséré au milieu entre l'origine de `id` et son extrémité (premier
// enfant, sinon bout du tronçon) ; les enfants d'origine lui sont rattachés (position monde conservée). Les
// poids ne changent pas. Renvoie l'identifiant du nouvel os.
Result<BoneId> subdivideBone(Character& character, BoneId id);

}  // namespace rigger
