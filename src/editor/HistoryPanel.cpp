// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/HistoryPanel.hpp"

#include <QEvent>
#include <QUndoView>
#include <QVBoxLayout>

#include "editor/Document.hpp"

namespace rigger::editor {

HistoryPanel::HistoryPanel(Document* document, QWidget* parent)
    : QWidget(parent), view_(new QUndoView(this)) {
    setObjectName("historyPanel");
    view_->setObjectName("historyView");
    view_->setStack(document->undoStack());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(view_);
    retranslate();
}

void HistoryPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void HistoryPanel::retranslate() { view_->setEmptyLabel(tr("<initial state>")); }

}  // namespace rigger::editor
