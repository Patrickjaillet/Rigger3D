# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(render)
if(RIGGER_BUILD_APP)
  add_subdirectory(editor)
  add_subdirectory(app)
endif()
