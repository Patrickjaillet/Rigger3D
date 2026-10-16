// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/animation/clip.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "core/util/profiler.hpp"

namespace rigger {

namespace {

struct Segment {
    std::size_t i0 = 0;
    std::size_t i1 = 0;
    double u = 0.0;  // fraction dans [0, 1]
    double dt = 0.0;
};

template <class T>
Segment findSegment(const Track<T>& track, double time) {
    const auto& keys = track.keys;
    Segment seg;
    if (keys.size() < 2 || time <= keys.front().time) {
        return seg;
    }
    if (time >= keys.back().time) {
        seg.i0 = seg.i1 = keys.size() - 1;
        return seg;
    }
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                        [](double t, const Keyframe<T>& k) { return t < k.time; });
    seg.i1 = static_cast<std::size_t>(upper - keys.begin());
    seg.i0 = seg.i1 - 1;
    seg.dt = keys[seg.i1].time - keys[seg.i0].time;
    seg.u = seg.dt > 0.0 ? (time - keys[seg.i0].time) / seg.dt : 0.0;
    return seg;
}

// Spline de Hermite (tangentes en valeur par seconde, multipliées par dt).
template <class V>
V hermite(const V& v0, const V& outTangent0, const V& v1, const V& inTangent1, double dt, double u) {
    const float s = static_cast<float>(u);
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float d = static_cast<float>(dt);
    return (2.0f * s3 - 3.0f * s2 + 1.0f) * v0 + (s3 - 2.0f * s2 + s) * d * outTangent0 +
           (-2.0f * s3 + 3.0f * s2) * v1 + (s3 - s2) * d * inTangent1;
}

glm::vec4 asVec(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }

}  // namespace

glm::vec3 sampleTrack(const Track<glm::vec3>& track, double time) {
    const Segment seg = findSegment(track, time);
    const auto& a = track.keys[seg.i0];
    if (seg.i0 == seg.i1 || a.mode == Interpolation::Constant) {
        return a.value;
    }
    const auto& b = track.keys[seg.i1];
    if (a.mode == Interpolation::Bezier) {
        return hermite(a.value, a.outTangent, b.value, b.inTangent, seg.dt, seg.u);
    }
    return glm::mix(a.value, b.value, static_cast<float>(seg.u));
}

glm::quat sampleTrack(const Track<glm::quat>& track, double time) {
    const Segment seg = findSegment(track, time);
    const auto& a = track.keys[seg.i0];
    if (seg.i0 == seg.i1 || a.mode == Interpolation::Constant) {
        return a.value;
    }
    const auto& b = track.keys[seg.i1];
    if (a.mode == Interpolation::Bezier) {
        // Hermite composante par composante, en alignant l'hémisphère de la clé d'arrivée (chemin le plus
        // court).
        const float sign = glm::dot(a.value, b.value) < 0.0f ? -1.0f : 1.0f;
        const glm::vec4 v = hermite(asVec(a.value), asVec(a.outTangent), sign * asVec(b.value),
                                    sign * asVec(b.inTangent), seg.dt, seg.u);
        const float len = glm::length(v);
        return len > 1e-12f ? glm::quat(v.w / len, v.x / len, v.y / len, v.z / len) : a.value;
    }
    return slerpShortest(a.value, b.value, static_cast<float>(seg.u));
}

void sampleClip(const AnimationClip& clip, const Skeleton& skeleton, double time, Pose& pose) {
    RIGGER_PROFILE_SCOPE("sampleClip");
    pose = skeleton.bindPose();
    if (clip.loop && clip.duration > 0.0 && time > clip.duration) {
        time = std::fmod(time, clip.duration);
    }
    time = std::max(time, 0.0);
    for (const auto& [bone, tracks] : clip.tracks) {
        if (!skeleton.contains(bone) || bone >= pose.size()) {
            continue;
        }
        Transform& local = pose[bone];
        if (!tracks.translation.empty()) {
            local.t = sampleTrack(tracks.translation, time);
        }
        if (!tracks.rotation.empty()) {
            local.r = sampleTrack(tracks.rotation, time);
        }
        if (!tracks.scale.empty()) {
            local.s = sampleTrack(tracks.scale, time);
        }
    }
}

Pose samplePose(const AnimationClip& clip, const Skeleton& skeleton, double time) {
    Pose pose;
    sampleClip(clip, skeleton, time, pose);
    return pose;
}

// --- Tangentes automatiques ----------------------------------------------------------------------

namespace {

template <class T, class Slope>
void computeAuto(Track<T>& track, const Slope& slope) {
    const std::size_t n = track.keys.size();
    for (std::size_t i = 0; i < n; ++i) {
        auto& key = track.keys[i];
        if (n < 2) {
            key.inTangent = key.outTangent = T{};
            continue;
        }
        const std::size_t prev = i == 0 ? 0 : i - 1;
        const std::size_t next = i + 1 == n ? n - 1 : i + 1;
        const T s = slope(track.keys[prev], key, track.keys[next], i == 0, i + 1 == n);
        key.inTangent = key.outTangent = s;
    }
}

float clampedSlope(float before, float here, float after, double dtPrev, double dtNext, bool first,
                   bool last) {
    if (first || last) {
        return 0.0f;  // extrémités : départ / arrivée en douceur
    }
    const double dt = dtPrev + dtNext;
    if (dt <= 0.0) {
        return 0.0f;
    }
    // Extremum local : pente nulle (pas de dépassement entre deux clés).
    if ((here - before) * (after - here) <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>((after - before) / dt);
}

}  // namespace

void autoTangents(Track<glm::vec3>& track) {
    computeAuto(track, [](const Keyframe<glm::vec3>& p, const Keyframe<glm::vec3>& k,
                          const Keyframe<glm::vec3>& n, bool first, bool last) {
        glm::vec3 s(0.0f);
        for (int c = 0; c < 3; ++c) {
            s[c] = clampedSlope(p.value[c], k.value[c], n.value[c], k.time - p.time, n.time - k.time, first,
                                last);
        }
        return s;
    });
}

void autoTangents(Track<glm::quat>& track) {
    computeAuto(track, [](const Keyframe<glm::quat>& p, const Keyframe<glm::quat>& k,
                          const Keyframe<glm::quat>& n, bool first, bool last) {
        if (first || last) {
            return glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
        }
        // Voisins alignés sur l'hémisphère de la clé courante, puis pente par composante.
        const float sp = glm::dot(p.value, k.value) < 0.0f ? -1.0f : 1.0f;
        const float sn = glm::dot(n.value, k.value) < 0.0f ? -1.0f : 1.0f;
        const glm::vec4 before = sp * asVec(p.value), here = asVec(k.value), after = sn * asVec(n.value);
        glm::vec4 s(0.0f);
        for (int c = 0; c < 4; ++c) {
            s[c] = clampedSlope(before[c], here[c], after[c], k.time - p.time, n.time - k.time, false, false);
        }
        return glm::quat(s.w, s.x, s.y, s.z);
    });
}

double lastKeyTime(const AnimationClip& clip) {
    double last = 0.0;
    for (const auto& [bone, tracks] : clip.tracks) {
        for (const auto* keys : {&tracks.translation.keys, &tracks.scale.keys}) {
            if (!keys->empty()) last = std::max(last, keys->back().time);
        }
        if (!tracks.rotation.keys.empty()) last = std::max(last, tracks.rotation.keys.back().time);
    }
    return last;
}

void extendDuration(AnimationClip& clip) { clip.duration = std::max(clip.duration, lastKeyTime(clip)); }

}  // namespace rigger
