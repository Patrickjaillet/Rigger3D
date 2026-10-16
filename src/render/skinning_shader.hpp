// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <string_view>

namespace rigger::render {

inline constexpr std::string_view kSkinningGlsl = R"(
layout(std140) uniform SkinMatrices {
    mat4 uSkinMatrices[256];
};
mat4 skinMatrix(uvec4 joints, vec4 weights) {
    return uSkinMatrices[joints.x] * weights.x +
           uSkinMatrices[joints.y] * weights.y +
           uSkinMatrices[joints.z] * weights.z +
           uSkinMatrices[joints.w] * weights.w;
}
vec3 skinPosition(vec3 position, uvec4 joints, vec4 weights) {
    vec4 result = skinMatrix(joints, weights) * vec4(position, 1.0);
    return result.xyz / result.w;
}
vec3 skinNormal(vec3 normal, uvec4 joints, vec4 weights) {
    return normalize(transpose(inverse(mat3(skinMatrix(joints, weights)))) * normal);
}
float boneInfluence(uvec4 joints, vec4 weights, uint selectedBone) {
    return (joints.x == selectedBone ? weights.x : 0.0) +
           (joints.y == selectedBone ? weights.y : 0.0) +
           (joints.z == selectedBone ? weights.z : 0.0) +
           (joints.w == selectedBone ? weights.w : 0.0);
}
)";

}  // namespace rigger::render
