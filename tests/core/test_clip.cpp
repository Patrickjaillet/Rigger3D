// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <cmath>

#include "core/animation/clip.hpp"
#include "core/animation/player.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

Keyframe<glm::vec3> key(double t, const glm::vec3& v, Interpolation mode = Interpolation::Linear) {
    Keyframe<glm::vec3> k;
    k.time = t;
    k.value = v;
    k.mode = mode;
    return k;
}

Track<glm::vec3> vecTrack(Interpolation mode) {
    Track<glm::vec3> track;
    track.keys = {key(0.0, {0, 0, 0}, mode), key(1.0, {2, 0, 0}, mode), key(3.0, {2, 4, 0}, mode)};
    return track;
}

}  // namespace

TEST_CASE("sampleTrack : LINEAR interpole et clampe aux extrémités") {
    const auto track = vecTrack(Interpolation::Linear);
    CHECK(sampleTrack(track, -1.0).x == doctest::Approx(0.0f));
    CHECK(sampleTrack(track, 0.5).x == doctest::Approx(1.0f));
    CHECK(sampleTrack(track, 2.0).y == doctest::Approx(2.0f));  // milieu du 2e segment
    CHECK(sampleTrack(track, 10.0).y == doctest::Approx(4.0f));
}

TEST_CASE("sampleTrack : CONSTANT garde la valeur de la clé précédente") {
    const auto track = vecTrack(Interpolation::Constant);
    CHECK(sampleTrack(track, 0.99).x == doctest::Approx(0.0f));
    CHECK(sampleTrack(track, 1.0).x == doctest::Approx(2.0f));
    CHECK(sampleTrack(track, 2.99).y == doctest::Approx(0.0f));
}

TEST_CASE("sampleTrack : le mode est PAR CLÉ (constant puis linéaire puis Bezier)") {
    Track<glm::vec3> track;
    track.keys = {key(0.0, {0, 0, 0}, Interpolation::Constant), key(1.0, {1, 0, 0}, Interpolation::Linear),
                  key(2.0, {2, 0, 0}, Interpolation::Bezier), key(3.0, {3, 0, 0})};
    CHECK(sampleTrack(track, 0.5).x == doctest::Approx(0.0f));  // segment 0 constant
    CHECK(sampleTrack(track, 1.5).x == doctest::Approx(1.5f));  // segment 1 linéaire
    CHECK(sampleTrack(track, 2.5).x ==
          doctest::Approx(2.5f));  // segment 2 Bezier, tangentes nulles : smoothstep
    track.keys[2].outTangent = {3, 0, 0};
    CHECK(sampleTrack(track, 2.25).x > 2.25f);  // tangente de sortie forte : démarre plus vite
}

TEST_CASE("sampleTrack : BEZIER passe par les clés et applique les tangentes") {
    Track<glm::vec3> track;
    track.keys = {key(0.0, {0, 0, 0}, Interpolation::Bezier), key(2.0, {1, 0, 0})};
    CHECK(sampleTrack(track, 0.0).x == doctest::Approx(0.0f));
    CHECK(sampleTrack(track, 2.0).x == doctest::Approx(1.0f));
    CHECK(sampleTrack(track, 1.0).x == doctest::Approx(0.5f));  // tangentes nulles : smoothstep
    CHECK(sampleTrack(track, 0.5).x < 0.25f);                   // démarre lentement
    track.keys[0].outTangent = {1, 0, 0};
    CHECK(sampleTrack(track, 0.5).x > 0.25f);
}

TEST_CASE("sampleTrack : slerp des rotations suit le chemin le plus court, à travers 180°") {
    Track<glm::quat> track;
    Keyframe<glm::quat> a, b;
    a.time = 0.0;
    a.value = glm::quat(1, 0, 0, 0);
    b.time = 1.0;
    b.value = -glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));  // q et -q
    track.keys = {a, b};
    const glm::quat mid = sampleTrack(track, 0.5);
    CHECK(std::fabs(glm::dot(mid, glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1)))) ==
          doctest::Approx(1.0f).epsilon(1e-5));

    // Rotation de 0° à 350° : le chemin le plus court est de -10° (pas +350°).
    b.value = glm::angleAxis(glm::radians(350.0f), glm::vec3(0, 1, 0));
    track.keys = {a, b};
    const glm::quat half = sampleTrack(track, 0.5);
    CHECK(std::fabs(glm::dot(half, glm::angleAxis(glm::radians(-5.0f), glm::vec3(0, 1, 0)))) ==
          doctest::Approx(1.0f).epsilon(1e-5));
    // Rotation exactement à travers 180° : continue, normalisée, sans saut.
    b.value = glm::angleAxis(glm::radians(180.0f), glm::vec3(0, 0, 1));
    track.keys = {a, b};
    glm::quat prev = sampleTrack(track, 0.0);
    for (int i = 1; i <= 100; ++i) {
        const glm::quat q = sampleTrack(track, i / 100.0);
        CHECK(glm::length(q) == doctest::Approx(1.0f));
        CHECK(std::fabs(glm::dot(prev, q)) > 0.99f);  // pas de saut entre deux échantillons
        prev = q;
    }
}

