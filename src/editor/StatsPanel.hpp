// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>
#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace rigger::editor {

class Document;

// Statistiques d'affichage fournies par le viewport (FPS, temps CPU / GPU du dernier rendu).
struct FrameStats {
    double fps = 0.0;
    double cpuMs = 0.0;
    double gpuMs = 0.0;
};

// Panneau « Statistiques » : FPS, temps CPU / GPU par image, taille de la scène et zones du profileur
// (RIGGER_PROFILE_SCOPE : appels, moyenne, max, dernier). Le profileur n'est actif que lorsque la case est
// cochée.
class StatsPanel : public QWidget {
    Q_OBJECT
public:
    StatsPanel(Document* document, std::function<FrameStats()> frameStats, QWidget* parent = nullptr);
    void retranslate();
    void refresh();  // met à jour les valeurs (appelé par un minuteur, et par les tests)

protected:
    void changeEvent(QEvent* event) override;

private:
    Document* document_;
    std::function<FrameStats()> frameStats_;
    QCheckBox* enabled_;
    QPushButton* reset_;
    QLabel* summary_;
    QLabel* scene_;
    QTableWidget* table_;
    QTimer* timer_;
};

}  // namespace rigger::editor
