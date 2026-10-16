// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;

namespace rigger::editor {

class Document;

// Panneau « Skin » : pinceau (mode, rayon, force, falloff), options (normalisation, sommets visibles,
// symétrie X), os actif et verrouillage, poids exact sur les sommets sélectionnés, outils globaux
// (auto-skinning, normaliser, limiter à 4, élaguer, copier d'un autre maillage). Tout passe par des commandes
// annulables du Document.
class SkinPanel : public QWidget {
    Q_OBJECT
public:
    explicit SkinPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh();
    void pushBrush();
    void report(const QString& message);
    void reportCount(const QString& what, std::size_t count);

    Document* document_;
    QPushButton* paintButton_;
    QPushButton* selectButton_;
    QComboBox* selectShape_;
    QComboBox* autoMethod_;
    QLabel* autoMethodLabel_;
    QGroupBox* brushBox_;
    QComboBox* mode_;
    QDoubleSpinBox* radius_;
    QDoubleSpinBox* strength_;
    QComboBox* falloff_;
    QDoubleSpinBox* value_;
    QLabel* modeLabel_;
    QLabel* radiusLabel_;
    QLabel* strengthLabel_;
    QLabel* falloffLabel_;
    QLabel* valueLabel_;
    QCheckBox* normalize_;
    QCheckBox* visibleOnly_;
    QCheckBox* mirror_;
    QLabel* boneLabel_;
    QCheckBox* lockBone_;
    QGroupBox* vertexBox_;
    QLabel* vertexCount_;
    QDoubleSpinBox* exactWeight_;
    QPushButton* applyWeight_;
    QGroupBox* toolsBox_;
    QPushButton* autoSkin_;
    QPushButton* normalizeAll_;
    QPushButton* limit_;
    QPushButton* prune_;
    QComboBox* sourceMesh_;
    QComboBox* targetMesh_;
    QPushButton* copy_;
    bool updating_ = false;
};

}  // namespace rigger::editor
