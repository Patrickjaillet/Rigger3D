# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

add_library(rigger_editor STATIC
  ActionRegistry.cpp
  ActionRegistry.hpp
  BackgroundTask.cpp
  BackgroundTask.hpp
  BoneTreeModel.cpp
  BoneTreeModel.hpp
  CharacterPanel.cpp
  CharacterPanel.hpp
  CommandNames.cpp
  CommandNames.hpp
  ConsolePanel.cpp
  ConsolePanel.hpp
  Document.cpp
  DocumentIK.cpp
  DocumentSkin.cpp
  DocumentMix.cpp
  DocumentDeform.cpp
  MainWindowProject.cpp
  ExportDialogs.cpp
  ExportDialogs.hpp
  Document.hpp
  HistoryPanel.cpp
  HistoryPanel.hpp
  HierarchyPanel.cpp
  HierarchyPanel.hpp
  StatsPanel.cpp
  StatsPanel.hpp
  TutorialGuide.cpp
  TutorialGuide.hpp
  AboutDialog.cpp
  AboutDialog.hpp
  DeformPanel.cpp
  DeformPanel.hpp
  MixPanel.cpp
  MixPanel.hpp
  SkinPanel.cpp
  SkinPanel.hpp
  IKPanel.cpp
  IKPanel.hpp
  GraphView.cpp
  GraphView.hpp
  GraphPanel.cpp
  GraphPanel.hpp
  PoseLibraryPanel.cpp
  PoseLibraryPanel.hpp
  TimelinePanel.cpp
  TimelinePanel.hpp
  TimelineView.cpp
  TimelineView.hpp
  InspectorPanel.cpp
  InspectorPanel.hpp
  MainWindow.cpp
  MainWindow.hpp
  Preferences.cpp
  Preferences.hpp
  PreferencesDialog.cpp
  PreferencesDialog.hpp
  ScrubField.cpp
  ScrubField.hpp
  Theme.cpp
  UndoAdapter.cpp
  UndoAdapter.hpp
  Theme.hpp
  Translations.cpp
  Translations.hpp
  ViewportWidget.cpp
  ViewportWidget.hpp
  WelcomeWidget.cpp
  WelcomeWidget.hpp
  ${PROJECT_SOURCE_DIR}/assets/ui/resources.qrc)
target_include_directories(rigger_editor PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(rigger_editor PUBLIC Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Concurrent
                                           ads::qtadvanceddocking-qt6 rigger_core rigger_io rigger_render)
rigger_set_warnings(rigger_editor)

# Traductions : rigger_en.ts / rigger_fr.ts (générés par lupdate, cible `update_translations`), compilés en .qm
# et embarqués dans l'exécutable (préfixe :/i18n/).
qt_add_translations(rigger_editor
  TS_FILES "${PROJECT_SOURCE_DIR}/assets/ui/i18n/rigger_en.ts" "${PROJECT_SOURCE_DIR}/assets/ui/i18n/rigger_fr.ts"
  RESOURCE_PREFIX "/i18n"
  LUPDATE_OPTIONS -no-obsolete)
