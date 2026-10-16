// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/Preferences.hpp"

#include <QFileInfo>
#include <QSettings>
#include <QVariant>
#include <algorithm>

namespace rigger::editor {

namespace {
const QString kShortcutGroup = QStringLiteral("shortcuts/");
}

Preferences::Preferences(QObject* parent) : QObject(parent) {
    QSettings settings;
    theme_ = settings.value("ui/theme", "dark").toString() == "light" ? ThemeKind::Light : ThemeKind::Dark;
    language_ = settings.value("ui/language", QString()).toString();
    defaultFps_ = std::clamp(settings.value("animation/defaultFps", 30).toInt(), 1, 240);
    autosaveEnabled_ = settings.value("project/autosave", true).toBool();
    autosaveMinutes_ = std::clamp(settings.value("project/autosaveMinutes", 2).toInt(), 1, 60);
    fontSize_ = std::clamp(settings.value("ui/fontSize", 9.5).toDouble(), 8.0, 20.0);
    angleUnit_ = settings.value("ui/angleUnit", "degrees").toString() == "radians" ? AngleUnit::Radians
                                                                                   : AngleUnit::Degrees;
    recentFiles_ = settings.value("recent/files").toStringList();
}

void Preferences::write(const QString& key, const QVariant& value) {
    QSettings settings;
    settings.setValue(key, value);
}

void Preferences::setTheme(ThemeKind theme) {
    if (theme == theme_) {
        return;
    }
    theme_ = theme;
    write("ui/theme", theme == ThemeKind::Light ? "light" : "dark");
    emit changed("theme");
}

void Preferences::setLanguage(const QString& language) {
    if (language == language_) {
        return;
    }
    language_ = language;
    write("ui/language", language);
    emit changed("language");
}

void Preferences::setDefaultFps(int fps) {
    fps = std::clamp(fps, 1, 240);
    if (fps == defaultFps_) {
        return;
    }
    defaultFps_ = fps;
    write("animation/defaultFps", fps);
    emit changed("defaultFps");
}

void Preferences::setAutosave(bool enabled, int minutes) {
    minutes = std::clamp(minutes, 1, 60);
    if (enabled == autosaveEnabled_ && minutes == autosaveMinutes_) {
        return;
    }
    autosaveEnabled_ = enabled;
    autosaveMinutes_ = minutes;
    write("project/autosave", enabled);
    write("project/autosaveMinutes", minutes);
    emit changed("autosave");
}

void Preferences::setFontSize(double points) {
    points = std::clamp(points, 8.0, 20.0);
    if (points == fontSize_) {
        return;
    }
    fontSize_ = points;
    write("ui/fontSize", points);
    emit changed("fontSize");
}

void Preferences::setAngleUnit(AngleUnit unit) {
    if (unit == angleUnit_) {
        return;
    }
    angleUnit_ = unit;
    write("ui/angleUnit", unit == AngleUnit::Radians ? "radians" : "degrees");
    emit changed("angleUnit");
}

bool Preferences::hasCustomShortcut(const QString& actionId) const {
    QSettings settings;
    return settings.contains(kShortcutGroup + actionId);
}

QKeySequence Preferences::customShortcut(const QString& actionId) const {
    QSettings settings;
    const QString text = settings.value(kShortcutGroup + actionId).toString();
    return text == QLatin1String("none") ? QKeySequence()
                                         : QKeySequence::fromString(text, QKeySequence::PortableText);
}

void Preferences::setCustomShortcut(const QString& actionId, const QKeySequence& sequence) {
    write(kShortcutGroup + actionId,
          sequence.isEmpty() ? QStringLiteral("none") : sequence.toString(QKeySequence::PortableText));
    emit changed("shortcut:" + actionId);
}

void Preferences::clearCustomShortcut(const QString& actionId) {
    QSettings settings;
    settings.remove(kShortcutGroup + actionId);
    emit changed("shortcut:" + actionId);
}

void Preferences::addRecentFile(const QString& path) {
    const QString clean = QFileInfo(path).absoluteFilePath();
    recentFiles_.removeAll(clean);
    recentFiles_.prepend(clean);
    while (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.removeLast();
    }
    write("recent/files", recentFiles_);
    emit changed("recentFiles");
}

void Preferences::clearRecentFiles() {
    recentFiles_.clear();
    write("recent/files", recentFiles_);
    emit changed("recentFiles");
}

QByteArray Preferences::layoutState() const {
    QSettings settings;
    return settings.value("layout/state").toByteArray();
}

void Preferences::setLayoutState(const QByteArray& state) { write("layout/state", state); }

QByteArray Preferences::windowGeometry() const {
    QSettings settings;
    return settings.value("layout/geometry").toByteArray();
}

void Preferences::setWindowGeometry(const QByteArray& geometry) { write("layout/geometry", geometry); }

}  // namespace rigger::editor
