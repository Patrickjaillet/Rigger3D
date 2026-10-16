// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "core/mesh/mesh.hpp"
#include "core/result.hpp"

namespace rigger {

Result<std::vector<Vertex>> skinLbs(const Mesh& mesh, const std::vector<glm::mat4>& skinMatrices);

}  // namespace rigger
