// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/TimelinePanel.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/Theme.hpp"
#include "editor/TimelineView.hpp"

namespace rigger::editor {

namespace {

QToolButton* iconButton(QWidget* parent, const QString& name, bool checkable = false) {
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    button->setIcon(Icons::get(name));
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    return button;
}

constexpr double kSpeeds[] = {-1.0, 0.25, 0.5, 1.0, 2.0, 4.0};

}  // namespace

TimelinePanel::TimelinePanel(Document* document, ActionRegistry* actions, QWidget* parent)
    : QWidget(parent), document_(document), view_(new TimelineView(document, this)) {
    setObjectName("timelinePanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(4);

    for (const QString& id : {ActionId::AnimFirstFrame, ActionId::AnimPrevFrame, ActionId::AnimPlay,
                              ActionId::AnimNextFrame, ActionId::AnimLastFrame}) {
        auto* button = new QToolButton(this);
        button->setDefaultAction(actions->action(id));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        bar->addWidget(button);
    }
    frameLabel_ = new QLabel(this);
    frameBox_ = new QSpinBox(this);
    frameBox_->setObjectName("timelineFrame");
    frameBox_->setRange(0, 100000);
    bar->addWidget(frameLabel_);
    bar->addWidget(frameBox_);

    for (const QString& id : {ActionId::AnimInsertKey, ActionId::AnimAutoKey, ActionId::AnimOnionSkin}) {
        auto* button = new QToolButton(this);
        button->setDefaultAction(actions->action(id));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        bar->addWidget(button);
    }

    bar->addSpacing(12);
    clipLabel_ = new QLabel(this);
    clipBox_ = new QComboBox(this);
    clipBox_->setObjectName("timelineClip");
    clipBox_->setMinimumContentsLength(12);
    clipBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    newClip_ = iconButton(this, QStringLiteral("file-new"));
    duplicateClip_ = iconButton(this, QStringLiteral("copy"));
    deleteClip_ = iconButton(this, QStringLiteral("clear"));
    renameClip_ = iconButton(this, QStringLiteral("settings"));
    renameClip_->setObjectName("timelineRenameClip");
    newClip_->setObjectName("timelineNewClip");
    duplicateClip_->setObjectName("timelineDuplicateClip");
    deleteClip_->setObjectName("timelineDeleteClip");
    bar->addWidget(clipLabel_);
    bar->addWidget(clipBox_);
    bar->addWidget(newClip_);
    bar->addWidget(duplicateClip_);
    bar->addWidget(renameClip_);
    bar->addWidget(deleteClip_);

    fpsLabel_ = new QLabel(this);
    fpsBox_ = new QDoubleSpinBox(this);
    fpsBox_->setObjectName("timelineFps");
    fpsBox_->setRange(1.0, 240.0);
    fpsBox_->setDecimals(0);
    bar->addWidget(fpsLabel_);
    bar->addWidget(fpsBox_);

    loopButton_ = iconButton(this, QStringLiteral("loop"), true);
    loopButton_->setObjectName("timelineLoop");
    pingPongButton_ = iconButton(this, QStringLiteral("pingpong"), true);
    pingPongButton_->setObjectName("timelinePingPong");
    bar->addWidget(loopButton_);
    bar->addWidget(pingPongButton_);

    speedLabel_ = new QLabel(this);
    speedBox_ = new QComboBox(this);
    speedBox_->setObjectName("timelineSpeed");
    for (const double s : kSpeeds) {
        speedBox_->addItem(QStringLiteral("%1x").arg(s), s);
    }
    bar->addWidget(speedLabel_);
    bar->addWidget(speedBox_);
    bar->addStretch(1);

    root->addLayout(bar);
    root->addWidget(view_, 1);

    connect(frameBox_, &QSpinBox::valueChanged, this, [this](int frame) {
        if (!updating_) {
            document_->setPlaying(false);
            document_->setFrame(frame);
        }
    });
    connect(clipBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!updating_) {
            document_->setClipIndex(index);
        }
    });
    connect(newClip_, &QToolButton::clicked, this, [this] { document_->newClip(); });
    connect(duplicateClip_, &QToolButton::clicked, this, [this] { document_->duplicateClip(); });
    connect(deleteClip_, &QToolButton::clicked, this, [this] { document_->deleteClip(); });
    connect(renameClip_, &QToolButton::clicked, this, [this] {
        if (document_->clip() == nullptr) {
            return;
        }
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Rename Clip"), tr("Clip name"), QLineEdit::Normal,
                                  QString::fromStdString(document_->clip()->name), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            document_->renameClip(name.trimmed());
        }
    });
    connect(fpsBox_, &QDoubleSpinBox::valueChanged, this, [this](double fps) {
        if (!updating_) {
            document_->setClipFps(static_cast<float>(fps));
        }
    });
    connect(loopButton_, &QToolButton::toggled, this, [this](bool on) {
        if (!updating_) {
            document_->setLoopPlayback(on);
        }
    });
    connect(pingPongButton_, &QToolButton::toggled, this, [this](bool on) {
        if (!updating_) {
            document_->setPingPong(on);
        }
    });
    connect(speedBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!updating_ && index >= 0) {
            document_->setPlaybackSpeed(speedBox_->itemData(index).toDouble());
        }
    });

    connect(document_, &Document::timeChanged, this, &TimelinePanel::refreshValues);
    connect(document_, &Document::playbackSettingsChanged, this, &TimelinePanel::refreshValues);
    connect(document_, &Document::clipChanged, this, [this] {
        refreshClips();
        refreshValues();
    });
    connect(document_, &Document::animationChanged, this, [this] {
        refreshClips();
        refreshValues();
    });
    connect(document_, &Document::characterChanged, this, [this] {
        refreshClips();
        refreshValues();
    });
    retranslate();
    refreshClips();
    refreshValues();
}

