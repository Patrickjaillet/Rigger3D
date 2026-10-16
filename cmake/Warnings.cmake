# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Sanitizers : global, doit être inclus AVANT les dépendances pour qu'elles soient aussi instrumentées.
if(RIGGER_ENABLE_SANITIZERS)
  if(MSVC)
    message(WARNING "Sanitizers ignorés avec MSVC dans ce projet.")
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# Warnings stricts, appliqués uniquement à NOS cibles (pas aux dépendances).
function(rigger_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
    if(RIGGER_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(RIGGER_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
