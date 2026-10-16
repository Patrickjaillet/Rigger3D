// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

#include "core/deform/autorig.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace rigger::editor {

class Document;

// Panneau « Déformation » : blend shapes (sliders, shapes correctives), os de twist, contraintes (Copy
// Transform, Limit, Child-Of, Track-To), spring bones (cheveux, queue : raideur, amortissement, gravité,
// colliders, bake) et auto-rig humanoïde (repères -> squelette + skinning + IK). Tout passe par des commandes
// annulables du Document.
class DeformPanel : public QWidget {
    Q_OBJECT
public:
    explicit DeformPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildMorphTab(QWidget* page);
    void buildTwistTab(QWidget* page);
    void buildConstraintTab(QWidget* page);
    void buildSpringTab(QWidget* page);
    void buildRigTab(QWidget* page);
    void refresh();
    void refreshMorphs();
    void refreshLists();
    void refreshLandmarks(const Landmarks& landmarks);
    Landmarks readLandmarks() const;
    int constraintIndex() const;
    int springIndex() const;

    Document* document_;
    QTabWidget* tabs_;
    bool updating_ = false;

    // Blend shapes
    QWidget* morphHost_;
    QLabel* morphEmpty_;
    QListWidget* driverList_;
    QComboBox* driverAxis_;
    QDoubleSpinBox* driverFrom_;
    QDoubleSpinBox* driverTo_;
    QPushButton* addDriver_;
    QPushButton* removeDriver_;
    QLabel* driverHint_;
    int selectedMesh_ = -1;
    int selectedTarget_ = -1;

    // Twist
    QSpinBox* twistCount_;
    QPushButton* createTwist_;
    QListWidget* twistList_;
    QPushButton* removeTwist_;
    QLabel* twistHint_;

    // Contraintes
    QComboBox* constraintKind_;
    QPushButton* addConstraint_;
    QListWidget* constraintList_;
    QSlider* constraintInfluence_;
    QCheckBox* constraintEnabled_;
    QPushButton* removeConstraint_;
    QLabel* constraintHint_;
    QLabel* influenceLabel_;

    // Springs
    QSpinBox* springBones_;
    QPushButton* createSpring_;
    QListWidget* springList_;
    QDoubleSpinBox* springStiffness_;
    QDoubleSpinBox* springDamping_;
    QDoubleSpinBox* springGravity_;
    QCheckBox* springEnabled_;
    QPushButton* removeSpring_;
    QCheckBox* springsGlobal_;
    QDoubleSpinBox* colliderRadius_;
    QPushButton* addCollider_;
    QLabel* colliderCount_;
    QSpinBox* bakeFirst_;
    QSpinBox* bakeLast_;
    QCheckBox* bakeDisable_;
    QPushButton* bakeSprings_;
    QLabel* stiffnessLabel_;
    QLabel* dampingLabel_;
    QLabel* gravityLabel_;
    QLabel* bakeLabel_;

    // Auto-rig
    QTableWidget* landmarks_;
    QPushButton* guess_;
    QComboBox* skinMethod_;
    QCheckBox* withIk_;
    QPushButton* generate_;
    QLabel* rigHint_;
};

}  // namespace rigger::editor
