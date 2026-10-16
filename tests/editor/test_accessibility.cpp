// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QTableWidget>
#include <QTest>
#include <cmath>

#include "core/util/profiler.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Preferences.hpp"
#include "editor/StatsPanel.hpp"
#include "editor/Theme.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger::editor;

namespace {

double channel(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double luminance(const QColor& c) {
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}
double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace

class AccessibilityTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void bothThemesMeetWcagAaContrast() {
        for (const ThemeKind kind : {ThemeKind::Dark, ThemeKind::Light}) {
            const ThemeColors c = themeColors(kind);
            for (const QColor& background : {c.window, c.panel, c.base, c.hover}) {
                QVERIFY2(contrast(c.text, background) >= 4.5, "text contrast");
                QVERIFY2(contrast(c.textDim, background) >= 4.4,
                         "secondary text contrast");  // 4,5 : 1 arrondi
            }
            QVERIFY2(contrast(c.accentText, c.accent) >= 4.5, "text on the accent colour");
            QVERIFY2(contrast(c.text, c.selection) >= 4.5, "text on the selection colour");
            QVERIFY2(contrast(c.accent, c.window) >= 3.0,
                     "accent against the window (non-text UI, WCAG 1.4.11)");
        }
    }

    void fontSizeIsAdjustableAndPersisted() {
        Preferences prefs;
        QCOMPARE(prefs.fontSize(), 9.5);
        prefs.setFontSize(14.0);
        QCOMPARE(prefs.fontSize(), 14.0);
        prefs.setFontSize(99.0);
        QCOMPARE(prefs.fontSize(), 20.0);  // borné
        prefs.setFontSize(1.0);
        QCOMPARE(prefs.fontSize(), 8.0);
        Preferences again;
        QCOMPARE(again.fontSize(), 8.0);  // persisté
    }

    void changingTheFontSizeResizesTheApplicationFont() {
        MainWindow window;
        const double before = QApplication::font().pointSizeF();
        window.preferences()->setFontSize(before + 3.0);
        QVERIFY(std::abs(QApplication::font().pointSizeF() - (before + 3.0)) < 0.01);
        window.preferences()->setFontSize(before);
        QVERIFY(std::abs(QApplication::font().pointSizeF() - before) < 0.01);
    }

    void shortcutsCanBeRemapped() {
        MainWindow window;
        auto* action = window.actions()->action(ActionId::AnimPlay);
        window.actions()->setShortcut(ActionId::AnimPlay, QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        window.actions()->resetShortcut(ActionId::AnimPlay);
        QCOMPARE(action->shortcut(), window.actions()->defaultShortcut(ActionId::AnimPlay));
    }

    void statsPanelListsProfilerZones() {
        rigger::prof::reset();
        rigger::prof::setEnabled(false);
        {
            RIGGER_PROFILE_SCOPE("not recorded");  // profileur désactivé : aucune zone
        }
        QVERIFY(rigger::prof::snapshot().empty());
        Document doc;
        doc.setCharacter(rigger::testing::loadCharacter("RiggedFigure.glb"));
        StatsPanel panel(&doc, [] { return FrameStats{60.0, 1.5, 0.5}; });
        auto* enabled = panel.findChild<QCheckBox*>("statsEnabled");
        auto* table = panel.findChild<QTableWidget*>("statsTable");
        auto* summary = panel.findChild<QLabel*>("statsSummary");
        QVERIFY(enabled && table && summary);
        enabled->setChecked(true);
        QVERIFY(rigger::prof::enabled());
        for (int i = 0; i < 5; ++i)
            doc.setFrame(i);  // évalue des poses : zones « sampleClip » / « computeGlobals »
        panel.refresh();
        QVERIFY(table->rowCount() > 0);
        QVERIFY(summary->text().contains("60 FPS"));
        bool found = false;
        for (int row = 0; row < table->rowCount(); ++row)
            found = found || table->item(row, 0)->text() == QLatin1String("sampleClip");
        QVERIFY(found);
        enabled->setChecked(false);
        QVERIFY(!rigger::prof::enabled());
        rigger::prof::reset();
    }

    void autosavePreferenceControlsTheTimer() {
        MainWindow window;
        window.preferences()->setAutosave(false, 5);
        window.preferences()->setAutosave(true, 7);
        QVERIFY(window.preferences()->autosaveEnabled());
        QCOMPARE(window.preferences()->autosaveMinutes(), 7);
    }
};

RIGGER_REGISTER_QTEST(AccessibilityTest);
#include "test_accessibility.moc"
