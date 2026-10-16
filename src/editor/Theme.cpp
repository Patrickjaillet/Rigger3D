// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/Theme.hpp"

#include <spdlog/spdlog.h>

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QSvgRenderer>

static void initEditorResourcesImpl() { Q_INIT_RESOURCE(resources); }

namespace rigger::editor {

void initEditorResources() { initEditorResourcesImpl(); }

ThemeColors themeColors(ThemeKind kind) {
    if (kind == ThemeKind::Light) {
        return {QColor("#f2f3f5"), QColor("#ffffff"), QColor("#ffffff"), QColor("#1f2328"),
                QColor("#5f6770"), QColor("#d0d4da"), QColor("#e6e8ec"), QColor("#2f6fed"),
                QColor("#ffffff"), QColor("#bcd3ff")};
    }
    return {QColor("#25272b"), QColor("#2d2f34"), QColor("#1c1d20"), QColor("#d7dae0"), QColor("#a0a6b0"),
            QColor("#3a3d44"), QColor("#363940"), QColor("#2f6fed"), QColor("#ffffff"), QColor("#2a54a0")};
}

QString Theme::dockStyleSheet(const ThemeColors& c) {
    const QString qss = styleSheet(c);
    const int begin = qss.indexOf(QStringLiteral("/*[ads-begin]*/"));
    const int end = qss.indexOf(QStringLiteral("/*[ads-end]*/"));
    return begin >= 0 && end > begin ? qss.mid(begin, end - begin) : QString();
}

QString Theme::styleSheet(const ThemeColors& c) {
    initEditorResources();
    QFile file(QStringLiteral(":/theme.qss"));
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::error("theme.qss resource not found");
        return {};
    }
    QString qss = QString::fromUtf8(file.readAll());
    const std::pair<const char*, QColor> tokens[] = {
        {"window", c.window},         {"panel", c.panel},        {"base", c.base},   {"text", c.text},
        {"textDim", c.textDim},       {"border", c.border},      {"hover", c.hover}, {"accent", c.accent},
        {"accentText", c.accentText}, {"selection", c.selection}};
    for (const auto& [name, color] : tokens) {
        qss.replace(QStringLiteral("@%1@").arg(QLatin1String(name)), color.name());
    }
    return qss;
}

void Theme::loadFonts() {
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    initEditorResources();
    bool any = false;
    for (const char* file : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf"}) {
        any |= QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1").arg(QLatin1String(file))) >= 0;
    }
    if (any) {
        QFont font(QStringLiteral("Inter"));
        font.setPointSizeF(fontSize_);
        QApplication::setFont(font);
    } else {
        spdlog::warn("embedded Inter font could not be loaded; using the system font");
    }
}

void Theme::setFontSize(double points) {
    fontSize_ = points;
    QFont font = QApplication::font();
    font.setPointSizeF(points);
    QApplication::setFont(font);
}

void Theme::apply(QApplication& app, ThemeKind kind) {
    current_ = kind;
    colors_ = themeColors(kind);
    loadFonts();
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }
    const ThemeColors& c = colors_;
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.panel);
    palette.setColor(QPalette::ToolTipBase, c.panel);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.panel);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.accentText);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.text);
    palette.setColor(QPalette::PlaceholderText, c.textDim);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.textDim);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDim);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textDim);
    app.setPalette(palette);
    app.setStyleSheet(styleSheet(c));
}

QPixmap Icons::pixmap(const QString& name, const QColor& color, int size, qreal devicePixelRatio) {
    initEditorResources();
    static QHash<QString, QByteArray> svgCache;  // les fichiers SVG ne sont lus qu'une fois
    auto cached = svgCache.find(name);
    if (cached == svgCache.end()) {
        QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
        if (!file.open(QIODevice::ReadOnly)) {
            spdlog::warn("missing icon '{}'", name.toStdString());
            return {};
        }
        cached = svgCache.insert(name, file.readAll());
    }
    QByteArray svg = *cached;
    svg.replace("currentColor", color.name().toUtf8());
    QSvgRenderer renderer(svg);
    const int px = static_cast<int>(size * devicePixelRatio);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

QIcon Icons::get(const QString& name) {
    static QHash<QString, QIcon> cache;
    const ThemeColors& c = Theme::colors();
    const QString key = name + c.text.name() + c.textDim.name() + c.accentText.name();
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    // Une seule taille source (24 px logiques à 2x) : Qt la réduit pour les barres 16 px. Rendre toutes les
    // tailles pour tous les états coûtait plus de 600 ms au démarrage (build debug).
    QIcon icon;
    icon.addPixmap(pixmap(name, c.text, 24, 2.0), QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap(name, c.textDim, 24, 2.0), QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap(name, c.accentText, 24, 2.0), QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap(name, c.accentText, 24, 2.0), QIcon::Normal, QIcon::On);
    cache.insert(key, icon);
    return icon;
}

}  // namespace rigger::editor
