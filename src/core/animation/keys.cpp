// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/animation/keys.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace rigger {

namespace {

template <class T>
void keyChannel(Track<T>& track, double time, const T& value, Interpolation mode) {
    if (const auto existing = findKey(track, time)) {
        track.keys[*existing].value = value;  // garde le mode et les tangentes de la clé
        return;
    }
    Keyframe<T> key;
    key.time = time;
    key.value = value;
    key.mode = mode;
    insertKey(track, key);
}

// Applique `fn` à la piste du canal (vec3 pour T / S, quat pour R).
template <class FnVec, class FnQuat>
auto withChannel(BoneTracks& tracks, int channel, FnVec&& onVec, FnQuat&& onQuat) {
    switch (channel) {
        case ChannelRotation:
            return onQuat(tracks.rotation);
        case ChannelScale:
            return onVec(tracks.scale);
        default:
            return onVec(tracks.translation);
    }
}

template <class FnVec, class FnQuat>
auto withChannel(const BoneTracks& tracks, int channel, FnVec&& onVec, FnQuat&& onQuat) {
    switch (channel) {
        case ChannelRotation:
            return onQuat(tracks.rotation);
        case ChannelScale:
            return onVec(tracks.scale);
        default:
            return onVec(tracks.translation);
    }
}

// Re-pointe la tangente automatique d'une seule clé (voisins compris) sans toucher aux autres.
template <class T>
void autoTangentAt(Track<T>& track, std::size_t index) {
    Track<T> window;
    const std::size_t lo = index == 0 ? 0 : index - 1;
    const std::size_t hi = std::min(index + 2, track.keys.size());
    window.keys.assign(track.keys.begin() + static_cast<std::ptrdiff_t>(lo),
                       track.keys.begin() + static_cast<std::ptrdiff_t>(hi));
    autoTangents(window);
    // Les extrémités de la fenêtre n'ont pas leurs vrais voisins : on ne retient que la clé visée.
    const std::size_t local = index - lo;
    const bool realFirst = index == 0, realLast = index + 1 == track.keys.size();
    if (!realFirst && !realLast && window.keys.size() == 3) {
        track.keys[index].inTangent = window.keys[local].inTangent;
        track.keys[index].outTangent = window.keys[local].outTangent;
    } else {
        track.keys[index].inTangent = T{};
        track.keys[index].outTangent = T{};
    }
}

bool tangentsAreZero(const Keyframe<glm::vec3>& k) {
    return k.inTangent == glm::vec3(0.0f) && k.outTangent == glm::vec3(0.0f);
}
bool tangentsAreZero(const Keyframe<glm::quat>& k) {
    return k.inTangent == glm::quat(0.0f, 0.0f, 0.0f, 0.0f) &&
           k.outTangent == glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
}

}  // namespace

void keyTransform(BoneTracks& tracks, double time, const Transform& value, unsigned channelMask,
                  Interpolation mode) {
    if (channelMask & (1u << ChannelTranslation)) keyChannel(tracks.translation, time, value.t, mode);
    if (channelMask & (1u << ChannelRotation)) keyChannel(tracks.rotation, time, value.r, mode);
    if (channelMask & (1u << ChannelScale)) keyChannel(tracks.scale, time, value.s, mode);
}

bool hasKey(const BoneTracks& tracks, int channel, double time) {
    return withChannel(
        tracks, channel, [&](const Track<glm::vec3>& t) { return findKey(t, time).has_value(); },
        [&](const Track<glm::quat>& t) { return findKey(t, time).has_value(); });
}

unsigned keyedChannels(const BoneTracks& tracks, double time) {
    unsigned mask = 0;
    for (int c = 0; c < 3; ++c) {
        if (hasKey(tracks, c, time)) mask |= 1u << c;
    }
    return mask;
}

std::vector<KeyRef> keysOfBone(const AnimationClip& clip, BoneId bone) {
    std::vector<KeyRef> out;
    const auto it = clip.tracks.find(bone);
    if (it == clip.tracks.end()) {
        return out;
    }
    for (const auto& k : it->second.translation.keys) out.push_back({bone, ChannelTranslation, k.time});
    for (const auto& k : it->second.rotation.keys) out.push_back({bone, ChannelRotation, k.time});
    for (const auto& k : it->second.scale.keys) out.push_back({bone, ChannelScale, k.time});
    return out;
}

std::vector<KeyRef> allKeys(const AnimationClip& clip) {
    std::vector<BoneId> bones;
    for (const auto& [id, tracks] : clip.tracks) bones.push_back(id);
    std::sort(bones.begin(), bones.end());
    std::vector<KeyRef> out;
    for (const BoneId id : bones) {
        const auto keys = keysOfBone(clip, id);
        out.insert(out.end(), keys.begin(), keys.end());
    }
    return out;
}

std::size_t keyCount(const AnimationClip& clip) {
    std::size_t n = 0;
    for (const auto& [id, tracks] : clip.tracks) {
        n += tracks.translation.keys.size() + tracks.rotation.keys.size() + tracks.scale.keys.size();
    }
    return n;
}

