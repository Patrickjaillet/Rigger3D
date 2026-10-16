// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QPushButton;
class QToolButton;

namespace rigger::editor {

class Document;
class GraphView;

// Panneau « Courbes » : filtres de canaux (Position / Rotation / Échelle), « Tout voir » et la vue GraphView.
class GraphPanel : public QWidget {
    Q_OBJECT
public:
    explicit GraphPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();
    GraphView* view() const { return view_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    GraphView* view_;
    QToolButton* channelButtons_[3];
    QPushButton* fitButton_;
};

}  // namespace rigger::editor
