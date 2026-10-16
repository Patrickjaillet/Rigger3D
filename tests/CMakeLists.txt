# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

add_executable(rigger_tests
  main.cpp
  core/test_smoke.cpp
  core/test_version.cpp
  core/test_result.cpp
  io/test_formats.cpp
  render/test_requirements.cpp
  render/test_camera.cpp
  render/test_gizmo.cpp
  core/test_transform.cpp
  core/test_quat_utils.cpp
  core/test_ray.cpp
  core/test_skeleton.cpp
  core/test_mesh.cpp
  core/test_skinning.cpp
  core/test_clip.cpp
  core/test_history.cpp
  core/test_keys.cpp
  core/test_picking.cpp
  core/test_manipulate.cpp
  core/test_symmetry_edit.cpp
  core/test_ik.cpp
  core/test_paint.cpp
  core/test_mix.cpp
  core/test_morph.cpp
  core/test_deform.cpp
  core/test_example.cpp
  io/test_gltf_import.cpp
  io/test_project.cpp
  io/test_mutation.cpp)
target_include_directories(rigger_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rigger_tests PRIVATE
  RIGGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
  DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(rigger_tests PRIVATE rigger_core rigger_io rigger_render doctest::doctest)
rigger_set_warnings(rigger_tests)
add_test(NAME rigger_tests COMMAND rigger_tests)

# Budgets de performance (imposés en Release seulement) : 200 os + 200 000 sommets, ouverture < 2 s, undo < 16 ms.
add_executable(rigger_perf perf/bench.cpp)
target_link_libraries(rigger_perf PRIVATE rigger_core rigger_io doctest::doctest)
rigger_set_warnings(rigger_perf)
add_test(NAME rigger_perf COMMAND rigger_perf)
set_tests_properties(rigger_perf PROPERTIES LABELS perf TIMEOUT 600)

# Règles de dépendances entre modules (core sans Qt/GL, etc.)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME check_deps
           COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_deps.py"
                   --root "${PROJECT_SOURCE_DIR}")
  add_test(NAME check_license
           COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/license_headers.py"
                   --root "${PROJECT_SOURCE_DIR}")
  add_test(NAME check_deps_selftest
           COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_deps.py" --self-test)
endif()

# Tests d'interface (Qt Test, plateforme "offscreen" : aucun écran requis)
if(RIGGER_BUILD_APP)
  add_executable(rigger_editor_tests
    editor/test_main.cpp
    editor/test_mainwindow.cpp
    editor/test_document.cpp
    editor/test_rigging.cpp
    editor/test_undo.cpp
    editor/test_animation.cpp
    editor/test_timeline.cpp
    editor/test_ik.cpp
    editor/test_skin.cpp
    editor/test_project.cpp
    editor/test_mix.cpp
    editor/test_deform.cpp
    editor/test_accessibility.cpp
    editor/test_bonetree.cpp
    editor/test_inspector.cpp
    editor/test_background.cpp
    editor/test_preferences.cpp
    editor/test_console.cpp
    editor/test_about.cpp)
  target_include_directories(rigger_editor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/editor"
                                                        "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(rigger_editor_tests PRIVATE rigger_editor Qt6::Test)
  target_compile_definitions(rigger_editor_tests PRIVATE
    RIGGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  rigger_set_warnings(rigger_editor_tests)
  add_test(NAME rigger_editor_tests COMMAND rigger_editor_tests)
  set(_ui_env "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    get_filename_component(_qt_root "${Qt6Core_DIR}/../../.." ABSOLUTE)
    set(_qt_bin "${_qt_root}/bin")
    list(APPEND _ui_env "PATH=${_qt_bin};$ENV{PATH}")
  endif()
  if(RIGGER_ENABLE_SANITIZERS)
    list(APPEND _ui_env "ASAN_OPTIONS=detect_leaks=0")  # fuites internes de Qt/fontconfig
  endif()
  set_tests_properties(rigger_editor_tests PROPERTIES ENVIRONMENT "${_ui_env}" LABELS ui)

  # Rapport de crash local : le processus plante volontairement et doit laisser un rapport (+ minidump sous Windows).
  if(Python3_FOUND)
    add_test(NAME crash_report
             COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_crash_report.py" "$<TARGET_FILE:rigger_app>")
    set_tests_properties(crash_report PROPERTIES ENVIRONMENT "${_ui_env}" LABELS ui)
  endif()

  add_executable(rigger_gpu_tests render/test_gpu_skinning.cpp)
  target_include_directories(rigger_gpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(rigger_gpu_tests PRIVATE rigger_core rigger_render Qt6::Gui doctest::doctest)
  target_compile_definitions(rigger_gpu_tests PRIVATE RIGGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  rigger_set_warnings(rigger_gpu_tests)
  add_test(NAME rigger_gpu_tests COMMAND rigger_gpu_tests)
  set(_gpu_env "${_ui_env}")
  if(WIN32)
    list(REMOVE_ITEM _gpu_env "QT_QPA_PLATFORM=offscreen")
    list(PREPEND _gpu_env "QT_QPA_PLATFORM=windows")
  endif()
  set_tests_properties(rigger_gpu_tests PROPERTIES ENVIRONMENT "${_gpu_env}" LABELS gpu
                       SKIP_RETURN_CODE 77)
endif()
