# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

add_library(rigger_io STATIC
  formats.cpp
  formats.hpp
  gltf_import.cpp
  gltf_import.hpp
  gltf_export.cpp
  gltf_export.hpp
  project.cpp
  project.hpp)
target_include_directories(rigger_io PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(rigger_io PUBLIC rigger_core nlohmann_json::nlohmann_json PRIVATE tinygltf)
rigger_set_warnings(rigger_io)
