// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <cstdint>
#include <vector>

#include "core/mesh/mesh.hpp"
#include "core/result.hpp"
#include "render/mesh_renderer.hpp"

namespace rigger::render {

struct GpuSkinningResult {
    std::vector<MeshVertex> vertices;
    std::uint64_t gpuTimeNanoseconds = 0;
};

Result<GpuSkinningResult> skinVerticesGpu(const Mesh& mesh, const std::vector<glm::mat4>& skinMatrices);

}  // namespace rigger::render
