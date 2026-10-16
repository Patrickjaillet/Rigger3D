// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace rigger::editor {

class ActionRegistry;
class Preferences;

// Préférences : thème, langue, FPS par défaut, sauvegarde automatique, unités d'angle, raccourcis.
// Les changements sont appliqués immédiatement (Preferences émet changed(), MainWindow réagit).
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    PreferencesDialog(Preferences* preferences, ActionRegistry* actions, QWidget* parent = nullptr);
    void retranslate();

    // Accès pour les tests.
    QComboBox* themeCombo() const { return theme_; }
    QComboBox* languageCombo() const { return language_; }
    QSpinBox* fpsSpin() const { return fps_; }
    QTableWidget* shortcutTable() const { return shortcuts_; }
    // Tente d'affecter un raccourci à une action (comme la saisie dans le tableau) ; renvoie false en cas de
    // conflit.
    bool assignShortcut(const QString& actionId, const QKeySequence& sequence);

protected:
    void changeEvent(QEvent* event) override;

private:
    void build();
    void loadValues();
    void rebuildShortcutTable();

    Preferences* preferences_;
    ActionRegistry* actions_;
    QTabWidget* tabs_ = nullptr;
    QDoubleSpinBox* fontSize_ = nullptr;
    QLabel* fontSizeLabel_ = nullptr;
    QComboBox* theme_ = nullptr;
    QComboBox* language_ = nullptr;
    QSpinBox* fps_ = nullptr;
    QCheckBox* autosave_ = nullptr;
    QSpinBox* autosaveMinutes_ = nullptr;
    QComboBox* angleUnit_ = nullptr;
    QTableWidget* shortcuts_ = nullptr;
    QLabel* conflictLabel_ = nullptr;
    QLabel* themeLabel_ = nullptr;
    QLabel* languageLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* autosaveLabel_ = nullptr;
    QLabel* angleLabel_ = nullptr;
    bool loading_ = false;
};

}  // namespace rigger::editor
