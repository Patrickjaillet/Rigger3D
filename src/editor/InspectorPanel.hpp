// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>
#include <array>

#include "core/skeleton/skeleton.hpp"

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace rigger::editor {

class Document;
class Preferences;
class ScrubField;

// Éditeur de propriétés de la sélection : nom, T (m), R (Euler affiché, degrés ou radians), S, longueur,
// couleur, drapeaux Masqué / Verrouillé (les drapeaux IK arriveront avec la Phase 10).
// Multi-sélection : les valeurs différentes s'affichent « — » ; une édition s'applique à tous les os.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    InspectorPanel(Document* document, Preferences* preferences, QWidget* parent = nullptr);
    void retranslate();

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    enum Group { Translation = 0, Rotation = 1, Scale = 2 };

    void build();
    void refresh();
    void editComponent(Group group, int axis, double value);
    void editLength(double value);
    void editName();
    void pickColor();
    void editFlag(BoneFlags flag, bool on);

    Document* document_;
    Preferences* preferences_;
    bool updating_ = false;

    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QGroupBox* transformGroup_ = nullptr;
    std::array<QLabel*, 3> rowLabels_{};
    std::array<std::array<ScrubField*, 3>, 3> fields_{};
    QLabel* lengthLabel_ = nullptr;
    ScrubField* lengthField_ = nullptr;
    QLabel* colorLabel_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QCheckBox* hiddenBox_ = nullptr;
    QCheckBox* lockedBox_ = nullptr;
    QLabel* nameLabel_ = nullptr;
};

}  // namespace rigger::editor
