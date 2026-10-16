// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skinning/paint.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "core/skinning/weights.hpp"
#include "core/util/profiler.hpp"

namespace rigger {

namespace {

struct Entry {
    BoneId bone;
    float weight;
};

std::vector<Entry> entriesOf(const SkinWeight& skin) {
    std::vector<Entry> out;
    for (std::size_t k = 0; k < 4; ++k) {
        if (skin.weights[k] > 0.0f && skin.joints[k] != kNoBone)
            out.push_back({skin.joints[k], skin.weights[k]});
    }
    return out;
}

SkinWeight fromEntries(const std::vector<Entry>& entries) {
    SkinWeight skin;
    std::size_t k = 0;
    for (const Entry& e : entries) {
        if (e.weight > 1e-7f && k < 4) {
            skin.joints[k] = e.bone;
            skin.weights[k] = e.weight;
            ++k;
        }
    }
    return skin;
}

bool sameSkin(const SkinWeight& a, const SkinWeight& b) {
    return a.joints == b.joints && a.weights == b.weights;
}

}  // namespace

float influenceOf(const SkinWeight& skin, BoneId bone) {
    for (std::size_t k = 0; k < 4; ++k) {
        if (skin.joints[k] == bone && skin.weights[k] > 0.0f) return skin.weights[k];
    }
    return 0.0f;
}

bool setBoneWeight(SkinWeight& skin, BoneId bone, float value, const WeightLocks& locks, bool autoNormalize) {
    if (bone == kNoBone || locks.isLocked(bone) || !std::isfinite(value)) return false;
    value = std::clamp(value, 0.0f, 1.0f);
    std::vector<Entry> entries = entriesOf(skin);
    const auto find = [&](BoneId b) {
        return std::find_if(entries.begin(), entries.end(), [b](const Entry& e) { return e.bone == b; });
    };
    auto it = find(bone);
    if (it == entries.end() && value <= 0.0f) return false;

    // Fait de la place pour un 5e os : éjecte le plus petit os non verrouillé.
    if (it == entries.end() && entries.size() == 4) {
        auto smallest = entries.end();
        for (auto e = entries.begin(); e != entries.end(); ++e) {
            if (!locks.isLocked(e->bone) && (smallest == entries.end() || e->weight < smallest->weight))
                smallest = e;
        }
        if (smallest == entries.end()) return false;  // les 4 emplacements sont verrouillés
        entries.erase(smallest);
    }
    it = find(bone);
    if (it == entries.end()) {
        entries.push_back({bone, 0.0f});
        it = entries.end() - 1;
    }

    if (!autoNormalize) {
        it->weight = value;
    } else {
        float lockedSum = 0.0f, freeSum = 0.0f;
        for (const Entry& e : entries) {
            if (e.bone == bone) continue;
            (locks.isLocked(e.bone) ? lockedSum : freeSum) += e.weight;
        }
        value = std::min(value, std::max(0.0f, 1.0f - lockedSum));
        const float remaining = std::max(0.0f, 1.0f - lockedSum - value);
        if (freeSum > 0.0f) {
            for (Entry& e : entries) {
                if (e.bone != bone && !locks.isLocked(e.bone)) e.weight *= remaining / freeSum;
            }
        } else {
            value += remaining;  // personne d'autre pour prendre la masse : l'os visé la garde
        }
        it->weight = value;
    }
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [](const Entry& e) { return e.weight <= 1e-7f; }),
        entries.end());
    if (entries.empty()) return false;  // jamais de sommet sans aucun poids
    const SkinWeight result = fromEntries(entries);
    if (sameSkin(result, skin)) return false;
    skin = result;
    return true;
}

float falloffWeight(Falloff falloff, float distance, float radius) {
    if (radius <= 0.0f || distance > radius) return 0.0f;
    const float t = 1.0f - distance / radius;
    switch (falloff) {
        case Falloff::Constant:
            return 1.0f;
        case Falloff::Linear:
            return t;
        case Falloff::Smooth:
            return t * t * (3.0f - 2.0f * t);
    }
    return 0.0f;
}

