// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "io/project.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

namespace rigger::io {

namespace {

using Json = nlohmann::json;

// --- base64 ----------------------------------------------------------------------------------------

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode64(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = static_cast<std::uint8_t>(bytes[i]);
        const std::uint32_t b1 = i + 1 < bytes.size() ? static_cast<std::uint8_t>(bytes[i + 1]) : 0;
        const std::uint32_t b2 = i + 2 < bytes.size() ? static_cast<std::uint8_t>(bytes[i + 2]) : 0;
        const std::uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

bool decode64(const std::string& text, std::string& out) {
    static const std::array<std::int8_t, 256> table = [] {
        std::array<std::int8_t, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i) t[static_cast<std::uint8_t>(kAlphabet[i])] = static_cast<std::int8_t>(i);
        return t;
    }();
    if (text.size() % 4 != 0) return false;
    out.clear();
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        std::uint32_t n = 0;
        int pad = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=') {
                if (i + 4 != text.size() || k < 2) return false;
                ++pad;
                n <<= 6;
                continue;
            }
            if (pad > 0 || table[static_cast<std::uint8_t>(c)] < 0) return false;
            n = (n << 6) | static_cast<std::uint32_t>(table[static_cast<std::uint8_t>(c)]);
        }
        out += static_cast<char>((n >> 16) & 255);
        if (pad < 2) out += static_cast<char>((n >> 8) & 255);
        if (pad < 1) out += static_cast<char>(n & 255);
    }
    return true;
}

// --- tampons binaires (little-endian) -----------------------------------------------------------------

class Writer {
public:
    void f32(float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes_ += static_cast<char>((v >> (8 * i)) & 255);
    }
    void u8(std::uint8_t v) { bytes_ += static_cast<char>(v); }
    const std::string& bytes() const { return bytes_; }

private:
    std::string bytes_;
};

class Reader {
public:
    explicit Reader(const std::string& bytes) : bytes_(bytes) {}
    bool ok() const { return !failed_; }
    bool done() const { return pos_ == bytes_.size(); }
    std::uint32_t u32() {
        if (pos_ + 4 > bytes_.size()) {
            failed_ = true;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[pos_ + i])) << (8 * i);
        pos_ += 4;
        return v;
    }
    float f32() {
        const std::uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    std::uint8_t u8() {
        if (pos_ >= bytes_.size()) {
            failed_ = true;
            return 0;
        }
        return static_cast<std::uint8_t>(bytes_[pos_++]);
    }

private:
    const std::string& bytes_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

// --- JSON <-> types simples -----------------------------------------------------------------------------

Json vec3Json(const glm::vec3& v) { return Json::array({v.x, v.y, v.z}); }
Json quatJson(const glm::quat& q) { return Json::array({q.w, q.x, q.y, q.z}); }
Json boneJson(BoneId id) { return id == kNoBone ? Json(-1) : Json(static_cast<std::int64_t>(id)); }

Json transformJson(const Transform& t) {
    return Json{{"t", vec3Json(t.t)}, {"r", quatJson(t.r)}, {"s", vec3Json(t.s)}};
}

bool finite(float v) { return std::isfinite(v); }

// Les lecteurs renvoient false (et laissent une valeur neutre) si le JSON n'a pas la forme attendue.
bool readVec3(const Json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (!j[static_cast<std::size_t>(i)].is_number()) return false;
        out[i] = j[static_cast<std::size_t>(i)].get<float>();
        if (!finite(out[i])) return false;
    }
    return true;
}

bool readQuat(const Json& j, glm::quat& out) {
    if (!j.is_array() || j.size() != 4) return false;
    float v[4];
    for (std::size_t i = 0; i < 4; ++i) {
        if (!j[i].is_number()) return false;
        v[i] = j[i].get<float>();
        if (!finite(v[i])) return false;
    }
    out = glm::quat(v[0], v[1], v[2], v[3]);
    const float len = glm::length(out);
    if (len < 1e-6f) return false;
    out = out / len;
    return true;
}

bool readTransform(const Json& j, Transform& out) {
    return j.is_object() && j.contains("t") && j.contains("r") && j.contains("s") &&
           readVec3(j["t"], out.t) && readQuat(j["r"], out.r) && readVec3(j["s"], out.s);
}

BoneId readBone(const Json& j) {
    if (!j.is_number_integer()) return kNoBone;
    const auto v = j.get<std::int64_t>();
    return v < 0 ? kNoBone : static_cast<BoneId>(v);
}

Error bad(const std::string& what) { return Error{"Invalid project file: " + what}; }

// --- sérialisation ---------------------------------------------------------------------------------------

Json skeletonJson(const Skeleton& sk) {
    Json bones = Json::array();
    for (const BoneId id : sk.order()) {
        const Bone& b = sk.bone(id);
        bones.push_back(Json{{"id", b.id},
                             {"name", b.name},
                             {"parent", boneJson(b.parent)},
                             {"bind", transformJson(b.bindLocal)},
                             {"length", b.length},
                             {"flags", static_cast<std::uint32_t>(b.flags)},
                             {"color", vec3Json(b.color)}});
    }
    return Json{{"bones", std::move(bones)}};
}

