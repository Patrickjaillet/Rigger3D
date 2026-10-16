// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "io/gltf_export.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "core/animation/clip.hpp"
#include "core/deform/deform.hpp"
#include "core/ik/rig.hpp"

namespace rigger::io {

namespace {

class ModelBuilder {
public:
    explicit ModelBuilder(tinygltf::Model& model) : model_(model) { model_.buffers.emplace_back(); }

    std::vector<unsigned char>& data() { return model_.buffers[0].data; }

    int addView(const void* bytes, std::size_t size, int target = 0) {
        auto& d = data();
        while (d.size() % 4 != 0) d.push_back(0);
        tinygltf::BufferView view;
        view.buffer = 0;
        view.byteOffset = d.size();
        view.byteLength = size;
        view.target = target;
        const auto* p = static_cast<const unsigned char*>(bytes);
        d.insert(d.end(), p, p + size);
        model_.bufferViews.push_back(view);
        return static_cast<int>(model_.bufferViews.size()) - 1;
    }

    int addAccessor(int view, std::size_t byteOffset, int componentType, std::size_t count, int type,
                    const std::vector<double>& min = {}, const std::vector<double>& max = {}) {
        tinygltf::Accessor a;
        a.bufferView = view;
        a.byteOffset = byteOffset;
        a.componentType = componentType;
        a.count = count;
        a.type = type;
        a.minValues = min;
        a.maxValues = max;
        model_.accessors.push_back(a);
        return static_cast<int>(model_.accessors.size()) - 1;
    }

private:
    tinygltf::Model& model_;
};

std::vector<double> toDoubles(const glm::vec3& v) { return {v.x, v.y, v.z}; }

bool included(const std::vector<bool>& mask, std::size_t i) {
    return mask.empty() || (i < mask.size() && mask[i]);
}

}  // namespace

Result<void> exportGltf(const Character& character, const std::filesystem::path& path,
                        const GltfExportOptions& options) {
    const Skeleton& sk = character.skeleton;
    if (sk.boneCount() == 0) return Error{"There is no skeleton to export."};
    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "Rigger";
    ModelBuilder builder(model);

    // --- Nœuds : un par os (hors os de contrôle IK) -------------------------------------------------
    std::vector<BoneId> bones;
    std::map<BoneId, int> nodeOf;
    std::map<BoneId, int> jointOf;
    for (const BoneId id : sk.order()) {
        if (hasFlag(sk.bone(id).flags, BoneFlags::Control)) continue;
        nodeOf[id] = static_cast<int>(bones.size());
        jointOf[id] = static_cast<int>(bones.size());
        bones.push_back(id);
    }
    if (bones.empty()) return Error{"The skeleton only contains control bones."};
    model.nodes.resize(bones.size());
    tinygltf::Scene scene;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const Bone& b = sk.bone(bones[i]);
        tinygltf::Node& node = model.nodes[i];
        node.name = b.name;
        node.translation = toDoubles(b.bindLocal.t);
        node.rotation = {b.bindLocal.r.x, b.bindLocal.r.y, b.bindLocal.r.z, b.bindLocal.r.w};
        node.scale = toDoubles(b.bindLocal.s);
        const auto parent = nodeOf.find(b.parent);
        if (b.parent != kNoBone && parent != nodeOf.end()) {
            model.nodes[static_cast<std::size_t>(parent->second)].children.push_back(static_cast<int>(i));
        } else {
            scene.nodes.push_back(static_cast<int>(i));
        }
    }