TEST_CASE("sampleTrack : rotation Bezier garde l'hémisphère et reste normalisée") {
    Track<glm::quat> track;
    Keyframe<glm::quat> a, b;
    a.time = 0.0;
    a.value = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 0, 1));
    a.mode = Interpolation::Bezier;
    b.time = 1.0;
    b.value = -glm::angleAxis(glm::radians(80.0f), glm::vec3(0, 0, 1));  // même rotation, signe opposé
    track.keys = {a, b};
    for (int i = 0; i <= 20; ++i) {
        const glm::quat q = sampleTrack(track, i / 20.0);
        CHECK(glm::length(q) == doctest::Approx(1.0f));
        float angle = glm::degrees(glm::angle(q));
        if (angle > 180.0f) angle = 360.0f - angle;  // q et -q désignent la même rotation
        CHECK(angle >= 9.9f);
        CHECK(angle <= 80.1f);  // jamais de détour par l'autre hémisphère
    }
}

TEST_CASE("sampleClip : bind pose écrasée par les pistes, boucle sans saut") {
    Skeleton skeleton;
    Transform rest;
    rest.t = {0, 1, 0};
    const BoneId a = skeleton.addBone("a", kNoBone, rest).value();
    const BoneId b = skeleton.addBone("b", a, rest).value();

    AnimationClip clip;
    clip.duration = 2.0;
    clip.tracks[b].translation = vecTrack(Interpolation::Linear);

    Pose pose;
    sampleClip(clip, skeleton, 0.5, pose);
    CHECK(pose[a].t.y == doctest::Approx(1.0f));  // non animé : bind
    CHECK(pose[b].t.x == doctest::Approx(1.0f));

    sampleClip(clip, skeleton, 2.5, pose);  // loop : 2.5 -> 0.5
    CHECK(pose[b].t.x == doctest::Approx(1.0f));

    // Bornes : la durée elle-même est la dernière image (pas de retour à 0), au-delà on boucle.
    sampleClip(clip, skeleton, 2.0, pose);
    CHECK(pose[b].t.y == doctest::Approx(2.0f));
    sampleClip(clip, skeleton, 2.0001, pose);
    CHECK(pose[b].t.x == doctest::Approx(0.0f).epsilon(1e-3));

    clip.loop = false;
    sampleClip(clip, skeleton, 2.5, pose);
    CHECK(pose[b].t.y == doctest::Approx(3.0f));
    CHECK(samplePose(clip, skeleton, 0.0)[b].t.x == doctest::Approx(0.0f));
}

TEST_CASE("échantillonnage : recherche binaire sur 100 000 clés, résultat exact") {
    Track<glm::vec3> track;
    for (int i = 0; i < 100000; ++i) {
        track.keys.push_back(key(i * 0.01, {static_cast<float>(i), 0, 0}));
    }
    CHECK(sampleTrack(track, 500.005).x == doctest::Approx(50000.5f).epsilon(1e-5));
    CHECK(sampleTrack(track, 0.0).x == doctest::Approx(0.0f));
    CHECK(sampleTrack(track, 1e9).x == doctest::Approx(99999.0f));
}