template <class T>
Json trackJson(const Track<T>& track, int components) {
    Json times = Json::array(), values = Json::array(), in = Json::array(), out = Json::array(),
         modes = Json::array();
    const auto push = [&](Json& arr, const T& v) {
        if constexpr (std::is_same_v<T, glm::quat>) {
            arr.push_back(v.w), arr.push_back(v.x), arr.push_back(v.y), arr.push_back(v.z);
        } else {
            for (int i = 0; i < components; ++i) arr.push_back(v[i]);
        }
    };
    for (const auto& k : track.keys) {
        times.push_back(k.time);
        push(values, k.value);
        push(in, k.inTangent);
        push(out, k.outTangent);
        modes.push_back(static_cast<int>(k.mode));
    }
    return Json{{"time", times}, {"value", values}, {"in", in}, {"out", out}, {"mode", modes}};
}

Json clipJson(const AnimationClip& clip) {
    Json tracks = Json::array();
    std::vector<BoneId> ids;
    for (const auto& [id, t] : clip.tracks) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const BoneId id : ids) {
        const BoneTracks& t = clip.tracks.at(id);
        tracks.push_back(Json{{"bone", id},
                              {"translation", trackJson(t.translation, 3)},
                              {"rotation", trackJson(t.rotation, 4)},
                              {"scale", trackJson(t.scale, 3)}});
    }
    Json markers = Json::array();
    for (const Marker& m : clip.markers) markers.push_back(Json{{"time", m.time}, {"name", m.name}});
    return Json{{"name", clip.name},
                {"duration", clip.duration},
                {"fps", clip.fps},
                {"loop", clip.loop},
                {"rootMotion", clip.rootMotion},
                {"rootBone", boneJson(clip.rootBone)},
                {"markers", markers},
                {"tracks", tracks}};
}

Json ikJson(const IKControl& c) {
    Json limits = Json::array();
    for (const JointLimit& l : c.chain.limits) {
        limits.push_back(Json{{"type", static_cast<int>(l.type)},
                              {"axis", vec3Json(l.axis)},
                              {"min", l.min},
                              {"max", l.max},
                              {"swing", l.swing}});
    }
    Json bones = Json::array();
    for (const BoneId b : c.chain.bones) bones.push_back(b);
    Json locks = Json::array();
    for (const FootLock& l : c.locks) {
        locks.push_back(Json{{"start", l.start}, {"end", l.end}, {"world", transformJson(l.world)}});
    }
    return Json{{"name", c.name},
                {"kind", static_cast<int>(c.kind)},
                {"aimAxis", vec3Json(c.aimAxis)},
                {"locks", locks},
                {"chain", Json{{"bones", bones},
                               {"effector", boneJson(c.chain.effector)},
                               {"solver", static_cast<int>(c.chain.solver)},
                               {"limits", limits},
                               {"maxIterations", c.chain.maxIterations},
                               {"tolerance", c.chain.tolerance},
                               {"damping", c.chain.damping},
                               {"effectorOrientation", static_cast<int>(c.chain.effectorOrientation)}}},
                {"targetBone", boneJson(c.targetBone)},
                {"poleBone", boneJson(c.poleBone)},
                {"poleTwist", c.poleTwist},
                {"weight", c.weight},
                {"enabled", c.enabled}};
}

Json mixJson(const AnimationMix& mix) {
    Json layers = Json::array();
    for (const AnimationLayer& l : mix.layers) {
        Json mask = Json::array();
        for (const BoneId b : l.mask) mask.push_back(b);
        layers.push_back(Json{{"name", l.name},
                              {"clip", l.clip},
                              {"weight", l.weight},
                              {"mode", static_cast<int>(l.mode)},
                              {"mask", mask},
                              {"speed", l.speed},
                              {"timeOffset", l.timeOffset},
                              {"loop", l.loop},
                              {"enabled", l.enabled}});
    }
    Json strips = Json::array();
    for (const NlaStrip& s : mix.strips) {
        strips.push_back(Json{{"name", s.name},
                              {"clip", s.clip},
                              {"track", s.track},
                              {"start", s.start},
                              {"clipStart", s.clipStart},
                              {"clipEnd", s.clipEnd},
                              {"speed", s.speed},
                              {"repeat", s.repeat},
                              {"blendIn", s.blendIn},
                              {"blendOut", s.blendOut},
                              {"weight", s.weight},
                              {"mode", static_cast<int>(s.mode)},
                              {"enabled", s.enabled}});
    }
    Json entries = Json::array();
    for (const BlendTreeEntry& e : mix.tree.entries)
        entries.push_back(Json{{"clip", e.clip}, {"threshold", e.threshold}});
    return Json{{"mode", static_cast<int>(mix.mode)},
                {"layers", layers},
                {"strips", strips},
                {"tree", Json{{"parameterName", mix.tree.parameterName},
                              {"parameter", mix.tree.parameter},
                              {"syncTime", mix.tree.syncTime},
                              {"entries", entries}}}};
}

