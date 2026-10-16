// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace rigger::editor {

class Preferences;

// Écran d'accueil (page d'un QStackedWidget) : nouveau, importer, fichiers récents, tutoriel.
class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(Preferences* preferences, QWidget* parent = nullptr);
    void retranslate();
    void refreshRecent();

    QListWidget* recentList() const { return recentList_; }
    QPushButton* newButton() const { return newButton_; }
    QPushButton* importButton() const { return importButton_; }
    QPushButton* exampleButton() const { return exampleButton_; }

signals:
    void newRequested();
    void importRequested();
    void openRecentRequested(const QString& path);
    void tutorialRequested();
    void exampleRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    Preferences* preferences_;
    QLabel* logo_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* exampleButton_ = nullptr;
    QLabel* recentLabel_ = nullptr;
    QLabel* emptyRecent_ = nullptr;
    QListWidget* recentList_ = nullptr;
    QPushButton* tutorialButton_ = nullptr;
};

}  // namespace rigger::editor
