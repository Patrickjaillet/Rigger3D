// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QFrame>
#include <QPointer>
#include <QString>
#include <vector>

class QLabel;
class QPushButton;

namespace rigger::editor {

// Visite guidée : une bulle (enfant de la fenêtre principale) qui se place sur chaque widget cible, avec le
// texte de l'étape, « Next » / « Skip ». Aucun état persistant, aucune donnée envoyée.
class TutorialGuide : public QFrame {
    Q_OBJECT
public:
    struct Step {
        QPointer<QWidget> target;  // peut être nul ou invisible : la bulle est alors centrée dans la fenêtre
        QString text;
    };

    explicit TutorialGuide(QWidget* window);

    void setSteps(std::vector<Step> steps);
    void start();
    int stepIndex() const { return index_; }
    int stepCount() const { return static_cast<int>(steps_.size()); }
    bool isRunning() const { return running_; }
    QString currentText() const;

public slots:
    void next();
    void skip();

signals:
    void finished(bool completed);  // false : ignorée avant la fin

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showStep();
    void reposition();
    void end(bool completed);

    QWidget* window_;
    std::vector<Step> steps_;
    int index_ = -1;
    bool running_ = false;
    QLabel* counter_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* skip_ = nullptr;
};

}  // namespace rigger::editor