Result<AnimationMix> readMix(const Json& j, std::size_t clipCount) {
    AnimationMix mix;
    if (!j.is_object()) return mix;  // projets sans mélange
    try {
        const int mode = j.at("mode").get<int>();
        if (mode < 0 || mode > 3) return bad("mix mode");
        mix.mode = static_cast<MixMode>(mode);
        const auto clipOk = [&](int c) { return c >= 0 && static_cast<std::size_t>(c) < clipCount; };
        for (const Json& l : j.at("layers")) {
            AnimationLayer layer;
            layer.name = l.at("name").get<std::string>();
            layer.clip = l.at("clip").get<int>();
            layer.weight = l.at("weight").get<float>();
            const int lm = l.at("mode").get<int>();
            layer.speed = l.at("speed").get<float>();
            layer.timeOffset = l.at("timeOffset").get<float>();
            layer.loop = l.at("loop").get<bool>();
            layer.enabled = l.at("enabled").get<bool>();
            for (const Json& b : l.at("mask")) layer.mask.push_back(readBone(b));
            if (lm < 0 || lm > 1 || !clipOk(layer.clip) || !finite(layer.weight) || !finite(layer.speed) ||
                !finite(layer.timeOffset)) {
                return bad("animation layer");
            }
            layer.mode = static_cast<LayerMode>(lm);
            mix.layers.push_back(std::move(layer));
        }
        for (const Json& s : j.at("strips")) {
            NlaStrip strip;
            strip.name = s.at("name").get<std::string>();
            strip.clip = s.at("clip").get<int>();
            strip.track = s.at("track").get<int>();
            strip.start = s.at("start").get<double>();
            strip.clipStart = s.at("clipStart").get<double>();
            strip.clipEnd = s.at("clipEnd").get<double>();
            strip.speed = s.at("speed").get<float>();
            strip.repeat = s.at("repeat").get<float>();
            strip.blendIn = s.at("blendIn").get<double>();
            strip.blendOut = s.at("blendOut").get<double>();
            strip.weight = s.at("weight").get<float>();
            const int sm = s.at("mode").get<int>();
            strip.enabled = s.at("enabled").get<bool>();
            if (sm < 0 || sm > 1 || !clipOk(strip.clip) || !std::isfinite(strip.start) ||
                !std::isfinite(strip.clipStart) || !std::isfinite(strip.blendIn) ||
                !std::isfinite(strip.blendOut) || !finite(strip.speed) || !finite(strip.repeat) ||
                !finite(strip.weight)) {
                return bad("NLA strip");
            }
            strip.mode = static_cast<LayerMode>(sm);
            mix.strips.push_back(std::move(strip));
        }
        const Json& tree = j.at("tree");
        mix.tree.parameterName = tree.at("parameterName").get<std::string>();
        mix.tree.parameter = tree.at("parameter").get<float>();
        mix.tree.syncTime = tree.at("syncTime").get<bool>();
        for (const Json& e : tree.at("entries")) {
            BlendTreeEntry entry;
            entry.clip = e.at("clip").get<int>();
            entry.threshold = e.at("threshold").get<float>();
            if (!clipOk(entry.clip) || !finite(entry.threshold)) return bad("blend tree entry");
            mix.tree.entries.push_back(entry);
        }
        if (!finite(mix.tree.parameter)) return bad("blend tree parameter");
    } catch (const Json::exception& e) {
        return bad(std::string("animation mix (") + e.what() + ")");
    }
    return mix;
}

Json deformJson(const DeformRig& d) {
    Json twists = Json::array();
    for (const TwistChain& t : d.twists) {
        Json bones = Json::array(), factors = Json::array();
        for (const BoneId b : t.bones) bones.push_back(b);
        for (const float f : t.factors) factors.push_back(f);
        twists.push_back(Json{{"name", t.name},
                              {"source", boneJson(t.source)},
                              {"bones", bones},
                              {"factors", factors},
                              {"axis", vec3Json(t.axis)},
                              {"enabled", t.enabled}});
    }
    Json constraints = Json::array();
    for (const Constraint& c : d.constraints) {
        Json inverse = Json::array();
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) inverse.push_back(c.inverse[col][row]);
        }
        constraints.push_back(Json{{"name", c.name},
                                   {"kind", static_cast<int>(c.kind)},
                                   {"bone", boneJson(c.bone)},
                                   {"target", boneJson(c.target)},
                                   {"influence", c.influence},
                                   {"enabled", c.enabled},
                                   {"copy", Json::array({c.copyLocation, c.copyRotation, c.copyScale})},
                                   {"limit", Json::array({c.limitLocation, c.limitRotation, c.limitScale})},
                                   {"locationMin", vec3Json(c.locationMin)},
                                   {"locationMax", vec3Json(c.locationMax)},
                                   {"rotationMin", vec3Json(c.rotationMin)},
                                   {"rotationMax", vec3Json(c.rotationMax)},
                                   {"scaleMin", vec3Json(c.scaleMin)},
                                   {"scaleMax", vec3Json(c.scaleMax)},
                                   {"inverse", inverse},
                                   {"aimAxis", vec3Json(c.aimAxis)}});
    }
    Json springs = Json::array();
    for (const SpringChain& s : d.springs) {
        Json bones = Json::array();
        for (const BoneId b : s.bones) bones.push_back(b);
        springs.push_back(Json{{"name", s.name},
                               {"bones", bones},
                               {"stiffness", s.stiffness},
                               {"damping", s.damping},
                               {"gravity", s.gravity},
                               {"enabled", s.enabled}});
    }
    Json colliders = Json::array();
    for (const SpringCollider& c : d.colliders) {
        colliders.push_back(
            Json{{"bone", boneJson(c.bone)}, {"offset", vec3Json(c.offset)}, {"radius", c.radius}});
    }
    return Json{{"twists", twists},
                {"constraints", constraints},
                {"springs", springs},
                {"colliders", colliders},
                {"springsEnabled", d.springsEnabled}};
}

