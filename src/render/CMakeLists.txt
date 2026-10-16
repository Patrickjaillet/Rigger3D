# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Moteur de rendu : AUCUNE dépendance à Qt (le contexte GL est fourni par l'éditeur).
add_library(rigger_render STATIC
  buffer.cpp
  buffer.hpp
  camera.cpp
  camera.hpp
  gl_loader.cpp
  gl_loader.hpp
  gl_state.cpp
  gl_state.hpp
  gl_requirements.cpp
  gl_requirements.hpp
  gizmo.cpp
  gizmo.hpp
  gpu_skinning.cpp
  gpu_skinning.hpp
  grid.cpp
  grid.hpp
  mesh_renderer.cpp
  mesh_renderer.hpp
  skinning_shader.hpp
  shader.cpp
  shader.hpp
  skeleton_renderer.cpp
  skeleton_renderer.hpp)
target_include_directories(rigger_render PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(rigger_render PUBLIC rigger_core glad_gl_41)
rigger_set_warnings(rigger_render)
