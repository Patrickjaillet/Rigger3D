// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/command/command.hpp"
#include "core/math/transform.hpp"

namespace rigger {

// Transformations locales de plusieurs os : bind pose (mode Rig) ou pose courante. Couvre SetBoneTransform,
// SetPose, le résultat d'un glissé de gizmo, le reset et le collage de pose. `mergeKey` non vide : fusion
// avec la commande précédente de même clé, même cible et mêmes os (curseur de l'inspecteur : un glissé = une
// entrée).
class SetBoneTransformsCommand : public Command {
public:
    enum class Target { Bind, Pose };
    struct Entry {
        BoneId id = kNoBone;
        Transform before;
        Transform after;
    };

    SetBoneTransformsCommand(Target target, std::vector<Entry> entries, std::string name,
                             std::string mergeKey = {});

    void execute(EditContext& context) override { apply(context, true); }
    void undo(EditContext& context) override { apply(context, false); }
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    Effect effects() const override { return Effect::Transforms; }
    std::vector<BoneId> affectedBones() const override;
    Target target() const { return target_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    void apply(EditContext& context, bool forward) const;
    Target target_;
    std::vector<Entry> entries_;
    std::string name_;
    std::string mergeKey_;
};

// Propriétés d'os non géométriques : nom, longueur, couleur, drapeaux (renommage, masquage, verrouillage…).
struct BoneProps {
    std::string name;
    float length = 0.1f;
    glm::vec3 color{0.1f, 0.7f, 1.0f};
    BoneFlags flags = BoneFlags::None;

    static BoneProps of(const Bone& bone) { return {bone.name, bone.length, bone.color, bone.flags}; }
    bool operator==(const BoneProps& o) const {
        return name == o.name && length == o.length && color == o.color && flags == o.flags;
    }
};

class SetBonePropertiesCommand : public Command {
public:
    struct Entry {
        BoneId id = kNoBone;
        BoneProps before;
        BoneProps after;
    };

    SetBonePropertiesCommand(std::vector<Entry> entries, std::string name, std::string mergeKey = {});

    void execute(EditContext& context) override { apply(context, true); }
    void undo(EditContext& context) override { apply(context, false); }
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    std::string detail() const override;
    Effect effects() const override { return Effect::Properties; }
    std::vector<BoneId> affectedBones() const override;

private:
    void apply(EditContext& context, bool forward) const;
    std::vector<Entry> entries_;
    std::string name_;
    std::string mergeKey_;
};

// État éditable « structurel » d'un personnage : squelette, poids de skinning de chaque maillage, pistes
// d'animation. Les sommets des maillages ne changent jamais par ces éditions et ne sont donc pas copiés.
struct StructureState {
    Skeleton skeleton;
    std::vector<std::vector<SkinWeight>> weights;
    std::vector<AnimationClip> clips;
    std::vector<IKControl> ikControls;
    AnimationMix mix;
    DeformRig deform;
    std::vector<std::vector<BoneId>> morphBones;  // os de poids des blend shapes, par maillage et par cible

    static StructureState capture(const Character& character);
    void applyTo(Character& character) const;
};

// Ajout / suppression / subdivision / re-parentage d'os : instantanés avant / après (identifiants d'os
// stables restaurés tels quels). La commande est enregistrée APRÈS l'édition (record), jamais exécutée à
// froid.
class StructureCommand : public Command {
public:
    StructureCommand(std::string name, std::string detail, StructureState before, StructureState after);

    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override {
        return Effect::Structure | Effect::Weights | Effect::Transforms | Effect::IK;
    }

private:
    std::string name_;
    std::string detail_;
    StructureState before_;
    StructureState after_;
};

// --- Animation (Phase 9) ---------------------------------------------------------------------------

// Pistes de plusieurs os d'un clip (insertion / suppression / déplacement / copie de clés, mode
// d'interpolation, tangentes) : instantanés complets des BoneTracks touchés avant / après, plus la durée du
// clip. Fusion possible (même clip, mêmes os, même clé) pour les modifications continues.
class EditTracksCommand : public Command {
public:
    struct Entry {
        BoneId id = kNoBone;
        BoneTracks before;
        BoneTracks after;
    };

    EditTracksCommand(std::size_t clipIndex, std::vector<Entry> entries, double durationBefore,
                      double durationAfter, std::string name, std::string mergeKey = {});

    void execute(EditContext& context) override { apply(context, true); }
    void undo(EditContext& context) override { apply(context, false); }
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    Effect effects() const override { return Effect::Animation; }
    std::vector<BoneId> affectedBones() const override;

private:
    void apply(EditContext& context, bool forward) const;
    std::size_t clipIndex_;
    std::vector<Entry> entries_;
    double durationBefore_;
    double durationAfter_;
    std::string name_;
    std::string mergeKey_;
};

// Réglages d'un clip : nom, durée, cadence, boucle, repères, mouvement de racine.
struct ClipMeta {
    std::string name;
    double duration = 0.0;
    float fps = 30.0f;
    bool loop = true;
    std::vector<Marker> markers;
    bool rootMotion = false;
    BoneId rootBone = kNoBone;

