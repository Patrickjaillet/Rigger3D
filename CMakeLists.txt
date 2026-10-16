# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

cmake_minimum_required(VERSION 3.25)
project(Rigger VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RIGGER_BUILD_APP "Construire l'application Qt (nécessite Qt 6)" ON)
option(RIGGER_BUILD_TESTS "Construire les tests" ON)
option(RIGGER_WARNINGS_AS_ERRORS "Traiter les warnings comme des erreurs" ON)
option(RIGGER_ENABLE_SANITIZERS "ASan + UBSan (GCC/Clang)" OFF)
option(RIGGER_BUILD_FUZZERS "Cibles de fuzzing libFuzzer (Clang uniquement)" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

# Qt : MOC/RCC automatiques pour NOS cibles uniquement (défini après les dépendances téléchargées),
# interface écrite en code (pas de fichiers .ui).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

add_subdirectory(src)
if(RIGGER_BUILD_APP)
  include(Packaging)
endif()
if(RIGGER_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()

if(RIGGER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
