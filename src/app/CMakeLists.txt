# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

add_executable(rigger_app main.cpp crash_handler.cpp crash_handler.hpp)
target_include_directories(rigger_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_target_properties(rigger_app PROPERTIES OUTPUT_NAME rigger)
target_link_libraries(rigger_app PRIVATE rigger_editor)
if(WIN32)
  target_link_libraries(rigger_app PRIVATE dbghelp)
  # Icône et métadonnées de version de rigger.exe
  enable_language(RC)
  configure_file(rigger.rc.in "${CMAKE_CURRENT_BINARY_DIR}/rigger.rc" @ONLY)
  target_sources(rigger_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rigger.rc")
endif()
rigger_set_warnings(rigger_app)

if(WIN32)
  get_filename_component(_qt_root "${Qt6Core_DIR}/../../.." ABSOLUTE)
  find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt.exe HINTS "${_qt_root}/bin")
  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET rigger_app POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:rigger_app>"
      COMMENT "Deploying Qt runtime libraries for rigger_app"
      VERBATIM)
  else()
    message(WARNING "windeployqt.exe not found; Qt runtime DLLs may be missing when running rigger_app")
  endif()
endif()
