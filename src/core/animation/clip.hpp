// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/skeleton/skeleton.hpp"

namespace rigger {

// Interpolation depuis une clé vers la suivante (par clé). Bezier = spline de Hermite : les tangentes de
// `Keyframe` sont des pentes en valeur par seconde (équivalent des poignées Bézier, dont les abscisses sont à
// un tiers de l'intervalle) et correspondent exactement aux tangentes CUBICSPLINE de glTF.
enum class Interpolation { Constant, Linear, Bezier };

template <class T>
struct Keyframe {
    double time = 0.0;  // secondes
    T value{};
    T inTangent{};   // utilisées uniquement quand la clé précédente est en Bezier
    T outTangent{};  // utilisées uniquement quand cette clé est en Bezier
    Interpolation mode = Interpolation::Linear;
};

template <class T>
struct Track {
    std::vector<Keyframe<T>> keys;  // triées par temps croissant, un seul temps par clé

    bool empty() const { return keys.empty(); }
};

// Un canal par composante d'os : translation (x, y, z), rotation (quaternion, interpolée par slerp ; les
// angles d'Euler ne servent qu'à l'édition de courbes) et échelle (x, y, z).
struct BoneTracks {
    Track<glm::vec3> translation;
    Track<glm::quat> rotation;
    Track<glm::vec3> scale;

    bool empty() const { return translation.empty() && rotation.empty() && scale.empty(); }
};

struct Marker {
    double time = 0.0;
    std::string name;
};

struct AnimationClip {
    std::string name;
    double duration = 0.0;  // secondes
    float fps = 30.0f;
    bool loop = true;
    std::unordered_map<BoneId, BoneTracks> tracks;
    std::vector<Marker> markers;  // repères nommés de la timeline, triés par temps
    // Mouvement de racine : indicateur et os concerné (l'extraction elle-même viendra avec l'export).
    bool rootMotion = false;
    BoneId rootBone = kNoBone;
};

// --- Échantillonnage (recherche binaire des clés : O(log n)) -------------------------------------

// Évalue une piste au temps `time` (clampé à la première / dernière clé). La piste ne doit pas être vide.
glm::vec3 sampleTrack(const Track<glm::vec3>& track, double time);
glm::quat sampleTrack(const Track<glm::quat>& track, double time);

// Remplit `pose` : bind pose du squelette, écrasée par les pistes du clip évaluées à `time`.
// Si clip.loop, `time` est ramené dans [0, duration) (au-delà de la durée seulement).
void sampleClip(const AnimationClip& clip, const Skeleton& skeleton, double time, Pose& pose);
Pose samplePose(const AnimationClip& clip, const Skeleton& skeleton, double time);

// --- Édition de pistes ----------------------------------------------------------------------------

// Tolérance de temps pour considérer deux clés comme étant à la même position (une demi-milliseconde).
inline constexpr double kKeyTimeEpsilon = 5e-4;

template <class T>
std::optional<std::size_t> findKey(const Track<T>& track, double time, double tolerance = kKeyTimeEpsilon) {
    const auto it = std::lower_bound(track.keys.begin(), track.keys.end(), time - tolerance,
                                     [](const Keyframe<T>& k, double t) { return k.time < t; });
    if (it != track.keys.end() && it->time <= time + tolerance) {
        return static_cast<std::size_t>(it - track.keys.begin());
    }
    return std::nullopt;
}

// Insère (ou remplace, si une clé existe déjà à ce temps) en gardant la piste triée. Renvoie l'indice de la
// clé.
template <class T>
std::size_t insertKey(Track<T>& track, const Keyframe<T>& key) {
    if (const auto existing = findKey(track, key.time)) {
        track.keys[*existing] = key;
        return *existing;
    }
    const auto it = std::upper_bound(track.keys.begin(), track.keys.end(), key.time,
                                     [](double t, const Keyframe<T>& k) { return t < k.time; });
    return static_cast<std::size_t>(track.keys.insert(it, key) - track.keys.begin());
}

template <class T>
bool eraseKeyAt(Track<T>& track, double time) {
    if (const auto existing = findKey(track, time)) {
        track.keys.erase(track.keys.begin() + static_cast<std::ptrdiff_t>(*existing));
        return true;
    }
    return false;
}

// Retrie par temps et fusionne les clés confondues (la dernière rencontrée gagne) : à appeler après avoir
// déplacé des clés en modifiant directement `time`.
template <class T>
void normalizeTrack(Track<T>& track) {
    std::stable_sort(track.keys.begin(), track.keys.end(),
                     [](const Keyframe<T>& a, const Keyframe<T>& b) { return a.time < b.time; });
    std::vector<Keyframe<T>> unique;
    for (const Keyframe<T>& key : track.keys) {
        if (!unique.empty() && key.time - unique.back().time <= kKeyTimeEpsilon) {
            unique.back() = key;
        } else {
            unique.push_back(key);
        }
    }
    track.keys = std::move(unique);
}

// Tangentes automatiques (Catmull-Rom « bridé » : pente nulle aux extrema pour éviter les dépassements) pour
// toutes les clés de la piste ; ne touche pas au mode d'interpolation.
void autoTangents(Track<glm::vec3>& track);
void autoTangents(Track<glm::quat>& track);

// Durée minimale couvrant toutes les clés (0 si aucune) ; extendDuration() agrandit clip.duration au besoin.
double lastKeyTime(const AnimationClip& clip);
void extendDuration(AnimationClip& clip);

}  // namespace rigger
