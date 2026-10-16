// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/character/character.hpp"
#include "core/skeleton/pose.hpp"

namespace rigger {

// Données sur lesquelles les commandes agissent : le personnage (squelette, poids, clips) et la pose courante
// (retouches manuelles en modes Skin / Animate, tant que les clés d'animation de la Phase 9 n'existent pas).
struct EditContext {
    Character* character = nullptr;
    Pose* pose = nullptr;
};

// Ce qu'une commande modifie : l'interface s'en sert pour savoir quoi rafraîchir.
enum class Effect : std::uint32_t {
    None = 0,
    Transforms = 1u << 0,  // transformations locales (bind ou pose)
    Properties = 1u << 1,  // nom, longueur, couleur, drapeaux
    Structure = 1u << 2,   // hiérarchie : ajout, suppression, re-parentage
    Weights = 1u << 3,     // poids de skinning
    Animation = 1u << 4,   // clips : clés, durée, liste de clips, bibliothèque de poses
    IK = 1u << 5,          // contrôles IK (chaînes, poids, cibles)
    Deform = 1u << 6,      // twist, contraintes, spring bones, drivers de blend shapes
};
constexpr Effect operator|(Effect a, Effect b) {
    return static_cast<Effect>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr bool hasEffect(Effect set, Effect flag) {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

// Commande annulable. execute() et undo() ne lèvent pas d'exception et sont symétriques : après execute()
// puis undo() l'état est identique à l'état de départ (comparaison structurelle, testée par fuzzing).
class Command {
public:
    virtual ~Command() = default;

    virtual void execute(EditContext& context) = 0;
    virtual void undo(EditContext& context) = 0;

    // Absorbe `next` (exécutée juste après cette commande) si les deux ne forment qu'une seule action pour
    // l'utilisateur (p. ex. un glissé de curseur) : cette commande contient alors l'effet des deux.
    virtual bool merge(const Command& next) {
        (void)next;
        return false;
    }

    // Clé de nom stable (anglais, traduite par l'interface) et détail libre (nom d'os…).
    virtual std::string name() const = 0;
    virtual std::string detail() const { return {}; }
    virtual Effect effects() const = 0;
    // Os dont la transformation / les propriétés changent (pour un rafraîchissement ciblé).
    virtual std::vector<BoneId> affectedBones() const { return {}; }
};

}  // namespace rigger
