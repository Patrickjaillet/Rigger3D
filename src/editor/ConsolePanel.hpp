// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

class QComboBox;
class QPlainTextEdit;
class QToolButton;

namespace spdlog::sinks {
class sink;
}

namespace rigger::editor {

// Pont thread-safe : le sink spdlog (n'importe quel thread) émet, le panneau reçoit dans le thread UI.
class LogBridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void record(int level, const QString& text, qint64 msecsSinceEpoch);
};

// Journal : affiche les messages spdlog, filtrables par niveau ; copie dans le presse-papiers / export texte.
class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    ~ConsolePanel() override;
    void retranslate();

    // Ajoute un message (reçu du sink ou injecté par les tests). `level` : valeur spdlog::level (0 = trace …
    // 5 = critical).
    void append(int level, const QString& text, qint64 msecsSinceEpoch);
    void clear();

    // Niveau minimal affiché (valeur spdlog::level), texte actuellement visible.
    int minimumLevel() const;
    void setMinimumLevel(int level);
    QString visibleText() const;
    int recordCount() const { return static_cast<int>(records_.size()); }
    bool exportTo(const QString& path) const;

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    struct Record {
        int level;
        QString text;
        QDateTime time;
    };
    static QString format(const Record& record);
    void render();
    void exportDialog();

    LogBridge* bridge_;
    std::shared_ptr<spdlog::sinks::sink> sink_;
    std::vector<Record> records_;
    QPlainTextEdit* view_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    QToolButton* copyButton_ = nullptr;
    QToolButton* exportButton_ = nullptr;

    static constexpr std::size_t kMaxRecords = 5000;
};

}  // namespace rigger::editor
