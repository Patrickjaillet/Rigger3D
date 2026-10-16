// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

#include "editor/ActionRegistry.hpp"
#include "editor/Preferences.hpp"
#include "editor/PreferencesDialog.hpp"
#include "editor/Theme.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger::editor;

class PreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        applyLanguage(QStringLiteral("en"));
    }

    void defaultsAreSafe() {
        Preferences prefs;
        QCOMPARE(prefs.theme(), ThemeKind::Dark);
        QCOMPARE(prefs.language(), QString());
        QCOMPARE(prefs.defaultFps(), 30);
        QVERIFY(prefs.autosaveEnabled());
        QCOMPARE(prefs.autosaveMinutes(), 2);  // « toutes les 2 minutes » (§1 de la roadmap)
        QCOMPARE(prefs.angleUnit(), AngleUnit::Degrees);
        QVERIFY(prefs.recentFiles().isEmpty());
    }

    void valuesPersistAcrossInstances() {
        {
            Preferences prefs;
            QSignalSpy changed(&prefs, &Preferences::changed);
            prefs.setTheme(ThemeKind::Light);
            prefs.setLanguage(QStringLiteral("fr"));
            prefs.setDefaultFps(60);
            prefs.setAutosave(false, 7);
            prefs.setAngleUnit(AngleUnit::Radians);
            QCOMPARE(changed.count(), 5);
            prefs.setTheme(ThemeKind::Light);  // identique : pas de signal
            QCOMPARE(changed.count(), 5);
            prefs.setLayoutState(QByteArray("layout"));
            prefs.setWindowGeometry(QByteArray("geometry"));
        }
        Preferences again;
        QCOMPARE(again.theme(), ThemeKind::Light);
        QCOMPARE(again.language(), QStringLiteral("fr"));
        QCOMPARE(again.defaultFps(), 60);
        QVERIFY(!again.autosaveEnabled());
        QCOMPARE(again.autosaveMinutes(), 7);
        QCOMPARE(again.angleUnit(), AngleUnit::Radians);
        QCOMPARE(again.layoutState(), QByteArray("layout"));
        QCOMPARE(again.windowGeometry(), QByteArray("geometry"));
    }

    void valuesAreClamped() {
        Preferences prefs;
        prefs.setDefaultFps(9999);
        QCOMPARE(prefs.defaultFps(), 240);
        prefs.setDefaultFps(0);
        QCOMPARE(prefs.defaultFps(), 1);
        prefs.setAutosave(true, 500);
        QCOMPARE(prefs.autosaveMinutes(), 60);
    }

    void recentFilesAreDeduplicatedOrderedAndCapped() {
        Preferences prefs;
        for (int i = 0; i < 12; ++i) {
            prefs.addRecentFile(QStringLiteral("model%1.glb").arg(i));
        }
        QCOMPARE(prefs.recentFiles().size(), Preferences::kMaxRecentFiles);
        prefs.addRecentFile(QStringLiteral("model5.glb"));  // remonte en tête, sans doublon
        QVERIFY(prefs.recentFiles().first().endsWith(QStringLiteral("model5.glb")));
        QCOMPARE(prefs.recentFiles().count(QFileInfo(QStringLiteral("model5.glb")).absoluteFilePath()), 1);
        Preferences again;
        QCOMPARE(again.recentFiles(), prefs.recentFiles());
        prefs.clearRecentFiles();
        QVERIFY(Preferences().recentFiles().isEmpty());
    }

    void defaultShortcutsAreUniqueAndFollowTheRoadmapTable() {
        Preferences prefs;
        ActionRegistry registry(prefs, nullptr);
        QSet<QString> ids;
        QSet<QString> shortcuts;
        for (const auto& entry : registry.entries()) {
            QVERIFY2(!ids.contains(entry.id), qPrintable(entry.id));
            ids.insert(entry.id);
            const QKeySequence seq = registry.shortcut(entry.id);
            if (!seq.isEmpty()) {
                const QString text = seq.toString(QKeySequence::PortableText);
                QVERIFY2(!shortcuts.contains(text), qPrintable(text + " dupliqué (" + entry.id + ")"));
                shortcuts.insert(text);
            }
        }
        // Tableau du §1 : Q/W/E/R, K, Espace, flèches, F, 1/2/3, Ctrl+Z/Y, Ctrl+S/O.
        const auto sc = [&](const QString& id) {
            return registry.shortcut(id).toString(QKeySequence::PortableText);
        };
        QCOMPARE(sc(ActionId::ToolSelect), QStringLiteral("Q"));
        QCOMPARE(sc(ActionId::ToolMove), QStringLiteral("W"));
        QCOMPARE(sc(ActionId::ToolRotate), QStringLiteral("E"));
        QCOMPARE(sc(ActionId::ToolScale), QStringLiteral("R"));
        QCOMPARE(sc(ActionId::AnimInsertKey), QStringLiteral("K"));
        QCOMPARE(sc(ActionId::AnimPlay), QStringLiteral("Space"));
        QCOMPARE(sc(ActionId::AnimNextFrame), QStringLiteral("Right"));
        QCOMPARE(sc(ActionId::AnimPrevFrame), QStringLiteral("Left"));
        QCOMPARE(sc(ActionId::ViewFocus), QStringLiteral("F"));
        QCOMPARE(sc(ActionId::ModeAnimate), QStringLiteral("3"));
        QCOMPARE(sc(ActionId::EditUndo), QStringLiteral("Ctrl+Z"));
        QCOMPARE(sc(ActionId::EditRedo), QStringLiteral("Ctrl+Y"));
        QCOMPARE(sc(ActionId::FileSave), QStringLiteral("Ctrl+S"));
        QCOMPARE(sc(ActionId::FileOpen), QStringLiteral("Ctrl+O"));
    }

    void everyActionHasAnIconThatExistsInTheResources() {
        initEditorResources();
        Preferences prefs;
        ActionRegistry registry(prefs, nullptr);
        for (const auto& entry : registry.entries()) {
            if (entry.icon[0] != '\0') {
                QVERIFY2(QFile::exists(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(entry.icon))),
                         entry.icon);
                QVERIFY(!registry.action(entry.id)->icon().isNull());
            }
            QVERIFY2(!entry.action->text().isEmpty(), qPrintable(entry.id));
            QVERIFY2(!entry.action->toolTip().isEmpty(), qPrintable(entry.id));
        }
    }

    void customShortcutsPersistAndConflictsAreDetected() {
        Preferences prefs;
        {
            ActionRegistry registry(prefs, nullptr);
            // « Q » est déjà l'outil de sélection.
            QCOMPARE(registry.conflictingAction(ActionId::ModeRig, QKeySequence(QStringLiteral("Q"))),
                     ActionId::ToolSelect);
            QVERIFY(registry.conflictingAction(ActionId::ToolSelect, QKeySequence(QStringLiteral("Q")))
                        .isEmpty());
            QVERIFY(registry.conflictingAction(ActionId::ModeRig, QKeySequence()).isEmpty());

            registry.setShortcut(ActionId::ToolScale, QKeySequence(QStringLiteral("Ctrl+Alt+T")));
            QCOMPARE(registry.shortcut(ActionId::ToolScale), QKeySequence(QStringLiteral("Ctrl+Alt+T")));
            QVERIFY(registry.action(ActionId::ToolScale)->toolTip().contains(QStringLiteral("Ctrl+Alt+T")));
            registry.setShortcut(ActionId::ToolMove, QKeySequence());  // volontairement aucun raccourci
        }
        ActionRegistry reloaded(prefs, nullptr);  // « sauvegardés dans QSettings »
        QCOMPARE(reloaded.shortcut(ActionId::ToolScale), QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        QVERIFY(reloaded.shortcut(ActionId::ToolMove).isEmpty());
        reloaded.resetShortcut(ActionId::ToolScale);
        reloaded.resetShortcut(ActionId::ToolMove);
        QCOMPARE(reloaded.shortcut(ActionId::ToolScale), QKeySequence(QStringLiteral("R")));
        QCOMPARE(reloaded.shortcut(ActionId::ToolMove), QKeySequence(QStringLiteral("W")));
        QVERIFY(!prefs.hasCustomShortcut(ActionId::ToolScale));
    }

    void themeStyleSheetHasNoUnresolvedTokens() {
        for (const ThemeKind kind : {ThemeKind::Dark, ThemeKind::Light}) {
            const QString qss = Theme::styleSheet(themeColors(kind));
            QVERIFY(qss.size() > 500);
            QVERIFY(!qss.contains(QRegularExpression(QStringLiteral("@[A-Za-z]+@"))));  // aucun jeton restant
            QVERIFY(qss.contains(themeColors(kind).accent.name()));
        }
    }

    void iconsAreRecoloredPerTheme() {
        const QPixmap pixmap = Icons::pixmap(QStringLiteral("play"), QColor("#ff0000"), 24);
        QVERIFY(!pixmap.isNull());
        const QImage image = pixmap.toImage();
        bool foundRed = false;
        for (int y = 0; y < image.height() && !foundRed; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.alpha() > 200 && c.red() > 200 && c.green() < 60) {
                    foundRed = true;
                    break;
                }
            }
        }
        QVERIFY2(foundRed, "l'icône SVG doit être recolorée (currentColor)");
    }

    void translationCatalogsLoad() {
        QCOMPARE(resolveLanguage(QStringLiteral("fr")), QStringLiteral("fr"));
        QCOMPARE(resolveLanguage(QStringLiteral("xx")).size(), 2);  // repli sur une langue connue
        QVERIFY(applyLanguage(QStringLiteral("fr")));
        QCOMPARE(QCoreApplication::translate("ActionRegistry", "&Quit"), QStringLiteral("&Quitter"));
        QVERIFY(applyLanguage(QStringLiteral("en")));
        QCOMPARE(QCoreApplication::translate("ActionRegistry", "&Quit"), QStringLiteral("&Quit"));
    }

    void preferencesDialogEditsPreferencesLive() {
        Preferences prefs;
        ActionRegistry registry(prefs, nullptr);
        PreferencesDialog dialog(&prefs, &registry);
        dialog.show();

        dialog.themeCombo()->setCurrentIndex(1);
        QCOMPARE(prefs.theme(), ThemeKind::Light);
        dialog.languageCombo()->setCurrentIndex(2);
        QCOMPARE(prefs.language(), QStringLiteral("fr"));
        applyLanguage(QStringLiteral("en"));
        dialog.fpsSpin()->setValue(24);
        QCOMPARE(prefs.defaultFps(), 24);
        dialog.findChild<QSpinBox*>("prefAutosaveMinutes")->setValue(5);
        QCOMPARE(prefs.autosaveMinutes(), 5);
        dialog.findChild<QComboBox*>("prefAngleUnit")->setCurrentIndex(1);
        QCOMPARE(prefs.angleUnit(), AngleUnit::Radians);

        // Valeurs relues à l'ouverture.
        PreferencesDialog reopened(&prefs, &registry);
        QCOMPARE(reopened.themeCombo()->currentIndex(), 1);
        QCOMPARE(reopened.fpsSpin()->value(), 24);
    }

    void preferencesDialogShortcutTableRejectsConflicts() {
        Preferences prefs;
        ActionRegistry registry(prefs, nullptr);
        PreferencesDialog dialog(&prefs, &registry);
        QCOMPARE(dialog.shortcutTable()->rowCount(), static_cast<int>(registry.entries().size()));

        QVERIFY(!dialog.assignShortcut(ActionId::ModeRig,
                                       QKeySequence(QStringLiteral("Q"))));  // pris par Sélection
        QCOMPARE(registry.shortcut(ActionId::ModeRig), QKeySequence(QStringLiteral("1")));
        QVERIFY(!dialog.findChild<QLabel*>("prefConflict")->text().isEmpty());
        QVERIFY(dialog.assignShortcut(ActionId::ModeRig, QKeySequence(QStringLiteral("Ctrl+1"))));
        QCOMPARE(registry.shortcut(ActionId::ModeRig), QKeySequence(QStringLiteral("Ctrl+1")));
        QVERIFY(dialog.findChild<QLabel*>("prefConflict")->text().isEmpty());
    }
};

RIGGER_REGISTER_QTEST(PreferencesTest);
#include "test_preferences.moc"