MeshTopology MeshTopology::build(const Mesh& mesh) {
    MeshTopology topology;
    topology.neighbors.resize(mesh.vertices.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t a = mesh.indices[i + static_cast<std::size_t>(e)];
            const std::uint32_t b = mesh.indices[i + static_cast<std::size_t>((e + 1) % 3)];
            if (a >= topology.neighbors.size() || b >= topology.neighbors.size() || a == b) continue;
            topology.neighbors[a].push_back(b);
            topology.neighbors[b].push_back(a);
        }
    }
    for (auto& list : topology.neighbors) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return topology;
}

BrushHits collectBrushHits(const std::vector<glm::vec3>& positions, const glm::vec3& center,
                           const Brush& brush, const std::vector<std::uint8_t>* allowed) {
    BrushHits hits;
    for (std::size_t v = 0; v < positions.size(); ++v) {
        if (allowed != nullptr && (v >= allowed->size() || (*allowed)[v] == 0)) continue;
        const float f = falloffWeight(brush.falloff, glm::length(positions[v] - center), brush.radius);
        if (f > 0.0f) hits.emplace_back(static_cast<std::uint32_t>(v), f);
    }
    return hits;
}

std::vector<std::uint32_t> applyBrushHits(Mesh& mesh, const MeshTopology& topology, const BrushHits& hits,
                                          const Brush& brush, BoneId bone, const WeightLocks& locks,
                                          bool autoNormalize) {
    std::vector<std::uint32_t> changed;
    if (!mesh.hasSkinning() || bone == kNoBone || locks.isLocked(bone)) return changed;
    // Les cibles sont calculées AVANT toute modification : le lissage lit les poids d'avant le coup.
    std::vector<float> targets(hits.size());
    for (std::size_t h = 0; h < hits.size(); ++h) {
        const auto [v, f] = hits[h];
        if (v >= mesh.skinWeights.size()) {
            targets[h] = -1.0f;
            continue;
        }
        const float current = influenceOf(mesh.skinWeights[v], bone);
        const float k = std::clamp(brush.strength * f, 0.0f, 1.0f);
        switch (brush.mode) {
            case BrushMode::Add:
                targets[h] = current + brush.strength * f;
                break;
            case BrushMode::Subtract:
                targets[h] = current - brush.strength * f;
                break;
            case BrushMode::Replace:
                targets[h] = current + (brush.value - current) * k;
                break;
            case BrushMode::Smooth: {
                float sum = 0.0f;
                int count = 0;
                if (v < topology.neighbors.size()) {
                    for (const std::uint32_t n : topology.neighbors[v]) {
                        if (n < mesh.skinWeights.size()) {
                            sum += influenceOf(mesh.skinWeights[n], bone);
                            ++count;
                        }
                    }
                }
                targets[h] = count > 0 ? current + (sum / static_cast<float>(count) - current) * k : current;
                break;
            }
        }
    }
    for (std::size_t h = 0; h < hits.size(); ++h) {
        if (targets[h] < -0.5f) continue;
        if (setBoneWeight(mesh.skinWeights[hits[h].first], bone, targets[h], locks, autoNormalize)) {
            changed.push_back(hits[h].first);
        }
    }
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    return changed;
}

