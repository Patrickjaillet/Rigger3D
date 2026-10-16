// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skeleton/symmetry.hpp"

#include <cctype>

namespace rigger {

namespace {

bool isSeparator(char c) { return c == '.' || c == '_'; }

// Le jeton L/R en position `i` est valide s'il est précédé d'un séparateur et suivi de la fin, d'un
// séparateur ou d'un chiffre.
bool isSideToken(const std::string& s, std::size_t i) {
    if (i == 0 || !isSeparator(s[i - 1])) {
        return false;
    }
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    if (c != 'l' && c != 'r') {
        return false;
    }
    if (i + 1 == s.size()) {
        return true;
    }
    const char next = s[i + 1];
    return isSeparator(next) || std::isdigit(static_cast<unsigned char>(next)) != 0;
}

}  // namespace

std::optional<std::string> mirrorName(const std::string& name) {
    // On retient le DERNIER jeton valide (« arm_L.002 » : le L est suivi d'un séparateur).
    for (std::size_t i = name.size(); i-- > 0;) {
        if (!isSideToken(name, i)) {
            continue;
        }
        std::string out = name;
        const char c = name[i];
        const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool left = std::tolower(static_cast<unsigned char>(c)) == 'l';
        out[i] = left ? (upper ? 'R' : 'r') : (upper ? 'L' : 'l');
        return out;
    }
    return std::nullopt;
}

BoneId findMirrorBone(const Skeleton& skeleton, BoneId id) {
    if (!skeleton.contains(id)) {
        return kNoBone;
    }
    const auto mirrored = mirrorName(skeleton.bone(id).name);
    if (!mirrored) {
        return kNoBone;
    }
    const BoneId other = skeleton.find(*mirrored);
    return other == id ? kNoBone : other;
}

glm::mat4 mirrorMatrixX() {
    return glm::mat4(glm::vec4(-1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0),
                     glm::vec4(0, 0, 0, 1));
}

glm::mat4 mirrorWorldX(const glm::mat4& world) {
    const glm::mat4 s = mirrorMatrixX();
    return s * world * s;
}

Transform mirroredLocal(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, BoneId target,
                        const glm::mat4& sourceWorld) {
    const BoneId parent = skeleton.bone(target).parent;
    const glm::mat4 parentWorld = parent == kNoBone ? glm::mat4(1.0f) : globals[parent];
    return Transform::fromMat4(glm::inverse(parentWorld) * mirrorWorldX(sourceWorld));
}

}  // namespace rigger
