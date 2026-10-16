// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QAbstractItemModelTester>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMimeData>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>

#include "editor/BoneTreeModel.hpp"
#include "editor/Document.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

// A ── B ── C
//  └─ D
struct Rig {
    std::shared_ptr<Character> character = std::make_shared<Character>();
    BoneId a, b, c, d;
    Rig() {
        Skeleton& s = character->skeleton;
        Transform up;
        up.t = {0, 1, 0};
        a = s.addBone("A", kNoBone, up).value();
        b = s.addBone("B", a, up).value();
        c = s.addBone("C", b, up).value();
        d = s.addBone("D", a, up).value();
    }
};

QMimeData* dragOf(const BoneTreeModel& model, const std::vector<BoneId>& ids) {
    QModelIndexList indexes;
    for (const BoneId id : ids) {
        indexes << model.indexOf(id);
    }
    return model.mimeData(indexes);
}

}  // namespace

class BoneTreeTest : public QObject {
    Q_OBJECT
private slots:
    void modelPassesQtsModelTester() {
        Document doc;
        BoneTreeModel model(&doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 0);  // document vide

        Rig rig;
        doc.setCharacter(rig.character);
        QCOMPARE(model.rowCount(), 1);
        doc.setCharacter(loadCharacter("CesiumMan.glb"));
        QVERIFY(model.rowCount() >= 1);
        doc.setCharacter(loadCharacter("RiggedFigure.glb"));
        doc.setCharacter(nullptr);
        QCOMPARE(model.rowCount(), 0);
    }

    void structureMatchesTheSkeleton() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        BoneTreeModel model(&doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        const QModelIndex root = model.index(0, 0);
        QCOMPARE(model.boneAt(root), rig.a);
        QCOMPARE(model.rowCount(root), 2);  // B et D
        QCOMPARE(model.boneAt(model.index(0, 0, root)), rig.b);
        QCOMPARE(model.boneAt(model.index(1, 0, root)), rig.d);
        const QModelIndex c = model.indexOf(rig.c);
        QCOMPARE(model.boneAt(model.parent(c)), rig.b);
        QCOMPARE(model.data(c, Qt::DisplayRole).toString(), QStringLiteral("C"));
        QVERIFY(model.flags(c) & Qt::ItemIsEditable);
        QVERIFY(model.flags(c) & Qt::ItemIsDragEnabled);
        QVERIFY(!(model.flags(model.indexOf(rig.c, BoneTreeModel::VisibleColumn)) & Qt::ItemIsEditable));
        QVERIFY(model.columnCount() == 3);
    }

    void renameThroughTheModel() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        BoneTreeModel model(&doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy errors(&model, &BoneTreeModel::errorOccurred);

        QVERIFY(model.setData(model.indexOf(rig.d), QStringLiteral("Hand")));
        QCOMPARE(model.data(model.indexOf(rig.d)).toString(), QStringLiteral("Hand"));
        QVERIFY(changed.count() >= 1);
        QVERIFY(!model.setData(model.indexOf(rig.d), QStringLiteral("  ")));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(model.data(model.indexOf(rig.d)).toString(), QStringLiteral("Hand"));  // inchangé
        QVERIFY(model.setData(model.indexOf(rig.d), QStringLiteral("A")));              // nom déjà pris
        QVERIFY(model.data(model.indexOf(rig.d)).toString() != QStringLiteral("A"));    // suffixe ajouté
    }

    void dragAndDropReparentsKeepingWorldPosition() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        BoneTreeModel model(&doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const glm::vec3 before(doc.skeleton()->bindGlobals()[rig.d][3]);

        // D devient enfant de C.
        std::unique_ptr<QMimeData> drag(dragOf(model, {rig.d}));
        QVERIFY(model.canDropMimeData(drag.get(), Qt::MoveAction, -1, -1, model.indexOf(rig.c)));
        QVERIFY(model.dropMimeData(drag.get(), Qt::MoveAction, -1, -1, model.indexOf(rig.c)));
        QCOMPARE(doc.skeleton()->bone(rig.d).parent, rig.c);
        QCOMPARE(model.boneAt(model.parent(model.indexOf(rig.d))), rig.c);
        const glm::vec3 after(doc.skeleton()->bindGlobals()[rig.d][3]);
        QVERIFY(glm::length(before - after) < 1e-5f);

        // Dépôt dans le vide (index invalide) : B devient racine.
        drag.reset(dragOf(model, {rig.b}));
        QVERIFY(model.dropMimeData(drag.get(), Qt::MoveAction, -1, -1, QModelIndex()));
        QCOMPARE(doc.skeleton()->bone(rig.b).parent, kNoBone);
        QCOMPARE(model.rowCount(), 2);  // A et B
    }

