// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

// glad doit précéder tout en-tête OpenGL (Qt inclus) : ne pas laisser clang-format réordonner.
// clang-format off
#include <glad/glad.h>
// clang-format on

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

#include "core/skinning/lbs.hpp"
#include "render/gl_loader.hpp"
#include "render/gl_test_context.hpp"
#include "render/gpu_skinning.hpp"
#include "render/mesh_renderer.hpp"
#include "render/shader.hpp"
#include "render/skinning_shader.hpp"

namespace {

bool vectorsClose(const glm::vec3& a, const glm::vec3& b, float epsilon) {
    return glm::length(a - b) <= epsilon;
}

rigger::Mesh makeTestMesh(std::size_t vertexCount) {
    rigger::Mesh mesh;
    mesh.vertices.resize(vertexCount);
    mesh.skinWeights.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const float x = static_cast<float>(i % 251) * 0.01f;
        const float y = static_cast<float>((i / 251) % 251) * 0.01f;
        mesh.vertices[i].position = {x, y, 0.25f};
        mesh.vertices[i].normal = glm::normalize(glm::vec3(x + 1.0f, y + 1.0f, 1.0f));
        auto& skin = mesh.skinWeights[i];
        skin.joints = {0u, 1u, rigger::kNoBone, rigger::kNoBone};
        skin.weights = {0.65f, 0.35f, 0.0f, 0.0f};
    }
    return mesh;
}

std::vector<glm::mat4> makeMatrices() {
    glm::mat4 first = glm::translate(glm::mat4(1.0f), glm::vec3(0.25f, -0.5f, 0.75f));
    first = glm::rotate(first, glm::radians(32.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    first = glm::scale(first, glm::vec3(1.2f, 0.9f, 1.0f));
    glm::mat4 second = glm::translate(glm::mat4(1.0f), glm::vec3(-0.75f, 0.2f, -0.1f));
    second = glm::rotate(second, glm::radians(-57.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    return {first, second};
}

}  // namespace

TEST_CASE("GPU skinning : correspond au LBS CPU sous le seuil de 1e-4") {
    const rigger::Mesh mesh = makeTestMesh(64);
    const auto matrices = makeMatrices();
    const auto cpu = rigger::skinLbs(mesh, matrices);
    const auto gpu = rigger::render::skinVerticesGpu(mesh, matrices);

    REQUIRE(cpu.ok());
    REQUIRE(gpu.ok());
    REQUIRE(cpu.value().size() == gpu.value().vertices.size());
    for (std::size_t i = 0; i < cpu.value().size(); ++i) {
        CHECK(vectorsClose(cpu.value()[i].position, gpu.value().vertices[i].position, 1.0e-4f));
        CHECK(vectorsClose(cpu.value()[i].normal, gpu.value().vertices[i].normal, 1.0e-4f));
    }
}

TEST_CASE("GPU skinning : 200000 sommets et 200 os en moins de 16.67 ms (60 FPS)") {
    constexpr std::size_t kVertexCount = 200000;
    constexpr unsigned kBones = 200;
    std::vector<rigger::render::MeshVertex> vertices(kVertexCount);
    std::vector<std::uint32_t> indices;
    indices.reserve((kVertexCount / 3) * 3);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        auto& vertex = vertices[i];
        vertex.position = {static_cast<float>(i % 251) * 0.01f, static_cast<float>((i / 251) % 251) * 0.01f,
                           0.25f};
        vertex.normal = glm::normalize(glm::vec3(vertex.position.x + 1.0f, vertex.position.y + 1.0f, 1.0f));
        vertex.color = glm::vec3(0.8f, 0.5f, 0.3f);
        vertex.joints = {static_cast<unsigned>(i % kBones), static_cast<unsigned>((i + 1) % kBones), 0u, 0u};
        vertex.weights = {0.65f, 0.35f, 0.0f, 0.0f};
        if (i < (kVertexCount / 3) * 3) {
            indices.push_back(static_cast<std::uint32_t>(i));
        }
    }

    const std::string vertexSource = std::string(R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform int uSkinningEnabled;
uniform int uSelectedBone;
out vec3 vNormal;
out vec3 vColor;
out float vInfluence;
)") + std::string(rigger::render::kSkinningGlsl) +
                                     R"(
void main() {
    gl_Position = vec4(uSkinningEnabled != 0
        ? skinPosition(aPosition, aJoints, aWeights)
        : aPosition, 1.0);
    vNormal = uSkinningEnabled != 0
        ? skinNormal(aNormal, aJoints, aWeights)
        : normalize(aNormal);
    vColor = aColor;
    vInfluence = uSelectedBone >= 0
        ? boneInfluence(aJoints, aWeights, uint(uSelectedBone))
        : 0.0;
}
)";
    constexpr char kFragmentSource[] = R"(
#version 330 core
in vec3 vNormal;
in vec3 vColor;
in float vInfluence;
out vec4 FragColor;
void main() {
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(0.8, 1.0, 0.6))), 0.0);
    FragColor = vec4(vColor * (0.25 + diffuse * 0.75) + vec3(vInfluence * 0.0), 1.0);
}
)";

    rigger::render::Shader shader;
    REQUIRE(shader.compileFromSource(vertexSource, kFragmentSource));
    rigger::render::MeshRenderer renderer;
    renderer.setMesh(vertices, indices);
    std::vector<glm::mat4> matrices;  // 200 os : palette presque complète (limite de 256)
    for (unsigned b = 0; b < kBones; ++b) {
        matrices.push_back(glm::rotate(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.001f * static_cast<float>(b), 0.0f, 0.0f)),
            0.01f * static_cast<float>(b), glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    REQUIRE(renderer.setBoneMatrices(matrices));
    constexpr std::size_t kRuns = 5;
    std::array<GLuint, kRuns> queries{};
    glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
    shader.bind();
    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    renderer.draw(shader, true, rigger::kNoBone);
    std::uint64_t totalGpuTime = 0;
    for (std::size_t run = 0; run < kRuns; ++run) {
        glBeginQuery(GL_TIME_ELAPSED, queries[run]);
        renderer.draw(shader, true, rigger::kNoBone);
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(queries[run], GL_QUERY_RESULT, &elapsed);
        totalGpuTime += elapsed;
    }
    glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
    shader.unbind();
    CHECK(glGetError() == GL_NO_ERROR);
    const double averageMilliseconds = static_cast<double>(totalGpuTime) / static_cast<double>(kRuns) / 1.0e6;
    MESSAGE("GPU skinning render: " << averageMilliseconds << " ms for 200000 vertices, 200 bones");
    CHECK(averageMilliseconds < 16.67);
}

