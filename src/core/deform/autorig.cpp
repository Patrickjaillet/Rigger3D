// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/deform/autorig.hpp"

#include <algorithm>
#include <cmath>

namespace rigger {

const char* landmarkName(Landmark landmark) {
    static const char* names[kLandmarkCount] = {
        "Pelvis",  "Spine",      "Chest",      "Neck",    "Head",    "Head top",   "Shoulder L", "Elbow L",
        "Wrist L", "Hand tip L", "Shoulder R", "Elbow R", "Wrist R", "Hand tip R", "Hip L",      "Knee L",
        "Ankle L", "Toe L",      "Hip R",      "Knee R",  "Ankle R", "Toe R"};
    return names[static_cast<int>(landmark)];
}

Landmarks guessLandmarks(const std::vector<glm::vec3>& points) {
    Landmarks out{};
    if (points.empty()) return out;
    glm::vec3 lo = points.front(), hi = points.front();
    for (const glm::vec3& p : points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const float height = std::max(hi.y - lo.y, 1e-3f);
    const float width = hi.x - lo.x;
    const float cx = (lo.x + hi.x) * 0.5f;
    const float cz = (lo.z + hi.z) * 0.5f;
    const auto at = [&](float x, float y, float z = 0.0f) {
        return glm::vec3(cx + x * height, lo.y + y * height, cz + z * height);
    };
    const auto set = [&](Landmark l, const glm::vec3& v) { out[static_cast<std::size_t>(l)] = v; };
    set(Landmark::Pelvis, at(0, 0.53f));
    set(Landmark::Spine, at(0, 0.62f));
    set(Landmark::Chest, at(0, 0.72f));
    set(Landmark::Neck, at(0, 0.82f));
    set(Landmark::Head, at(0, 0.88f));
    set(Landmark::HeadTop, at(0, 1.0f));
    const bool tPose = width > 0.7f * height;
    const float halfArms = width * 0.5f / height;  // demi-envergure (fraction de la hauteur)
    for (int side = 0; side < 2; ++side) {
        const float s = side == 0 ? 1.0f : -1.0f;
        const int base =
            side == 0 ? static_cast<int>(Landmark::ShoulderL) : static_cast<int>(Landmark::ShoulderR);
        const float sx = 0.11f;
        if (tPose) {
            out[static_cast<std::size_t>(base)] = at(s * sx, 0.80f);
            out[static_cast<std::size_t>(base + 1)] = at(s * (sx + (halfArms - sx) * 0.45f), 0.80f);
            out[static_cast<std::size_t>(base + 2)] = at(s * (sx + (halfArms - sx) * 0.85f), 0.80f);
            out[static_cast<std::size_t>(base + 3)] = at(s * halfArms, 0.80f);
        } else {
            out[static_cast<std::size_t>(base)] = at(s * sx, 0.80f);
            out[static_cast<std::size_t>(base + 1)] = at(s * 0.15f, 0.62f);
            out[static_cast<std::size_t>(base + 2)] = at(s * 0.17f, 0.46f);
            out[static_cast<std::size_t>(base + 3)] = at(s * 0.17f, 0.38f);
        }
        const int leg = side == 0 ? static_cast<int>(Landmark::HipL) : static_cast<int>(Landmark::HipR);
        out[static_cast<std::size_t>(leg)] = at(s * 0.06f, 0.53f);
        out[static_cast<std::size_t>(leg + 1)] = at(s * 0.06f, 0.28f);
        out[static_cast<std::size_t>(leg + 2)] = at(s * 0.06f, 0.05f);
        out[static_cast<std::size_t>(leg + 3)] = at(s * 0.06f, 0.0f, 0.12f);
    }
    return out;
}

std::string validateLandmarks(const Landmarks& l) {
    const auto p = [&](Landmark m) { return l[static_cast<std::size_t>(m)]; };
    if (!(p(Landmark::HeadTop).y > p(Landmark::Head).y && p(Landmark::Head).y > p(Landmark::Neck).y &&
          p(Landmark::Neck).y > p(Landmark::Chest).y && p(Landmark::Chest).y > p(Landmark::Spine).y &&
          p(Landmark::Spine).y > p(Landmark::Pelvis).y)) {
        return "The spine landmarks must go up from the pelvis to the top of the head.";
    }
    for (const auto& [hip, knee, ankle] :
         {std::array<Landmark, 3>{Landmark::HipL, Landmark::KneeL, Landmark::AnkleL},
          std::array<Landmark, 3>{Landmark::HipR, Landmark::KneeR, Landmark::AnkleR}}) {
        if (!(p(hip).y > p(knee).y && p(knee).y > p(ankle).y))
            return "Each leg must go down from the hip to the ankle.";
    }
    if (!(p(Landmark::ShoulderL).x > p(Landmark::ShoulderR).x)) {
        return "The left side of the character must be on the +X side (left shoulder at larger X than the "
               "right).";
    }
    for (const glm::vec3& v : l) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return "A landmark has an invalid position.";
    }
    return {};
}

Skeleton buildHumanoidSkeleton(const Landmarks& l, HumanoidBones* out) {
    Skeleton sk;
    HumanoidBones ids;
    const auto p = [&](Landmark m) { return l[static_cast<std::size_t>(m)]; };
    // Les os sont définis par leurs origines (rotations de repos identité) : translation locale = différence
    // des positions monde. La longueur d'affichage est la distance à l'articulation suivante.
    std::vector<std::pair<BoneId, glm::vec3>> world;  // position monde de chaque os créé
    const auto add = [&](const std::string& name, BoneId parent, const glm::vec3& position,
                         const glm::vec3& tip) {
        glm::vec3 parentWorld(0.0f);
        for (const auto& [id, pos] : world) {
            if (id == parent) parentWorld = pos;
        }
        Transform bind;
        bind.t = position - parentWorld;
        const BoneId id = sk.addBone(name, parent, bind).value();
        sk.setLength(id, std::max(glm::length(tip - position), 0.02f));
        world.emplace_back(id, position);
        return id;
    };
    ids.pelvis = add("pelvis", kNoBone, p(Landmark::Pelvis), p(Landmark::Spine));
    ids.spine = add("spine", ids.pelvis, p(Landmark::Spine), p(Landmark::Chest));
    ids.chest = add("chest", ids.spine, p(Landmark::Chest), p(Landmark::Neck));
    ids.neck = add("neck", ids.chest, p(Landmark::Neck), p(Landmark::Head));
    ids.head = add("head", ids.neck, p(Landmark::Head), p(Landmark::HeadTop));
    for (int side = 0; side < 2; ++side) {
        const std::string suffix = side == 0 ? ".L" : ".R";
        const int a =
            side == 0 ? static_cast<int>(Landmark::ShoulderL) : static_cast<int>(Landmark::ShoulderR);
        const auto lm = [&](int i) { return l[static_cast<std::size_t>(i)]; };
        ids.upperArm[side] = add("upperarm" + suffix, ids.chest, lm(a), lm(a + 1));
        ids.foreArm[side] = add("forearm" + suffix, ids.upperArm[side], lm(a + 1), lm(a + 2));
        ids.hand[side] = add("hand" + suffix, ids.foreArm[side], lm(a + 2), lm(a + 3));
        const int h = side == 0 ? static_cast<int>(Landmark::HipL) : static_cast<int>(Landmark::HipR);
        ids.thigh[side] = add("thigh" + suffix, ids.pelvis, lm(h), lm(h + 1));
        ids.shin[side] = add("shin" + suffix, ids.thigh[side], lm(h + 1), lm(h + 2));
        ids.foot[side] = add("foot" + suffix, ids.shin[side], lm(h + 2), lm(h + 3));
        ids.toe[side] = add("toe" + suffix, ids.foot[side], lm(h + 3), lm(h + 3) + glm::vec3(0, 0, 0.05f));
    }
    if (out != nullptr) *out = ids;
    return sk;
}

}  // namespace rigger