std::vector<std::uint32_t> mirrorVertexMap(const std::vector<glm::vec3>& restPositions, float tolerance) {
    std::vector<std::uint32_t> map(restPositions.size(), kNoVertex);
    // Tri par x pour limiter la recherche ; maillages de quelques dizaines de milliers de sommets : O(n log n
    // + n k).
    std::vector<std::uint32_t> order(restPositions.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
    std::sort(order.begin(), order.end(),
              [&](std::uint32_t a, std::uint32_t b) { return restPositions[a].x < restPositions[b].x; });
    for (std::size_t v = 0; v < restPositions.size(); ++v) {
        const glm::vec3 want(-restPositions[v].x, restPositions[v].y, restPositions[v].z);
        auto it = std::lower_bound(order.begin(), order.end(), want.x - tolerance,
                                   [&](std::uint32_t i, float x) { return restPositions[i].x < x; });
        float best = tolerance;
        for (; it != order.end() && restPositions[*it].x <= want.x + tolerance; ++it) {
            const float d = glm::length(restPositions[*it] - want);
            if (d <= best) {
                best = d;
                map[v] = *it;
            }
        }
    }
    return map;
}

namespace {

// Möller-Trumbore : distance le long du rayon, ou -1.
float rayTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& a, const glm::vec3& b,
                  const glm::vec3& c) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(dir, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return -1.0f;
    const float inv = 1.0f / det;
    const glm::vec3 t = origin - a;
    const float u = glm::dot(t, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const glm::vec3 q = glm::cross(t, e1);
    const float v = glm::dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    return glm::dot(e2, q) * inv;
}

}  // namespace

std::vector<std::uint8_t> visibleVertices(const std::vector<glm::vec3>& positions,
                                          const std::vector<glm::vec3>& normals,
                                          const std::vector<std::uint32_t>& indices, const glm::vec3& eye,
                                          const std::vector<std::uint32_t>& candidates) {
    std::vector<std::uint8_t> visible(positions.size(), 0);
    for (const std::uint32_t v : candidates) {
        if (v >= positions.size()) continue;
        const glm::vec3 toEye = eye - positions[v];
        const float distance = glm::length(toEye);
        if (distance < 1e-9f) {
            visible[v] = 1;
            continue;
        }
        const glm::vec3 dir = toEye / distance;
        if (v < normals.size() && glm::dot(normals[v], dir) < -0.05f) continue;  // dos tourné à l'œil
        bool blocked = false;
        for (std::size_t i = 0; i + 2 < indices.size() && !blocked; i += 3) {
            const std::uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a == v || b == v || c == v) continue;
            // Origine décalée vers l'œil pour ne pas s'auto-intersecter.
            const float t =
                rayTriangle(positions[v] + dir * 1e-4f, dir, positions[a], positions[b], positions[c]);
            blocked = t > 0.0f && t < distance - 1e-4f;
        }
        visible[v] = blocked ? 0 : 1;
    }
    return visible;
}

// --- Skinning automatique ----------------------------------------------------------------------

namespace {

struct Segment {
    BoneId bone;
    glm::vec3 a, b;
};

float distanceToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

}  // namespace

namespace {

// Poids par diffusion de chaleur : pour chaque os b, on résout (L + H) w = H p sur le graphe du maillage,
// avec L le laplacien (arêtes pondérées par 1 / longueur), H_i = k / d_i^2 (d_i = distance du sommet à l'os
// le plus proche) et p_i = 1 si b est cet os le plus proche, 0 sinon. Gauss-Seidel : pas de dépendance à une
// lib d'algèbre.
void heatDiffusionSkin(Mesh& mesh, const std::vector<Segment>& segments, const AutoSkinOptions& options) {
    const std::size_t n = mesh.vertices.size();
    const MeshTopology topology = MeshTopology::build(mesh);
    std::vector<BoneId> nearestBone(n, kNoBone);
    std::vector<float> anchor(n, 0.0f);  // H_i
    std::set<BoneId> bones;
    const float floorDistance = 0.04f * mesh.computeBounds().radius;
    const float floorDistance2 = std::max(floorDistance * floorDistance, 1e-8f);
    for (std::size_t v = 0; v < n; ++v) {
        float best = std::numeric_limits<float>::max();
        for (const Segment& s : segments) {
            const float d = distanceToSegment(mesh.vertices[v].position, s.a, s.b);
            if (d < best) {
                best = d;
                nearestBone[v] = s.bone;
            }
        }
        // Plancher de distance : un sommet exactement sur un os ne doit pas être ancré par un H infini
        // (marche brutale).
        anchor[v] = options.diffusionStrength / std::max(best * best, floorDistance2);
        bones.insert(nearestBone[v]);
    }
    // Poids des arêtes (1 / longueur) calculés une fois.
    std::vector<std::vector<float>> edge(n);
    for (std::size_t v = 0; v < n; ++v) {
        for (const std::uint32_t m : topology.neighbors[v]) {
            edge[v].push_back(
                1.0f / std::max(glm::length(mesh.vertices[v].position - mesh.vertices[m].position), 1e-5f));
        }
    }
    std::vector<std::map<BoneId, float>> result(n);
    std::vector<float> w(n);
    for (const BoneId bone : bones) {
        for (std::size_t v = 0; v < n; ++v) w[v] = nearestBone[v] == bone ? 1.0f : 0.0f;
        for (int iteration = 0; iteration < options.diffusionIterations; ++iteration) {
            for (std::size_t v = 0; v < n; ++v) {
                float num = nearestBone[v] == bone ? anchor[v] : 0.0f;
                float den = anchor[v];
                for (std::size_t k = 0; k < topology.neighbors[v].size(); ++k) {
                    num += edge[v][k] * w[topology.neighbors[v][k]];
                    den += edge[v][k];
                }
                w[v] = den > 0.0f ? num / den : w[v];
            }
        }
        for (std::size_t v = 0; v < n; ++v) {
            if (w[v] > 1e-4f) result[v][bone] = w[v];
        }
    }
    for (std::size_t v = 0; v < n; ++v) {
        std::vector<Entry> entries;
        for (const auto& [bone, weight] : result[v]) entries.push_back({bone, weight});
        if (entries.empty()) entries.push_back({nearestBone[v], 1.0f});
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.weight > b.weight; });
        SkinWeight skin;
        for (std::size_t k = 0; k < 4 && k < entries.size(); ++k) {
            skin.joints[k] = entries[k].bone;
            skin.weights[k] = entries[k].weight;
        }
        (void)limitInfluences(skin, std::clamp<std::size_t>(options.maxInfluences, 1, 4));
        (void)normalizeWeights(skin);
        mesh.skinWeights[v] = skin;
    }
}

}  // namespace

