// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/StatsPanel.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/util/profiler.hpp"
#include "editor/Document.hpp"

namespace rigger::editor {

StatsPanel::StatsPanel(Document* document, std::function<FrameStats()> frameStats, QWidget* parent)
    : QWidget(parent), document_(document), frameStats_(std::move(frameStats)) {
    setObjectName("statsPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* bar = new QHBoxLayout;
    enabled_ = new QCheckBox(this);
    enabled_->setObjectName("statsEnabled");
    reset_ = new QPushButton(this);
    reset_->setObjectName("statsReset");
    bar->addWidget(enabled_);
    bar->addStretch(1);
    bar->addWidget(reset_);
    root->addLayout(bar);
    summary_ = new QLabel(this);
    summary_->setObjectName("statsSummary");
    scene_ = new QLabel(this);
    scene_->setObjectName("statsScene");
    root->addWidget(summary_);
    root->addWidget(scene_);
    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName("statsTable");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    root->addWidget(table_, 1);
    timer_ = new QTimer(this);
    timer_->setInterval(500);
    connect(timer_, &QTimer::timeout, this, &StatsPanel::refresh);
    timer_->start();
    connect(enabled_, &QCheckBox::toggled, this, [](bool on) { prof::setEnabled(on); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        prof::reset();
        refresh();
    });
    retranslate();
    refresh();
}

void StatsPanel::refresh() {
    const FrameStats stats = frameStats_ ? frameStats_() : FrameStats{};
    summary_->setText(tr("%1 FPS  -  CPU %2 ms  -  GPU %3 ms")
                          .arg(stats.fps, 0, 'f', 0)
                          .arg(stats.cpuMs, 0, 'f', 2)
                          .arg(stats.gpuMs, 0, 'f', 2));
    const auto character = document_->character();
    if (character) {
        std::size_t vertices = 0, triangles = 0;
        for (const Mesh& m : character->meshes) {
            vertices += m.vertices.size();
            triangles += m.indices.size() / 3;
        }
        scene_->setText(tr("%1 bones  -  %2 vertices  -  %3 triangles  -  %4 clips")
                            .arg(character->skeleton.boneCount())
                            .arg(vertices)
                            .arg(triangles)
                            .arg(character->clips.size()));
    } else {
        scene_->setText(tr("No character"));
    }
    const auto zones = prof::snapshot();
    table_->setRowCount(static_cast<int>(zones.size()));
    for (int row = 0; row < static_cast<int>(zones.size()); ++row) {
        const prof::Stat& z = zones[static_cast<std::size_t>(row)];
        const auto num = [](double v) { return QString::number(v, 'f', 3); };
        const QStringList cells{QString::fromStdString(z.name), QString::number(z.count), num(z.averageMs()),
                                num(z.maxMs), num(z.lastMs)};
        for (int col = 0; col < 5; ++col) {
            auto* item = table_->item(row, col);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                table_->setItem(row, col, item);
            }
            item->setText(cells[col]);
            if (col > 0) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    if (enabled_->isChecked() != prof::enabled()) {
        const QSignalBlocker blocker(enabled_);
        enabled_->setChecked(prof::enabled());
    }
}

void StatsPanel::retranslate() {
    enabled_->setText(tr("Profile code zones"));
    enabled_->setToolTip(tr("Measure the time spent in the instrumented functions (small overhead)"));
    reset_->setText(tr("Reset"));
    table_->setHorizontalHeaderLabels(
        {tr("Zone"), tr("Calls"), tr("Average (ms)"), tr("Max (ms)"), tr("Last (ms)")});
    refresh();
}

void StatsPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
