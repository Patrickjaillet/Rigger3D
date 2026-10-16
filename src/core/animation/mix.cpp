// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/animation/mix.hpp"

#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "core/util/profiler.hpp"

namespace rigger {

namespace {

bool validClip(const std::vector<AnimationClip>& clips, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < clips.size();
}

double wrapTime(double t, double duration, bool loop) {
    if (duration <= 0.0) return 0.0;
    if (loop) {
        const double w = std::fmod(t, duration);
        return w < 0.0 ? w + duration : w;
    }
    return std::clamp(t, 0.0, duration);
}

// Os réellement animés par le clip (pistes non vides) : un clip n'écrase que les os qu'il anime.
std::vector<bool> animatedBones(const AnimationClip& clip, std::size_t slots) {
    std::vector<bool> animated(slots, false);
    for (const auto& [id, tracks] : clip.tracks) {
        if (id < slots && !tracks.empty()) animated[id] = true;
    }
    return animated;
}

std::vector<bool> intersect(std::vector<bool> a, const std::vector<bool>& b) {
    if (a.empty()) return b;
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = a[i] && i < b.size() && b[i];
    return a;
}

glm::vec3 safeRatio(const glm::vec3& a, const glm::vec3& b) {
    return {std::abs(b.x) > 1e-8f ? a.x / b.x : 1.0f, std::abs(b.y) > 1e-8f ? a.y / b.y : 1.0f,
            std::abs(b.z) > 1e-8f ? a.z / b.z : 1.0f};
}

}  // namespace

std::vector<bool> boneMask(const Skeleton& skeleton, const std::vector<BoneId>& roots) {
    if (roots.empty()) return {};
    std::vector<bool> mask(skeleton.slotCount(), false);
    for (const BoneId id : skeleton.order()) {  // parents d'abord : un seul passage suffit
        const BoneId parent = skeleton.bone(id).parent;
        const bool root = std::find(roots.begin(), roots.end(), id) != roots.end();
        mask[id] = root || (parent != kNoBone && mask[parent]);
    }
    return mask;
}

void blendPoseInto(Pose& destination, const Pose& source, const Pose& reference, float weight, LayerMode mode,
                   const std::vector<bool>* mask) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (weight <= 0.0f) return;
    const std::size_t n = std::min({destination.size(), source.size(), reference.size()});
    for (std::size_t id = 0; id < n; ++id) {
        if (mask != nullptr && !mask->empty() && (id >= mask->size() || !(*mask)[id])) continue;
        Transform& d = destination[id];
        const Transform& s = source[id];
        if (mode == LayerMode::Override) {
            d = lerp(d, s, weight);
        } else {
            const Transform& ref = reference[id];
            d.t += (s.t - ref.t) * weight;
            const glm::quat delta = glm::inverse(ref.r) * s.r;
            d.r = glm::normalize(d.r * slerpShortest(glm::quat(1, 0, 0, 0), delta, weight));
            d.s *= glm::mix(glm::vec3(1.0f), safeRatio(s.s, ref.s), weight);
        }
    }
}

Pose crossfade(const Pose& a, const Pose& b, float u) {
    Pose out = a;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) out[i] = lerp(a[i], b[i], std::clamp(u, 0.0f, 1.0f));
    return out;
}

Pose evaluateLayers(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                    const std::vector<AnimationLayer>& layers, double time) {
    Pose pose = skeleton.bindPose();
    const Pose reference = pose;
    for (const AnimationLayer& layer : layers) {
        if (!layer.enabled || layer.weight <= 0.0f || !validClip(clips, layer.clip)) continue;
        const AnimationClip& clip = clips[static_cast<std::size_t>(layer.clip)];
        const double t = wrapTime(time * layer.speed + layer.timeOffset, clip.duration, layer.loop);
        const Pose sampled = samplePose(clip, skeleton, t);
        const std::vector<bool> mask =
            intersect(boneMask(skeleton, layer.mask), animatedBones(clip, skeleton.slotCount()));
        blendPoseInto(pose, sampled, reference, layer.weight, layer.mode, &mask);
    }
    return pose;
}

double stripLength(const NlaStrip& strip, const std::vector<AnimationClip>& clips) {
    if (!validClip(clips, strip.clip) || strip.speed <= 0.0f) return 0.0;
    const AnimationClip& clip = clips[static_cast<std::size_t>(strip.clip)];
    const double end = strip.clipEnd < 0.0 ? clip.duration : std::min(strip.clipEnd, clip.duration);
    const double span = std::max(0.0, end - strip.clipStart);
    return span / strip.speed * std::max(strip.repeat, 0.0f);
}