// --- Test de snapshot visuel : rendu hors écran d'une pose de référence comparé à une image de référence
// -------------

namespace {

std::filesystem::path snapshotPath() {
    return std::filesystem::path(RIGGER_TEST_DATA_DIR) / "snapshots" / "skinned_grid.png";
}

// Rendu 128 x 128 d'une grille skinnée sur deux os (pose déformée) : positions directement en espace clip.
QImage renderSnapshot() {
    constexpr int kSize = 128, kGrid = 24;
    std::vector<rigger::render::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int j = 0; j <= kGrid; ++j) {
        for (int i = 0; i <= kGrid; ++i) {
            rigger::render::MeshVertex v;
            const float x = -0.7f + 1.4f * static_cast<float>(i) / kGrid,
                        y = -0.7f + 1.4f * static_cast<float>(j) / kGrid;
            v.position = {x, y, 0.0f};
            v.normal = {0.0f, 0.0f, 1.0f};
            v.color = {0.5f + 0.5f * x, 0.5f + 0.5f * y, 0.6f};
            const float blend = static_cast<float>(j) / kGrid;
            v.joints = {0u, 1u, 0u, 0u};
            v.weights = {1.0f - blend, blend, 0.0f, 0.0f};
            vertices.push_back(v);
        }
    }
    for (int j = 0; j < kGrid; ++j) {
        for (int i = 0; i < kGrid; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j * (kGrid + 1) + i);
            indices.insert(indices.end(), {a, a + 1, a + kGrid + 1, a + 1, a + kGrid + 2, a + kGrid + 1});
        }
    }
    const std::string vertexSource = std::string(R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform int uSkinningEnabled;
uniform int uSelectedBone;
out vec3 vNormal;
out vec3 vColor;
out float vInfluence;
)") + std::string(rigger::render::kSkinningGlsl) +
                                     R"(
