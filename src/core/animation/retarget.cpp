// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/animation/retarget.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "core/math/quat_utils.hpp"

namespace rigger {

namespace {

// Découpe en jetons : séparateurs, camelCase, lettres / chiffres.
std::vector<std::string> tokenize(const std::string& name) {
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&] {
        if (!current.empty()) tokens.push_back(current);
        current.clear();
    };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalnum(c)) {
            flush();
            continue;
        }
        const bool upper = std::isupper(c) != 0;
        const bool prevLower = i > 0 && std::islower(static_cast<unsigned char>(name[i - 1])) != 0;
        const bool prevDigit = i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1])) != 0;
        const bool digit = std::isdigit(c) != 0;
        if ((upper && prevLower) || (digit && !prevDigit && !current.empty()) || (!digit && prevDigit))
            flush();
        current += static_cast<char>(std::tolower(c));
    }
    flush();
    return tokens;
}

const std::map<std::string, std::string>& synonyms() {
    static const std::map<std::string, std::string> table = {
        {"upperleg", "upleg"},   {"thigh", "upleg"},      {"upleg", "upleg"},       {"calf", "leg"},
        {"shin", "leg"},         {"lowerleg", "leg"},     {"upperarm", "arm"},      {"uparm", "arm"},
        {"lowerarm", "forearm"}, {"wrist", "hand"},       {"clavicle", "shoulder"}, {"collar", "shoulder"},
        {"pelvis", "hips"},      {"hip", "hips"},         {"ankle", "foot"},        {"toebase", "toe"},
        {"toes", "toe"},         {"upperchest", "chest"}, {"spine0", "spine"},      {"root", "hips"},
    };
    return table;
}

}  // namespace

std::string canonicalBoneName(const std::string& name, char& side) {
    static const std::set<std::string> noise = {"joint", "bone",     "jnt", "def",    "mixamorig", "bip",
                                                "bip01", "armature", "mch", "deform", "skel",      "j"};
    side = 0;
    std::vector<std::string> kept;
    std::vector<std::string> tokens = tokenize(name);
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {  // « Bip01 » : jetons « bip » + « 01 »
        if (tokens[i] == "bip" && tokens[i + 1] == "01")
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(i),
                         tokens.begin() + static_cast<std::ptrdiff_t>(i) + 2);
    }
    for (const std::string& t : tokens) {
        if (t == "l" || t == "left") {
            side = 'L';
        } else if (t == "r" || t == "right") {
            side = 'R';
        } else if (noise.count(t) == 0) {
            kept.push_back(t);
        }
    }
    std::string joined;
    for (const std::string& t : kept) joined += t;
    const auto it = synonyms().find(joined);
    return it != synonyms().end() ? it->second : joined;
}

BoneMap autoMapBones(const Skeleton& source, const Skeleton& target) {
    BoneMap map;
    std::map<std::pair<std::string, char>, BoneId> candidates;
    for (const BoneId id : target.order()) {
        if (hasFlag(target.bone(id).flags, BoneFlags::Control)) continue;
        char side = 0;
        const std::string key = canonicalBoneName(target.bone(id).name, side);
        if (!key.empty()) candidates.emplace(std::make_pair(key, side), id);  // premier os cible rencontré
    }
    std::set<BoneId> used;
    for (const BoneId id : source.order()) {
        if (hasFlag(source.bone(id).flags, BoneFlags::Control)) continue;
        char side = 0;
        const std::string key = canonicalBoneName(source.bone(id).name, side);
        const auto it = candidates.find({key, side});
        if (key.empty() || it == candidates.end() || used.count(it->second) != 0) continue;
        used.insert(it->second);
        map.pairs.emplace_back(id, it->second);
    }
    return map;
}

AnimationClip retargetClip(const AnimationClip& clip, const Skeleton& source, const Skeleton& target,
                           const BoneMap& map, const RetargetOptions& options) {
    AnimationClip out;
    out.name = clip.name + " (retargeted)";
    out.duration = clip.duration;
    out.fps = options.fps > 0.0f ? options.fps : clip.fps;
    out.loop = clip.loop;
    out.markers = clip.markers;

    const std::vector<glm::mat4>& srcRest = source.bindGlobals();
    const std::vector<glm::mat4>& dstRest = target.bindGlobals();
    const auto restRotation = [](const std::vector<glm::mat4>& globals, BoneId id) {
        return glm::normalize(Transform::fromMat4(globals[id]).r);
    };

    // Échelle des translations : rapport des hauteurs de repos de l'os apparié racine de la correspondance.
    float scale = 1.0f;
    if (options.scaleTranslation) {
        for (const auto& [s, t] : map.pairs) {
            const BoneId parent = source.bone(s).parent;
            const bool root = parent == kNoBone || map.target(parent) == kNoBone;
            const float hs = std::abs(srcRest[s][3][1]), ht = std::abs(dstRest[t][3][1]);
            if (root && hs > 1e-4f && ht > 1e-4f) {
                scale = ht / hs;
                break;
            }
        }
    }

    // Os animés de la source et leur correspondant.
    struct Item {
        BoneId src, dst;
        bool translation, rotation, scaling;
    };
    std::vector<Item> items;
    for (const auto& [s, t] : map.pairs) {
        const auto it = clip.tracks.find(s);
        if (it == clip.tracks.end()) continue;
        items.push_back(
            {s, t, !it->second.translation.empty(), !it->second.rotation.empty(), !it->second.scale.empty()});
    }
    if (items.empty()) return out;

    const int frames = std::max(1, static_cast<int>(std::floor(clip.duration * out.fps + 1e-6)) + 1);
    std::vector<double> times;
    for (int f = 0; f < frames; ++f) times.push_back(static_cast<double>(f) / out.fps);
    if (clip.duration - times.back() > 1e-4) times.push_back(clip.duration);

    for (const double t : times) {
        const Pose pose = samplePose(clip, source, t);
        for (const Item& item : items) {
            const Transform& anim = pose[item.src];
            const Transform& srcBind = source.bone(item.src).bindLocal;
            const Transform& dstBind = target.bone(item.dst).bindLocal;
            BoneTracks& tracks = out.tracks[item.dst];
            if (item.rotation) {
                const glm::quat rel = glm::inverse(srcBind.r) * anim.r;
                const glm::quat rs = restRotation(srcRest, item.src), rd = restRotation(dstRest, item.dst);
                const glm::quat relWorld = rs * rel * glm::inverse(rs);
                const glm::quat relDst = glm::inverse(rd) * relWorld * rd;
                Keyframe<glm::quat> key;
                key.time = t;
                key.value = glm::normalize(dstBind.r * relDst);
                key.mode = Interpolation::Linear;
                tracks.rotation.keys.push_back(key);
            }
            if (item.translation) {
                Keyframe<glm::vec3> key;
                key.time = t;
                key.value = dstBind.t + (anim.t - srcBind.t) * scale;
                key.mode = Interpolation::Linear;
                tracks.translation.keys.push_back(key);
            }
            if (item.scaling) {
                Keyframe<glm::vec3> key;
                key.time = t;
                key.value = anim.s;
                key.mode = Interpolation::Linear;
                tracks.scale.keys.push_back(key);
            }
        }
    }
    return out;
}

}  // namespace rigger