void autoSkin(Mesh& mesh, const Skeleton& skeleton, const AutoSkinOptions& options) {
    RIGGER_PROFILE_SCOPE("autoSkin");
    std::vector<glm::mat4> bindWorld(skeleton.slotCount(), glm::mat4(1.0f));
    for (const BoneId id : skeleton.order()) {
        const Bone& bone = skeleton.bone(id);
        const glm::mat4 parent = bone.parent == kNoBone ? glm::mat4(1.0f) : bindWorld[bone.parent];
        bindWorld[id] = parent * bone.bindLocal.toMat4();
    }
    std::vector<Segment> segments;
    for (const BoneId id : skeleton.order()) {
        const Bone& bone = skeleton.bone(id);
        if (hasFlag(bone.flags, BoneFlags::Control)) continue;
        const glm::vec3 head = glm::vec3(bindWorld[id][3]);
        std::vector<BoneId> kids;
        for (const BoneId child : skeleton.children(id)) {
            if (!hasFlag(skeleton.bone(child).flags, BoneFlags::Control)) kids.push_back(child);
        }
        if (kids.empty()) {  // os terminal : segment le long de son axe +Y sur sa longueur
            segments.push_back({id, head, head + glm::vec3(bindWorld[id] * glm::vec4(0, bone.length, 0, 0))});
        } else {
            for (const BoneId child : kids) segments.push_back({id, head, glm::vec3(bindWorld[child][3])});
        }
    }
    if (segments.empty() || mesh.vertices.empty()) return;

    mesh.skinWeights.assign(mesh.vertices.size(), SkinWeight{});
    if (options.method == AutoSkinMethod::HeatDiffusion) {
        heatDiffusionSkin(mesh, segments, options);
        return;
    }
    // Poids bruts : pour chaque os, la distance minimale à ses segments, puis 1 / (d + eps)^power sur les
    // plus proches.
    std::vector<std::vector<Entry>> raw(mesh.vertices.size());
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
        std::map<BoneId, float> best;
        for (const Segment& s : segments) {
            const float d = distanceToSegment(mesh.vertices[v].position, s.a, s.b);
            auto [it, inserted] = best.emplace(s.bone, d);
            if (!inserted) it->second = std::min(it->second, d);
        }
        std::vector<std::pair<float, BoneId>> sorted;
        for (const auto& [bone, d] : best) sorted.emplace_back(d, bone);
        std::sort(sorted.begin(), sorted.end());
        const float nearest = sorted.front().first;
        for (std::size_t i = 0; i < sorted.size() && i < 4; ++i) {
            // Les os bien plus loin que le plus proche n'influencent pas (coupure à 3x la distance minimale).
            if (i > 0 && sorted[i].first > nearest * 3.0f + 0.02f) break;
            raw[v].push_back({sorted[i].second, 1.0f / std::pow(sorted[i].first + 0.01f, options.power)});
        }
    }
    // Lissage sur la topologie : moyenne des poids (vecteurs creux) avec les voisins.
    const MeshTopology topology = MeshTopology::build(mesh);
    for (int iteration = 0; iteration < options.smoothIterations; ++iteration) {
        std::vector<std::vector<Entry>> next(raw.size());
        for (std::size_t v = 0; v < raw.size(); ++v) {
            std::map<BoneId, float> sum;
            const auto add = [&](const std::vector<Entry>& entries, float factor) {
                float total = 0.0f;
                for (const Entry& e : entries) total += e.weight;
                if (total <= 0.0f) return;
                for (const Entry& e : entries) sum[e.bone] += factor * e.weight / total;
            };
            add(raw[v], 2.0f);
            for (const std::uint32_t n : topology.neighbors[v]) add(raw[n], 1.0f);
            for (const auto& [bone, w] : sum) next[v].push_back({bone, w});
        }
        raw = std::move(next);
    }
    for (std::size_t v = 0; v < raw.size(); ++v) {
        std::sort(raw[v].begin(), raw[v].end(),
                  [](const Entry& a, const Entry& b) { return a.weight > b.weight; });
        SkinWeight skin;
        for (std::size_t k = 0; k < 4 && k < raw[v].size(); ++k) {
            skin.joints[k] = raw[v][k].bone;
            skin.weights[k] = raw[v][k].weight;
        }
        if (skin.empty()) {
            skin.joints[0] = segments.front().bone;
            skin.weights[0] = 1.0f;
        }
        (void)limitInfluences(skin, std::clamp<std::size_t>(options.maxInfluences, 1, 4));
        (void)normalizeWeights(skin);
        mesh.skinWeights[v] = skin;
    }
}

