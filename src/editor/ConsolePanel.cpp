// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/ConsolePanel.hpp"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <mutex>

#include "editor/Theme.hpp"

namespace rigger::editor {

namespace {

// Sink spdlog -> LogBridge (le signal est mis en file d'attente vers le thread UI).
class QtLogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit QtLogSink(LogBridge* bridge) : bridge_(bridge) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch()).count();
        const QString text =
            QString::fromUtf8(msg.payload.data(), static_cast<qsizetype>(msg.payload.size()));
        emit bridge_->record(static_cast<int>(msg.level), text, static_cast<qint64>(millis));
    }
    void flush_() override {}

private:
    LogBridge* bridge_;
};

QString levelName(int level) {
    const auto name = spdlog::level::to_string_view(static_cast<spdlog::level::level_enum>(level));
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

}  // namespace

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent), bridge_(new LogBridge(this)) {
    setObjectName("consolePanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* bar = new QHBoxLayout;
    levelCombo_ = new QComboBox(this);
    levelCombo_->setObjectName("consoleLevel");
    levelCombo_->addItems({QString(), QString(), QString(), QString(), QString()});
    levelCombo_->setCurrentIndex(2);  // Info
    bar->addWidget(levelCombo_);
    bar->addStretch(1);
    clearButton_ = new QToolButton(this);
    clearButton_->setObjectName("consoleClear");
    clearButton_->setIcon(Icons::get(QStringLiteral("clear")));
    copyButton_ = new QToolButton(this);
    copyButton_->setObjectName("consoleCopy");
    copyButton_->setIcon(Icons::get(QStringLiteral("copy")));
    exportButton_ = new QToolButton(this);
    exportButton_->setObjectName("consoleExport");
    exportButton_->setIcon(Icons::get(QStringLiteral("export")));
    bar->addWidget(clearButton_);
    bar->addWidget(copyButton_);
    bar->addWidget(exportButton_);
    layout->addLayout(bar);

    view_ = new QPlainTextEdit(this);
    view_->setObjectName("consoleView");
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(static_cast<int>(kMaxRecords));
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    view_->setFont(mono);
    layout->addWidget(view_, 1);

    connect(levelCombo_, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(clearButton_, &QToolButton::clicked, this, &ConsolePanel::clear);
    connect(copyButton_, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(visibleText());
        emit statusMessage(tr("Log copied to the clipboard"));
    });
    connect(exportButton_, &QToolButton::clicked, this, &ConsolePanel::exportDialog);
    connect(bridge_, &LogBridge::record, this, &ConsolePanel::append);

    sink_ = std::make_shared<QtLogSink>(bridge_);
    spdlog::default_logger()->sinks().push_back(sink_);
    retranslate();
}

ConsolePanel::~ConsolePanel() {
    auto& sinks = spdlog::default_logger()->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), sink_), sinks.end());
}

void ConsolePanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void ConsolePanel::retranslate() {
    const QStringList names = {tr("Trace and above"), tr("Debug and above"), tr("Info and above"),
                               tr("Warnings and above"), tr("Errors only")};
    for (int i = 0; i < names.size(); ++i) {
        levelCombo_->setItemText(i, names[i]);
    }
    clearButton_->setIcon(Icons::get(QStringLiteral("clear")));
    copyButton_->setIcon(Icons::get(QStringLiteral("copy")));
    exportButton_->setIcon(Icons::get(QStringLiteral("export")));
    clearButton_->setToolTip(tr("Clear the log"));
    copyButton_->setToolTip(tr("Copy the visible log to the clipboard"));
    exportButton_->setToolTip(tr("Export the visible log to a text file..."));
}

int ConsolePanel::minimumLevel() const {
    // 0 trace, 1 debug, 2 info, 3 warn ; la dernière entrée regroupe error et critical (4 et plus).
    return levelCombo_->currentIndex();
}

void ConsolePanel::setMinimumLevel(int level) { levelCombo_->setCurrentIndex(std::clamp(level, 0, 4)); }

QString ConsolePanel::format(const Record& record) {
    return QStringLiteral("[%1] [%2] %3")
        .arg(record.time.toString(QStringLiteral("HH:mm:ss.zzz")), levelName(record.level), record.text);
}

void ConsolePanel::append(int level, const QString& text, qint64 msecsSinceEpoch) {
    Record record{level, text, QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch)};
    if (records_.size() >= kMaxRecords) {
        records_.erase(records_.begin());
    }
    records_.push_back(record);
    if (level >= minimumLevel()) {
        view_->appendPlainText(format(record));
    }
}

void ConsolePanel::clear() {
    records_.clear();
    view_->clear();
}

void ConsolePanel::render() {
    view_->clear();
    QStringList lines;
    for (const Record& record : records_) {
        if (record.level >= minimumLevel()) {
            lines << format(record);
        }
    }
    view_->setPlainText(lines.join(QLatin1Char('\n')));
}

QString ConsolePanel::visibleText() const { return view_->toPlainText(); }

bool ConsolePanel::exportTo(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << visibleText() << '\n';
    return out.status() == QTextStream::Ok;
}

void ConsolePanel::exportDialog() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export the log"), QStringLiteral("rigger-log.txt"), tr("Text files (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    emit statusMessage(exportTo(path) ? tr("Log exported to %1").arg(path) : tr("Cannot write %1").arg(path));
}

}  // namespace rigger::editor
