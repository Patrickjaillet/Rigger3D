// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/TutorialGuide.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace rigger::editor {

TutorialGuide::TutorialGuide(QWidget* window) : QFrame(window), window_(window) {
    setObjectName("tutorialGuide");
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setFixedWidth(340);
    auto* layout = new QVBoxLayout(this);
    counter_ = new QLabel(this);
    counter_->setProperty("dim", true);
    text_ = new QLabel(this);
    text_->setObjectName("tutorialText");
    text_->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    skip_ = new QPushButton(tr("Skip"), this);
    skip_->setObjectName("tutorialSkip");
    next_ = new QPushButton(tr("Next"), this);
    next_->setObjectName("tutorialNext");
    next_->setProperty("primary", true);
    buttons->addWidget(skip_);
    buttons->addStretch(1);
    buttons->addWidget(next_);
    layout->addWidget(counter_);
    layout->addWidget(text_);
    layout->addLayout(buttons);
    connect(next_, &QPushButton::clicked, this, &TutorialGuide::next);
    connect(skip_, &QPushButton::clicked, this, &TutorialGuide::skip);
    hide();
}

void TutorialGuide::setSteps(std::vector<Step> steps) { steps_ = std::move(steps); }

void TutorialGuide::start() {
    if (steps_.empty()) return;
    running_ = true;
    index_ = 0;
    window_->installEventFilter(this);
    showStep();
}

QString TutorialGuide::currentText() const {
    return index_ >= 0 && index_ < stepCount() ? steps_[static_cast<std::size_t>(index_)].text : QString();
}

void TutorialGuide::next() {
    if (!running_) return;
    if (index_ + 1 >= stepCount()) {
        end(true);
        return;
    }
    ++index_;
    showStep();
}

void TutorialGuide::skip() {
    if (running_) end(false);
}

void TutorialGuide::end(bool completed) {
    running_ = false;
    index_ = -1;
    window_->removeEventFilter(this);
    hide();
    emit finished(completed);
}

void TutorialGuide::showStep() {
    const bool last = index_ + 1 >= stepCount();
    counter_->setText(tr("Step %1 of %2").arg(index_ + 1).arg(stepCount()));
    text_->setText(currentText());
    next_->setText(last ? tr("Done") : tr("Next"));
    skip_->setVisible(!last);
    adjustSize();
    reposition();
    show();
    raise();
}

void TutorialGuide::reposition() {
    if (index_ < 0 || index_ >= stepCount()) return;
    const QWidget* target = steps_[static_cast<std::size_t>(index_)].target;
    QRect area = window_->rect();
    if (target != nullptr && target->isVisible()) {
        area = QRect(target->mapTo(window_, QPoint(0, 0)), target->size()).intersected(window_->rect());
    }
    // Dans le coin supérieur gauche de la zone ciblée (donc toujours dans la fenêtre) ; centrée si la zone
    // est trop petite.
    QPoint position = area.topLeft() + QPoint(16, 16);
    if (area.width() < width() + 32 || area.height() < height() + 32) {
        position = window_->rect().center() - QPoint(width() / 2, height() / 2);
    }
    position.setX(std::clamp(position.x(), 0, std::max(0, window_->width() - width())));
    position.setY(std::clamp(position.y(), 0, std::max(0, window_->height() - height())));
    move(position);
}

bool TutorialGuide::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ && running_ &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        reposition();
    }
    return QFrame::eventFilter(watched, event);
}

}  // namespace rigger::editor
