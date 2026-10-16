// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QDialog>

#include "io/gltf_export.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTreeWidget;

namespace rigger::editor {

// Options d'export glTF / GLB : format, plage d'images, FPS, maillages et clips inclus (avec renommage des
// clips), bake IK. Les valeurs par défaut exportent tout.
class GltfExportDialog : public QDialog {
    Q_OBJECT
public:
    GltfExportDialog(const Character& character, QWidget* parent = nullptr);
    io::GltfExportOptions options() const;

private:
    QComboBox* format_;
    QSpinBox* first_;
    QSpinBox* last_;
    QDoubleSpinBox* fps_;
    QCheckBox* bakeIk_;
    QTreeWidget* meshes_;
    QTreeWidget* clips_;
    int meshCount_;
    int clipCount_;
};

// Options d'export d'images : plage, fond transparent, assemblage MP4 via ffmpeg (si présent).
struct ImageSequenceOptions {
    int firstFrame = 0;
    int lastFrame = 0;
    bool transparent = true;
    bool hideOverlays = true;
    bool makeVideo = false;
};

class ImageSequenceDialog : public QDialog {
    Q_OBJECT
public:
    ImageSequenceDialog(int frameCount, bool ffmpegAvailable, QWidget* parent = nullptr);
    ImageSequenceOptions options() const;

private:
    QSpinBox* first_;
    QSpinBox* last_;
    QCheckBox* transparent_;
    QCheckBox* overlays_;
    QCheckBox* video_;
};

}  // namespace rigger::editor
