// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace rigger::editor {

class Document;

// Panneau « IK » : création de chaînes depuis l'os sélectionné (2 os / 3 os / N os), look-at, préréglage
// humanoïde, réglages du contrôle choisi (solveur, poids, twist du pole, orientation de l'effecteur), bascule
// IK <-> FK, bake IK -> FK et verrous de pied. Tout passe par des commandes annulables du Document.
class IKPanel : public QWidget {
    Q_OBJECT
public:
    explicit IKPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();
    QListWidget* controlList() const { return list_; }
    int currentControl() const;

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh();
    void refreshDetails();
    void createChain();
    void createLookAt();
    void createHumanoid();
    void report(const QString& message);

    Document* document_;
    // Création.
    QSpinBox* boneCount_;
    QComboBox* solver_;
    QCheckBox* withPole_;
    QPushButton* createButton_;
    QPushButton* lookAtButton_;
    QPushButton* humanoidButton_;
    // Liste + réglages.
    QListWidget* list_;
    QGroupBox* settingsBox_;
    QPushButton* toggleButton_;
    QSlider* weight_;
    QLabel* weightLabel_;
    QLabel* solverLabel_;
    QComboBox* controlSolver_;
    QLabel* orientationLabel_;
    QComboBox* orientation_;
    QLabel* twistLabel_;
    QDoubleSpinBox* twist_;
    QPushButton* removeButton_;
    // Bake.
    QGroupBox* bakeBox_;
    QSpinBox* bakeStart_;
    QSpinBox* bakeEnd_;
    QCheckBox* bakeDisable_;
    QPushButton* bakeButton_;
    // Verrous de pied.
    QGroupBox* lockBox_;
    QSpinBox* lockStart_;
    QSpinBox* lockEnd_;
    QPushButton* lockButton_;
    QListWidget* lockList_;
    QPushButton* unlockButton_;
    bool updating_ = false;
};

}  // namespace rigger::editor