TEST_CASE("édition de pistes : findKey / insertKey / eraseKeyAt / normalizeTrack") {
    Track<glm::vec3> track;
    CHECK_FALSE(findKey(track, 1.0).has_value());
    insertKey(track, key(2.0, {2, 0, 0}));
    insertKey(track, key(0.0, {0, 0, 0}));
    CHECK(insertKey(track, key(1.0, {1, 0, 0})) == 1);  // insérée au bon rang
    CHECK(track.keys.size() == 3);
    CHECK(insertKey(track, key(1.0002, {9, 0, 0})) == 1);  // dans la tolérance : remplace
    CHECK(track.keys.size() == 3);
    CHECK(track.keys[1].value.x == 9.0f);
    CHECK(findKey(track, 2.0004).has_value());
    CHECK_FALSE(findKey(track, 2.01).has_value());
    CHECK(eraseKeyAt(track, 0.0));
    CHECK_FALSE(eraseKeyAt(track, 0.0));
    CHECK(track.keys.size() == 2);

    // normalizeTrack : retrie après un déplacement direct et fusionne les clés confondues.
    track.keys = {key(3.0, {3, 0, 0}), key(1.0, {1, 0, 0}), key(1.0001, {7, 0, 0}), key(2.0, {2, 0, 0})};
    normalizeTrack(track);
    REQUIRE(track.keys.size() == 3);
    CHECK(track.keys[0].time == doctest::Approx(1.0).epsilon(1e-3));
    CHECK(track.keys[0].value.x == 7.0f);  // la dernière rencontrée gagne
    CHECK(track.keys.back().time == 3.0);
}

TEST_CASE("autoTangents : pente nulle aux extrémités et extrema, sinon Catmull-Rom") {
    Track<glm::vec3> track;
    track.keys = {key(0.0, {0, 0, 0}, Interpolation::Bezier), key(1.0, {1, 5, 0}, Interpolation::Bezier),
                  key(2.0, {2, 2, 0}, Interpolation::Bezier), key(3.0, {3, 3, 0}, Interpolation::Bezier)};
    autoTangents(track);
    CHECK(near(track.keys[0].outTangent, {0, 0, 0}));            // extrémité
    CHECK(track.keys[1].outTangent.x == doctest::Approx(1.0f));  // (2 - 0) / 2
    CHECK(track.keys[1].outTangent.y == doctest::Approx(0.0f));  // extremum local en y
    CHECK(track.keys[2].inTangent.x == doctest::Approx(1.0f));
    CHECK(track.keys[2].outTangent.y == doctest::Approx(0.0f));  // 5 -> 2 -> 3 : minimum
    CHECK(near(track.keys[3].inTangent, {0, 0, 0}));
    // Pas de dépassement : la courbe reste entre les valeurs voisines.
    for (int i = 0; i <= 100; ++i) {
        const float y = sampleTrack(track, 1.0 + i / 100.0).y;
        CHECK(y <= 5.0f + 1e-4f);
        CHECK(y >= 2.0f - 1e-4f);
    }
    Track<glm::quat> rot;
    Keyframe<glm::quat> k;
    k.mode = Interpolation::Bezier;
    for (int i = 0; i < 4; ++i) {
        k.time = i;
        k.value = glm::angleAxis(glm::radians(30.0f * static_cast<float>(i)), glm::vec3(0, 0, 1));
        rot.keys.push_back(k);
    }
    autoTangents(rot);
    CHECK(glm::length(sampleTrack(rot, 1.5)) == doctest::Approx(1.0f));
}

TEST_CASE("durée : extendDuration / lastKeyTime") {
    AnimationClip clip;
    clip.duration = 1.0;
    CHECK(lastKeyTime(clip) == 0.0);
    clip.tracks[0].translation.keys = {key(0.0, {0, 0, 0}), key(2.5, {1, 0, 0})};
    CHECK(lastKeyTime(clip) == 2.5);
    extendDuration(clip);
    CHECK(clip.duration == 2.5);
    clip.duration = 9.0;
    extendDuration(clip);
    CHECK(clip.duration == 9.0);  // ne raccourcit jamais
}

// --- Lecteur -------------------------------------------------------------------------------------

