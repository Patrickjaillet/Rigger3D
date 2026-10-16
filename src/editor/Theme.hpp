// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

#include "editor/Preferences.hpp"

class QApplication;

namespace rigger::editor {

// Enregistre les ressources de l'éditeur (Q_INIT_RESOURCE : nécessaire car editor est une bibliothèque
// statique).
void initEditorResources();

// Palette de couleurs d'un thème (jetons @nom@ de assets/ui/theme.qss).
struct ThemeColors {
    QColor window, panel, base, text, textDim, border, hover, accent, accentText, selection;
};
ThemeColors themeColors(ThemeKind kind);

class Theme {
public:
    // Fusion + palette + feuille de style unique + police Inter embarquée. Idempotent.
    // Taille de police de toute l'application (points).
    static void setFontSize(double points);
    static double fontSize() { return fontSize_; }
    static void apply(QApplication& app, ThemeKind kind);
    static ThemeKind current() { return current_; }
    static const ThemeColors& colors() { return colors_; }

    // Feuille de style après substitution des jetons (exposée pour les tests).
    static QString styleSheet(const ThemeColors& colors);
    // Seulement la section « docking » de la feuille de style, à ajouter à celle du CDockManager.
    static QString dockStyleSheet(const ThemeColors& colors);

private:
    static void loadFonts();

    static inline ThemeKind current_ = ThemeKind::Dark;
    static inline double fontSize_ = 9.5;
    static inline ThemeColors colors_ = themeColors(ThemeKind::Dark);
};

// Icônes SVG monochromes (assets/ui/icons/<nom>.svg, en currentColor) recolorées selon le thème courant.
class Icons {
public:
    static QIcon get(const QString& name);
    static QPixmap pixmap(const QString& name, const QColor& color, int size, qreal devicePixelRatio = 1.0);
};

}  // namespace rigger::editor