Result<DeformRig> readDeform(const Json& j, const Skeleton& sk) {
    DeformRig rig;
    if (!j.is_object()) return rig;
    try {
        for (const Json& t : j.at("twists")) {
            TwistChain chain;
            chain.name = t.at("name").get<std::string>();
            chain.source = readBone(t.at("source"));
            for (const Json& b : t.at("bones")) chain.bones.push_back(readBone(b));
            for (const Json& f : t.at("factors")) chain.factors.push_back(f.get<float>());
            if (!readVec3(t.at("axis"), chain.axis)) return bad("twist axis");
            chain.enabled = t.at("enabled").get<bool>();
            rig.twists.push_back(std::move(chain));
        }
        for (const Json& c : j.at("constraints")) {
            Constraint k;
            k.name = c.at("name").get<std::string>();
            const int kind = c.at("kind").get<int>();
            if (kind < 0 || kind > 3) return bad("constraint kind");
            k.kind = static_cast<ConstraintKind>(kind);
            k.bone = readBone(c.at("bone"));
            k.target = readBone(c.at("target"));
            k.influence = c.at("influence").get<float>();
            k.enabled = c.at("enabled").get<bool>();
            const Json& copy = c.at("copy");
            const Json& limit = c.at("limit");
            if (!copy.is_array() || copy.size() != 3 || !limit.is_array() || limit.size() != 3)
                return bad("constraint flags");
            k.copyLocation = copy[0].get<bool>(), k.copyRotation = copy[1].get<bool>(),
            k.copyScale = copy[2].get<bool>();
            k.limitLocation = limit[0].get<bool>(), k.limitRotation = limit[1].get<bool>(),
            k.limitScale = limit[2].get<bool>();
            if (!readVec3(c.at("locationMin"), k.locationMin) ||
                !readVec3(c.at("locationMax"), k.locationMax) ||
                !readVec3(c.at("rotationMin"), k.rotationMin) ||
                !readVec3(c.at("rotationMax"), k.rotationMax) || !readVec3(c.at("scaleMin"), k.scaleMin) ||
                !readVec3(c.at("scaleMax"), k.scaleMax) || !readVec3(c.at("aimAxis"), k.aimAxis)) {
                return bad("constraint limits");
            }
            const Json& inverse = c.at("inverse");
            if (!inverse.is_array() || inverse.size() != 16) return bad("constraint matrix");
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    const Json& e = inverse[static_cast<std::size_t>(col * 4 + row)];
                    if (!e.is_number()) return bad("constraint matrix");
                    k.inverse[col][row] = e.get<float>();
                    if (!finite(k.inverse[col][row])) return bad("constraint matrix");
                }
            }
            rig.constraints.push_back(std::move(k));
        }
        for (const Json& s : j.at("springs")) {
            SpringChain chain;
            chain.name = s.at("name").get<std::string>();
            for (const Json& b : s.at("bones")) chain.bones.push_back(readBone(b));
            chain.stiffness = s.at("stiffness").get<float>();
            chain.damping = s.at("damping").get<float>();
            chain.gravity = s.at("gravity").get<float>();
            chain.enabled = s.at("enabled").get<bool>();
            if (!finite(chain.stiffness) || !finite(chain.damping) || !finite(chain.gravity))
                return bad("spring chain");
            rig.springs.push_back(std::move(chain));
        }
        for (const Json& c : j.at("colliders")) {
            SpringCollider collider;
            collider.bone = readBone(c.at("bone"));
            if (!readVec3(c.at("offset"), collider.offset)) return bad("collider");
            collider.radius = c.at("radius").get<float>();
            if (!finite(collider.radius)) return bad("collider radius");
            rig.colliders.push_back(collider);
        }
        rig.springsEnabled = j.at("springsEnabled").get<bool>();
    } catch (const Json::exception& e) {
        return bad(std::string("deformation setup (") + e.what() + ")");
    }
    pruneDeformRig(sk, rig);  // références à des os absents : ignorées
    return rig;
}