bool pointInPolygon(const glm::vec2& point, const std::vector<glm::vec2>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const glm::vec2& a = polygon[i];
        const glm::vec2& b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

// --- Outils globaux ----------------------------------------------------------------------------

std::size_t normalizeAllWeights(Mesh& mesh) {
    std::size_t fixed = 0;
    for (SkinWeight& skin : mesh.skinWeights) {
        const SkinWeight before = skin;
        if (!normalizeWeights(skin)) {
            skin = before;
            continue;
        }
        if (!sameSkin(skin, before)) ++fixed;
    }
    return fixed;
}

std::size_t limitAllInfluences(Mesh& mesh, std::size_t maxInfluences) {
    std::size_t changed = 0;
    for (SkinWeight& skin : mesh.skinWeights) {
        const SkinWeight before = skin;
        if (limitInfluences(skin, maxInfluences) && !sameSkin(skin, before)) {
            ++changed;
        } else {
            skin = before;
        }
    }
    return changed;
}

std::size_t pruneAllWeights(Mesh& mesh, float threshold) {
    std::size_t changed = 0;
    for (SkinWeight& skin : mesh.skinWeights) {
        const SkinWeight before = skin;
        if (pruneWeights(skin, threshold) && !sameSkin(skin, before)) {
            ++changed;
        } else {
            skin = before;
        }
    }
    return changed;
}

std::size_t copyWeightsNearest(Mesh& destination, const Mesh& source) {
    if (!source.hasSkinning() || source.vertices.empty()) return 0;
    destination.skinWeights.assign(destination.vertices.size(), SkinWeight{});
    for (std::size_t v = 0; v < destination.vertices.size(); ++v) {
        float best = std::numeric_limits<float>::max();
        std::size_t nearest = 0;
        for (std::size_t s = 0; s < source.vertices.size(); ++s) {
            const glm::vec3 d = source.vertices[s].position - destination.vertices[v].position;
            const float d2 = glm::dot(d, d);
            if (d2 < best) {
                best = d2;
                nearest = s;
            }
        }
        destination.skinWeights[v] = source.skinWeights[nearest];
    }
    return destination.vertices.size();
}

}  // namespace rigger
