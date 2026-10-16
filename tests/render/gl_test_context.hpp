// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <memory>

// Contexte OpenGL 4.1 hors écran pour les tests GPU. Il est défini dans son propre fichier : sous macOS, les
// en-têtes OpenGL de Qt et ceux de glad ne peuvent pas être inclus dans la même unité de compilation.
class GlTestContext {
public:
    GlTestContext(int& argc, char** argv);
    ~GlTestContext();
    GlTestContext(const GlTestContext&) = delete;
    GlTestContext& operator=(const GlTestContext&) = delete;

    bool ok() const;  // contexte créé et actif
    // Adresse d'une fonction OpenGL du contexte actif (pour render::loadOpenGL).
    static void* procAddress(const char* name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