    static ClipMeta of(const AnimationClip& clip);
    void applyTo(AnimationClip& clip) const;
};

class EditClipMetaCommand : public Command {
public:
    EditClipMetaCommand(std::size_t clipIndex, ClipMeta before, ClipMeta after, std::string name);
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return after_.name; }
    Effect effects() const override { return Effect::Animation; }

private:
    std::size_t clipIndex_;
    ClipMeta before_;
    ClipMeta after_;
    std::string name_;
};

// Liste des clips (nouveau, duplication, suppression) : instantanés complets avant / après.
class EditClipListCommand : public Command {
public:
    EditClipListCommand(std::vector<AnimationClip> before, std::vector<AnimationClip> after, std::string name,
                        std::string detail = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Animation; }

private:
    std::vector<AnimationClip> before_;
    std::vector<AnimationClip> after_;
    std::string name_;
    std::string detail_;
};

// Bibliothèque de poses nommées (enregistrer / supprimer / renommer).
class EditPoseLibraryCommand : public Command {
public:
    EditPoseLibraryCommand(std::vector<NamedPose> before, std::vector<NamedPose> after, std::string name,
                           std::string detail = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Animation; }

private:
    std::vector<NamedPose> before_;
    std::vector<NamedPose> after_;
    std::string name_;
    std::string detail_;
};

// --- Poids de skinning (Phase 11) -----------------------------------------------------------------

// Poids modifiés d'un maillage : seuls les sommets touchés sont stockés (delta avant / après).
struct VertexWeightDelta {
    std::uint32_t vertex = 0;
    SkinWeight before;
    SkinWeight after;
};
struct MeshWeightDelta {
    std::size_t mesh = 0;
    std::vector<VertexWeightDelta> vertices;
};

// Un coup de pinceau (glissé entier), un outil global ou une saisie numérique = une commande, stockant
// seulement les sommets modifiés. Enregistrée après l'édition (record) : execute() ré-applique le « après ».
class EditWeightsCommand : public Command {
public:
    EditWeightsCommand(std::vector<MeshWeightDelta> deltas, std::string name, std::string detail = {});
    void execute(EditContext& context) override { apply(context, true); }
    void undo(EditContext& context) override { apply(context, false); }
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Weights; }
    std::size_t vertexCount() const;

private:
    void apply(EditContext& context, bool forward) const;
    std::vector<MeshWeightDelta> deltas_;
    std::string name_;
    std::string detail_;
};

// Compare les poids actuels du personnage aux poids `before` (un tableau par maillage) et ne garde que les
// sommets modifiés (liste vide si rien n'a changé).
std::vector<MeshWeightDelta> diffWeights(const Character& character,
                                         const std::vector<std::vector<SkinWeight>>& before);

// --- Mélange d'animations (Phase 13) ---------------------------------------------------------------------

// Couches, bandes NLA, blend tree et mode de mélange : instantanés avant / après, fusion possible (curseurs).
class EditMixCommand : public Command {
public:
    EditMixCommand(AnimationMix before, AnimationMix after, std::string name, std::string detail = {},
                   std::string mergeKey = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Animation; }

private:
    AnimationMix before_;
    AnimationMix after_;
    std::string name_;
    std::string detail_;
    std::string mergeKey_;
};

// --- Déformations avancées (Phase 14)
// ------------------------------------------------------------------------

// Twist, contraintes, spring bones et colliders : instantanés avant / après, fusion possible (curseurs).
class EditDeformCommand : public Command {
public:
    EditDeformCommand(DeformRig before, DeformRig after, std::string name, std::string detail = {},
                      std::string mergeKey = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Deform; }

private:
    DeformRig before_;
    DeformRig after_;
    std::string name_;
    std::string detail_;
    std::string mergeKey_;
};

// Pilotes correctifs d'une blend shape (maillage, cible) : avant / après.
class EditMorphDriversCommand : public Command {
public:
    EditMorphDriversCommand(std::size_t mesh, std::size_t target, std::vector<MorphDriver> before,
                            std::vector<MorphDriver> after, std::string name, std::string detail = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::Deform; }

private:
    void set(EditContext& context, const std::vector<MorphDriver>& drivers) const;
    std::size_t mesh_, target_;
    std::vector<MorphDriver> before_, after_;
    std::string name_, detail_;
};

// --- IK (Phase 10) -----------------------------------------------------------------------------------

// Liste des contrôles IK (création, suppression, réglages) : instantanés avant / après. Fusion possible pour
// les réglages continus (curseur de poids...).
class EditIKControlsCommand : public Command {
public:
    EditIKControlsCommand(std::vector<IKControl> before, std::vector<IKControl> after, std::string name,
                          std::string detail = {}, std::string mergeKey = {});
    void execute(EditContext& context) override;
    void undo(EditContext& context) override;
    bool merge(const Command& next) override;
    std::string name() const override { return name_; }
    std::string detail() const override { return detail_; }
    Effect effects() const override { return Effect::IK; }

private:
    std::vector<IKControl> before_;
    std::vector<IKControl> after_;
    std::string name_;
    std::string detail_;
    std::string mergeKey_;
};

}  // namespace rigger
