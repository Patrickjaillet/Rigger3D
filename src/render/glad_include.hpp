// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
// Point d'entrée unique pour glad (OpenGL). À inclure AVANT tout en-tête OpenGL de Qt dans une même unité de
// compilation.
#include <glad/glad.h>

#if defined(__APPLE__)
// glad remplace les fonctions OpenGL par des macros (glFoo -> glad_glFoo) : le glext.h d'Apple, inclus par
// les en-têtes OpenGL de Qt, les redéclarerait et provoquerait « redefinition of 'glad_gl...' ». On l'empêche
// d'être traité.
#ifndef __glext_h_
#define __glext_h_ 1
#endif
#endif
