// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/PreferencesDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "editor/ActionRegistry.hpp"
#include "editor/Preferences.hpp"
#include "editor/Translations.hpp"

namespace rigger::editor {

PreferencesDialog::PreferencesDialog(Preferences* preferences, ActionRegistry* actions, QWidget* parent)
    : QDialog(parent), preferences_(preferences), actions_(actions) {
    setObjectName("preferencesDialog");
    resize(560, 520);
    build();
    retranslate();
}

void PreferencesDialog::build() {
    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);

    // --- Général ---
    auto* general = new QWidget(this);
    auto* form = new QFormLayout(general);
    theme_ = new QComboBox(general);
    theme_->setObjectName("prefTheme");
    theme_->addItems({QString(), QString()});
    language_ = new QComboBox(general);
    language_->setObjectName("prefLanguage");
    language_->addItems({QString(), QStringLiteral("English"), QStringLiteral("Français")});
    fps_ = new QSpinBox(general);
    fps_->setObjectName("prefFps");
    fps_->setRange(1, 240);
    auto* autosaveRow = new QHBoxLayout;
    autosave_ = new QCheckBox(general);
    autosave_->setObjectName("prefAutosave");
    autosaveMinutes_ = new QSpinBox(general);
    autosaveMinutes_->setObjectName("prefAutosaveMinutes");
    autosaveMinutes_->setRange(1, 60);
    autosaveRow->addWidget(autosave_);
    autosaveRow->addWidget(autosaveMinutes_);
    autosaveRow->addStretch(1);
    angleUnit_ = new QComboBox(general);
    angleUnit_->setObjectName("prefAngleUnit");
    angleUnit_->addItems({QString(), QString()});
    fontSize_ = new QDoubleSpinBox(general);
    fontSize_->setObjectName("prefFontSize");
    fontSize_->setRange(8.0, 20.0);
    fontSize_->setSingleStep(0.5);
    fontSize_->setDecimals(1);
    fontSizeLabel_ = new QLabel(general);
    themeLabel_ = new QLabel(general);
    languageLabel_ = new QLabel(general);
    fpsLabel_ = new QLabel(general);
    autosaveLabel_ = new QLabel(general);
    angleLabel_ = new QLabel(general);
    form->addRow(themeLabel_, theme_);
    form->addRow(fontSizeLabel_, fontSize_);
    form->addRow(languageLabel_, language_);
    form->addRow(fpsLabel_, fps_);
    form->addRow(autosaveLabel_, autosaveRow);
    form->addRow(angleLabel_, angleUnit_);
    tabs_->addTab(general, QString());

    // --- Raccourcis ---
    auto* shortcutsPage = new QWidget(this);
    auto* shortcutsLayout = new QVBoxLayout(shortcutsPage);
    shortcuts_ = new QTableWidget(shortcutsPage);
    shortcuts_->setObjectName("prefShortcuts");
    shortcuts_->setColumnCount(3);
    shortcuts_->verticalHeader()->setVisible(false);
    shortcuts_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shortcuts_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    shortcuts_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    shortcuts_->setSelectionMode(QAbstractItemView::NoSelection);
    shortcuts_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    conflictLabel_ = new QLabel(shortcutsPage);
    conflictLabel_->setObjectName("prefConflict");
    conflictLabel_->setWordWrap(true);
    shortcutsLayout->addWidget(shortcuts_, 1);
    shortcutsLayout->addWidget(conflictLabel_);
    tabs_->addTab(shortcutsPage, QString());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName("prefButtons");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(theme_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!loading_) {
            preferences_->setTheme(i == 1 ? ThemeKind::Light : ThemeKind::Dark);
        }
    });
    connect(language_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!loading_) {
            preferences_->setLanguage(i == 1 ? QStringLiteral("en")
                                             : (i == 2 ? QStringLiteral("fr") : QString()));
        }
    });
    connect(fontSize_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!loading_) {
            preferences_->setFontSize(v);
        }
    });
    connect(fps_, &QSpinBox::valueChanged, this, [this](int v) {
        if (!loading_) {
            preferences_->setDefaultFps(v);
        }
    });
    const auto applyAutosave = [this] {
        if (!loading_) {
            preferences_->setAutosave(autosave_->isChecked(), autosaveMinutes_->value());
        }
        autosaveMinutes_->setEnabled(autosave_->isChecked());
    };
    connect(autosave_, &QCheckBox::toggled, this, applyAutosave);
    connect(autosaveMinutes_, &QSpinBox::valueChanged, this, applyAutosave);
    connect(angleUnit_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!loading_) {
            preferences_->setAngleUnit(i == 1 ? AngleUnit::Radians : AngleUnit::Degrees);
        }
    });
}

void PreferencesDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void PreferencesDialog::retranslate() {
    setWindowTitle(tr("Preferences"));
    tabs_->setTabText(0, tr("General"));
    tabs_->setTabText(1, tr("Shortcuts"));
    themeLabel_->setText(tr("Theme"));
    fontSizeLabel_->setText(tr("Font size"));
    languageLabel_->setText(tr("Language"));
    fpsLabel_->setText(tr("Default frame rate (FPS)"));
    autosaveLabel_->setText(tr("Autosave"));
    angleLabel_->setText(tr("Angle unit"));
    autosave_->setText(tr("Every"));
    autosaveMinutes_->setSuffix(tr(" min"));
    loading_ = true;
    theme_->setItemText(0, tr("Dark"));
    theme_->setItemText(1, tr("Light"));
    language_->setItemText(0, tr("System default"));
    angleUnit_->setItemText(0, tr("Degrees"));
    angleUnit_->setItemText(1, tr("Radians"));
    loading_ = false;
    shortcuts_->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut"), QString()});
    loadValues();
    rebuildShortcutTable();
}

void PreferencesDialog::loadValues() {
    loading_ = true;
    theme_->setCurrentIndex(preferences_->theme() == ThemeKind::Light ? 1 : 0);
    fontSize_->setValue(preferences_->fontSize());
    const QString lang = preferences_->language();
    language_->setCurrentIndex(lang == QLatin1String("en") ? 1 : (lang == QLatin1String("fr") ? 2 : 0));
    fps_->setValue(preferences_->defaultFps());
    autosave_->setChecked(preferences_->autosaveEnabled());
    autosaveMinutes_->setValue(preferences_->autosaveMinutes());
    autosaveMinutes_->setEnabled(preferences_->autosaveEnabled());
    angleUnit_->setCurrentIndex(preferences_->angleUnit() == AngleUnit::Radians ? 1 : 0);
    loading_ = false;
}

bool PreferencesDialog::assignShortcut(const QString& actionId, const QKeySequence& sequence) {
    const QString other = actions_->conflictingAction(actionId, sequence);
    if (!other.isEmpty()) {
        conflictLabel_->setText(tr("\"%1\" is already used by \"%2\".")
                                    .arg(sequence.toString(QKeySequence::NativeText),
                                         ActionRegistry::plainText(actions_->action(other))));
        return false;
    }
    conflictLabel_->clear();
    actions_->setShortcut(actionId, sequence);
    return true;
}

void PreferencesDialog::rebuildShortcutTable() {
    shortcuts_->setRowCount(0);
    for (const ActionRegistry::Entry& entry : actions_->entries()) {
        const int row = shortcuts_->rowCount();
        shortcuts_->insertRow(row);
        auto* name = new QTableWidgetItem(entry.action->icon(), ActionRegistry::plainText(entry.action));
        shortcuts_->setItem(row, 0, name);

        auto* edit = new QKeySequenceEdit(entry.action->shortcut(), shortcuts_);
        edit->setObjectName(QStringLiteral("prefShortcut_") + entry.id);
        edit->setMaximumSequenceLength(1);
        shortcuts_->setCellWidget(row, 1, edit);
        auto* reset = new QPushButton(tr("Reset"), shortcuts_);
        shortcuts_->setCellWidget(row, 2, reset);

        const QString id = entry.id;
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, id, edit] {
            if (!assignShortcut(id, edit->keySequence())) {
                edit->setKeySequence(actions_->shortcut(id));  // conflit : on revient à l'ancien raccourci
            }
        });
        connect(reset, &QPushButton::clicked, this, [this, id, edit] {
            const QString other = actions_->conflictingAction(id, actions_->defaultShortcut(id));
            if (other.isEmpty()) {
                actions_->resetShortcut(id);
                conflictLabel_->clear();
            } else {
                conflictLabel_->setText(tr("The default shortcut of this action is used by \"%1\".")
                                            .arg(ActionRegistry::plainText(actions_->action(other))));
            }
            edit->setKeySequence(actions_->shortcut(id));
        });
    }
}

}  // namespace rigger::editor
