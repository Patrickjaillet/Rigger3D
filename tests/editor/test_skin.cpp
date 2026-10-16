// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <cmath>

#include "core/skinning/paint.hpp"
#include "editor/Document.hpp"
#include "editor/SkinPanel.hpp"
#include "editor/Translations.hpp"
#include "editor_test_support.hpp"

using namespace rigger;
using namespace rigger::editor;
using rigger::testing::loadCharacter;

namespace {

std::vector<SkinWeight> weightsOf(const Document& doc) { return doc.character()->meshes[0].skinWeights; }

bool sameWeights(const std::vector<SkinWeight>& a, const std::vector<SkinWeight>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].joints != b[i].joints || a[i].weights != b[i].weights) return false;
    }
    return true;
}

bool allValid(const std::vector<SkinWeight>& w) {
    for (const SkinWeight& s : w) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            if (!std::isfinite(s.weights[k]) || s.weights[k] < 0.0f || s.weights[k] > 1.0f + 1e-6f)
                return false;
            sum += s.weights[k];
        }
        if (std::abs(sum - 1.0f) > 1e-4f) return false;
    }
    return true;
}

struct SkinScene {
    Document doc;
    BoneId bone = kNoBone;
    glm::vec3 spot{0.0f};
    SkinScene() {
        doc.setCharacter(loadCharacter("RiggedFigure.glb"));
        doc.setPlaying(false);
        doc.setMode(Mode::Skin);
        bone = doc.skeleton()->find("arm_joint_L_2");
        doc.setSelection({bone});
        spot = doc.skinnedVertices(0)[10].position;
        Brush b;
        b.radius = 0.25f;
        b.strength = 1.0f;
        b.falloff = Falloff::Constant;
        b.mode = BrushMode::Add;
        doc.setBrush(b);
        doc.setPaintOnlyVisible(false);
    }
};

}  // namespace

class SkinTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QSettings().clear();
        QSettings().setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    }

    void strokeIsOneUndoStepWithExactDelta() {
        SkinScene s;
        const auto before = weightsOf(s.doc);
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(s.doc.beginPaintStroke());
        for (int i = 0; i < 5; ++i)
            s.doc.paintAt(s.spot + glm::vec3(0.01f * static_cast<float>(i), 0, 0), {0, 0, 10});
        s.doc.endPaintStroke();
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QCOMPARE(s.doc.undoText(), QStringLiteral("Paint Weights (arm_joint_L_2)"));
        const auto painted = weightsOf(s.doc);
        QVERIFY(!sameWeights(before, painted));
        QVERIFY(allValid(painted));
        QVERIFY(influenceOf(painted[10], s.bone) > 0.99f);
        s.doc.undo();
        QVERIFY(sameWeights(before, weightsOf(s.doc)));  // delta exact
        s.doc.redo();
        QVERIFY(sameWeights(painted, weightsOf(s.doc)));
    }

    void lockedBonesAreNeverModified() {
        SkinScene s;
        const auto before = weightsOf(s.doc);
        // Verrouille tous les os sauf l'os actif : aucun poids ne peut bouger (la masse est verrouillée).
        for (const BoneId id : s.doc.skeleton()->order()) {
            if (id != s.bone) s.doc.setWeightLocked(id, true);
        }
        QVERIFY(s.doc.beginPaintStroke());
        s.doc.paintAt(s.spot, {0, 0, 10});
        s.doc.endPaintStroke();
        for (std::size_t v = 0; v < before.size(); ++v) {
            for (const BoneId id : s.doc.skeleton()->order()) {
                if (id != s.bone) QCOMPARE(influenceOf(weightsOf(s.doc)[v], id), influenceOf(before[v], id));
            }
        }
        QVERIFY(allValid(weightsOf(s.doc)));
    }

    void emptyStrokeLeavesNoHistoryEntry() {
        SkinScene s;
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(s.doc.beginPaintStroke());
        s.doc.paintAt(glm::vec3(100.0f), {0, 0, 10});  // loin du maillage
        s.doc.endPaintStroke();
        QCOMPARE(s.doc.history().undoCount(), undo);
    }

    void paintNeedsAnActiveBone() {
        SkinScene s;
        s.doc.setSelection({});
        QVERIFY(!s.doc.beginPaintStroke());
    }

    void exactWeightOnSelectedVertices() {
        SkinScene s;
        s.doc.setVertexSelection({{0, 10}, {0, 11}});
        QVERIFY(s.doc.setSelectedVertexWeight(0.4f));
        QVERIFY(std::abs(influenceOf(weightsOf(s.doc)[10], s.bone) - 0.4f) < 1e-5f);
        QVERIFY(std::abs(influenceOf(weightsOf(s.doc)[11], s.bone) - 0.4f) < 1e-5f);
        QVERIFY(allValid(weightsOf(s.doc)));
        s.doc.undo();
        QVERIFY(influenceOf(weightsOf(s.doc)[10], s.bone) != 0.4f);
    }

    void globalToolsAreUndoable() {
        SkinScene s;
        const auto original = weightsOf(s.doc);
        const auto skinned = s.doc.autoSkin();
        QVERIFY(bool(skinned));
        QVERIFY(allValid(weightsOf(s.doc)));
        QCOMPARE(s.doc.undoText(), QStringLiteral("Auto Skin"));
        s.doc.undo();
        QVERIFY(sameWeights(original, weightsOf(s.doc)));

        // Désordre volontaire puis outils : normaliser, limiter à 2, élaguer.
        const std::size_t undo = s.doc.history().undoCount();
        QVERIFY(bool(s.doc.limitSkinInfluences(2)));
        for (const SkinWeight& w : weightsOf(s.doc)) {
            int n = 0;
            for (const float x : w.weights) n += x > 0.0f;
            QVERIFY(n <= 2);
        }
        QVERIFY(bool(s.doc.normalizeSkinWeights()));
        QVERIFY(bool(s.doc.pruneSkinWeights(0.05f)));
        QVERIFY(allValid(weightsOf(s.doc)));
        QVERIFY(s.doc.history().undoCount() >= undo);
    }

    void raycastHitsThePosedSurface() {
        SkinScene s;
        const glm::vec3 target = s.doc.skinnedVertices(0)[10].position;
        const Ray ray = Ray::make(target + glm::vec3(0, 0, 5), glm::vec3(0, 0, -1));
        const auto hit = s.doc.raycastSurface(ray);
        QVERIFY(hit.has_value());
        QVERIFY(glm::length(*hit - target) < 0.5f);
        QVERIFY(!s.doc.raycastSurface(Ray::make({50, 50, 50}, {0, 0, 1})).has_value());
    }

    void panelDrivesTheBrushAndTools() {
        SkinScene s;
        SkinPanel panel(&s.doc);
        auto* paint = panel.findChild<QPushButton*>("skinPaint");
        auto* radius = panel.findChild<QDoubleSpinBox*>("skinRadius");
        auto* mode = panel.findChild<QComboBox*>("skinMode");
        auto* autoSkin = panel.findChild<QPushButton*>("skinAuto");
        auto* apply = panel.findChild<QPushButton*>("skinApplyWeight");
        auto* exact = panel.findChild<QDoubleSpinBox*>("skinExactWeight");
        QVERIFY(paint && radius && mode && autoSkin && apply && exact);
        paint->click();
        QVERIFY(s.doc.paintActive());
        radius->setValue(0.5);
        QVERIFY(std::abs(s.doc.brush().radius - 0.5f) < 1e-5f);
        mode->setCurrentIndex(1);
        emit mode->activated(1);
        QCOMPARE(static_cast<int>(s.doc.brush().mode), static_cast<int>(BrushMode::Subtract));
        QVERIFY(!apply->isEnabled());  // pas de sommets sélectionnés
        s.doc.setVertexSelection({{0, 3}});
        QVERIFY(apply->isEnabled());
        exact->setValue(0.5);
        apply->click();
        QVERIFY(std::abs(influenceOf(weightsOf(s.doc)[3], s.bone) - 0.5f) < 1e-5f);
        const std::size_t undo = s.doc.history().undoCount();
        autoSkin->click();
        QCOMPARE(s.doc.history().undoCount(), undo + 1);
        QVERIFY(allValid(weightsOf(s.doc)));
    }

    void heatDiffusionAutoSkinIsValidAndUndoable() {
        SkinScene s;
        const auto original = weightsOf(s.doc);
        AutoSkinOptions options;
        options.method = AutoSkinMethod::HeatDiffusion;
        const auto result = s.doc.autoSkin(options);
        QVERIFY(bool(result));
        QVERIFY(allValid(weightsOf(s.doc)));
        QVERIFY(!sameWeights(original, weightsOf(s.doc)));
        s.doc.undo();
        QVERIFY(sameWeights(original, weightsOf(s.doc)));
    }

    void selectionShapeIsRemembered() {
        SkinScene s;
        QCOMPARE(static_cast<int>(s.doc.vertexSelectShape()), static_cast<int>(VertexSelectShape::Rectangle));
        s.doc.setVertexSelectShape(VertexSelectShape::Lasso);
        QCOMPARE(static_cast<int>(s.doc.vertexSelectShape()), static_cast<int>(VertexSelectShape::Lasso));
    }

    void mirroredStrokePaintsTheOppositeSideToo() {
        SkinScene s;
        const BoneId left = s.doc.skeleton()->find("arm_joint_L_1");
        const BoneId right = s.doc.skeleton()->find("arm_joint_R_1");
        QVERIFY(left != kNoBone && right != kNoBone);
        s.doc.setSelection({left});
        s.doc.setSymmetryX(true);
        // Un sommet du côté gauche de l'os gauche.
        const auto verts = s.doc.skinnedVertices(0);
        std::uint32_t pick = 0;
        for (std::uint32_t v = 0; v < verts.size(); ++v) {
            if (influenceOf(weightsOf(s.doc)[v], left) > 0.5f) {
                pick = v;
                break;
            }
        }
        QVERIFY(influenceOf(weightsOf(s.doc)[pick], left) > 0.5f);
        Brush b = s.doc.brush();
        b.mode = BrushMode::Replace;
        b.value = 1.0f;
        b.radius = 0.1f;
        s.doc.setBrush(b);
        QVERIFY(s.doc.beginPaintStroke());
        s.doc.paintAt(verts[pick].position, {0, 0, 10});
        s.doc.endPaintStroke();
        QVERIFY(allValid(weightsOf(s.doc)));
        // Au moins un sommet de l'os droit a reçu du poids.
        bool any = false;
        for (const SkinWeight& w : weightsOf(s.doc)) any = any || influenceOf(w, right) > 0.9f;
        QVERIFY(any);
    }
};

RIGGER_REGISTER_QTEST(SkinTest);
#include "test_skin.moc"