void main() {
    gl_Position = vec4(skinPosition(aPosition, aJoints, aWeights), 1.0);
    vNormal = skinNormal(aNormal, aJoints, aWeights);
    vColor = aColor;
    vInfluence = 0.0;
}
)";
    constexpr char kFragmentSource[] = R"(
#version 330 core
in vec3 vNormal;
in vec3 vColor;
in float vInfluence;
out vec4 FragColor;
void main() {
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(0.3, 0.4, 1.0))), 0.0);
    FragColor = vec4(vColor * (0.3 + diffuse * 0.7), 1.0);
}
)";
    rigger::render::Shader shader;
    rigger::render::MeshRenderer renderer;
    if (!shader.compileFromSource(vertexSource, kFragmentSource)) return {};
    renderer.setMesh(vertices, indices);
    const std::vector<glm::mat4> matrices = {
        glm::mat4(1.0f), glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.15f, 0.0f, 0.0f)),
                                     glm::radians(35.0f), glm::vec3(0.0f, 0.0f, 1.0f))};
    if (!renderer.setBoneMatrices(matrices)) return {};

    GLuint fbo = 0, color = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    QImage image;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, kSize, kSize);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shader.bind();
        renderer.draw(shader, true, rigger::kNoBone);
        shader.unbind();
        image = QImage(kSize, kSize, QImage::Format_RGBA8888);
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        image = image.mirrored(false, true);  // OpenGL : origine en bas
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &color);
    glDeleteFramebuffers(1, &fbo);
    return image;
}

}  // namespace

TEST_CASE("snapshot visuel : la pose de référence rendue hors écran reste conforme à l'image de référence") {
    const QImage rendered = renderSnapshot();
    REQUIRE(!rendered.isNull());
    const QString updateFlag = qEnvironmentVariable("RIGGER_UPDATE_SNAPSHOTS");
    const QString path = QString::fromStdString(snapshotPath().string());
    if (!updateFlag.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        REQUIRE(rendered.save(path));
        MESSAGE("snapshot mis à jour : " << path.toStdString());
        return;
    }
    QImage reference(path);
    REQUIRE_MESSAGE(
        !reference.isNull(),
        "image de référence absente (RIGGER_UPDATE_SNAPSHOTS=1 pour la créer) : " << path.toStdString());
    reference = reference.convertToFormat(QImage::Format_RGBA8888);
    REQUIRE(reference.size() == rendered.size());
    // Tolérance : les pilotes GPU diffèrent légèrement (arrondis, arêtes) ; on compare l'écart moyen et la
    // part de pixels très différents.
    double sum = 0.0;
    int outliers = 0;
    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const QRgb a = rendered.pixel(x, y), b = reference.pixel(x, y);
            const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                                    std::abs(qBlue(a) - qBlue(b))});
            sum += d;
            outliers += d > 24 ? 1 : 0;
        }
    }
    const double mean = sum / (rendered.width() * rendered.height());
    MESSAGE("écart moyen " << mean << "/255, pixels très différents " << outliers);
    CHECK(mean < 2.0);
    CHECK(outliers < rendered.width() * rendered.height() / 50);  // moins de 2 %
}

int main(int argc, char** argv) {
    GlTestContext context(argc, argv);
    if (!context.ok()) {
        return 77;  // pas de contexte OpenGL 4.1 : test ignoré (SKIP_RETURN_CODE)
    }
    if (!rigger::render::loadOpenGL(&GlTestContext::procAddress)) {
        std::fprintf(stderr, "Failed to initialize OpenGL functions for GPU skinning tests.\n");
        return 77;
    }

    doctest::Context testContext;
    testContext.applyCommandLine(argc, argv);
    return testContext.run();
}
