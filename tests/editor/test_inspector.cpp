// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "editor/Document.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/Preferences.hpp"
#include "editor/ScrubField.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {
const QString kMixed = QStringLiteral("—");

// Saisie clavier comme un utilisateur : double-clic, texte, Entrée.
void typeInto(ScrubField* field, const QString& text) {
    QTest::mouseDClick(field, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY2(!field->isReadOnly(), "le double-clic doit ouvrir la saisie");
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}
}  // namespace

class InspectorTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        applyLanguage(QStringLiteral("en"));
    }

    // --- ScrubField ---------------------------------------------------------------------

    void scrubFieldDragChangesValueByStepPerPixel() {
        ScrubField field;
        field.resize(120, 24);
        field.setStep(0.01);
        field.setValue(1.0);
        QSignalSpy edited(&field, &ScrubField::valueEdited);
        QSignalSpy finished(&field, &ScrubField::editFinished);

        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseMove(&field, QPoint(60, 10));
        QVERIFY(edited.count() >= 1);
        QVERIFY(std::abs(field.value() - 1.5) < 1e-9);  // 50 px * 0.01
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, QPoint(60, 10));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!field.isBusy());
    }

    void scrubFieldModifiersScaleTheDrag() {
        ScrubField field;
        field.resize(120, 24);
        field.setStep(0.1);
        field.setValue(0.0);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseMove(&field, QPoint(30, 10));
        QVERIFY(std::abs(field.value() - 2.0) < 1e-9);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, QPoint(30, 10));

        field.setValue(0.0);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QMouseEvent fine(QEvent::MouseMove, QPointF(30, 10), QPointF(30, 10), Qt::NoButton, Qt::LeftButton,
                         Qt::ShiftModifier);
        QApplication::sendEvent(&field, &fine);
        QVERIFY(std::abs(field.value() - 0.2) < 1e-9);  // Maj : ×0,1
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, QPoint(30, 10));
    }

    void scrubFieldSimpleClickDoesNotChangeTheValue() {
        ScrubField field;
        field.setValue(3.0);
        QSignalSpy edited(&field, &ScrubField::valueEdited);
        QTest::mouseClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QCOMPARE(edited.count(), 0);
        QCOMPARE(field.value(), 3.0);
    }

    void scrubFieldTypedValueAndEscape() {
        ScrubField field;
        field.resize(120, 24);
        field.setValue(1.0);
        QSignalSpy edited(&field, &ScrubField::valueEdited);

        typeInto(&field, QStringLiteral("2.5"));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(field.value(), 2.5);
        QVERIFY(field.isReadOnly());  // retour en mode glissé

        QTest::mouseDClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        field.selectAll();
        QTest::keyClicks(&field, QStringLiteral("9"));
        QTest::keyClick(&field, Qt::Key_Escape);  // annule
        QCOMPARE(edited.count(), 1);
        QCOMPARE(field.value(), 2.5);
    }

    void scrubFieldRangeAndMixedState() {
        ScrubField field;
        field.setRange(0.0, 10.0);
        field.setValue(50.0);
        QCOMPARE(field.value(), 10.0);  // borné
        field.setMixed(true);
        QCOMPARE(field.text(), kMixed);
        QVERIFY(field.isMixed());
        field.setValue(2.0);
        QVERIFY(!field.isMixed());
        field.setDecimals(1);
        field.setSuffix(QStringLiteral(" m"));
        QCOMPARE(field.formatted(1.25), QLocale().toString(1.25, 'f', 1) + QStringLiteral(" m"));
    }

    // --- InspectorPanel -----------------------------------------------------------------

    void emptySelectionShowsAHint() {
        Document doc;
        Preferences prefs;
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        QVERIFY(panel.findChild<QLabel*>("inspectorEmptyLabel")->isVisible());
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setSelection({0});
        QVERIFY(!panel.findChild<QLabel*>("inspectorEmptyLabel")->isVisible());
    }

    void singleSelectionEditsBindTransformInRigMode() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setMode(Mode::Rig);
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({elbow});

        auto* y = panel.findChild<ScrubField*>("inspectorTranslationY");
        auto* x = panel.findChild<ScrubField*>("inspectorTranslationX");
        QCOMPARE(y->value(), 1.0);
        QVERIFY(!y->isMixed());

        typeInto(x, QStringLiteral("0.25"));
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.x, 0.25f);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.y, 1.0f);  // les autres composantes sont conservées

        typeInto(panel.findChild<ScrubField*>("inspectorScaleZ"), QStringLiteral("2"));
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.s.z, 2.0f);
    }

    void rotationIsEditedInEulerDegreesThenRadians() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setMode(Mode::Rig);
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({elbow});

        auto* z = panel.findChild<ScrubField*>("inspectorRotationZ");
        QVERIFY(z->text().contains(QStringLiteral("°")));  // degrés par défaut
        typeInto(z, QStringLiteral("90"));
        const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        QVERIFY(std::abs(glm::dot(doc.skeleton()->bone(elbow).bindLocal.r, expected)) > 0.9999f);

        prefs.setAngleUnit(AngleUnit::Radians);  // l'inspecteur suit la préférence
        QVERIFY(z->text().contains(QStringLiteral("rad")));
        QVERIFY2(std::abs(z->value() - 1.5707963) < 1e-4, qPrintable(QString::number(z->value())));
        typeInto(z, QStringLiteral("0"));
        QVERIFY(std::abs(doc.skeleton()->bone(elbow).bindLocal.r.z) < 1e-6f);
    }

    void multiSelectionShowsMixedValuesAndEditsAll() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setMode(Mode::Rig);
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({shoulder, elbow});

        auto* x = panel.findChild<ScrubField*>("inspectorTranslationX");
        auto* y = panel.findChild<ScrubField*>("inspectorTranslationY");
        QVERIFY(!x->isMixed());  // les deux os ont x = 0
        QVERIFY(y->isMixed());   // 0 et 1
        QCOMPARE(y->text(), kMixed);
        QVERIFY(
            !panel.findChild<QLineEdit*>("inspectorName")->isEnabled());  // noms uniques : un seul à la fois

        typeInto(x, QStringLiteral("0.5"));
        QCOMPARE(doc.skeleton()->bone(shoulder).bindLocal.t.x, 0.5f);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.x, 0.5f);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.y, 1.0f);  // Y mixte non touché

        typeInto(y, QStringLiteral("2"));  // la première édition d'une valeur mixte l'applique à tous
        QCOMPARE(doc.skeleton()->bone(shoulder).bindLocal.t.y, 2.0f);
        QCOMPARE(doc.skeleton()->bone(elbow).bindLocal.t.y, 2.0f);
        QVERIFY(!y->isMixed());
    }

    void animateModeEditsThePoseNotTheBind() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        doc.setPlaying(false);
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        doc.setSelection({shoulder});
        typeInto(panel.findChild<ScrubField*>("inspectorTranslationZ"), QStringLiteral("0.75"));
        QCOMPARE(doc.pose()[shoulder].t.z, 0.75f);
        QCOMPARE(doc.skeleton()->bone(shoulder).bindLocal.t.z, 0.0f);
    }

    void nameLengthAndFlagsCanBeEdited() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({elbow});

        auto* name = panel.findChild<QLineEdit*>("inspectorName");
        QCOMPARE(name->text(), QStringLiteral("elbow"));
        name->setText(QStringLiteral("forearm"));
        name->editingFinished();
        QCOMPARE(QString::fromStdString(doc.skeleton()->bone(elbow).name), QStringLiteral("forearm"));

        typeInto(panel.findChild<ScrubField*>("inspectorLength"), QStringLiteral("0.4"));
        QCOMPARE(doc.skeleton()->bone(elbow).length, 0.4f);

        auto* hidden = panel.findChild<QCheckBox*>("inspectorHidden");
        auto* locked = panel.findChild<QCheckBox*>("inspectorLocked");
        QVERIFY(!hidden->isChecked());
        hidden->click();
        QVERIFY(hasFlag(doc.skeleton()->bone(elbow).flags, BoneFlags::Hidden));
        locked->click();
        QVERIFY(hasFlag(doc.skeleton()->bone(elbow).flags, BoneFlags::Locked));
        // Verrouillé : les champs de transformation sont désactivés et l'édition refusée.
        QVERIFY(!panel.findChild<ScrubField*>("inspectorTranslationX")->isEnabled());
        QVERIFY(!doc.setBoneLocal(elbow, Transform{}));
        locked->click();
        QVERIFY(panel.findChild<ScrubField*>("inspectorTranslationX")->isEnabled());
    }

    void renameConflictKeepsTheNameUnique() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setSelection({elbow});
        auto* name = panel.findChild<QLineEdit*>("inspectorName");
        name->setText(QStringLiteral("shoulder"));
        name->editingFinished();
        QVERIFY(doc.skeleton()->bone(elbow).name != "shoulder");
        QCOMPARE(name->text(), QString::fromStdString(doc.skeleton()->bone(elbow).name));
    }

    void mixedFlagsUseATristateCheckbox() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setBoneFlags(elbow, BoneFlags::Hidden);
        doc.setSelection({shoulder, elbow});
        auto* hidden = panel.findChild<QCheckBox*>("inspectorHidden");
        QCOMPARE(hidden->checkState(), Qt::PartiallyChecked);
        hidden->click();  // partiel -> coché : tous masqués
        QVERIFY(hasFlag(doc.skeleton()->bone(shoulder).flags, BoneFlags::Hidden));
        QVERIFY(hasFlag(doc.skeleton()->bone(elbow).flags, BoneFlags::Hidden));
    }

    void boneColorIsDisplayedFromTheSkeleton() {
        Document doc;
        Preferences prefs;
        doc.setCharacter(loadCharacter("arm.gltf"));
        InspectorPanel panel(&doc, &prefs);
        panel.show();
        const BoneId shoulder = doc.skeleton()->find("shoulder");
        const BoneId elbow = doc.skeleton()->find("elbow");
        doc.setBoneColor(shoulder, {1, 0, 0});
        doc.setSelection({shoulder});
        auto* button = panel.findChild<QPushButton*>("inspectorColorButton");
        QVERIFY(!button->icon().isNull());
        doc.setSelection({shoulder, elbow});  // couleurs différentes : « — »
        QCOMPARE(button->text(), kMixed);
    }
};

RIGGER_REGISTER_QTEST(InspectorTest);
#include "test_inspector.moc"
