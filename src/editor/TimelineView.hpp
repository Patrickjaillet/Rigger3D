// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QPoint>
#include <QWidget>
#include <optional>
#include <set>
#include <vector>

#include "core/animation/keys.hpp"
#include "core/skeleton/skeleton.hpp"

class QScrollBar;

namespace rigger::editor {

class Document;

// Timeline : règle en images, curseur de lecture (scrub), pistes par os (hiérarchie indentée, canaux T / R /
// S dépliables), losanges de clés, sélection (clic, Ctrl+clic, rectangle), glissé (déplacer), Alt+glisser
// (dupliquer), Ctrl+glisser sur une clé sélectionnée (étirer), Maj+glisser (décalage global), Suppr, copier /
// coller, zoom à la molette, panoramique (clic milieu / Maj+molette). Dessin 100 % QPainter : seules les clés
// visibles sont parcourues (recherche binaire), fluide jusqu'à plusieurs dizaines de milliers de clés.
class TimelineView : public QWidget {
    Q_OBJECT
public:
    static constexpr int kLabelWidth = 190;
    static constexpr int kRulerHeight = 26;
    static constexpr int kRowHeight = 20;
    static constexpr int kScrollBarSize = 12;

    struct Row {
        BoneId bone = kNoBone;
        int channel = -1;  // -1 : ligne de l'os (résumé) ; 0 / 1 / 2 : T / R / S
        int depth = 0;
    };

    explicit TimelineView(Document* document, QWidget* parent = nullptr);

    // --- Vue ---
    double pixelsPerFrame() const { return pixelsPerFrame_; }
    double firstVisibleFrame() const { return firstFrame_; }
    void setView(double firstFrame, double pixelsPerFrame);
    void fitToClip();
    int xForTime(double seconds) const;
    double timeForX(int x) const;
    double fps() const;
    int visibleRowCount() const;

    // --- Pistes ---
    const std::vector<Row>& rows() const { return rows_; }
    void setBoneExpanded(BoneId bone, bool expanded);  // affiche / masque les canaux T R S
    bool isBoneExpanded(BoneId bone) const { return expanded_.count(bone) != 0; }
    void setBranchCollapsed(BoneId bone, bool collapsed);  // masque les descendants
    bool isBranchCollapsed(BoneId bone) const { return collapsed_.count(bone) != 0; }
    QRect rowRect(int rowIndex) const;  // rectangle de la ligne dans le widget (zone canevas incluse)

    // --- Test / hit-testing ---
    std::vector<KeyRef> keysAt(
        const QPoint& position) const;  // clés sous le point (résumé : tous les canaux)
    int rowAt(int y) const;

    QSize sizeHint() const override { return {900, 260}; }

signals:
    void viewChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool event(QEvent* event) override;

private:
    enum class Drag { None, Scrub, Pan, KeyPending, Keys, Rectangle, Offset };

    void rebuildRows();
    void updateScrollBars();
    void clampView();
    bool inCanvas(const QPoint& p) const { return p.x() >= kLabelWidth && p.y() >= kRulerHeight; }
    int frameForX(int x) const;
    void setFrameFromX(int x);
    std::vector<KeyRef> rowKeysInRange(const Row& row, double t0, double t1) const;
    std::vector<KeyRef> keysInRect(const QRect& rect) const;
    void selectWithModifiers(const std::vector<KeyRef>& hit, Qt::KeyboardModifiers mods);
    bool isSelected(const KeyRef& key) const;
    void paintRuler(QPainter& p);
    void paintRows(QPainter& p);

    Document* document_;
    std::vector<Row> rows_;
    std::set<BoneId> expanded_;
    std::set<BoneId> collapsed_;
    double firstFrame_ = 0.0;
    double pixelsPerFrame_ = 12.0;
    int scrollRow_ = 0;
    QScrollBar* hbar_;
    QScrollBar* vbar_;
    bool updatingBars_ = false;

    Drag drag_ = Drag::None;
    QPoint pressPos_;
    QPoint lastPos_;
    QRect rubber_;
    double pivotTime_ = 0.0;
    int pivotX_ = 0;
    bool dragScale_ = false;
    bool dragDuplicate_ = false;
};

}  // namespace rigger::editor
