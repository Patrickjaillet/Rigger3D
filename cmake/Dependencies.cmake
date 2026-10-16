# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

include(FetchContent)
find_package(Python COMPONENTS Interpreter REQUIRED)

# Compatibilité avec CMake 4 pour les dépendances qui déclarent un vieux cmake_minimum_required.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Chaque dépendance est figée sur un tag précis (jamais master/main).
FetchContent_Declare(glm            GIT_REPOSITORY https://github.com/g-truc/glm.git
                                    GIT_TAG 1.0.1   GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(spdlog         GIT_REPOSITORY https://github.com/gabime/spdlog.git
                                    GIT_TAG v1.14.1 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(nlohmann_json  GIT_REPOSITORY https://github.com/nlohmann/json.git
                                    GIT_TAG v3.11.3 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(glad           GIT_REPOSITORY https://github.com/Dav1dde/glad.git
                                    GIT_TAG v2.0.8 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(tinygltf       GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
                                    GIT_TAG v2.9.3  GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(doctest        GIT_REPOSITORY https://github.com/doctest/doctest.git
                                    GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
set(SPDLOG_INSTALL OFF CACHE INTERNAL "")
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE INTERNAL "")
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE INTERNAL "")
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE INTERNAL "")
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE INTERNAL "")
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "")
set(TINYGLTF_HEADER_ONLY OFF CACHE INTERNAL "")
set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(glm spdlog nlohmann_json glad tinygltf)
if(NOT TARGET glad_gl_41)
  set(GLAD_OUTPUT_DIR "${CMAKE_BINARY_DIR}/gladsources/glad_gl_41")
  set(GLAD_GENERATED_FILES
      "${GLAD_OUTPUT_DIR}/include/KHR/khrplatform.h"
      "${GLAD_OUTPUT_DIR}/include/glad/gl.h"
      "${GLAD_OUTPUT_DIR}/src/glad.c")

  add_custom_command(
    OUTPUT ${GLAD_GENERATED_FILES}
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${GLAD_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GLAD_OUTPUT_DIR}"
    # Do not run from the vendored glad checkout: its local source tree shadows the
    # installed glad Python package and makes CMake pick the wrong CLI implementation.
    COMMAND ${Python_EXECUTABLE} -m glad --profile core --out-path "${GLAD_OUTPUT_DIR}" --api "gl=4.1" --generator c
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating OpenGL glad loader"
    VERBATIM)

  add_custom_target(glad_gl_41_generate DEPENDS ${GLAD_GENERATED_FILES})

  add_library(glad_gl_41 STATIC "${GLAD_OUTPUT_DIR}/src/glad.c")
  add_dependencies(glad_gl_41 glad_gl_41_generate)
  target_include_directories(glad_gl_41 PUBLIC "${GLAD_OUTPUT_DIR}/include")
  set_target_properties(glad_gl_41 PROPERTIES C_STANDARD 99)
endif()
if(RIGGER_BUILD_TESTS)
  FetchContent_MakeAvailable(doctest)
endif()

# Qt 6 : installé sur le système (installeur Qt, aqtinstall, apt, brew ou vcpkg).
if(RIGGER_BUILD_APP)
  set(_qt_components Core Gui Widgets OpenGL OpenGLWidgets Svg Concurrent LinguistTools)
  if(RIGGER_BUILD_TESTS)
    list(APPEND _qt_components Test)
  endif()
  find_package(Qt6 6.4 REQUIRED COMPONENTS ${_qt_components})
  message(STATUS "Qt ${Qt6_VERSION} trouvé")

  # Qt Advanced Docking System (LGPL 2.1) : liaison statique, à documenter en Phase 16 (mentions de licence).
  FetchContent_Declare(qtads GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
                             GIT_TAG 4.4.1 GIT_SHALLOW TRUE SYSTEM)
  set(ADS_VERSION 4.4.1 CACHE INTERNAL "")
  set(BUILD_STATIC ON CACHE INTERNAL "")
  set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(qtads)
endif()
