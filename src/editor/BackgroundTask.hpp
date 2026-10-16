// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

namespace rigger::editor {

class BackgroundTask;

// Passé au travail exécuté hors du thread UI : lecture du drapeau d'annulation et remontée de progression.
class TaskContext {
public:
    bool isCancelled() const { return cancelled_.load(); }
    // Thread-safe : la progression est postée vers le thread de la tâche (signal BackgroundTask::progress).
    void report(double fraction, const QString& stage);

private:
    friend class BackgroundTask;
    explicit TaskContext(BackgroundTask* task) : task_(task) {}
    BackgroundTask* task_;
    std::atomic_bool cancelled_{false};
};

// Exécute un travail long (import, export, auto-skinning) dans le pool de threads : le thread UI n'est jamais
// bloqué. Le résultat est transmis par la fonction elle-même (capture d'un état partagé) ; finished() est
// émis dans le thread de l'objet une fois le travail terminé (ou interrompu après cancel()).
class BackgroundTask : public QObject {
    Q_OBJECT
public:
    using Work = std::function<void(TaskContext&)>;

    explicit BackgroundTask(Work work, QObject* parent = nullptr);
    ~BackgroundTask() override;  // attend la fin du travail

    void start();
    void cancel() { context_.cancelled_.store(true); }
    bool isCancelled() const { return context_.isCancelled(); }
    bool isRunning() const { return running_; }

signals:
    void progress(double fraction, const QString& stage);
    void finished();

private:
    friend class TaskContext;
    Work work_;
    TaskContext context_;
    QFutureWatcher<void> watcher_;
    bool running_ = false;
};

}  // namespace rigger::editor
