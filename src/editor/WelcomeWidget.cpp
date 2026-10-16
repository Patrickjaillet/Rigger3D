// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/WelcomeWidget.hpp"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "editor/Preferences.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

WelcomeWidget::WelcomeWidget(Preferences* preferences, QWidget* parent)
    : QWidget(parent), preferences_(preferences) {
    setObjectName("welcomeWidget");
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(12);
    logo_ = new QLabel(this);
    logo_->setAlignment(Qt::AlignCenter);
    title_ = new QLabel(QStringLiteral("Rigger"), this);
    title_->setProperty("title", true);
    title_->setAlignment(Qt::AlignCenter);
    subtitle_ = new QLabel(this);
    subtitle_->setProperty("dim", true);
    subtitle_->setAlignment(Qt::AlignCenter);
    column->addWidget(logo_);
    column->addWidget(title_);
    column->addWidget(subtitle_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    newButton_ = new QPushButton(this);
    newButton_->setObjectName("welcomeNew");
    newButton_->setProperty("primary", true);
    importButton_ = new QPushButton(this);
    importButton_->setObjectName("welcomeImport");
    exampleButton_ = new QPushButton(this);
    exampleButton_->setObjectName("welcomeExample");
    buttons->addWidget(newButton_);
    buttons->addWidget(importButton_);
    buttons->addWidget(exampleButton_);
    buttons->addStretch(1);
    column->addLayout(buttons);

    recentLabel_ = new QLabel(this);
    recentLabel_->setProperty("dim", true);
    column->addWidget(recentLabel_);
    recentList_ = new QListWidget(this);
    recentList_->setObjectName("welcomeRecent");
    recentList_->setMaximumHeight(180);
    column->addWidget(recentList_);
    emptyRecent_ = new QLabel(this);
    emptyRecent_->setProperty("dim", true);
    emptyRecent_->setAlignment(Qt::AlignCenter);
    column->addWidget(emptyRecent_);

    tutorialButton_ = new QPushButton(this);
    tutorialButton_->setObjectName("welcomeTutorial");
    tutorialButton_->setProperty("link", true);
    tutorialButton_->setCursor(Qt::PointingHandCursor);
    column->addWidget(tutorialButton_, 0, Qt::AlignCenter);

    auto* centered = new QHBoxLayout;
    centered->addStretch(1);
    auto* holder = new QWidget(this);
    holder->setLayout(column);
    holder->setMinimumWidth(420);
    holder->setMaximumWidth(520);
    centered->addWidget(holder, 3);
    centered->addStretch(1);
    outer->addLayout(centered);
    outer->addStretch(2);

    connect(newButton_, &QPushButton::clicked, this, &WelcomeWidget::newRequested);
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWidget::importRequested);
    connect(exampleButton_, &QPushButton::clicked, this, &WelcomeWidget::exampleRequested);
    connect(tutorialButton_, &QPushButton::clicked, this, &WelcomeWidget::tutorialRequested);
    connect(recentList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { emit openRecentRequested(item->data(Qt::UserRole).toString()); });
    connect(preferences_, &Preferences::changed, this, [this](const QString& key) {
        if (key == QLatin1String("recentFiles")) {
            refreshRecent();
        }
    });
    retranslate();
}

void WelcomeWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void WelcomeWidget::retranslate() {
    logo_->setPixmap(
        Icons::pixmap(QStringLiteral("mode-rig"), Theme::colors().accent, 64, devicePixelRatioF()));
    subtitle_->setText(tr("Professional 3D character animation"));
    newButton_->setText(tr("New scene"));
    importButton_->setText(tr("Import a model..."));
    exampleButton_->setText(tr("Open the example"));
    exampleButton_->setToolTip(
        tr("Open Rigby, a ready-made character with two animations (free to use, CC0)."));
    recentLabel_->setText(tr("Recent files"));
    emptyRecent_->setText(tr("No recent file yet. Import a glTF / GLB character to get started."));
    tutorialButton_->setText(tr("Quick start tutorial"));
    refreshRecent();
}

void WelcomeWidget::refreshRecent() {
    recentList_->clear();
    for (const QString& path : preferences_->recentFiles()) {
        auto* item =
            new QListWidgetItem(Icons::get(QStringLiteral("mesh")), QFileInfo(path).fileName(), recentList_);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }
    recentList_->setVisible(recentList_->count() > 0);
    recentLabel_->setVisible(recentList_->count() > 0);
    emptyRecent_->setVisible(recentList_->count() == 0);
}

}  // namespace rigger::editor