void TimelinePanel::refreshClips() {
    updating_ = true;
    clipBox_->clear();
    const auto character = document_->character();
    if (character) {
        for (const AnimationClip& c : character->clips) {
            clipBox_->addItem(QString::fromStdString(c.name));
        }
    }
    clipBox_->setCurrentIndex(document_->clipIndex());
    clipBox_->setEnabled(clipBox_->count() > 0);
    updating_ = false;
}

void TimelinePanel::refreshValues() {
    updating_ = true;
    const AnimationClip* clip = document_->clip();
    frameBox_->setMaximum(std::max(document_->frameCount() - 1, 0));
    frameBox_->setValue(document_->frame());
    fpsBox_->setValue(clip != nullptr ? clip->fps : document_->fps());
    fpsBox_->setEnabled(clip != nullptr);
    duplicateClip_->setEnabled(clip != nullptr);
    deleteClip_->setEnabled(clip != nullptr);
    renameClip_->setEnabled(clip != nullptr);
    loopButton_->setChecked(document_->loopPlayback());
    pingPongButton_->setChecked(document_->pingPong());
    int best = 0;
    for (int i = 0; i < speedBox_->count(); ++i) {
        if (std::abs(speedBox_->itemData(i).toDouble() - document_->playbackSpeed()) < 1e-6) {
            best = i;
        }
    }
    speedBox_->setCurrentIndex(best);
    updating_ = false;
}

void TimelinePanel::retranslate() {
    frameLabel_->setText(tr("Frame"));
    clipLabel_->setText(tr("Clip"));
    fpsLabel_->setText(tr("FPS"));
    speedLabel_->setText(tr("Speed"));
    newClip_->setToolTip(tr("New clip"));
    duplicateClip_->setToolTip(tr("Duplicate clip"));
    deleteClip_->setToolTip(tr("Delete clip"));
    renameClip_->setToolTip(tr("Rename clip"));
    loopButton_->setToolTip(tr("Loop playback"));
    pingPongButton_->setToolTip(tr("Ping-pong playback"));
    speedBox_->setToolTip(tr("Playback speed"));
}

void TimelinePanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
