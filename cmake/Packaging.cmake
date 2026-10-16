# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Installation et paquets (CPack). Dispositions identiques sur les trois OS :
#   bin/rigger(.exe) + bibliothèques Qt (liaison DYNAMIQUE, conformité LGPL) ; share/rigger/examples ; share/doc/rigger.
# L'application cherche le guide dans ../share/doc/rigger et l'exemple dans ../share/rigger/examples (voir AboutDialog / MainWindow).
include_guard(GLOBAL)

# Tout est rangé dans le composant « Rigger » : les règles d'installation des dépendances téléchargées (en-têtes, .lib,
# cibles CMake de doctest / Qt Advanced Docking System) n'entrent ainsi jamais dans les paquets (CPACK_COMPONENTS_ALL).
set(RIGGER_COMPONENT Rigger)
install(TARGETS rigger_app
        RUNTIME DESTINATION bin COMPONENT ${RIGGER_COMPONENT}
        BUNDLE DESTINATION . COMPONENT ${RIGGER_COMPONENT})

# Déploiement de Qt (windeployqt / macdeployqt / copie des .so) au moment de l'installation.
if(COMMAND qt_generate_deploy_app_script)
  qt_generate_deploy_app_script(
    TARGET rigger_app
    OUTPUT_SCRIPT RIGGER_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    NO_TRANSLATIONS)
  install(SCRIPT "${RIGGER_DEPLOY_SCRIPT}" COMPONENT ${RIGGER_COMPONENT})
else()
  message(WARNING "qt_generate_deploy_app_script indisponible (Qt < 6.3) : les bibliothèques Qt ne seront pas déployées")
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/examples/" DESTINATION share/rigger/examples
        COMPONENT ${RIGGER_COMPONENT} FILES_MATCHING PATTERN "*.rig" PATTERN "*.md")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION share/doc/rigger COMPONENT ${RIGGER_COMPONENT})
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
              "${PROJECT_SOURCE_DIR}/COPYING"
              "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
              "${PROJECT_SOURCE_DIR}/README.md"
              "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
              "${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt"
              "${PROJECT_SOURCE_DIR}/assets/LICENSES.md"
        DESTINATION share/doc/rigger COMPONENT ${RIGGER_COMPONENT})

# --- CPack ------------------------------------------------------------------------------------------------------
set(CPACK_COMPONENTS_ALL ${RIGGER_COMPONENT})
set(CPACK_PACKAGE_NAME "Rigger3D")
set(CPACK_PACKAGE_VENDOR "Patrick JAILLET")
set(CPACK_PACKAGE_CONTACT "sandefjord.development@proton.me")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://patrickjaillet.github.io/Rigger3D")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D character animation: skeleton, skinning, FK / IK, timeline")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Rigger")
set(CPACK_PACKAGE_EXECUTABLES "rigger;Rigger")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME "rigger-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  find_program(RIGGER_MAKENSIS NAMES makensis)
  if(RIGGER_MAKENSIS)
    list(APPEND CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Rigger ${PROJECT_VERSION}")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/app/rigger.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/app/rigger.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\rigger.exe")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  endif()
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ;DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Patrick JAILLET <sandefjord.development@proton.me>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
include(CPack)