Pose evaluateNla(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                 const std::vector<NlaStrip>& strips, double time) {
    Pose pose = skeleton.bindPose();
    const Pose reference = pose;
    std::vector<const NlaStrip*> ordered;
    for (const NlaStrip& s : strips) {
        if (s.enabled) ordered.push_back(&s);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const NlaStrip* a, const NlaStrip* b) {
        return a->track != b->track ? a->track < b->track : a->start < b->start;
    });
    for (const NlaStrip* s : ordered) {
        if (!validClip(clips, s->clip)) continue;
        const double length = stripLength(*s, clips);
        const double local = time - s->start;
        if (length <= 0.0 || local < 0.0 || local > length) continue;
        const AnimationClip& clip = clips[static_cast<std::size_t>(s->clip)];
        const double end = s->clipEnd < 0.0 ? clip.duration : std::min(s->clipEnd, clip.duration);
        const double span = std::max(end - s->clipStart, 1e-9);
        double clipTime = s->clipStart + std::fmod(local * s->speed, span);
        if (local >= length) clipTime = end;  // dernière image exactement à la fin de la bande
        double fade = 1.0;
        if (s->blendIn > 0.0) fade *= std::clamp(local / s->blendIn, 0.0, 1.0);
        if (s->blendOut > 0.0) fade *= std::clamp((length - local) / s->blendOut, 0.0, 1.0);
        const Pose sampled = samplePose(clip, skeleton, clipTime);
        const std::vector<bool> animated = animatedBones(clip, skeleton.slotCount());
        blendPoseInto(pose, sampled, reference, s->weight * static_cast<float>(fade), s->mode, &animated);
    }
    return pose;
}

Pose evaluateBlendTree(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                       const BlendTree1D& tree, double time) {
    std::vector<BlendTreeEntry> entries;
    for (const BlendTreeEntry& e : tree.entries) {
        if (validClip(clips, e.clip)) entries.push_back(e);
    }
    if (entries.empty()) return skeleton.bindPose();
    std::stable_sort(entries.begin(), entries.end(), [](const BlendTreeEntry& a, const BlendTreeEntry& b) {
        return a.threshold < b.threshold;
    });
    const float p = std::clamp(tree.parameter, entries.front().threshold, entries.back().threshold);
    std::size_t hi = 0;
    while (hi + 1 < entries.size() && entries[hi].threshold < p) ++hi;
    const std::size_t lo = hi == 0 ? 0 : hi - 1;
    const float span = entries[hi].threshold - entries[lo].threshold;
    const float u = span > 1e-9f ? (p - entries[lo].threshold) / span : 0.0f;
    const AnimationClip& a = clips[static_cast<std::size_t>(entries[lo].clip)];
    const AnimationClip& b = clips[static_cast<std::size_t>(entries[hi].clip)];
    double ta = time, tb = time;
    if (tree.syncTime && a.duration > 0.0 && b.duration > 0.0) {
        const double blended = a.duration + (b.duration - a.duration) * static_cast<double>(u);
        const double phase = std::fmod(std::max(time, 0.0), blended) / blended;  // phase commune [0, 1)
        ta = phase * a.duration;
        tb = phase * b.duration;
    } else {
        ta = wrapTime(time, a.duration, true);
        tb = wrapTime(time, b.duration, true);
    }
    return crossfade(samplePose(a, skeleton, ta), samplePose(b, skeleton, tb), u);
}

Pose evaluateMix(const Skeleton& skeleton, const std::vector<AnimationClip>& clips, const AnimationMix& mix,
                 double time) {
    RIGGER_PROFILE_SCOPE("evaluateMix");
    switch (mix.mode) {
        case MixMode::Layers:
            return evaluateLayers(skeleton, clips, mix.layers, time);
        case MixMode::Nla:
            return evaluateNla(skeleton, clips, mix.strips, time);
        case MixMode::BlendTree:
            return evaluateBlendTree(skeleton, clips, mix.tree, time);
        case MixMode::Clip:
            break;
    }
    return skeleton.bindPose();
}

double mixDuration(const std::vector<AnimationClip>& clips, const AnimationMix& mix) {
    double duration = 0.0;
    switch (mix.mode) {
        case MixMode::Layers:
            for (const AnimationLayer& l : mix.layers) {
                if (l.enabled && validClip(clips, l.clip) && std::abs(l.speed) > 1e-6f) {
                    duration = std::max(duration,
                                        clips[static_cast<std::size_t>(l.clip)].duration / std::abs(l.speed));
                }
            }
            break;
        case MixMode::Nla:
            for (const NlaStrip& s : mix.strips) {
                if (s.enabled) duration = std::max(duration, s.start + stripLength(s, clips));
            }
            break;
        case MixMode::BlendTree:
            for (const BlendTreeEntry& e : mix.tree.entries) {
                if (validClip(clips, e.clip))
                    duration = std::max(duration, clips[static_cast<std::size_t>(e.clip)].duration);
            }
            break;
        case MixMode::Clip:
            break;
    }
    return duration;
}

}  // namespace rigger