    void dropRefusesCyclesAndSelfDrops() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        BoneTreeModel model(&doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        std::unique_ptr<QMimeData> drag(dragOf(model, {rig.a}));
        QVERIFY(
            !model.canDropMimeData(drag.get(), Qt::MoveAction, -1, -1, model.indexOf(rig.c)));  // descendant
        QVERIFY(
            !model.canDropMimeData(drag.get(), Qt::MoveAction, -1, -1, model.indexOf(rig.a)));  // soi-même
        QVERIFY(!model.dropMimeData(drag.get(), Qt::MoveAction, -1, -1, model.indexOf(rig.c)));
        QCOMPARE(doc.skeleton()->bone(rig.a).parent, kNoBone);
        QVERIFY(!model.canDropMimeData(drag.get(), Qt::CopyAction, -1, -1,
                                       QModelIndex()));  // seul le déplacement
    }

    void panelSelectionIsSynchronisedWithTheDocument() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        HierarchyPanel panel(&doc);
        panel.resize(300, 300);
        panel.show();

        // Document -> vue.
        doc.setSelection({rig.c, rig.d});
        const auto selected = panel.tree()->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 2);

        // Vue -> Document (sélection étendue : Ctrl).
        const QModelIndex bProxy = panel.proxy()->mapFromSource(panel.model()->indexOf(rig.b));
        panel.tree()->selectionModel()->select(
            bProxy, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        QCOMPARE(doc.selection(), (std::vector<BoneId>{rig.b}));

        doc.clearSelection();
        QVERIFY(panel.tree()->selectionModel()->selectedRows().isEmpty());
    }

    void panelFilterKeepsParentsOfMatches() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        HierarchyPanel panel(&doc);
        QCOMPARE(panel.proxy()->rowCount(), 1);

        panel.filterEdit()->setText(
            QStringLiteral("c"));  // insensible à la casse : « C » enfant de B enfant de A
        const QModelIndex a = panel.proxy()->index(0, 0);
        QVERIFY(a.isValid());  // le parent de la correspondance reste visible
        QCOMPARE(panel.proxy()->rowCount(a), 1);
        QCOMPARE(panel.proxy()->data(panel.proxy()->index(0, 0, a)).toString(), QStringLiteral("B"));

        panel.filterEdit()->setText(QStringLiteral("zzz"));
        QCOMPARE(panel.proxy()->rowCount(), 0);
        panel.filterEdit()->clear();
        QCOMPARE(panel.proxy()->rowCount(), 1);
    }

    void visibilityAndLockColumnsToggleFlags() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        HierarchyPanel panel(&doc);
        panel.resize(320, 300);
        panel.show();
        panel.tree()->expandAll();

        const auto clickColumn = [&](int column) {
            const QModelIndex index = panel.proxy()->mapFromSource(panel.model()->indexOf(rig.d, column));
            QTest::mouseClick(panel.tree()->viewport(), Qt::LeftButton, Qt::NoModifier,
                              panel.tree()->visualRect(index).center());
        };
        clickColumn(BoneTreeModel::VisibleColumn);
        QVERIFY(hasFlag(doc.skeleton()->bone(rig.d).flags, BoneFlags::Hidden));
        clickColumn(BoneTreeModel::VisibleColumn);
        QVERIFY(!hasFlag(doc.skeleton()->bone(rig.d).flags, BoneFlags::Hidden));
        clickColumn(BoneTreeModel::LockedColumn);
        QVERIFY(hasFlag(doc.skeleton()->bone(rig.d).flags, BoneFlags::Locked));
    }

    void panelForwardsRenameErrors() {
        Document doc;
        Rig rig;
        doc.setCharacter(rig.character);
        HierarchyPanel panel(&doc);
        QSignalSpy messages(&panel, &HierarchyPanel::statusMessage);
        panel.model()->setData(panel.model()->indexOf(rig.a), QString());
        QCOMPARE(messages.count(), 1);
    }
};

RIGGER_REGISTER_QTEST(BoneTreeTest);
#include "test_bonetree.moc"