Json meshJson(const Mesh& mesh, std::vector<std::shared_ptr<const Image>>& images) {
    Writer vertices;
    for (const Vertex& v : mesh.vertices) {
        for (int i = 0; i < 3; ++i) vertices.f32(v.position[i]);
        for (int i = 0; i < 3; ++i) vertices.f32(v.normal[i]);
        for (int i = 0; i < 3; ++i) vertices.f32(v.tangent[i]);
        vertices.f32(v.uv0.x), vertices.f32(v.uv0.y);
    }
    Writer indices;
    for (const std::uint32_t i : mesh.indices) indices.u32(i);
    Json submeshes = Json::array();
    for (const Submesh& s : mesh.submeshes) {
        int imageIndex = -1;
        if (s.material.baseColorTexture) {
            const auto it = std::find(images.begin(), images.end(), s.material.baseColorTexture);
            imageIndex = static_cast<int>(it - images.begin());
            if (it == images.end()) images.push_back(s.material.baseColorTexture);
        }
        submeshes.push_back(Json{{"name", s.name},
                                 {"firstIndex", s.firstIndex},
                                 {"indexCount", s.indexCount},
                                 {"material", Json{{"name", s.material.name},
                                                   {"baseColor", vec3Json(s.material.baseColor)},
                                                   {"metallic", s.material.metallic},
                                                   {"roughness", s.material.roughness},
                                                   {"image", imageIndex}}}});
    }
    Json out{{"vertexCount", mesh.vertices.size()},
             {"vertices", encode64(vertices.bytes())},
             {"indices", encode64(indices.bytes())},
             {"submeshes", submeshes}};
    if (!mesh.morphTargets.empty()) {
        Json morphs = Json::array();
        for (const MorphTarget& t : mesh.morphTargets) {
            Writer deltas;
            for (const glm::vec3& d : t.positionDeltas) deltas.f32(d.x), deltas.f32(d.y), deltas.f32(d.z);
            Json drivers = Json::array();
            for (const MorphDriver& d : t.drivers) {
                drivers.push_back(Json{{"bone", boneJson(d.bone)},
                                       {"axis", vec3Json(d.axis)},
                                       {"angle0", d.angle0},
                                       {"angle1", d.angle1}});
            }
            morphs.push_back(Json{{"name", t.name},
                                  {"weightBone", boneJson(t.weightBone)},
                                  {"deltas", encode64(deltas.bytes())},
                                  {"drivers", drivers}});
        }
        out["morphs"] = morphs;
    }
    if (mesh.hasSkinning()) {
        Writer skin;
        for (const SkinWeight& w : mesh.skinWeights) {
            for (int k = 0; k < 4; ++k) skin.u32(w.joints[static_cast<std::size_t>(k)]);
            for (int k = 0; k < 4; ++k) skin.f32(w.weights[static_cast<std::size_t>(k)]);
        }
        out["skin"] = encode64(skin.bytes());
    }
    return out;
}

// --- lecture ---------------------------------------------------------------------------------------------

template <class T>
bool readTrack(const Json& j, int components, Track<T>& track) {
    if (!j.is_object()) return false;
    for (const char* key : {"time", "value", "in", "out", "mode"}) {
        if (!j.contains(key) || !j[key].is_array()) return false;
    }
    const std::size_t n = j["time"].size();
    const std::size_t c = static_cast<std::size_t>(components);
    if (j["value"].size() != n * c || j["in"].size() != n * c || j["out"].size() != n * c ||
        j["mode"].size() != n) {
        return false;
    }
    track.keys.clear();
    double previous = -1e300;
    const auto at = [&](const char* key, std::size_t i) -> float {
        const Json& e = j[key][i];
        return e.is_number() ? e.get<float>() : std::nanf("");
    };
    const auto make = [&](const char* key, std::size_t k) {
        T v{};
        if constexpr (std::is_same_v<T, glm::quat>) {
            v = glm::quat(at(key, k * 4), at(key, k * 4 + 1), at(key, k * 4 + 2), at(key, k * 4 + 3));
        } else {
            for (std::size_t i = 0; i < c; ++i) v[static_cast<int>(i)] = at(key, k * c + i);
        }
        return v;
    };
    for (std::size_t k = 0; k < n; ++k) {
        Keyframe<T> key;
        if (!j["time"][k].is_number() || !j["mode"][k].is_number_integer()) return false;
        key.time = j["time"][k].get<double>();
        if (!std::isfinite(key.time) || key.time < previous) return false;  // temps triés (égalité tolérée)
        previous = key.time;
        const int mode = j["mode"][k].get<int>();
        if (mode < 0 || mode > 2) return false;
        key.mode = static_cast<Interpolation>(mode);
        key.value = make("value", k);
        key.inTangent = make("in", k);
        key.outTangent = make("out", k);
        const auto allFinite = [&](const T& v) {
            if constexpr (std::is_same_v<T, glm::quat>) {
                return finite(v.w) && finite(v.x) && finite(v.y) && finite(v.z);
            } else {
                return finite(v.x) && finite(v.y) && finite(v.z);
            }
        };
        if (!allFinite(key.value) || !allFinite(key.inTangent) || !allFinite(key.outTangent)) return false;
        track.keys.push_back(key);
    }
    return true;
}

Result<AnimationClip> readClip(const Json& j, const Skeleton& sk) {
    if (!j.is_object()) return bad("clip");
    AnimationClip clip;
    try {
        clip.name = j.at("name").get<std::string>();
        clip.duration = j.at("duration").get<double>();
        clip.fps = j.at("fps").get<float>();
        clip.loop = j.at("loop").get<bool>();
        clip.rootMotion = j.value("rootMotion", false);
        clip.rootBone = readBone(j.value("rootBone", Json(-1)));
        if (!std::isfinite(clip.duration) || clip.duration < 0.0 || !(clip.fps > 0.0f) ||
            !std::isfinite(clip.fps)) {
            return bad("clip duration / fps");
        }
        for (const Json& m : j.at("markers")) {
            clip.markers.push_back({m.at("time").get<double>(), m.at("name").get<std::string>()});
        }
        for (const Json& t : j.at("tracks")) {
            const BoneId bone = readBone(t.at("bone"));
            if (!sk.contains(bone)) return bad("animation track on an unknown bone");
            BoneTracks tracks;
            if (!readTrack(t.at("translation"), 3, tracks.translation) ||
                !readTrack(t.at("rotation"), 4, tracks.rotation) ||
                !readTrack(t.at("scale"), 3, tracks.scale)) {
                return bad("animation keys of clip '" + clip.name + "'");
            }
            clip.tracks[bone] = std::move(tracks);
        }
    } catch (const Json::exception& e) {
        return bad(std::string("clip (") + e.what() + ")");
    }
    return clip;
}