std::size_t eraseKeys(AnimationClip& clip, const std::vector<KeyRef>& refs) {
    std::size_t erased = 0;
    for (const KeyRef& ref : refs) {
        const auto it = clip.tracks.find(ref.bone);
        if (it == clip.tracks.end()) {
            continue;
        }
        erased += withChannel(
            it->second, ref.channel, [&](Track<glm::vec3>& t) { return eraseKeyAt(t, ref.time) ? 1u : 0u; },
            [&](Track<glm::quat>& t) { return eraseKeyAt(t, ref.time) ? 1u : 0u; });
    }
    for (auto it = clip.tracks.begin(); it != clip.tracks.end();) {
        it = it->second.empty() ? clip.tracks.erase(it) : std::next(it);
    }
    return erased;
}

std::vector<KeyRef> remapKeys(AnimationClip& clip, const std::vector<KeyRef>& refs,
                              const std::function<double(double)>& mapTime, bool duplicate) {
    // 1. Extrait les clés visées (copie) ; 2. retire les originales (sauf duplication) ; 3. réinsère aux
    // nouveaux temps.
    struct Moved {
        KeyRef where;  // nouvelle référence
        Keyframe<glm::vec3> vec;
        Keyframe<glm::quat> quat;
    };
    std::vector<Moved> moved;
    for (const KeyRef& ref : refs) {
        const auto it = clip.tracks.find(ref.bone);
        if (it == clip.tracks.end()) {
            continue;
        }
        Moved m;
        bool found = false;
        withChannel(
            it->second, ref.channel,
            [&](const Track<glm::vec3>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    m.vec = t.keys[*i];
                    found = true;
                }
                return 0;
            },
            [&](const Track<glm::quat>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    m.quat = t.keys[*i];
                    found = true;
                }
                return 0;
            });
        if (!found) {
            continue;
        }
        m.where = {ref.bone, ref.channel, std::max(mapTime(ref.time), 0.0)};
        moved.push_back(m);
    }
    if (!duplicate) {
        eraseKeys(clip, refs);
    }
    std::vector<KeyRef> result;
    for (Moved& m : moved) {
        BoneTracks& tracks = clip.tracks[m.where.bone];
        withChannel(
            tracks, m.where.channel,
            [&](Track<glm::vec3>& t) {
                m.vec.time = m.where.time;
                insertKey(t, m.vec);
                return 0;
            },
            [&](Track<glm::quat>& t) {
                m.quat.time = m.where.time;
                insertKey(t, m.quat);
                return 0;
            });
        result.push_back(m.where);
    }
    extendDuration(clip);
    return result;
}

void setKeyInterpolation(AnimationClip& clip, const std::vector<KeyRef>& refs, Interpolation mode) {
    for (const KeyRef& ref : refs) {
        const auto it = clip.tracks.find(ref.bone);
        if (it == clip.tracks.end()) {
            continue;
        }
        withChannel(
            it->second, ref.channel,
            [&](Track<glm::vec3>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    t.keys[*i].mode = mode;
                    if (mode == Interpolation::Bezier && tangentsAreZero(t.keys[*i])) autoTangentAt(t, *i);
                }
                return 0;
            },
            [&](Track<glm::quat>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    t.keys[*i].mode = mode;
                    if (mode == Interpolation::Bezier && tangentsAreZero(t.keys[*i])) autoTangentAt(t, *i);
                }
                return 0;
            });
    }
}

std::vector<ClipKey> copyKeys(const AnimationClip& clip, const std::vector<KeyRef>& refs) {
    std::vector<ClipKey> out;
    double first = 1e300;
    for (const KeyRef& ref : refs) first = std::min(first, ref.time);
    for (const KeyRef& ref : refs) {
        const auto it = clip.tracks.find(ref.bone);
        if (it == clip.tracks.end()) {
            continue;
        }
        ClipKey key;
        key.bone = ref.bone;
        key.channel = ref.channel;
        key.relativeTime = ref.time - first;
        bool found = false;
        withChannel(
            it->second, ref.channel,
            [&](const Track<glm::vec3>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    key.vec = t.keys[*i];
                    found = true;
                }
                return 0;
            },
            [&](const Track<glm::quat>& t) {
                if (const auto i = findKey(t, ref.time)) {
                    key.quat = t.keys[*i];
                    found = true;
                }
                return 0;
            });
        if (found) out.push_back(key);
    }
    return out;
}

std::vector<KeyRef> pasteKeys(AnimationClip& clip, const std::vector<ClipKey>& keys, double atTime,
                              const std::function<bool(BoneId)>& validBone) {
    std::vector<KeyRef> out;
    for (const ClipKey& key : keys) {
        if (validBone && !validBone(key.bone)) {
            continue;
        }
        const double time = std::max(atTime + key.relativeTime, 0.0);
        BoneTracks& tracks = clip.tracks[key.bone];
        withChannel(
            tracks, key.channel,
            [&](Track<glm::vec3>& t) {
                Keyframe<glm::vec3> k = key.vec;
                k.time = time;
                insertKey(t, k);
                return 0;
            },
            [&](Track<glm::quat>& t) {
                Keyframe<glm::quat> k = key.quat;
                k.time = time;
                insertKey(t, k);
                return 0;
            });
        out.push_back({key.bone, key.channel, time});
    }
    extendDuration(clip);
    return out;
}

}  // namespace rigger
