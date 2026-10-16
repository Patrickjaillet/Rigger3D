# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

# Cibles de fuzzing libFuzzer (Clang uniquement) : cmake -DRIGGER_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
foreach(target IN ITEMS fuzz_project fuzz_gltf)
  add_executable(${target} ${target}.cpp)
  target_link_libraries(${target} PRIVATE rigger_core rigger_io)
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