Result<IKControl> readIk(const Json& j, const Skeleton& sk) {
    IKControl c;
    try {
        c.name = j.at("name").get<std::string>();
        const int kind = j.at("kind").get<int>();
        if (kind < 0 || kind > 1) return bad("IK kind");
        c.kind = static_cast<IKKind>(kind);
        if (!readVec3(j.at("aimAxis"), c.aimAxis)) return bad("IK aim axis");
        for (const Json& l : j.at("locks")) {
            FootLock lock;
            lock.start = l.at("start").get<double>();
            lock.end = l.at("end").get<double>();
            if (!readTransform(l.at("world"), lock.world)) return bad("foot lock");
            c.locks.push_back(lock);
        }
        const Json& chain = j.at("chain");
        for (const Json& b : chain.at("bones")) c.chain.bones.push_back(readBone(b));
        c.chain.effector = readBone(chain.at("effector"));
        const int solver = chain.at("solver").get<int>();
        const int orientation = chain.at("effectorOrientation").get<int>();
        if (solver < 0 || solver > 2 || orientation < 0 || orientation > 2) return bad("IK solver");
        c.chain.solver = static_cast<IKSolver>(solver);
        c.chain.effectorOrientation = static_cast<EffectorOrientation>(orientation);
        c.chain.maxIterations = std::clamp(chain.at("maxIterations").get<int>(), 1, 1000);
        c.chain.tolerance = chain.at("tolerance").get<float>();
        c.chain.damping = chain.at("damping").get<float>();
        for (const Json& l : chain.at("limits")) {
            JointLimit limit;
            const int type = l.at("type").get<int>();
            if (type < 0 || type > 2) return bad("joint limit");
            limit.type = static_cast<JointLimitType>(type);
            if (!readVec3(l.at("axis"), limit.axis)) return bad("joint limit axis");
            limit.min = l.at("min").get<float>();
            limit.max = l.at("max").get<float>();
            limit.swing = l.at("swing").get<float>();
            c.chain.limits.push_back(limit);
        }
        c.targetBone = readBone(j.at("targetBone"));
        c.poleBone = readBone(j.at("poleBone"));
        c.poleTwist = j.at("poleTwist").get<float>();
        c.weight = j.at("weight").get<float>();
        c.enabled = j.at("enabled").get<bool>();
    } catch (const Json::exception& e) {
        return bad(std::string("IK control (") + e.what() + ")");
    }
    (void)sk;
    return c;
}

