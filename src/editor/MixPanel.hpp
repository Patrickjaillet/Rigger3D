// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>
#include <memory>

#include "core/animation/retarget.hpp"
#include "core/character/character.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTableWidget;
class QTabWidget;

namespace rigger::editor {

class Document;

// Panneau « Mélange » : mode (clip seul / couches / NLA / blend tree), couches (poids, masque, Override /
// Additif), séquenceur NLA (bandes, répétition, fondus, fondu enchaîné), blend tree 1D et rétro-ciblage
// depuis un autre personnage. Tout passe par des commandes annulables du Document.
class MixPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();

    // Rétro-ciblage : personnage source (importé depuis un fichier, ou fourni par les tests).
    void setRetargetSource(std::shared_ptr<Character> source);

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildLayersTab(QWidget* page);
    void buildNlaTab(QWidget* page);
    void buildTreeTab(QWidget* page);
    void buildRetargetTab(QWidget* page);
    void refresh();
    void refreshLayers();
    void refreshStrips();
    void refreshTree();
    void refreshRetarget();
    void fillClipCombo(QComboBox* combo, int keep);
    void loadRetargetFile();
    void runRetarget();
    int layerIndex() const;
    int stripIndex() const;
    int treeIndex() const;

    Document* document_;
    QComboBox* mode_;
    QLabel* modeLabel_;
    QTabWidget* tabs_;
    bool updating_ = false;

    // Couches
    QListWidget* layerList_;
    QPushButton* addLayer_;
    QPushButton* removeLayer_;
    QPushButton* layerUp_;
    QPushButton* layerDown_;
    QComboBox* layerClip_;
    QSlider* layerWeight_;
    QComboBox* layerMode_;
    QDoubleSpinBox* layerSpeed_;
    QDoubleSpinBox* layerOffset_;
    QCheckBox* layerEnabled_;
    QLabel* layerMaskLabel_;
    QPushButton* maskFromSelection_;
    QPushButton* clearMask_;
    QLabel* layerClipLabel_;
    QLabel* layerWeightLabel_;
    QLabel* layerModeLabel_;
    QLabel* layerSpeedLabel_;
    QLabel* layerOffsetLabel_;

    // NLA
    QListWidget* stripList_;
    QPushButton* addStrip_;
    QPushButton* removeStrip_;
    QComboBox* stripClip_;
    QDoubleSpinBox* stripStart_;
    QDoubleSpinBox* stripTrack_;
    QDoubleSpinBox* stripRepeat_;
    QDoubleSpinBox* stripSpeed_;
    QDoubleSpinBox* stripBlendIn_;
    QDoubleSpinBox* stripBlendOut_;
    QSlider* stripWeight_;
    QComboBox* stripMode_;
    QComboBox* fadeA_;
    QComboBox* fadeB_;
    QDoubleSpinBox* fadeDuration_;
    QPushButton* fadeButton_;
    QLabel* stripLabels_[9];
    QLabel* fadeLabel_;

    // Blend tree
    QListWidget* treeList_;
    QPushButton* addEntry_;
    QPushButton* removeEntry_;
    QComboBox* entryClip_;
    QDoubleSpinBox* entryThreshold_;
    QSlider* treeParameter_;
    QLabel* treeParameterLabel_;
    QCheckBox* treeSync_;
    QLabel* entryClipLabel_;
    QLabel* entryThresholdLabel_;

    // Rétro-ciblage
    std::shared_ptr<Character> source_;
    BoneMap map_;
    QPushButton* loadSource_;
    QLabel* sourceLabel_;
    QComboBox* sourceClip_;
    QPushButton* autoMap_;
    QTableWidget* mapTable_;
    QCheckBox* scaleTranslation_;
    QPushButton* retarget_;
};

}  // namespace rigger::editor
