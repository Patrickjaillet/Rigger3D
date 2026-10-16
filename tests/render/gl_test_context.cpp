// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gl_test_context.hpp"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <cstdio>

namespace {
QOpenGLContext* activeContext = nullptr;

QSurfaceFormat testFormat() {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    return format;
}
}  // namespace

struct GlTestContext::Impl {
    Impl(int& argc, char** argv)
        : application((QSurfaceFormat::setDefaultFormat(testFormat()), argc), argv) {}
    QGuiApplication application;
    QOffscreenSurface surface;
    QOpenGLContext context;
    bool ready = false;
};

GlTestContext::GlTestContext(int& argc, char** argv) : impl_(std::make_unique<Impl>(argc, argv)) {
    const QSurfaceFormat format = testFormat();
    impl_->surface.setFormat(format);
    impl_->surface.create();
    if (!impl_->surface.isValid()) {
        std::fprintf(stderr, "Failed to create an offscreen OpenGL surface for GPU skinning tests.\n");
        return;
    }
    impl_->context.setFormat(format);
    if (!impl_->context.create() || !impl_->context.makeCurrent(&impl_->surface)) {
        std::fprintf(stderr, "Failed to create or activate OpenGL context (actual %d.%d).\n",
                     impl_->context.format().majorVersion(), impl_->context.format().minorVersion());
        return;
    }
    activeContext = &impl_->context;
    impl_->ready = true;
}

GlTestContext::~GlTestContext() { activeContext = nullptr; }

bool GlTestContext::ok() const { return impl_->ready; }

void* GlTestContext::procAddress(const char* name) {
    return activeContext != nullptr ? reinterpret_cast<void*>(activeContext->getProcAddress(name)) : nullptr;
}
