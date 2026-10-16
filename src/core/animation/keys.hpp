// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cmath>
#include <functional>
#include <vector>

#include "core/animation/clip.hpp"
#include "core/math/transform.hpp"

namespace rigger {

// Opérations d'édition de clés sur un clip (sans Qt) : insertion depuis une pose, suppression, déplacement /
// duplication / étirement, mode d'interpolation, copier / coller. Les commandes annulables de l'éditeur les
// appliquent sur une copie des pistes puis enregistrent l'avant / après (EditTracksCommand).

enum KeyChannel { ChannelTranslation = 0, ChannelRotation = 1, ChannelScale = 2 };
constexpr unsigned kAllChannels = 0b111;

// Référence à une clé : os, canal (T / R / S) et temps (secondes).
struct KeyRef {
    BoneId bone = kNoBone;
    int channel = ChannelTranslation;
    double time = 0.0;

    bool operator==(const KeyRef& o) const {
        return bone == o.bone && channel == o.channel && std::abs(time - o.time) <= kKeyTimeEpsilon;
    }
};

// Insère ou remplace, aux `time`, une clé par canal demandé (masque de bits 1 << KeyChannel) avec les
// composantes de `value`. Une clé existante garde son mode d'interpolation et ses tangentes ; une nouvelle
// clé reçoit `mode`.
void keyTransform(BoneTracks& tracks, double time, const Transform& value, unsigned channelMask,
                  Interpolation mode = Interpolation::Linear);

// Masque des canaux qui ont une clé à ce temps.
unsigned keyedChannels(const BoneTracks& tracks, double time);
bool hasKey(const BoneTracks& tracks, int channel, double time);

// Toutes les clés (références) d'un clip, ou d'un os, dans l'ordre os / canal / temps.
std::vector<KeyRef> allKeys(const AnimationClip& clip);
std::vector<KeyRef> keysOfBone(const AnimationClip& clip, BoneId bone);
// Valeur d'une clé en tant que transformation partielle (les canaux non concernés restent à la valeur par
// défaut).
std::size_t keyCount(const AnimationClip& clip);

// Supprime les clés données (références inconnues ignorées) et retire les pistes devenues vides. Renvoie le
// nombre de clés supprimées.
std::size_t eraseKeys(AnimationClip& clip, const std::vector<KeyRef>& refs);

// Applique `mapTime` au temps des clés données (déplacement, étirement). Avec `duplicate`, les clés d'origine
// sont conservées et des copies sont créées. Les clés qui tombent sur une clé existante la remplacent.
// Renvoie les nouvelles références, et agrandit la durée du clip si nécessaire. Les temps négatifs sont
// ramenés à 0.
std::vector<KeyRef> remapKeys(AnimationClip& clip, const std::vector<KeyRef>& refs,
                              const std::function<double(double)>& mapTime, bool duplicate = false);

// Change le mode d'interpolation des clés données ; en Bezier, les tangentes de ces clés sont recalculées
// automatiquement (Catmull-Rom bridé) tant qu'elles sont nulles.
void setKeyInterpolation(AnimationClip& clip, const std::vector<KeyRef>& refs, Interpolation mode);

// Presse-papiers de clés : copie indépendante du clip, temps relatifs à la clé la plus précoce.
struct ClipKey {
    BoneId bone = kNoBone;
    int channel = ChannelTranslation;
    double relativeTime = 0.0;
    Keyframe<glm::vec3> vec;   // translation / échelle
    Keyframe<glm::quat> quat;  // rotation
};
std::vector<ClipKey> copyKeys(const AnimationClip& clip, const std::vector<KeyRef>& refs);
// Colle à partir de `atTime` ; renvoie les références créées. Les os absents de `validBone` sont ignorés
// (suite à une suppression d'os par exemple).
std::vector<KeyRef> pasteKeys(AnimationClip& clip, const std::vector<ClipKey>& keys, double atTime,
                              const std::function<bool(BoneId)>& validBone = {});

}  // namespace rigger
