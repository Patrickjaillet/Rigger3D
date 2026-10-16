// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// MainWindow : projets (.rig), sauvegarde / ouverture, autosave et récupération, exports glTF et images
// (Phase 12).
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>
#include <filesystem>

#include "editor/ActionRegistry.hpp"
#include "editor/Document.hpp"
#include "editor/ExportDialogs.hpp"
#include "editor/MainWindow.hpp"
#include "editor/Preferences.hpp"
#include "editor/ViewportWidget.hpp"
#include "io/gltf_export.hpp"
#include "io/project.hpp"

namespace rigger::editor {

namespace {

std::filesystem::path toPath(const QString& path) { return std::filesystem::path(path.toStdWString()); }

QString withExtension(QString path, const QString& extension) {
    if (QFileInfo(path).suffix().isEmpty()) path += extension;
    return path;
}

}  // namespace

void MainWindow::reportError(const QString& title, const QString& message) {
    ++reportedErrors_;
    spdlog::error("{}: {}", title.toStdString(), message.toStdString());
    postStatus(QStringLiteral("%1: %2").arg(title, message), 15000);
    auto* box = new QMessageBox(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();  // non bloquant : l'interface et les tests restent réactifs
}

// --- Confirmation des modifications non sauvegardées ------------------------------------------------------

bool MainWindow::confirmDiscard() {
    if (!document_->hasCharacter() || document_->isClean()) return true;
    UnsavedChoice choice = UnsavedChoice::Cancel;
    if (unsavedPrompt_) {
        choice = unsavedPrompt_();
    } else {
        QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                        tr("The project has unsaved changes. Do you want to save them?"),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setDefaultButton(QMessageBox::Save);
        const int answer = box.exec();
        choice = answer == QMessageBox::Save      ? UnsavedChoice::Save
                 : answer == QMessageBox::Discard ? UnsavedChoice::Discard
                                                  : UnsavedChoice::Cancel;
    }
    if (choice == UnsavedChoice::Cancel) return false;
    if (choice == UnsavedChoice::Save) {
        saveProject();
        return document_->isClean();  // l'enregistrement a pu être annulé ou échouer
    }
    return true;
}

// --- Projet ------------------------------------------------------------------------------------------------

Result<void> MainWindow::saveProjectTo(const QString& path) {
    const auto character = document_->character();
    if (!character) return Error{"There is nothing to save."};
    io::Project project;
    project.character = *character;
    io::ProjectView& view = project.view;
    if (glViewport_ != nullptr) {
        const auto& camera = glViewport_->camera();
        view.yaw = camera.yaw;
        view.pitch = camera.pitch;
        view.distance = camera.distance;
        view.target = camera.target;
        view.orthographic = camera.orthographic;
    }
    view.mode = document_->mode() == Mode::Rig ? 0 : (document_->mode() == Mode::Skin ? 1 : 2);
    view.clipIndex = document_->clipIndex();
    view.time = document_->time();
    view.sourcePath = document_->sourcePath().toStdString();
    const auto saved = io::saveProject(project, toPath(path));
    if (!saved) return saved;
    projectPath_ = path;
    document_->markClean();
    preferences_->addRecentFile(path);
    discardAutosave();
    refreshTitle();
    return {};
}

Result<void> MainWindow::openProject(const QString& path) {
    auto loaded = io::loadProject(toPath(path));
    if (!loaded) return loaded.error();
    io::Project& project = loaded.value();
    const io::ProjectView view = project.view;
    document_->setCharacter(std::make_shared<Character>(std::move(project.character)),
                            QString::fromStdString(view.sourcePath));
    document_->setPlaying(false);
    document_->setMode(view.mode == 0 ? Mode::Rig : (view.mode == 1 ? Mode::Skin : Mode::Animate));
    if (view.clipIndex >= 0) document_->setClipIndex(view.clipIndex);
    document_->setFrame(static_cast<int>(std::lround(view.time * document_->fps())));
    if (glViewport_ != nullptr) {
        auto& camera = glViewport_->camera();
        camera.yaw = view.yaw;
        camera.pitch = view.pitch;
        camera.distance = view.distance;
        camera.target = view.target;
        camera.orthographic = view.orthographic;
        glViewport_->update();
    }
    document_->markClean();
    projectPath_ = path;
    preferences_->addRecentFile(path);
    showWorkspace();
    refreshTitle();
    postStatus(tr("Opened %1").arg(QFileInfo(path).fileName()));
    return {};
}

void MainWindow::openDialog() {
    if (!confirmDiscard()) return;
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open a project"), QString(), tr("Rigger project (*.rig)"));
    if (path.isEmpty()) return;
    if (const auto opened = openProject(path); !opened) {
        postStatus(tr("Cannot open the project: %1").arg(QString::fromStdString(opened.error().message)),
                   10000);
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Open failed"),
                                    QString::fromStdString(opened.error().message), QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
}

void MainWindow::saveProject() {
    if (!document_->hasCharacter()) return;
    if (projectPath_.isEmpty()) {
        saveProjectAs();
        return;
    }
    if (const auto saved = saveProjectTo(projectPath_); !saved) {
        reportError(tr("Save failed"), QString::fromStdString(saved.error().message));
    } else {
        postStatus(tr("Saved %1").arg(QFileInfo(projectPath_).fileName()));
    }
}

void MainWindow::saveProjectAs() {
    if (!document_->hasCharacter()) return;
    const QString suggestion =
        projectPath_.isEmpty()
            ? QString::fromStdString(document_->character()->name.empty() ? "project"
                                                                          : document_->character()->name) +
                  ".rig"
            : projectPath_;
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save the project"), suggestion, tr("Rigger project (*.rig)"));
    if (path.isEmpty()) return;
    if (const auto saved = saveProjectTo(withExtension(path, ".rig")); !saved) {
        reportError(tr("Save failed"), QString::fromStdString(saved.error().message));
    } else {
        postStatus(tr("Saved %1").arg(QFileInfo(projectPath_).fileName()));
    }
}

// --- Autosave / récupération
// ---------------------------------------------------------------------------------

QString MainWindow::autosaveDirectory() {
    const QByteArray override = qgetenv("RIGGER_AUTOSAVE_DIR");
    if (!override.isEmpty()) return QString::fromLocal8Bit(override);
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/rigger_autosave");
}

void MainWindow::autosaveNow() {
    if (!document_->hasCharacter() || document_->isClean() || document_->isManipulating() ||
        document_->isPainting())
        return;
    QDir().mkpath(autosaveDirectory());
    const QString file = autosaveDirectory() + QStringLiteral("/autosave.rig");
    io::Project project;
    project.character = *document_->character();
    project.view.mode = document_->mode() == Mode::Rig ? 0 : (document_->mode() == Mode::Skin ? 1 : 2);
    project.view.clipIndex = document_->clipIndex();
    project.view.time = document_->time();
    project.view.sourcePath = document_->sourcePath().toStdString();
    if (glViewport_ != nullptr) {
        const auto& camera = glViewport_->camera();
        project.view.yaw = camera.yaw;
        project.view.pitch = camera.pitch;
        project.view.distance = camera.distance;
        project.view.target = camera.target;
        project.view.orthographic = camera.orthographic;
    }
    if (const auto saved = io::saveProject(project, toPath(file)); !saved) {
        spdlog::warn("autosave failed: {}", saved.error().message);
        return;
    }
    QFile meta(autosaveDirectory() + QStringLiteral("/autosave.json"));
    if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        meta.write(QJsonDocument(QJsonObject{{"projectPath", projectPath_}}).toJson());
    }
}

bool MainWindow::hasRecoverableAutosave() const {
    return QFileInfo::exists(autosaveDirectory() + QStringLiteral("/autosave.rig"));
}

Result<void> MainWindow::recoverAutosave() {
    const QString file = autosaveDirectory() + QStringLiteral("/autosave.rig");
    QString original;
    QFile meta(autosaveDirectory() + QStringLiteral("/autosave.json"));
    if (meta.open(QIODevice::ReadOnly))
        original = QJsonDocument::fromJson(meta.readAll()).object().value("projectPath").toString();
    if (const auto opened = openProject(file); !opened) return opened;
    projectPath_ = original;  // enregistrer remet le projet à son emplacement d'origine (ou demande un nom)
    document_->markDirty();
    refreshTitle();
    postStatus(tr("Recovered the unsaved work from the last session"));
    return {};
}

void MainWindow::discardAutosave() {
    QFile::remove(autosaveDirectory() + QStringLiteral("/autosave.rig"));
    QFile::remove(autosaveDirectory() + QStringLiteral("/autosave.json"));
}

void MainWindow::offerRecovery() {
    if (!hasRecoverableAutosave()) return;
    const auto answer = QMessageBox::question(
        this, tr("Recover unsaved work"),
        tr("Rigger did not close properly last time. Do you want to recover the unsaved work?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        if (const auto recovered = recoverAutosave(); !recovered) {
            postStatus(tr("Recovery failed: %1").arg(QString::fromStdString(recovered.error().message)),
                       10000);
            discardAutosave();
        }
    } else {
        discardAutosave();
    }
}

// --- Export glTF
// ------------------------------------------------------------------------------------------------

Result<void> MainWindow::exportGltfTo(const QString& path, const io::GltfExportOptions& options) {
    const auto character = document_->character();
    if (!character) return Error{"There is nothing to export."};
    return io::exportGltf(*character, toPath(path), options);
}

void MainWindow::exportGltfDialog() {
    const auto character = document_->character();
    if (!character) return;
    GltfExportDialog dialog(*character, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const io::GltfExportOptions options = dialog.options();
    const QString extension = options.binary ? QStringLiteral(".glb") : QStringLiteral(".gltf");
    const QString suggestion =
        (projectPath_.isEmpty() ? QStringLiteral("export") : QFileInfo(projectPath_).completeBaseName()) +
        extension;
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Export glTF"), suggestion,
                                     options.binary ? tr("glTF binary (*.glb)") : tr("glTF (*.gltf)"));
    if (path.isEmpty()) return;
    const QString target = withExtension(path, extension);
    if (const auto exported = exportGltfTo(target, options); !exported) {
        reportError(tr("Export failed"), QString::fromStdString(exported.error().message));
    } else {
        postStatus(tr("Exported %1").arg(QFileInfo(target).fileName()));
    }
}

// --- Export d'images / vidéo
// ---------------------------------------------------------------------------------------

Result<int> MainWindow::exportImage(const QString& path, bool transparent, bool hideOverlays) {
    if (glViewport_ == nullptr) return Error{"The 3D viewport is not available (no OpenGL rendering)."};
    glViewport_->setExportMode(transparent, hideOverlays);
    QImage image = glViewport_->grabFramebuffer();
    glViewport_->setExportMode(false, false);
    if (image.isNull()) return Error{"The viewport could not be captured."};
    if (!image.save(path, "PNG")) return Error{"Cannot write " + path.toStdString()};
    return 1;
}

Result<int> MainWindow::exportImageSequence(const QString& directory, const QString& baseName,
                                            const ImageSequenceOptions& options) {
    if (glViewport_ == nullptr) return Error{"The 3D viewport is not available (no OpenGL rendering)."};
    if (!QDir().mkpath(directory)) return Error{"Cannot create " + directory.toStdString()};
    const int savedFrame = document_->frame();
    const bool wasPlaying = document_->isPlaying();
    document_->setPlaying(false);
    int written = 0;
    glViewport_->setExportMode(options.transparent, options.hideOverlays);
    for (int frame = options.firstFrame; frame <= options.lastFrame; ++frame) {
        document_->setFrame(frame);
        const QImage image = glViewport_->grabFramebuffer();
        const QString file = QDir(directory).filePath(
            QStringLiteral("%1_%2.png").arg(baseName).arg(frame, 4, 10, QLatin1Char('0')));
        if (image.isNull() || !image.save(file, "PNG")) {
            glViewport_->setExportMode(false, false);
            document_->setFrame(savedFrame);
            return Error{"Cannot write " + file.toStdString()};
        }
        ++written;
    }
    glViewport_->setExportMode(false, false);
    document_->setFrame(savedFrame);
    if (wasPlaying) document_->setPlaying(true);
    if (options.makeVideo) {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty())
            return Error{"ffmpeg was not found: the PNG sequence was written but no video was made."};
        QProcess process;
        process.start(
            ffmpeg, {QStringLiteral("-y"), QStringLiteral("-framerate"), QString::number(document_->fps()),
                     QStringLiteral("-start_number"), QString::number(options.firstFrame),
                     QStringLiteral("-i"), QDir(directory).filePath(baseName + QStringLiteral("_%04d.png")),
                     QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                     QStringLiteral("yuv420p"), QDir(directory).filePath(baseName + QStringLiteral(".mp4"))});
        if (!process.waitForFinished(600000) || process.exitCode() != 0) {
            return Error{"ffmpeg failed: " +
                         QString::fromLocal8Bit(process.readAllStandardError()).right(300).toStdString()};
        }
    }
    return written;
}

void MainWindow::exportImageDialog() {
    if (glViewport_ == nullptr) {
        postStatus(tr("Image export needs the 3D viewport (OpenGL)"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("viewport.png"), tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    const auto result = exportImage(withExtension(path, ".png"), false, false);
    postStatus(result ? tr("Image exported")
                      : tr("Export failed: %1").arg(QString::fromStdString(result.error().message)),
               8000);
}

void MainWindow::exportSequenceDialog() {
    if (glViewport_ == nullptr || !document_->hasCharacter()) {
        postStatus(tr("Image export needs the 3D viewport (OpenGL) and a character"));
        return;
    }
    const bool ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    ImageSequenceDialog dialog(std::max(document_->frameCount(), 1), ffmpeg, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Export Image Sequence"));
    if (directory.isEmpty()) return;
    const auto result = exportImageSequence(directory, QStringLiteral("frame"), dialog.options());
    postStatus(result ? tr("Exported %1 images").arg(result.value())
                      : tr("Export failed: %1").arg(QString::fromStdString(result.error().message)),
               10000);
}

}  // namespace rigger::editor
