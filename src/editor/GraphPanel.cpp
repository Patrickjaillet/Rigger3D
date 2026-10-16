// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/GraphPanel.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "editor/GraphView.hpp"

namespace rigger::editor {

GraphPanel::GraphPanel(Document* document, QWidget* parent)
    : QWidget(parent), view_(new GraphView(document, this)), fitButton_(new QPushButton(this)) {
    setObjectName("graphPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    for (int channel = 0; channel < 3; ++channel) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("graphChannel%1").arg(channel));
        button->setCheckable(true);
        button->setChecked(true);
        connect(button, &QToolButton::toggled, this,
                [this, channel](bool on) { view_->setChannelVisible(channel, on); });
        channelButtons_[channel] = button;
        bar->addWidget(button);
    }
    fitButton_->setObjectName("graphFit");
    connect(fitButton_, &QPushButton::clicked, view_, &GraphView::fitView);
    bar->addWidget(fitButton_);
    bar->addStretch(1);
    root->addLayout(bar);
    root->addWidget(view_, 1);
    retranslate();
}

void GraphPanel::retranslate() {
    channelButtons_[0]->setText(tr("Position"));
    channelButtons_[1]->setText(tr("Rotation"));
    channelButtons_[2]->setText(tr("Scale"));
    fitButton_->setText(tr("Fit"));
    fitButton_->setToolTip(tr("Frame all visible curves (F)"));
}

void GraphPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
