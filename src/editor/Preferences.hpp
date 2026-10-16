// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QByteArray>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>

class QSettings;

namespace rigger::editor {

enum class ThemeKind { Dark, Light };
enum class AngleUnit { Degrees, Radians };

// Préférences utilisateur persistées dans QSettings (organisation / application de QApplication).
// Les valeurs sont lues une fois au démarrage ; chaque setter écrit immédiatement et émet changed().
class Preferences : public QObject {
    Q_OBJECT
public:
    explicit Preferences(QObject* parent = nullptr);

    ThemeKind theme() const { return theme_; }
    void setTheme(ThemeKind theme);

    // Code de langue ("en", "fr") ; vide = langue du système.
    QString language() const { return language_; }
    void setLanguage(const QString& language);

    int defaultFps() const { return defaultFps_; }
    void setDefaultFps(int fps);

    bool autosaveEnabled() const { return autosaveEnabled_; }
    int autosaveMinutes() const { return autosaveMinutes_; }
    void setAutosave(bool enabled, int minutes);

    // Taille de la police de l'interface (points) : 8 à 20, 9,5 par défaut (accessibilité).
    double fontSize() const { return fontSize_; }
    void setFontSize(double points);

    AngleUnit angleUnit() const { return angleUnit_; }
    void setAngleUnit(AngleUnit unit);

    // Raccourcis personnalisés (vide = valeur par défaut de l'action). « none » = volontairement aucun.
    bool hasCustomShortcut(const QString& actionId) const;
    QKeySequence customShortcut(const QString& actionId) const;
    void setCustomShortcut(const QString& actionId, const QKeySequence& sequence);
    void clearCustomShortcut(const QString& actionId);

    QStringList recentFiles() const { return recentFiles_; }
    void addRecentFile(const QString& path);
    void clearRecentFiles();

    QByteArray layoutState() const;
    void setLayoutState(const QByteArray& state);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    static constexpr int kMaxRecentFiles = 10;

signals:
    void changed(const QString& key);

private:
    void write(const QString& key, const QVariant& value);

    ThemeKind theme_ = ThemeKind::Dark;
    QString language_;
    int defaultFps_ = 30;
    bool autosaveEnabled_ = true;
    int autosaveMinutes_ = 2;
    AngleUnit angleUnit_ = AngleUnit::Degrees;
    double fontSize_ = 9.5;
    QStringList recentFiles_;
};

}  // namespace rigger::editor
