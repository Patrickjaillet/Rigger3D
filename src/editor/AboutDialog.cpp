// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/AboutDialog.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "core/version.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

QString AboutDialog::summaryHtml(const QString& renderInfo) {
    const QString title =
        QString::fromStdString(std::string(kProductName) + " v" + std::string(kVersionString));
    return tr("<h2>%1</h2>"
              "<p>Professional 3D character animation: skeleton, skinning, FK / IK, timeline, blend shapes, "
              "export to glTF.</p>"
              "<p>Rendering: %2</p>"
              "<p>%4<br>Distributed under the %5 license.</p>"
              "<p>Contact: <a href=\"mailto:%6\">%6</a><br>Website: <a href=\"%7\">%7</a></p>"
              "<p>Built with Qt %3, used under the <b>LGPL v3</b> through <b>dynamic linking</b>: the Qt "
              "libraries shipped next "
              "to the application can be replaced by a compatible modified version. Qt's source code is "
              "available at "
              "<a href=\"https://download.qt.io\">download.qt.io</a>.</p>"
              "<p><b>Privacy:</b> Rigger sends no data anywhere. There is no telemetry and no network "
              "access; crash reports "
              "stay on your computer.</p>")
        .arg(title, renderInfo, QStringLiteral(QT_VERSION_STR),
             QString::fromStdString(std::string(kCopyright)), QString::fromStdString(std::string(kLicenseId)),
             QString::fromStdString(std::string(kContactEmail)),
             QString::fromStdString(std::string(kWebsiteUrl)));
}

QString AboutDialog::userGuidePath() {
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates = {app.filePath(QStringLiteral("../share/doc/rigger/user_guide.md")),
                                    app.filePath(QStringLiteral("docs/user_guide.md")),
                                    app.filePath(QStringLiteral("../docs/user_guide.md")),
                                    app.filePath(QStringLiteral("../../docs/user_guide.md")),
                                    app.filePath(QStringLiteral("../../../docs/user_guide.md")),
                                    app.filePath(QStringLiteral("../../../../docs/user_guide.md"))};
    for (const QString& path : candidates) {
        if (QFile::exists(path)) return QDir::cleanPath(path);
    }
    return {};
}

QString AboutDialog::licensesText() {
    QFile file(QStringLiteral(":/THIRD_PARTY_LICENSES.txt"));
    if (!file.open(QIODevice::ReadOnly)) return tr("The license list is not available in this build.");
    return QString::fromUtf8(file.readAll());
}

AboutDialog::AboutDialog(const QString& renderInfo, QWidget* parent) : QDialog(parent) {
    setObjectName("aboutDialog");
    setWindowTitle(tr("About Rigger"));
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    auto* icon = new QLabel(this);
    icon->setPixmap(QPixmap(QStringLiteral(":/app/rigger-64.png")));
    icon->setAlignment(Qt::AlignTop);
    auto* text = new QLabel(summaryHtml(renderInfo), this);
    text->setObjectName("aboutText");
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setMinimumWidth(420);
    top->addWidget(icon);
    top->addWidget(text, 1);
    root->addLayout(top);

    auto* buttons = new QDialogButtonBox(this);
    auto* licenses = buttons->addButton(tr("Third-Party Licenses..."), QDialogButtonBox::ActionRole);
    licenses->setObjectName("aboutLicenses");
    auto* docs = buttons->addButton(tr("Documentation"), QDialogButtonBox::ActionRole);
    docs->setObjectName("aboutDocs");
    buttons->addButton(QDialogButtonBox::Close);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    connect(licenses, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Third-Party Licenses"));
        dialog.resize(760, 560);
        auto* layout = new QVBoxLayout(&dialog);
        auto* view = new QPlainTextEdit(&dialog);
        view->setObjectName("licensesText");
        view->setReadOnly(true);
        view->setPlainText(licensesText());
        layout->addWidget(view);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
        layout->addWidget(close);
        dialog.exec();
    });
    connect(docs, &QPushButton::clicked, this, [this] {
        const QString guide = userGuidePath();
        if (guide.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(guide))) {
            if (auto* label = findChild<QLabel*>("aboutText")) {
                label->setText(tr("The user guide is in docs/user_guide.md of the Rigger distribution."));
            }
        }
    });
}

}  // namespace rigger::editor
