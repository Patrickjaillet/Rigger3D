# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Version générée à la configuration
configure_file(version.hpp.in "${CMAKE_BINARY_DIR}/generated/core/version.hpp" @ONLY)

add_library(rigger_core STATIC
  version.cpp
  result.hpp
  mesh/mesh.hpp
  character/character.hpp
  animation/clip.cpp
  animation/clip.hpp
  animation/keys.cpp
  animation/keys.hpp
  animation/player.cpp
  animation/mix.cpp
  animation/mix.hpp
  animation/retarget.cpp
  animation/retarget.hpp
  animation/player.hpp
  skinning/lbs.cpp
  skinning/dqs.cpp
  skinning/weights.cpp
  skinning/paint.cpp
  skinning/paint.hpp
  math/transform.cpp
  math/quat_utils.cpp
  math/ray.cpp
  skeleton/skeleton.cpp
  skeleton/fk.cpp
  skeleton/picking.cpp
  skeleton/manipulate.cpp
  skeleton/symmetry.cpp
  util/profiler.cpp
  util/profiler.hpp
  example/example.cpp
  example/example.hpp
  deform/autorig.cpp
  deform/autorig.hpp
  deform/deform.cpp
  deform/deform.hpp
  deform/morph.cpp
  deform/morph.hpp
  ik/ik.cpp
  ik/ik.hpp
  ik/rig.cpp
  ik/rig.hpp
  character/edit.cpp
  command/command.hpp
  command/history.cpp
  command/history.hpp
  command/commands.cpp
  command/commands.hpp)
target_include_directories(rigger_core PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated")
# core : GLM + STL (+ logs). JAMAIS Qt / OpenGL (vérifié par tools/check_deps.py).
target_link_libraries(rigger_core PUBLIC glm::glm spdlog::spdlog)
rigger_set_warnings(rigger_core)
