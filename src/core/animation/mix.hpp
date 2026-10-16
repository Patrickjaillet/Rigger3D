// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <string>
#include <vector>

#include "core/animation/clip.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Mélange d'animations : couches (poids, masque d'os, Override / Additif), séquenceur NLA (bandes de clips
// avec décalage, répétition, fondus), blend tree 1D (marche <-> course). Tout est pur : on évalue une Pose à
// un instant donné à partir des clips du personnage (références par indice).

enum class LayerMode { Override, Additive };

// Couche d'animation. `mask` : os racines du masque (leurs descendants sont inclus) ; vide = tout le
// squelette.
struct AnimationLayer {
    std::string name;
    int clip = 0;
    float weight = 1.0f;
    LayerMode mode = LayerMode::Override;
    std::vector<BoneId> mask;
    float speed = 1.0f;
    float timeOffset = 0.0f;  // secondes de clip ajoutées au temps de la couche
    bool loop = true;
    bool enabled = true;

    bool operator==(const AnimationLayer&) const = default;
};

// Bande du séquenceur : joue [clipStart, clipEnd] du clip (clipEnd < 0 : fin du clip) à partir de `start`
// (temps global), `repeat` fois, à la vitesse `speed`, avec fondu d'entrée / de sortie. Les bandes sont
// empilées par piste (les pistes hautes recouvrent les basses) ; sur une même piste une bande qui en recouvre
// une autre fait un fondu enchaîné grâce à son `blendIn`.
struct NlaStrip {
    std::string name;
    int clip = 0;
    int track = 0;
    double start = 0.0;
    double clipStart = 0.0;
    double clipEnd = -1.0;
    float speed = 1.0f;
    float repeat = 1.0f;
    double blendIn = 0.0;
    double blendOut = 0.0;
    float weight = 1.0f;
    LayerMode mode = LayerMode::Override;
    bool enabled = true;

    bool operator==(const NlaStrip&) const = default;
};

struct BlendTreeEntry {
    int clip = 0;
    float threshold = 0.0f;
    bool operator==(const BlendTreeEntry&) const = default;
};

// Blend tree 1D : le paramètre choisit deux clips voisins (par seuil) et les mélange ; `syncTime` aligne
// leurs phases (un cycle de marche reste synchronisé avec un cycle de course).
struct BlendTree1D {
    std::string parameterName = "speed";
    float parameter = 0.0f;
    std::vector<BlendTreeEntry> entries;
    bool syncTime = true;
    bool operator==(const BlendTree1D&) const = default;
};

enum class MixMode { Clip, Layers, Nla, BlendTree };

struct AnimationMix {
    MixMode mode = MixMode::Clip;  // Clip : un seul clip (comportement historique)
    std::vector<AnimationLayer> layers;
    std::vector<NlaStrip> strips;
    BlendTree1D tree;
    bool operator==(const AnimationMix&) const = default;
};

// Masque (un booléen par BoneId) : os racines + descendants ; vide si `roots` est vide (= tous les os).
std::vector<bool> boneMask(const Skeleton& skeleton, const std::vector<BoneId>& roots);

// Fusionne `source` dans `destination` : Override = interpolation vers `source` ; Additif = ajoute la
// différence `source` - `reference` (pose de repos du clip). `mask` (facultatif) limite les os touchés. Poids
// 0 : aucun effet.
void blendPoseInto(Pose& destination, const Pose& source, const Pose& reference, float weight, LayerMode mode,
                   const std::vector<bool>* mask = nullptr);

// Interpolation de deux poses entières (crossfade) : u = 0 -> a, u = 1 -> b.
Pose crossfade(const Pose& a, const Pose& b, float u);

// Évaluations (la pose de départ est la pose de repos). Les clips hors limites / sans pistes sont ignorés.
Pose evaluateLayers(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                    const std::vector<AnimationLayer>& layers, double time);
Pose evaluateNla(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                 const std::vector<NlaStrip>& strips, double time);
Pose evaluateBlendTree(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                       const BlendTree1D& tree, double time);
Pose evaluateMix(const Skeleton& skeleton, const std::vector<AnimationClip>& clips, const AnimationMix& mix,
                 double time);

// Durée (secondes) de la timeline globale du mélange ; 0 en mode Clip.
double mixDuration(const std::vector<AnimationClip>& clips, const AnimationMix& mix);

// Durée jouée par une bande (répétitions et vitesse comprises).
double stripLength(const NlaStrip& strip, const std::vector<AnimationClip>& clips);

}  // namespace rigger