Result<Mesh> readMesh(const Json& j, const Skeleton& sk,
                      const std::vector<std::shared_ptr<const Image>>& images) {
    Mesh mesh;
    try {
        const std::size_t count = j.at("vertexCount").get<std::size_t>();
        if (count > 50'000'000) return bad("vertex count");
        std::string vertexBytes, indexBytes;
        if (!decode64(j.at("vertices").get<std::string>(), vertexBytes) ||
            !decode64(j.at("indices").get<std::string>(), indexBytes)) {
            return bad("mesh buffers (base64)");
        }
        if (vertexBytes.size() != count * 11 * 4 || indexBytes.size() % 12 != 0)
            return bad("mesh buffer sizes");
        Reader vr(vertexBytes);
        mesh.vertices.resize(count);
        for (Vertex& v : mesh.vertices) {
            for (int i = 0; i < 3; ++i) v.position[i] = vr.f32();
            for (int i = 0; i < 3; ++i) v.normal[i] = vr.f32();
            for (int i = 0; i < 3; ++i) v.tangent[i] = vr.f32();
            v.uv0.x = vr.f32();
            v.uv0.y = vr.f32();
            if (!finite(v.position.x + v.position.y + v.position.z + v.normal.x + v.normal.y + v.normal.z +
                        v.uv0.x + v.uv0.y)) {
                return bad("non-finite vertex data");
            }
        }
        Reader ir(indexBytes);
        mesh.indices.resize(indexBytes.size() / 4);
        for (std::uint32_t& i : mesh.indices) {
            i = ir.u32();
            if (i >= count) return bad("mesh index out of range");
        }
        for (const Json& s : j.at("submeshes")) {
            Submesh sub;
            sub.name = s.at("name").get<std::string>();
            sub.firstIndex = s.at("firstIndex").get<std::uint32_t>();
            sub.indexCount = s.at("indexCount").get<std::uint32_t>();
            if (static_cast<std::uint64_t>(sub.firstIndex) + sub.indexCount > mesh.indices.size()) {
                return bad("submesh range");
            }
            const Json& m = s.at("material");
            sub.material.name = m.at("name").get<std::string>();
            if (!readVec3(m.at("baseColor"), sub.material.baseColor)) return bad("material color");
            sub.material.metallic = m.at("metallic").get<float>();
            sub.material.roughness = m.at("roughness").get<float>();
            const int image = m.at("image").get<int>();
            if (image >= 0) {
                if (static_cast<std::size_t>(image) >= images.size()) return bad("material image");
                sub.material.baseColorTexture = images[static_cast<std::size_t>(image)];
            }
            mesh.submeshes.push_back(std::move(sub));
        }
        if (j.contains("morphs")) {
            for (const Json& m : j["morphs"]) {
                MorphTarget target;
                target.name = m.at("name").get<std::string>();
                target.weightBone = readBone(m.at("weightBone"));
                if (target.weightBone != kNoBone && !sk.contains(target.weightBone))
                    return bad("morph weight bone");
                std::string bytes;
                if (!decode64(m.at("deltas").get<std::string>(), bytes) || bytes.size() != count * 12) {
                    return bad("morph target buffer");
                }
                Reader mr(bytes);
                target.positionDeltas.resize(count);
                for (glm::vec3& d : target.positionDeltas) {
                    d.x = mr.f32();
                    d.y = mr.f32();
                    d.z = mr.f32();
                }
                for (const Json& dj : m.at("drivers")) {
                    MorphDriver d;
                    d.bone = readBone(dj.at("bone"));
                    if (!sk.contains(d.bone) || !readVec3(dj.at("axis"), d.axis))
                        return bad("corrective driver");
                    d.angle0 = dj.at("angle0").get<float>();
                    d.angle1 = dj.at("angle1").get<float>();
                    if (!finite(d.angle0) || !finite(d.angle1)) return bad("corrective driver angles");
                    target.drivers.push_back(d);
                }
                mesh.morphTargets.push_back(std::move(target));
            }
        }
        if (j.contains("skin")) {
            std::string skinBytes;
            if (!decode64(j["skin"].get<std::string>(), skinBytes) || skinBytes.size() != count * 32) {
                return bad("skin weights buffer");
            }
            Reader sr(skinBytes);
            mesh.skinWeights.resize(count);
            for (SkinWeight& w : mesh.skinWeights) {
                for (std::size_t k = 0; k < 4; ++k) w.joints[k] = sr.u32();
                for (std::size_t k = 0; k < 4; ++k) w.weights[k] = sr.f32();
                for (std::size_t k = 0; k < 4; ++k) {
                    if (!finite(w.weights[k]) || w.weights[k] < 0.0f || w.weights[k] > 1.0001f)
                        return bad("skin weight value");
                    if (w.weights[k] > 0.0f && !sk.contains(w.joints[k]))
                        return bad("skin weight on an unknown bone");
                    if (w.weights[k] <= 0.0f) w.joints[k] = kNoBone;
                }
            }
        }
    } catch (const Json::exception& e) {
        return bad(std::string("mesh (") + e.what() + ")");
    }
    if (const auto valid = mesh.validate(); !valid) return bad("mesh (" + valid.error().message + ")");
    return mesh;
}

}  // namespace

std::string serializeProject(const Project& project) {
    const Character& c = project.character;
    std::vector<std::shared_ptr<const Image>> images;
    Json meshes = Json::array();
    for (const Mesh& m : c.meshes) meshes.push_back(meshJson(m, images));
    Json imageList = Json::array();
    for (const auto& img : images) {
        Writer pixels;
        for (const std::uint8_t b : img->rgba) pixels.u8(b);
        imageList.push_back(
            Json{{"width", img->width}, {"height", img->height}, {"rgba", encode64(pixels.bytes())}});
    }
    Json clips = Json::array();
    for (const AnimationClip& clip : c.clips) clips.push_back(clipJson(clip));
    Json poses = Json::array();
    for (const NamedPose& p : c.poses) {
        Json bones = Json::array();
        for (const auto& [name, t] : p.bones)
            bones.push_back(Json{{"name", name}, {"transform", transformJson(t)}});
        poses.push_back(Json{{"name", p.name}, {"bones", bones}});
    }
    Json ik = Json::array();
    for (const IKControl& control : c.ikControls) ik.push_back(ikJson(control));
    const ProjectView& v = project.view;
    Json root{{"format", "rigger-project"},
              {"version", kProjectVersion},
              {"name", c.name},
              {"view", Json{{"yaw", v.yaw},
                            {"pitch", v.pitch},
                            {"distance", v.distance},
                            {"target", vec3Json(v.target)},
                            {"orthographic", v.orthographic},
                            {"mode", v.mode},
                            {"clipIndex", v.clipIndex},
                            {"time", v.time},
                            {"sourcePath", v.sourcePath}}},
              {"skeleton", skeletonJson(c.skeleton)},
              {"images", imageList},
              {"meshes", meshes},
              {"clips", clips},
              {"poses", poses},
              {"ikControls", ik},
              {"mix", mixJson(c.mix)},
              {"deform", deformJson(c.deform)},
              {"warnings", c.warnings}};
    return root.dump();
}

