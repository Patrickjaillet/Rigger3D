// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/BackgroundTask.hpp"

#include <QMetaObject>
#include <QtConcurrent>

namespace rigger::editor {

void TaskContext::report(double fraction, const QString& stage) {
    QMetaObject::invokeMethod(
        task_, [task = task_, fraction, stage] { emit task->progress(fraction, stage); },
        Qt::QueuedConnection);
}

BackgroundTask::BackgroundTask(Work work, QObject* parent)
    : QObject(parent), work_(std::move(work)), context_(this) {
    connect(&watcher_, &QFutureWatcher<void>::finished, this, [this] {
        running_ = false;
        emit finished();
    });
}

BackgroundTask::~BackgroundTask() {
    cancel();
    watcher_.waitForFinished();
}

void BackgroundTask::start() {
    if (running_) {
        return;
    }
    running_ = true;
    watcher_.setFuture(QtConcurrent::run([this] { work_(context_); }));
}

}  // namespace rigger::editor
