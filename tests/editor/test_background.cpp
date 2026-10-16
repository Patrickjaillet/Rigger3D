// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

#include "editor/BackgroundTask.hpp"
#include "editor_test_support.hpp"

using namespace rigger::editor;

class BackgroundTaskTest : public QObject {
    Q_OBJECT
private slots:
    void runsOffTheUiThreadWithoutBlockingTheEventLoop() {
        const QThread* uiThread = QThread::currentThread();
        auto workThread = std::make_shared<std::atomic<const QThread*>>(nullptr);
        BackgroundTask task([workThread](TaskContext&) {
            workThread->store(QThread::currentThread());
            QThread::msleep(250);  // travail « long »
        });
        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
        ticker.start(5);

        QSignalSpy finished(&task, &BackgroundTask::finished);
        task.start();
        QVERIFY(task.isRunning());
        QVERIFY(finished.wait(5000));
        QVERIFY(!task.isRunning());
        QVERIFY(workThread->load() != nullptr);
        QVERIFY(workThread->load() != uiThread);
        // Pendant les 250 ms de travail, le thread UI a continué à traiter ses événements.
        QVERIFY2(ticks >= 10, qPrintable(QString::number(ticks)));
    }

    void progressIsDeliveredInOrderOnTheUiThread() {
        BackgroundTask task([](TaskContext& context) {
            for (int i = 1; i <= 4; ++i) {
                context.report(i / 4.0, QStringLiteral("step %1").arg(i));
            }
        });
        QList<double> fractions;
        QStringList stages;
        QThread* receiver = nullptr;
        connect(&task, &BackgroundTask::progress, this, [&](double f, const QString& s) {
            fractions << f;
            stages << s;
            receiver = QThread::currentThread();
        });
        QSignalSpy finished(&task, &BackgroundTask::finished);
        task.start();
        QVERIFY(finished.wait(5000));
        QTest::qWait(20);  // laisse passer les signaux en file d'attente restants
        QCOMPARE(fractions, (QList<double>{0.25, 0.5, 0.75, 1.0}));
        QCOMPARE(stages.last(), QStringLiteral("step 4"));
        QCOMPARE(receiver, QThread::currentThread());
    }

    void cancelStopsACooperativeWorker() {
        auto iterations = std::make_shared<std::atomic<int>>(0);
        BackgroundTask task([iterations](TaskContext& context) {
            while (!context.isCancelled() && iterations->load() < 1000) {
                ++*iterations;
                QThread::msleep(5);
            }
        });
        QSignalSpy finished(&task, &BackgroundTask::finished);
        task.start();
        QTest::qWait(40);
        QVERIFY(!task.isCancelled());
        task.cancel();
        QVERIFY(task.isCancelled());
        QVERIFY(finished.wait(2000));
        QVERIFY(iterations->load() < 1000);
    }

    void destructorWaitsForTheWorker() {
        auto done = std::make_shared<std::atomic<bool>>(false);
        {
            BackgroundTask task([done](TaskContext& context) {
                while (!context.isCancelled()) {
                    QThread::msleep(2);
                }
                done->store(true);
            });
            task.start();
            QTest::qWait(20);
        }  // destruction : annule puis attend
        QVERIFY(done->load());
    }

    void startingTwiceIsHarmless() {
        auto runs = std::make_shared<std::atomic<int>>(0);
        BackgroundTask task([runs](TaskContext&) {
            ++*runs;
            QThread::msleep(40);
        });
        QSignalSpy finished(&task, &BackgroundTask::finished);
        task.start();
        task.start();  // ignoré : déjà en cours
        QVERIFY(finished.wait(2000));
        QCOMPARE(runs->load(), 1);
    }
};

RIGGER_REGISTER_QTEST(BackgroundTaskTest);
#include "test_background.moc"
