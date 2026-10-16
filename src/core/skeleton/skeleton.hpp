// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/math/transform.hpp"
#include "core/result.hpp"
#include "core/skeleton/pose.hpp"

namespace rigger {

// Identifiant STABLE d'un os : jamais réutilisé après suppression, jamais modifié par un re-parentage.
// C'est aussi l'indice dans une Pose / dans les tableaux renvoyés par computeGlobals.
using BoneId = std::uint32_t;
inline constexpr BoneId kNoBone = std::numeric_limits<BoneId>::max();

enum class BoneFlags : std::uint32_t {
    None = 0,
    Hidden = 1u << 0,
    Locked = 1u << 1,
    Control = 1u << 2,  // os de contrôle (cible / pole IK) : pas de skinning, dessiné comme un contrôleur
    Morph = 1u << 3     // os de contrôle d'une blend shape : sa translation X est le poids (jamais dessiné)
};
constexpr BoneFlags operator|(BoneFlags a, BoneFlags b) {
    return static_cast<BoneFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr BoneFlags operator&(BoneFlags a, BoneFlags b) {
    return static_cast<BoneFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr bool hasFlag(BoneFlags value, BoneFlags flag) { return (value & flag) == flag; }

struct Bone {
    BoneId id = kNoBone;
    std::string name;
    BoneId parent = kNoBone;
    Transform bindLocal;  // pose de repos, locale (relative au parent)
    float length = 0.1f;  // longueur d'affichage (mètres), sans effet sur l'animation
    BoneFlags flags = BoneFlags::None;
    glm::vec3 color{0.1f, 0.7f, 1.0f};  // couleur d'affichage (RGB 0..1), sans effet sur l'animation
};

enum class RemoveMode {
    WithDescendants,   // supprime l'os et tous ses descendants
    ReparentChildren,  // les enfants passent au parent de l'os supprimé (leur position monde est conservée)
};

// Hiérarchie d'os. Invariants garantis après chaque opération :
//  - les noms sont uniques et non vides ;
//  - order() est un tri topologique : un parent précède toujours ses enfants ;
//  - la hiérarchie est une forêt (aucun cycle).
// Non thread-safe (les caches bindGlobals/inverseBind sont recalculés paresseusement).
class Skeleton {
public:
    // Ajoute un os (parent = kNoBone pour une racine). Si le nom existe déjà, un suffixe ".001" est ajouté.
    Result<BoneId> addBone(std::string name, BoneId parent, const Transform& bindLocal = {});

    Result<void> removeBone(BoneId id, RemoveMode mode = RemoveMode::WithDescendants);

    // Change le parent. Refuse les cycles. keepWorld : conserve la position monde de bind de l'os
    // (met à jour bindLocal) ; sinon bindLocal est conservé tel quel.
    Result<void> reparent(BoneId id, BoneId newParent, bool keepWorld = false);

    // Renomme (nom rendu unique si déjà pris par un autre os ; lire bone(id).name pour le résultat).
    Result<void> rename(BoneId id, std::string newName);

    // Réinsère un os avec son identifiant d'origine (pour l'annulation / undo).
    Result<void> restoreBone(const Bone& bone);

    bool contains(BoneId id) const { return id < alive_.size() && alive_[id] != 0; }
    const Bone& bone(BoneId id) const;
    BoneId find(std::string_view name) const;  // kNoBone si introuvable
    std::vector<BoneId> children(BoneId id) const;
    std::vector<BoneId> rootBones() const;
    bool isDescendantOf(BoneId id, BoneId ancestor) const;

    std::size_t boneCount() const { return count_; }
    std::size_t slotCount() const { return slots_.size(); }      // taille des tableaux indexés par BoneId
    const std::vector<BoneId>& order() const { return order_; }  // ordre topologique (parents d'abord)

    void setBindLocal(BoneId id, const Transform& bindLocal);
    void setLength(BoneId id, float length);
    void setFlags(BoneId id, BoneFlags flags);
    void setColor(BoneId id, const glm::vec3& color);

    Pose bindPose() const;
    const std::vector<glm::mat4>& bindGlobals() const;  // transformations monde de la pose de repos
    const std::vector<glm::mat4>& inverseBind() const;

    // Remplace tout le contenu par celui de `other` (annulation d'une édition structurelle) en gardant une
    // révision strictement croissante : les caches externes indexés sur revision() restent valides.
    void restoreFrom(const Skeleton& other);

    // Incrémenté à chaque modification (utile pour invalider les caches externes).
    std::uint64_t revision() const { return revision_; }

private:
    std::string uniqueName(const std::string& base, BoneId self) const;
    void ensureSlot(BoneId id);
    void eraseSlot(BoneId id);
    void rebuildOrder();
    void ensureCache() const;
    void touch();

    std::vector<Bone> slots_;
    std::vector<std::uint8_t> alive_;
    std::unordered_map<std::string, BoneId> byName_;
    std::vector<BoneId> order_;
    std::size_t count_ = 0;
    BoneId nextId_ = 0;
    std::uint64_t revision_ = 0;

    mutable bool cacheDirty_ = true;
    mutable std::vector<glm::mat4> bindGlobals_;
    mutable std::vector<glm::mat4> inverseBind_;
};

}  // namespace rigger