Result<Project> parseProject(const std::string& text) {
    Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return Error{"This is not a valid Rigger project (the file is corrupted or truncated)."};
    }
    if (root.value("format", "") != "rigger-project") return Error{"This file is not a Rigger project."};
    if (!root.contains("version") || !root["version"].is_number_integer()) return bad("missing version");
    const int version = root["version"].get<int>();
    if (version > kProjectVersion) {
        return Error{"This project was saved by a newer version of Rigger (schema " +
                     std::to_string(version) + "); please update the application."};
    }
    if (version < 1) return bad("unknown schema version");
    // Migrations : (version == 1) -> rien à faire. Les futures versions ajouteront ici leurs étapes N -> N+1.

    Project project;
    try {
        project.character.name = root.value("name", "");
        // Squelette : os déjà triés parents d'abord.
        for (const Json& b : root.at("skeleton").at("bones")) {
            Bone bone;
            bone.id = readBone(b.at("id"));
            bone.name = b.at("name").get<std::string>();
            bone.parent = readBone(b.at("parent"));
            if (!readTransform(b.at("bind"), bone.bindLocal) || !readVec3(b.at("color"), bone.color)) {
                return bad("bone data");
            }
            bone.length = b.at("length").get<float>();
            bone.flags = static_cast<BoneFlags>(b.at("flags").get<std::uint32_t>());
            if (!finite(bone.length)) return bad("bone length");
            const auto added = project.character.skeleton.restoreBone(bone);
            if (!added) return bad("skeleton (" + added.error().message + ")");
        }
        if (project.character.skeleton.boneCount() == 0) return bad("the skeleton is empty");
        const Skeleton& sk = project.character.skeleton;

        std::vector<std::shared_ptr<const Image>> images;
        for (const Json& i : root.at("images")) {
            auto image = std::make_shared<Image>();
            image->width = i.at("width").get<int>();
            image->height = i.at("height").get<int>();
            std::string bytes;
            if (!decode64(i.at("rgba").get<std::string>(), bytes) || image->width < 0 || image->height < 0 ||
                bytes.size() !=
                    static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height) * 4) {
                return bad("texture image");
            }
            image->rgba.assign(bytes.begin(), bytes.end());
            images.push_back(std::move(image));
        }
        for (const Json& m : root.at("meshes")) {
            auto mesh = readMesh(m, sk, images);
            if (!mesh) return mesh.error();
            project.character.meshes.push_back(std::move(mesh.value()));
        }
        for (const Json& c : root.at("clips")) {
            auto clip = readClip(c, sk);
            if (!clip) return clip.error();
            project.character.clips.push_back(std::move(clip.value()));
        }
        for (const Json& p : root.at("poses")) {
            NamedPose pose;
            pose.name = p.at("name").get<std::string>();
            for (const Json& b : p.at("bones")) {
                Transform t;
                if (!readTransform(b.at("transform"), t)) return bad("pose transform");
                pose.bones.emplace_back(b.at("name").get<std::string>(), t);
            }
            project.character.poses.push_back(std::move(pose));
        }
        for (const Json& j : root.at("ikControls")) {
            auto control = readIk(j, sk);
            if (!control) return control.error();
            project.character.ikControls.push_back(std::move(control.value()));
        }
        {
            auto mix = readMix(root.value("mix", Json()), project.character.clips.size());
            if (!mix) return mix.error();
            project.character.mix = std::move(mix.value());
        }
        {
            auto deform = readDeform(root.value("deform", Json()), sk);
            if (!deform) return deform.error();
            project.character.deform = std::move(deform.value());
        }
        pruneIKControls(sk, project.character.ikControls);  // contrôles devenus invalides : ignorés
        for (const Json& w : root.value("warnings", Json::array()))
            project.character.warnings.push_back(w.get<std::string>());

        const Json view = root.value("view", Json::object());
        ProjectView& v = project.view;
        v.yaw = view.value("yaw", 0.0f);
        v.pitch = view.value("pitch", 0.0f);
        v.distance = view.value("distance", 4.0f);
        if (view.contains("target")) readVec3(view["target"], v.target);
        v.orthographic = view.value("orthographic", false);
        v.mode = std::clamp(view.value("mode", 2), 0, 2);
        v.clipIndex = view.value("clipIndex", -1);
        v.time = view.value("time", 0.0);
        v.sourcePath = view.value("sourcePath", "");
        if (!finite(v.yaw) || !finite(v.pitch) || !finite(v.distance) || !std::isfinite(v.time))
            return bad("view settings");
        if (v.clipIndex >= static_cast<int>(project.character.clips.size())) v.clipIndex = -1;
    } catch (const Json::exception& e) {
        return bad(std::string("missing or malformed field (") + e.what() + ")");
    }
    return project;
}

Result<void> saveProject(const Project& project, const std::filesystem::path& path) {
    const std::string text = serializeProject(project);
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"Cannot write " + temp.string()};
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return Error{"Writing the project failed (disk full?)"};
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {  // Windows : rename n'écrase pas toujours
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
        if (ec) return Error{"Cannot replace " + path.string() + ": " + ec.message()};
    }
    return {};
}

Result<Project> loadProject(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"Cannot open " + path.string()};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return Error{"Cannot read " + path.string()};
    return parseProject(buffer.str());
}

}  // namespace rigger::io
