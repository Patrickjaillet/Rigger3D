// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QUndoView;

namespace rigger::editor {

class Document;

// Panneau « Historique » : liste cliquable des actions (QUndoView) ; cliquer une ligne revient à cet état.
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();
    QUndoView* view() const { return view_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    QUndoView* view_;
};

}  // namespace rigger::editor
