// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QElapsedTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <cmath>

#include "core/animation/keys.hpp"
#include "editor/Document.hpp"
#include "editor/GraphView.hpp"
#include "editor/PoseLibraryPanel.hpp"
#include "editor/TimelineView.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

std::shared_ptr<Character> blankFigure() {
    auto c = loadCharacter("RiggedFigure.glb");
    c->clips.clear();
    return c;
}

// Document avec un clip de 2 s (30 fps) et des clés complètes de l'os « bras » aux images 0 et 15.
struct Scene {
    Document doc;
    BoneId arm = kNoBone;
    Scene() {
        doc.setCharacter(blankFigure());
        doc.setPlaying(false);
        doc.newClip();
        doc.setClipDuration(2.0);
        arm = doc.skeleton()->find("arm_joint_L_1");
        doc.setSelection({arm});
        doc.setFrame(0);
        doc.insertKeys({arm});
        Transform t = doc.boneLocal(arm);
        t.t.y += 0.4f;
        doc.setBoneLocal(arm, t);
        doc.setFrame(15);
        doc.insertKeys({arm});
        doc.setFrame(0);
    }
};

int rowOfBone(const TimelineView& view, BoneId bone, int channel = -1) {
    for (std::size_t i = 0; i < view.rows().size(); ++i) {
        if (view.rows()[i].bone == bone && view.rows()[i].channel == channel) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

class TimelineTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void rowsFollowTheHierarchyAndChannelsCanBeExpanded() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        QCOMPARE(view.rows().size(), s.doc.skeleton()->order().size());
        const int row = rowOfBone(view, s.arm);
        QVERIFY(row >= 0);
        const std::size_t before = view.rows().size();
        view.setBoneExpanded(s.arm, true);
        QCOMPARE(view.rows().size(), before + 3);
        QVERIFY(rowOfBone(view, s.arm, ChannelRotation) >= 0);
        view.setBoneExpanded(s.arm, false);
        QCOMPARE(view.rows().size(), before);
        view.setBranchCollapsed(s.doc.skeleton()->order().front(), true);
        QCOMPARE(view.rows().size(), std::size_t{1});
    }

    void clickingTheRulerScrubsTheTimeline() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 10.0);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(view.xForTime(20.0 / 30.0), 10));
        QCOMPARE(s.doc.frame(), 20);
    }

    void clickingAKeySelectsItAndRectangleSelects() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 12.0);
        const int row = rowOfBone(view, s.arm);
        const int y = view.rowRect(row).center().y();
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(view.xForTime(0.5), y));
        QCOMPARE(s.doc.keySelection().size(), std::size_t{3});  // ligne de résumé : T + R + S à ce temps
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(view.xForTime(1.5), y));  // vide : désélectionne
        QVERIFY(s.doc.keySelection().empty());

        view.setView(-4.0, 12.0);  // de la place à gauche de l'image 0, hors de la colonne des noms
        QTest::mousePress(&view, Qt::LeftButton, {}, QPoint(view.xForTime(-0.1), y - 6));
        QTest::mouseMove(&view, QPoint(view.xForTime(0.6), y + 6));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, QPoint(view.xForTime(0.6), y + 6));
        QCOMPARE(s.doc.keySelection().size(), std::size_t{6});
    }

    void draggingKeysMovesThemByWholeFramesAsOneUndoStep() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 12.0);
        const int y = view.rowRect(rowOfBone(view, s.arm)).center().y();
        const std::size_t undoBefore = s.doc.history().undoCount();
        const int x0 = view.xForTime(0.5);
        QTest::mousePress(&view, Qt::LeftButton, {}, QPoint(x0, y));
        QTest::mouseMove(&view, QPoint(x0 + 20, y));
        QTest::mouseMove(&view, QPoint(x0 + 3 * 12 + 4, y));  // 3 images (+ un peu de bruit)
        QTest::mouseRelease(&view, Qt::LeftButton, {}, QPoint(x0 + 3 * 12 + 4, y));
        QCOMPARE(s.doc.history().undoCount(), undoBefore + 1);
        const auto& keys = s.doc.clip()->tracks.at(s.arm).translation.keys;
        QCOMPARE(keys.size(), std::size_t{2});
        QVERIFY(std::abs(keys[1].time - 18.0 / 30.0) < 1e-6);
        s.doc.undo();
        QVERIFY(std::abs(s.doc.clip()->tracks.at(s.arm).translation.keys[1].time - 0.5) < 1e-6);
    }

    void deleteKeyRemovesSelectedKeys() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 12.0);
        const int y = view.rowRect(rowOfBone(view, s.arm)).center().y();
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(view.xForTime(0.5), y));
        QTest::keyClick(&view, Qt::Key_Delete);
        QCOMPARE(s.doc.clip()->tracks.at(s.arm).translation.keys.size(), std::size_t{1});
        s.doc.undo();
        QCOMPARE(s.doc.clip()->tracks.at(s.arm).translation.keys.size(), std::size_t{2});
    }

    void zoomKeepsTheFrameUnderTheCursor() {
        Scene s;
        TimelineView view(&s.doc);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 12.0);
        const double before = view.timeForX(400);
        QWheelEvent wheel(QPointF(400, 100), QPointF(400, 100), QPoint(), QPoint(0, 240), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&view, &wheel);
        QVERIFY(view.pixelsPerFrame() > 12.0);
        QVERIFY(std::abs(view.timeForX(400) - before) < 0.02);
    }

    void tenThousandKeysPaintQuickly() {
        auto character = blankFigure();
        AnimationClip clip;
        clip.name = "big";
        clip.fps = 30.0f;
        clip.duration = 120.0;
        const BoneId arm = character->skeleton.find("arm_joint_L_1");
        BoneTracks& tracks = clip.tracks[arm];
        for (int i = 0; i < 3500; ++i) {
            const double t = i / 30.0;
            insertKey(tracks.translation,
                      Keyframe<glm::vec3>{t, glm::vec3(0.0f, static_cast<float>(i % 7), 0.0f)});
            insertKey(tracks.rotation, Keyframe<glm::quat>{t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)});
            insertKey(tracks.scale, Keyframe<glm::vec3>{t, glm::vec3(1.0f)});
        }
        character->clips.push_back(clip);
        Document doc;
        doc.setCharacter(character);
        doc.setPlaying(false);
        doc.setSelection({arm});
        TimelineView view(&doc);
        view.resize(1200, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setView(0.0, 1.0);  // 1 px par image : environ 1000 clés visibles
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 5; ++i) {
            view.grab();
        }
        QVERIFY2(timer.elapsed() / 5 < 300,
                 qPrintable(QStringLiteral("paint took %1 ms").arg(timer.elapsed() / 5)));
        QCOMPARE(keyCount(*doc.clip()), std::size_t{10500});
    }

    void graphEditsKeyValuesWithOneUndoStep() {
        Scene s;
        GraphView graph(&s.doc);
        graph.resize(800, 300);
        graph.show();
        QVERIFY(QTest::qWaitForWindowExposed(&graph));
        QCOMPARE(graph.curves().size(), std::size_t{9});  // T, R, S x 3 composantes
        graph.setChannelVisible(ChannelRotation, false);
        graph.setChannelVisible(ChannelScale, false);
        QCOMPARE(graph.curves().size(), std::size_t{3});
        graph.fitView();
        const double valueBefore = s.doc.clip()->tracks.at(s.arm).translation.keys[1].value.y;
        const QPointF target = graph.toScreen(0.5, valueBefore);
        const auto picked = graph.pointAt(target.toPoint());
        QVERIFY(picked.has_value());
        const std::size_t undoBefore = s.doc.history().undoCount();
        QTest::mousePress(&graph, Qt::LeftButton, {}, target.toPoint());
        QTest::mouseMove(&graph, target.toPoint() + QPoint(0, -30));
        QTest::mouseMove(&graph, target.toPoint() + QPoint(0, -40));
        QTest::mouseRelease(&graph, Qt::LeftButton, {}, target.toPoint() + QPoint(0, -40));
        QCOMPARE(s.doc.history().undoCount(), undoBefore + 1);
        QVERIFY(
            s.doc.clip()->tracks.at(s.arm).translation.keys[1].value[picked->curve.component] !=
            (picked->curve.component == 1
                 ? valueBefore
                 : s.doc.clip()->tracks.at(s.arm).translation.keys[0].value[picked->curve.component] - 1e9f));
        s.doc.undo();
        QVERIFY(std::abs(s.doc.clip()->tracks.at(s.arm).translation.keys[1].value.y - valueBefore) < 1e-5);
    }

    void poseLibrarySavesAppliesAndDeletes() {
        Scene s;
        PoseLibraryPanel panel(&s.doc);
        auto* name = panel.findChild<QLineEdit*>("poseName");
        auto* save = panel.findChild<QPushButton*>("poseSave");
        auto* apply = panel.findChild<QPushButton*>("poseApply");
        auto* del = panel.findChild<QPushButton*>("poseDelete");
        QVERIFY(name && save && apply && del);
        s.doc.setMode(Mode::Rig);
        Transform t = s.doc.boneLocal(s.arm);
        const Transform original = t;
        name->setText("Wave");
        save->click();
        QCOMPARE(panel.list()->count(), 1);
        QCOMPARE(panel.list()->item(0)->text(), QStringLiteral("Wave"));
        t.t.x += 0.3f;
        s.doc.setBoneLocal(s.arm, t);
        panel.list()->setCurrentRow(0);
        apply->click();
        QVERIFY(std::abs(s.doc.boneLocal(s.arm).t.x - original.t.x) < 1e-5f);
        s.doc.undo();  // l'application d'une pose s'annule
        QVERIFY(std::abs(s.doc.boneLocal(s.arm).t.x - (original.t.x + 0.3f)) < 1e-5f);
        del->click();
        QCOMPARE(panel.list()->count(), 0);
        s.doc.undo();
        QCOMPARE(panel.list()->count(), 1);
    }
};

RIGGER_REGISTER_QTEST(TimelineTest);
#include "test_timeline.moc"
