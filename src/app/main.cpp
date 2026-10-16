// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QSurfaceFormat>
#include <QTimer>

#include "app/crash_handler.hpp"
#include "core/example/example.hpp"
#include "core/version.hpp"
#include "editor/Document.hpp"
#include "editor/ExportDialogs.hpp"
#include "editor/MainWindow.hpp"
#include "io/project.hpp"
#include "render/gl_requirements.hpp"

int main(int argc, char** argv) {
    // Rapport de crash local : installé avant tout le reste (Qt compris).
    rigger::app::installCrashHandler(rigger::versionBanner().c_str());
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--crash-test")
            rigger::app::triggerTestCrash();  // test automatisé du gestionnaire
    }

    // HiDPI natif (Qt 6) : facteurs d'Ã©chelle fractionnaires non arrondis.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Le format OpenGL par dÃ©faut doit Ãªtre dÃ©fini AVANT la crÃ©ation de QApplication.
    QSurfaceFormat format;
    format.setVersion(rigger::render::kGlMajor, rigger::render::kGlMinor);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(rigger::render::kMsaaSamples);
    format.setDepthBufferSize(rigger::render::kDepthBits);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QString::fromStdString(std::string(rigger::kAppName)));
    QApplication::setApplicationName(QString::fromStdString(std::string(rigger::kAppName)));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/rigger-256.png")));
    QApplication::setApplicationVersion(QString::fromStdString(std::string(rigger::kVersionString)));

    QCommandLineParser parser;
    parser.setApplicationDescription("Rigger - 3D character animation");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTest("smoke-test", "Open the main window, then exit (used by the CI).");
    parser.addOption(smokeTest);
    const QCommandLineOption screenshot(
        "screenshot", "Save the 3D viewport to a PNG after a short delay, then exit.", "file.png");
    parser.addOption(screenshot);
    const QCommandLineOption windowShot("screenshot-window", "Save the whole window to a PNG, then exit.",
                                        "file.png");
    parser.addOption(windowShot);
    const QCommandLineOption selectOption(
        "select", "Select bones by name (comma separated), for screenshots.", "names");
    parser.addOption(selectOption);
    const QCommandLineOption toolOption("tool", "Active tool: select, move, rotate or scale.", "tool");
    parser.addOption(toolOption);
    const QCommandLineOption modeOption("mode", "Active mode: rig, skin or animate.", "mode");
    parser.addOption(modeOption);
    const QCommandLineOption ikOption(
        "humanoid-ik", "Create the humanoid IK preset and nudge the first target, for screenshots.");
    parser.addOption(ikOption);
    const QCommandLineOption framesOption(
        "export-frames",
        "Export the first frames of the animation as transparent PNGs into a folder, then exit.", "folder");
    parser.addOption(framesOption);
    const QCommandLineOption exportGltfOption(
        "export-gltf", "Export the loaded model to a glTF / GLB file, then exit.", "file.glb");
    parser.addOption(exportGltfOption);
    const QCommandLineOption makeExampleOption(
        "make-example", "Write the procedural example project (CC0) to a .rig file, then exit.", "file.rig");
    parser.addOption(makeExampleOption);
    const QCommandLineOption exampleOption("example", "Open the bundled example character at startup.");
    parser.addOption(exampleOption);
    parser.addPositionalArgument("model", "glTF / GLB file to open at startup.", "[model]");
    parser.process(app);

    if (parser.isSet(makeExampleOption)) {
        rigger::io::Project project;
        project.character = rigger::buildExampleCharacter();
        project.view.clipIndex = 0;
        project.view.yaw = 0.5f;  // 3/4 face, personnage entier (1,8 m) dans le cadre
        project.view.pitch = 0.15f;
        project.view.distance = 4.2f;
        project.view.target = glm::vec3(0.0f, 0.9f, 0.0f);
        const auto saved = rigger::io::saveProject(project, parser.value(makeExampleOption).toStdString());
        if (!saved) {
            spdlog::error("Cannot write example: {}", saved.error().message);
            return 1;
        }
        return 0;
    }

    // Journal persistant (le panneau Console reçoit les mêmes messages et peut les exporter).
    try {
        std::error_code ec;
        std::filesystem::create_directories(rigger::app::logDirectory(), ec);
        spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (rigger::app::logDirectory() / "rigger.log").string(), 1024 * 1024, 3));
    } catch (const spdlog::spdlog_ex& e) {
        spdlog::warn("log file unavailable: {}", e.what());  // le journal fichier est facultatif
    }
    spdlog::info("{} ({})", rigger::versionBanner(), rigger::render::describeRequirements());

    rigger::editor::MainWindow window;
    window.show();
    // Rapports de crash des sessions précédentes : signalés une seule fois (journal + barre de statut).
    if (!parser.isSet(smokeTest) && !parser.isSet(screenshot) && !parser.isSet(windowShot)) {
        for (const auto& report : rigger::app::pendingReports()) {
            spdlog::warn("Rigger did not close properly last time. A crash report was saved: {}",
                         report.string());
            window.postMessage(QObject::tr("A crash report from the previous session was saved: %1")
                                   .arg(QString::fromStdString(report.string())));
            rigger::app::markReported(report);
        }
    }
    // Récupération après un plantage : seulement en usage interactif (pas en CI ni pour les captures).
    if (parser.positionalArguments().isEmpty() && !parser.isSet(exampleOption) && !parser.isSet(smokeTest) &&
        !parser.isSet(screenshot) && !parser.isSet(windowShot)) {
        window.offerRecovery();
    }

    if (!parser.positionalArguments().isEmpty()) {
        const auto imported = window.importFile(parser.positionalArguments().front());
        if (!imported) {
            spdlog::error("Cannot open model: {}", imported.error().message);
            return 1;
        }
    }

    if (parser.isSet(exampleOption)) {
        window.openExample();
    }
    if (parser.isSet(modeOption)) {
        const QString mode = parser.value(modeOption).toLower();
        window.document()->setMode(mode == "rig"    ? rigger::editor::Mode::Rig
                                   : mode == "skin" ? rigger::editor::Mode::Skin
                                                    : rigger::editor::Mode::Animate);
    }
    if (parser.isSet(ikOption) && window.document()->hasCharacter()) {
        auto* doc = window.document();
        if (doc->createHumanoidIK() && !doc->ikControls().empty()) {
            const rigger::BoneId target = doc->ikControls().front().targetBone;
            rigger::Transform moved = doc->boneLocal(target);
            moved.t += glm::vec3(0.0f, 0.25f, 0.15f);
            doc->setBoneLocal(target, moved);
            doc->setSelection({target});
        }
    }
    if (parser.isSet(toolOption)) {
        const QString tool = parser.value(toolOption).toLower();
        window.document()->setTool(tool == "move"     ? rigger::editor::Tool::Move
                                   : tool == "rotate" ? rigger::editor::Tool::Rotate
                                   : tool == "scale"  ? rigger::editor::Tool::Scale
                                                      : rigger::editor::Tool::Select);
    }
    if (parser.isSet(selectOption) && window.document()->skeleton() != nullptr) {
        std::vector<rigger::BoneId> ids;
        for (const QString& name : parser.value(selectOption).split(',', Qt::SkipEmptyParts)) {
            const rigger::BoneId id = window.document()->skeleton()->find(name.trimmed().toStdString());
            if (id != rigger::kNoBone) {
                ids.push_back(id);
            }
        }
        window.document()->setSelection(std::move(ids));
    }

    if (parser.isSet(screenshot)) {
        const QString target = parser.value(screenshot);
        QTimer::singleShot(1500, &app, [&app, &window, target]() {
            const QImage image = window.grabViewport();
            const bool saved = !image.isNull() && image.save(target);
            spdlog::info("Screenshot {}: {}", saved ? "saved" : "FAILED", target.toStdString());
            app.exit(saved ? 0 : 2);
        });
    }

    if (parser.isSet(framesOption)) {
        const QString folder = parser.value(framesOption);
        QTimer::singleShot(1500, &app, [&app, &window, folder]() {
            rigger::editor::ImageSequenceOptions options;
            options.lastFrame = std::max(0, std::min(window.document()->frameCount() - 1, 4));
            const auto result = window.exportImageSequence(folder, QStringLiteral("frame"), options);
            spdlog::info("Frames export {}", result ? "done" : result.error().message);
            app.exit(result ? 0 : 2);
        });
    }
    if (parser.isSet(exportGltfOption)) {
        const auto result = window.exportGltfTo(parser.value(exportGltfOption), {});
        spdlog::info("glTF export {}", result ? "done" : result.error().message);
        QTimer::singleShot(0, &app, [&app, ok = result.ok()] { app.exit(ok ? 0 : 2); });
    }

    if (parser.isSet(windowShot)) {
        const QString target = parser.value(windowShot);
        window.resize(1400, 860);
        QTimer::singleShot(2000, &app, [&app, &window, target]() {
            const bool saved = window.grab().save(target);
            spdlog::info("Window screenshot {}: {}", saved ? "saved" : "FAILED", target.toStdString());
            app.exit(saved ? 0 : 2);
        });
    }

    if (parser.isSet(smokeTest)) {
        QTimer::singleShot(0, &app, &QApplication::quit);
    }
    return QApplication::exec();
}