    // --- Skin ------------------------------------------------------------------------------------------
    std::vector<float> ibm;
    ibm.reserve(bones.size() * 16);
    for (const BoneId id : bones) {
        const glm::mat4& m = sk.inverseBind()[id];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) ibm.push_back(m[c][r]);
        }
    }
    tinygltf::Skin skin;
    skin.name = character.name.empty() ? "Armature" : character.name;
    for (std::size_t i = 0; i < bones.size(); ++i) skin.joints.push_back(static_cast<int>(i));
    skin.skeleton = scene.nodes.empty() ? 0 : scene.nodes.front();
    const int ibmView = builder.addView(ibm.data(), ibm.size() * sizeof(float));
    skin.inverseBindMatrices =
        builder.addAccessor(ibmView, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, bones.size(), TINYGLTF_TYPE_MAT4);
    model.skins.push_back(skin);

    // --- Maillages ----------------------------------------------------------------------------------------
    std::map<const Image*, int> textureOf;
    std::map<std::size_t, int>
        meshNodeOf;  // maillage du personnage -> nœud glTF (pour les poids de blend shapes)
    for (std::size_t mi = 0; mi < character.meshes.size(); ++mi) {
        if (!included(options.includeMeshes, mi)) continue;
        const Mesh& mesh = character.meshes[mi];
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;
        const std::size_t n = mesh.vertices.size();
        std::vector<float> positions, normals, uvs;
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const Vertex& v : mesh.vertices) {
            for (int i = 0; i < 3; ++i) {
                positions.push_back(v.position[i]);
                normals.push_back(v.normal[i]);
            }
            uvs.push_back(v.uv0.x);
            uvs.push_back(v.uv0.y);
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
        tinygltf::Primitive base;
        base.mode = TINYGLTF_MODE_TRIANGLES;
        const int pv = builder.addView(positions.data(), positions.size() * 4, TINYGLTF_TARGET_ARRAY_BUFFER);
        base.attributes["POSITION"] = builder.addAccessor(pv, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, n,
                                                          TINYGLTF_TYPE_VEC3, toDoubles(lo), toDoubles(hi));
        const int nv = builder.addView(normals.data(), normals.size() * 4, TINYGLTF_TARGET_ARRAY_BUFFER);
        base.attributes["NORMAL"] =
            builder.addAccessor(nv, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, n, TINYGLTF_TYPE_VEC3);
        const int uv = builder.addView(uvs.data(), uvs.size() * 4, TINYGLTF_TARGET_ARRAY_BUFFER);
        base.attributes["TEXCOORD_0"] =
            builder.addAccessor(uv, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, n, TINYGLTF_TYPE_VEC2);
        if (mesh.hasSkinning()) {
            std::vector<std::uint16_t> joints;
            std::vector<float> weights;
            for (const SkinWeight& w : mesh.skinWeights) {
                std::array<std::uint16_t, 4> j{0, 0, 0, 0};
                std::array<float, 4> ww{0, 0, 0, 0};
                float sum = 0.0f;
                for (std::size_t k = 0; k < 4; ++k) {
                    const auto it = jointOf.find(w.joints[k]);
                    if (w.weights[k] > 0.0f && it != jointOf.end()) {
                        j[k] = static_cast<std::uint16_t>(it->second);
                        ww[k] = w.weights[k];
                        sum += ww[k];
                    }
                }
                if (sum <= 0.0f) ww[0] = 1.0f, sum = 1.0f;  // jamais de sommet sans influence
                for (std::size_t k = 0; k < 4; ++k) {
                    joints.push_back(j[k]);
                    weights.push_back(ww[k] / sum);
                }
            }
            const int jv = builder.addView(joints.data(), joints.size() * 2, TINYGLTF_TARGET_ARRAY_BUFFER);
            base.attributes["JOINTS_0"] =
                builder.addAccessor(jv, 0, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, n, TINYGLTF_TYPE_VEC4);
            const int wv = builder.addView(weights.data(), weights.size() * 4, TINYGLTF_TARGET_ARRAY_BUFFER);
            base.attributes["WEIGHTS_0"] =
                builder.addAccessor(wv, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, n, TINYGLTF_TYPE_VEC4);
        }
        // Blend shapes : un accessor POSITION (déplacements) par cible, partagé par toutes les primitives.
        std::vector<std::map<std::string, int>> morphTargets;
        for (const MorphTarget& target : mesh.morphTargets) {
            std::vector<float> deltas;
            glm::vec3 dlo(1e30f), dhi(-1e30f);
            for (const glm::vec3& d : target.positionDeltas) {
                deltas.insert(deltas.end(), {d.x, d.y, d.z});
                dlo = glm::min(dlo, d);
                dhi = glm::max(dhi, d);
            }
            const int view = builder.addView(deltas.data(), deltas.size() * 4, TINYGLTF_TARGET_ARRAY_BUFFER);
            morphTargets.push_back(
                {{"POSITION", builder.addAccessor(view, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, n,
                                                  TINYGLTF_TYPE_VEC3, toDoubles(dlo), toDoubles(dhi))}});
        }
        tinygltf::Mesh out;
        out.name = "Mesh" + std::to_string(mi);
        if (!morphTargets.empty()) {
            tinygltf::Value::Array names;
            for (const MorphTarget& target : mesh.morphTargets) {
                names.emplace_back(target.name);
                const float rest =
                    sk.contains(target.weightBone) ? sk.bone(target.weightBone).bindLocal.t.x : 0.0f;
                out.weights.push_back(rest);
            }
            tinygltf::Value::Object extras;
            extras["targetNames"] = tinygltf::Value(names);
            out.extras = tinygltf::Value(extras);
        }
        std::vector<Submesh> submeshes = mesh.submeshes;
        if (submeshes.empty()) {
            Submesh s;
            s.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            submeshes.push_back(s);
        }
        for (const Submesh& s : submeshes) {
            if (s.indexCount == 0 ||
                static_cast<std::size_t>(s.firstIndex) + s.indexCount > mesh.indices.size())
                continue;
            tinygltf::Primitive prim = base;
            prim.targets = morphTargets;
            const int iv = builder.addView(mesh.indices.data() + s.firstIndex,
                                           static_cast<std::size_t>(s.indexCount) * 4,
                                           TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
            prim.indices = builder.addAccessor(iv, 0, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, s.indexCount,
                                               TINYGLTF_TYPE_SCALAR);
            tinygltf::Material material;
            material.name = s.material.name;
            material.pbrMetallicRoughness.baseColorFactor = {s.material.baseColor.r, s.material.baseColor.g,
                                                             s.material.baseColor.b, 1.0};
            material.pbrMetallicRoughness.metallicFactor = s.material.metallic;
            material.pbrMetallicRoughness.roughnessFactor = s.material.roughness;
            if (s.material.baseColorTexture && s.material.baseColorTexture->width > 0) {
                const Image* img = s.material.baseColorTexture.get();
                auto it = textureOf.find(img);
                if (it == textureOf.end()) {
                    tinygltf::Image image;
                    image.name = "texture" + std::to_string(textureOf.size());
                    image.uri = image.name + ".png";
                    image.width = img->width;
                    image.height = img->height;
                    image.component = 4;
                    image.bits = 8;
                    image.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
                    image.image = img->rgba;
                    image.mimeType = "image/png";
                    model.images.push_back(std::move(image));
                    tinygltf::Texture texture;
                    texture.source = static_cast<int>(model.images.size()) - 1;
                    model.textures.push_back(texture);
                    it = textureOf.emplace(img, static_cast<int>(model.textures.size()) - 1).first;
                }
                material.pbrMetallicRoughness.baseColorTexture.index = it->second;
            }
            model.materials.push_back(material);
            prim.material = static_cast<int>(model.materials.size()) - 1;
            out.primitives.push_back(prim);
        }
        if (out.primitives.empty()) continue;
        model.meshes.push_back(std::move(out));
        tinygltf::Node node;
        node.name = model.meshes.back().name;
        node.mesh = static_cast<int>(model.meshes.size()) - 1;
        if (mesh.hasSkinning()) node.skin = 0;
        model.nodes.push_back(node);
        meshNodeOf[mi] = static_cast<int>(model.nodes.size()) - 1;
        scene.nodes.push_back(static_cast<int>(model.nodes.size()) - 1);
    }

    // --- Animations
    // ---------------------------------------------------------------------------------------------
    for (std::size_t ci = 0; ci < character.clips.size(); ++ci) {
        if (!included(options.includeClips, ci)) continue;
        const AnimationClip& clip = character.clips[ci];
        const float fps = options.fps > 0.0f ? options.fps : clip.fps;
        const int totalFrames = std::max(0, static_cast<int>(std::floor(clip.duration * fps + 1e-6)));
        const int first = std::clamp(options.firstFrame, 0, totalFrames);
        const int last =
            std::clamp(options.lastFrame < 0 ? totalFrames : options.lastFrame, first, totalFrames);
        const std::size_t frames = static_cast<std::size_t>(last - first) + 1;

        // Os animés : ceux qui ont des pistes + ceux que l'IK pilote (bake).
        std::set<BoneId> animated;
        for (const auto& [id, tracks] : clip.tracks) {
            if (nodeOf.count(id) != 0 && !tracks.empty()) animated.insert(id);
        }
        const bool bake = options.bakeIK && anyIKActive(character.ikControls);
        if (bake) {
            for (const BoneId id : bones) {
                if (isIKDriven(character.ikControls, id)) animated.insert(id);
            }
        }
        // Os pilotés par les déformations (twist, contraintes, spring bones) : exportés image par image.
        const bool bakeDeform = options.bakeDeform && !character.deform.empty();
        std::set<BoneId> deformDriven, fullChannels;
        if (bakeDeform) {
            for (const TwistChain& t : character.deform.twists) {
                if (!t.enabled) continue;
                for (const BoneId b : t.bones) deformDriven.insert(b);
            }
            for (const Constraint& c : character.deform.constraints) {
                if (c.enabled) deformDriven.insert(c.bone), fullChannels.insert(c.bone);
            }
            if (character.deform.springsEnabled) {
                for (const SpringChain& s : character.deform.springs) {
                    if (!s.enabled) continue;
                    for (const BoneId b : s.bones) deformDriven.insert(b);
                }
            }
            for (const BoneId b : deformDriven) {
                if (nodeOf.count(b) != 0) animated.insert(b);
            }
        }
        SpringState springState;
        // Maillages exportés dont au moins une blend shape est animée dans ce clip.
        std::vector<std::size_t> morphMeshes;
        for (const auto& [mi, nodeIndex] : meshNodeOf) {
            const Mesh& mesh = character.meshes[mi];
            const bool animatedWeights =
                std::any_of(mesh.morphTargets.begin(), mesh.morphTargets.end(), [&](const MorphTarget& t) {
                    const auto it = clip.tracks.find(t.weightBone);
                    return it != clip.tracks.end() && !it->second.translation.empty();
                });
            if (animatedWeights) morphMeshes.push_back(mi);
        }
        if (animated.empty() && morphMeshes.empty()) continue;

        struct Samples {
            std::vector<float> t, r, s;
        };
        std::map<BoneId, Samples> samples;
        std::vector<float> times;
        std::map<std::size_t, std::vector<float>> morphSamples;
        std::map<BoneId, glm::quat> previousRotation;
        for (int f = first; f <= last; ++f) {
            const double time = static_cast<double>(f) / fps;
            Pose pose = samplePose(clip, sk, time);
            if (bake) applyIKControls(sk, pose, character.ikControls, time);
            if (bakeDeform) {
                applyStatelessDeform(sk, pose, character.deform);
                stepSprings(sk, pose, character.deform, springState, 1.0 / fps, 4);
            }
            times.push_back(static_cast<float>(static_cast<double>(f - first) / fps));
            for (const std::size_t mi : morphMeshes) {
                for (const MorphTarget& t : character.meshes[mi].morphTargets) {
                    morphSamples[mi].push_back(t.weightBone < pose.size() ? pose[t.weightBone].t.x : 0.0f);
                }
            }
            for (const BoneId id : animated) {
                Samples& s = samples[id];
                const Transform& tr = pose[id];
                glm::quat q = glm::length(tr.r) > 1e-6f ? glm::normalize(tr.r)
                                                        : glm::quat(1, 0, 0, 0);  // jamais de quaternion nul
                const auto prev = previousRotation.find(id);
                if (prev != previousRotation.end() && glm::dot(prev->second, q) < 0.0f)
                    q = -q;  // continuité d'hémisphère
                previousRotation[id] = q;
                s.t.insert(s.t.end(), {tr.t.x, tr.t.y, tr.t.z});
                s.r.insert(s.r.end(), {q.x, q.y, q.z, q.w});
                s.s.insert(s.s.end(), {tr.s.x, tr.s.y, tr.s.z});
            }
        }
        tinygltf::Animation animation;
        animation.name = (ci < options.clipNames.size() && !options.clipNames[ci].empty())
                             ? options.clipNames[ci]
                             : clip.name;
        const int timeView = builder.addView(times.data(), times.size() * 4);
        const int timeAccessor =
            builder.addAccessor(timeView, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, frames, TINYGLTF_TYPE_SCALAR,
                                {static_cast<double>(times.front())}, {static_cast<double>(times.back())});
        const auto addChannel = [&](int node, const char* pathName, const std::vector<float>& values,
                                    int type) {
            const int v = builder.addView(values.data(), values.size() * 4);
            tinygltf::AnimationSampler sampler;
            sampler.input = timeAccessor;
            sampler.output = builder.addAccessor(v, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, frames, type);
            sampler.interpolation = "LINEAR";
            animation.samplers.push_back(sampler);
            tinygltf::AnimationChannel channel;
            channel.sampler = static_cast<int>(animation.samplers.size()) - 1;
            channel.target_node = node;
            channel.target_path = pathName;
            animation.channels.push_back(channel);
        };
        for (const BoneId id : animated) {
            const BoneTracks* tracks = clip.tracks.count(id) != 0 ? &clip.tracks.at(id) : nullptr;
            const int node = nodeOf.at(id);
            const Samples& s = samples.at(id);
            if ((tracks != nullptr && !tracks->translation.empty()) || fullChannels.count(id) != 0) {
                addChannel(node, "translation", s.t, TINYGLTF_TYPE_VEC3);
            }
            if ((tracks != nullptr && !tracks->rotation.empty()) || isIKDriven(character.ikControls, id) ||
                deformDriven.count(id) != 0) {
                addChannel(node, "rotation", s.r, TINYGLTF_TYPE_VEC4);
            }
            if ((tracks != nullptr && !tracks->scale.empty()) || fullChannels.count(id) != 0) {
                addChannel(node, "scale", s.s, TINYGLTF_TYPE_VEC3);
            }
        }
        for (const std::size_t mi : morphMeshes) {
            const std::vector<float>& w = morphSamples[mi];
            const int v = builder.addView(w.data(), w.size() * 4);
            tinygltf::AnimationSampler sampler;
            sampler.input = timeAccessor;
            sampler.output =
                builder.addAccessor(v, 0, TINYGLTF_COMPONENT_TYPE_FLOAT, w.size(), TINYGLTF_TYPE_SCALAR);
            sampler.interpolation = "LINEAR";
            animation.samplers.push_back(sampler);
            tinygltf::AnimationChannel channel;
            channel.sampler = static_cast<int>(animation.samplers.size()) - 1;
            channel.target_node = meshNodeOf.at(mi);
            channel.target_path = "weights";
            animation.channels.push_back(channel);
        }
        if (!animation.channels.empty()) model.animations.push_back(std::move(animation));
    }

    model.scenes.push_back(scene);
    model.defaultScene = 0;
    if (!options.binary) model.buffers[0].uri = path.stem().string() + ".bin";

    tinygltf::TinyGLTF writer;
    const bool ok = writer.WriteGltfSceneToFile(&model, path.string(), options.binary, options.binary,
                                                !options.binary, options.binary);
    if (!ok) return Error{"Writing " + path.string() + " failed."};
    return {};
}

}  // namespace rigger::io