TEST_CASE("lecteur : lecture, pause, arrêt, fin hors boucle") {
    AnimationPlayer p;
    p.setDuration(2.0);
    p.setLoop(false);
    CHECK_FALSE(p.isPlaying());
    CHECK_FALSE(p.advance(0.5));  // à l'arrêt : le temps ne bouge pas
    CHECK(p.time() == 0.0);

    p.play();
    CHECK(p.isPlaying());
    p.advance(0.5);
    CHECK(p.time() == doctest::Approx(0.5));
    p.pause();
    p.advance(1.0);
    CHECK(p.time() == doctest::Approx(0.5));
    p.play();
    p.advance(5.0);  // dépasse la fin : s'arrête sur la borne
    CHECK(p.time() == doctest::Approx(2.0));
    CHECK_FALSE(p.isPlaying());
    p.play();  // relancer à la fin repart du début
    CHECK(p.time() == doctest::Approx(0.0));
    p.advance(0.25);
    p.stop();
    CHECK(p.time() == 0.0);
    CHECK_FALSE(p.isPlaying());
}

TEST_CASE("lecteur : boucle continue sans saut, y compris sur de grands pas") {
    AnimationPlayer p;
    p.setDuration(2.0);
    p.setLoop(true);
    p.play();
    p.setTime(1.9);
    CHECK(p.advance(0.3));
    CHECK(p.time() == doctest::Approx(0.2));  // 1.9 + 0.3 - 2.0 : le reste du pas est conservé
    CHECK(p.isPlaying());
    p.advance(4.5);  // plus de deux tours
    CHECK(p.time() == doctest::Approx(0.7));
    double prev = p.time();
    for (int i = 0; i < 500; ++i) {
        p.advance(1.0 / 60.0);
        const double d = std::fmod(p.time() - prev + 2.0, 2.0);
        CHECK(d == doctest::Approx(1.0 / 60.0).epsilon(1e-6));  // toujours le même incrément (mod durée)
        prev = p.time();
    }
}

TEST_CASE("lecteur : vitesse bornée à [-2, 4], lecture à l'envers") {
    AnimationPlayer p;
    p.setDuration(10.0);
    p.setSpeed(100.0);
    CHECK(p.speed() == 4.0);
    p.setSpeed(-100.0);
    CHECK(p.speed() == -2.0);
    p.setLoop(false);
    p.setTime(5.0);
    p.play();
    p.advance(1.0);
    CHECK(p.time() == doctest::Approx(3.0));  // -2 x
    p.advance(10.0);
    CHECK(p.time() == doctest::Approx(0.0));
    CHECK_FALSE(p.isPlaying());
    p.setSpeed(2.0);
    p.play();
    p.advance(1.0);
    CHECK(p.time() == doctest::Approx(2.0));
    p.setSpeed(-1.0);
    p.stop();
    CHECK(p.time() == doctest::Approx(10.0));  // arrêt = fin de plage pour une vitesse négative
}

TEST_CASE("lecteur : plage de lecture, boucle dans la plage") {
    AnimationPlayer p;
    p.setDuration(10.0);
    p.setRange(2.0, 4.0);
    CHECK(p.hasCustomRange());
    CHECK(p.rangeStart() == 2.0);
    CHECK(p.rangeEnd() == 4.0);
    p.setTime(3.5);
    p.setLoop(true);
    p.play();
    p.advance(1.0);  // 4.5 -> boucle dans [2, 4] : 2.5
    CHECK(p.time() == doctest::Approx(2.5));
    p.setRange(0.0, -1.0);  // plage par défaut : tout le clip
    CHECK(p.rangeEnd() == 10.0);
    p.setRange(8.0, 20.0);  // bornée à la durée
    CHECK(p.rangeEnd() == 10.0);
    p.setRange(9.0, 3.0);  // plage inversée : réduite à un point
    CHECK(p.rangeEnd() >= p.rangeStart());
}

TEST_CASE("lecteur : ping-pong rebondit aux bornes et inverse le sens") {
    AnimationPlayer p;
    p.setDuration(2.0);
    p.setPingPong(true);
    p.play();
    p.setTime(1.8);
    CHECK(p.advance(0.5));  // 2.3 -> rebond : 1.7, direction inversée
    CHECK(p.time() == doctest::Approx(1.7));
    p.advance(0.5);
    CHECK(p.time() == doctest::Approx(1.2));  // continue vers le début
    p.advance(1.4);                           // 1.2 - 1.4 = -0.2 -> rebond : 0.2, repart vers l'avant
    CHECK(p.time() == doctest::Approx(0.2));
    p.advance(0.3);
    CHECK(p.time() == doctest::Approx(0.5));
    p.advance(5.0);  // plusieurs rebonds dans le même pas : reste dans la plage
    CHECK(p.time() >= 0.0);
    CHECK(p.time() <= 2.0);
}
