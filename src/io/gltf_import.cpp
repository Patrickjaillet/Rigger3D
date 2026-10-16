// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "io/gltf_import.hpp"

#include <spdlog/spdlog.h>
#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/skinning/weights.hpp"

namespace rigger::io {

namespace {

// ---------------------------------------------------------------------------------------------
// Lecture des accessors (stride, types de composants, normalisation, sparse)
// ---------------------------------------------------------------------------------------------

struct AccessorData {
    std::vector<double> values;  // count * components, converties en double
    std::size_t count = 0;
    int components = 0;
};

double readComponent(const unsigned char* p, int componentType, bool normalized) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            std::int8_t v;
            std::memcpy(&v, p, sizeof v);
            return normalized ? std::max(static_cast<double>(v) / 127.0, -1.0) : static_cast<double>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            std::uint8_t v;
            std::memcpy(&v, p, sizeof v);
            return normalized ? static_cast<double>(v) / 255.0 : static_cast<double>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            std::int16_t v;
            std::memcpy(&v, p, sizeof v);
            return normalized ? std::max(static_cast<double>(v) / 32767.0, -1.0) : static_cast<double>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            std::uint16_t v;
            std::memcpy(&v, p, sizeof v);
            return normalized ? static_cast<double>(v) / 65535.0 : static_cast<double>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof v);
            return static_cast<double>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float v;
            std::memcpy(&v, p, sizeof v);
            return static_cast<double>(v);
        }
        default:
            return 0.0;
    }
}

bool validComponentType(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return true;
        default:
            return false;
    }
}

// Renvoie un pointeur vers le début de la vue `viewIndex` après vérification des bornes de `needBytes`
// octets à partir de `offset` dans cette vue.
Result<const unsigned char*> viewBytes(const tinygltf::Model& model, int viewIndex, std::size_t offset,
                                       std::size_t needBytes) {
    if (viewIndex < 0 || static_cast<std::size_t>(viewIndex) >= model.bufferViews.size()) {
        return Error{"glTF: invalid bufferView index " + std::to_string(viewIndex)};
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(viewIndex)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) {
        return Error{"glTF: bufferView refers to a missing buffer"};
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    if (buffer.data.empty()) {
        return Error{"glTF: buffer data is missing (external .bin file not found?)"};
    }
    if (view.byteOffset + view.byteLength > buffer.data.size()) {
        return Error{"glTF: bufferView exceeds its buffer"};
    }
    if (offset + needBytes > view.byteLength) {
        return Error{"glTF: accessor exceeds its bufferView"};
    }
    return buffer.data.data() + view.byteOffset + offset;
}

Result<AccessorData> readAccessor(const tinygltf::Model& model, int index, bool forceNormalized = false,
                                  int expectedComponents = 0) {
    if (index < 0 || static_cast<std::size_t>(index) >= model.accessors.size()) {
        return Error{"glTF: invalid accessor index " + std::to_string(index)};
    }
    const tinygltf::Accessor& acc = model.accessors[static_cast<std::size_t>(index)];
    const int components = tinygltf::GetNumComponentsInType(static_cast<std::uint32_t>(acc.type));
    if (components <= 0 || !validComponentType(acc.componentType)) {
        return Error{"glTF: unsupported accessor type"};
    }
    if (expectedComponents != 0 && components != expectedComponents) {
        return Error{"glTF: accessor " + std::to_string(index) + " has an unexpected component count"};
    }
    const bool integerType = acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT;
    const bool normalized = acc.normalized || (forceNormalized && integerType);
    const std::size_t compSize = static_cast<std::size_t>(
        tinygltf::GetComponentSizeInBytes(static_cast<std::uint32_t>(acc.componentType)));
    const std::size_t elemSize = compSize * static_cast<std::size_t>(components);

    AccessorData out;
    out.count = acc.count;
    out.components = components;
    out.values.assign(out.count * static_cast<std::size_t>(components), 0.0);

    if (acc.bufferView >= 0) {
        const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(acc.bufferView)];
        const std::size_t stride = view.byteStride != 0 ? view.byteStride : elemSize;
        if (stride < elemSize) {
            return Error{"glTF: bufferView byteStride is smaller than the accessor element"};
        }
        if (out.count > 0) {
            const std::size_t needed = (out.count - 1) * stride + elemSize;
            const auto base = viewBytes(model, acc.bufferView, acc.byteOffset, needed);
            if (!base) {
                return base.error();
            }
            for (std::size_t i = 0; i < out.count; ++i) {
                const unsigned char* element = base.value() + i * stride;
                for (int c = 0; c < components; ++c) {
                    out.values[i * static_cast<std::size_t>(components) + static_cast<std::size_t>(c)] =
                        readComponent(element + static_cast<std::size_t>(c) * compSize, acc.componentType,
                                      normalized);
                }
            }
        }
    }

    if (acc.sparse.isSparse) {
        const auto& sparse = acc.sparse;
        const std::size_t n = static_cast<std::size_t>(sparse.count);
        if (!validComponentType(sparse.indices.componentType) ||
            sparse.indices.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            return Error{"glTF: invalid sparse index type"};
        }
        const std::size_t idxSize = static_cast<std::size_t>(
            tinygltf::GetComponentSizeInBytes(static_cast<std::uint32_t>(sparse.indices.componentType)));
        const auto idxBase =
            viewBytes(model, sparse.indices.bufferView, sparse.indices.byteOffset, n * idxSize);
        if (!idxBase) {
            return idxBase.error();
        }
        const auto valBase =
            viewBytes(model, sparse.values.bufferView, sparse.values.byteOffset, n * elemSize);
        if (!valBase) {
            return valBase.error();
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double target =
                readComponent(idxBase.value() + i * idxSize, sparse.indices.componentType, false);
            if (target < 0.0 || target >= static_cast<double>(out.count)) {
                return Error{"glTF: sparse accessor index out of range"};
            }
            const std::size_t dst = static_cast<std::size_t>(target) * static_cast<std::size_t>(components);
            for (int c = 0; c < components; ++c) {
                out.values[dst + static_cast<std::size_t>(c)] =
                    readComponent(valBase.value() + i * elemSize + static_cast<std::size_t>(c) * compSize,
                                  acc.componentType, normalized);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Outils sur les nœuds
// ---------------------------------------------------------------------------------------------

Transform nodeLocal(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 m(1.0f);
        for (std::size_t i = 0; i < 16; ++i) {
            m[static_cast<int>(i / 4)][static_cast<int>(i % 4)] = static_cast<float>(node.matrix[i]);
        }
        return Transform::fromMat4(m);
    }
    Transform t;
    if (node.translation.size() == 3) {
        t.t = glm::vec3(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
                        static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        // glTF : (x, y, z, w) ; glm::quat : (w, x, y, z).
        glm::quat q(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        t.r = glm::normalize(q);
    }
    if (node.scale.size() == 3) {
        t.s = glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                        static_cast<float>(node.scale[2]));
    }
    return t;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    std::size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }
    return s.substr(first);
}

// ---------------------------------------------------------------------------------------------
// Importeur
// ---------------------------------------------------------------------------------------------

class Importer {
public:
    Importer(const tinygltf::Model& model, const ProgressCallback& progress)
        : model_(model), progress_(progress) {}

    Result<Character> run(std::string name) {
        character_.name = std::move(name);
        if (!report(0.35f, "Building skeleton")) {
            return cancelled();
        }
        if (auto r = buildNodeTables(); !r) {
            return r.error();
        }
        if (auto r = buildSkeleton(); !r) {
            return r.error();
        }
        computeBindShapes();
        loadMaterials();
        if (!report(0.5f, "Building meshes")) {
            return cancelled();
        }
        if (auto r = buildMeshes(); !r) {
            return r.error();
        }
        if (!report(0.85f, "Reading animations")) {
            return cancelled();
        }
        createMorphBones();
        if (auto r = buildAnimations(); !r) {
            return r.error();
        }
        if (!report(1.0f, "Validating")) {
            return cancelled();
        }
        for (const Mesh& mesh : character_.meshes) {
            if (auto valid = mesh.validate(); !valid) {
                return Error{"glTF: invalid mesh after import: " + valid.error().message};
            }
        }
        if (character_.skeleton.boneCount() > 256) {
            warn("the skeleton has more than 256 bones; GPU skinning supports at most 256");
        }
        return std::move(character_);
    }

private:
    using NodeIndex = int;

    bool report(float fraction, std::string_view stage) const {
        return !progress_ || progress_(fraction, stage);
    }
    static Error cancelled() { return Error{kImportCancelled}; }

    void warn(const std::string& message) {
        spdlog::warn("glTF import: {}", message);
        character_.warnings.push_back(message);
    }

    Result<void> buildNodeTables() {
        const std::size_t n = model_.nodes.size();
        parent_.assign(n, -1);
        for (std::size_t i = 0; i < n; ++i) {
            for (const int child : model_.nodes[i].children) {
                if (child < 0 || static_cast<std::size_t>(child) >= n) {
                    return Error{"glTF: node child index out of range"};
                }
                if (parent_[static_cast<std::size_t>(child)] != -1) {
                    return Error{"glTF: a node has several parents"};
                }
                parent_[static_cast<std::size_t>(child)] = static_cast<int>(i);
            }
        }
        // Détection de cycles : remonter depuis chaque nœud ne doit jamais dépasser n pas.
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t steps = 0;
            for (int p = parent_[i]; p != -1; p = parent_[static_cast<std::size_t>(p)]) {
                if (++steps > n) {
                    return Error{"glTF: the node hierarchy contains a cycle"};
                }
            }
        }
        nodeToBone_.assign(n, kNoBone);
        return {};
    }

    glm::mat4 globalMatrix(NodeIndex node) const {
        glm::mat4 m(1.0f);
        for (int i = node; i != -1; i = parent_[static_cast<std::size_t>(i)]) {
            m = nodeLocal(model_.nodes[static_cast<std::size_t>(i)]).toMat4() * m;
        }
        return m;
    }

    Result<void> buildSkeleton() {
        const std::size_t n = model_.nodes.size();
        std::vector<char> isJoint(n, 0);
        for (const tinygltf::Skin& skin : model_.skins) {
            for (const int joint : skin.joints) {
                if (joint < 0 || static_cast<std::size_t>(joint) >= n) {
                    return Error{"glTF: skin joint index out of range"};
                }
                isJoint[static_cast<std::size_t>(joint)] = 1;
            }
        }
        // Les nœuds non-joints situés ENTRE deux joints font partie de la hiérarchie d'os (sans poids) ;
        // ceux au-dessus du joint racine sont figés dans son bind local.
        std::vector<char> isBone = isJoint;
        for (std::size_t j = 0; j < n; ++j) {
            if (!isJoint[j]) {
                continue;
            }
            std::vector<int> path;
            int p = parent_[j];
            while (p != -1 && !isJoint[static_cast<std::size_t>(p)]) {
                path.push_back(p);
                p = parent_[static_cast<std::size_t>(p)];
            }
            if (p != -1) {
                for (const int q : path) {
                    isBone[static_cast<std::size_t>(q)] = 1;
                }
            }
        }

        for (std::size_t i = 0; i < n; ++i) {
            if (parent_[i] == -1) {
                if (auto r = visit(static_cast<int>(i), kNoBone, isBone); !r) {
                    return r;
                }
            }
        }

        // Longueur d'affichage : distance au premier enfant, sinon moitié de celle du parent.
        Skeleton& skeleton = character_.skeleton;
        for (const BoneId id : skeleton.order()) {
            const std::vector<BoneId> kids = skeleton.children(id);
            float length = 0.0f;
            if (!kids.empty()) {
                length = glm::length(skeleton.bone(kids.front()).bindLocal.t);
            } else if (skeleton.bone(id).parent != kNoBone) {
                length = skeleton.bone(skeleton.bone(id).parent).length * 0.5f;
            }
            skeleton.setLength(id, length > 1e-5f ? length : 0.1f);
        }
        return {};
    }

    Result<void> visit(NodeIndex node, BoneId boneParent, const std::vector<char>& isBone) {
        const std::size_t idx = static_cast<std::size_t>(node);
        const tinygltf::Node& gltfNode = model_.nodes[idx];
        BoneId childParent = boneParent;
        if (isBone[idx]) {
            Transform local = nodeLocal(gltfNode);
            Transform fold;  // ancêtres non-os figés dans le bind local d'un os racine
            if (boneParent == kNoBone && parent_[idx] != -1) {
                fold = Transform::fromMat4(globalMatrix(parent_[idx]));
                local = Transform::fromMat4(fold.toMat4() * local.toMat4());
            }
            std::string name = gltfNode.name.empty() ? "joint_" + std::to_string(node) : gltfNode.name;
            const auto id = character_.skeleton.addBone(std::move(name), boneParent, local);
            if (!id) {
                return id.error();
            }
            nodeToBone_[idx] = id.value();
            if (id.value() >= fold_.size()) {
                fold_.resize(static_cast<std::size_t>(id.value()) + 1);
            }
            fold_[id.value()] = fold;
            childParent = id.value();
        }
        for (const int child : gltfNode.children) {
            if (auto r = visit(child, childParent, isBone); !r) {
                return r;
            }
        }
        return {};
    }

    // Notre inverse bind est recalculé depuis la hiérarchie (inverse du bind global). Le fichier peut en
    // contenir d'autres : si IBM_j = inverse(Gbind_j) * C avec le MÊME C pour tous les joints (cas des
    // modèles dont le maillage vit dans un autre repère), C est appliqué une fois aux sommets à l'import
    // et le résultat est identique. Sinon le bind du fichier n'est pas représentable : avertissement.
    void computeBindShapes() {
        bindShape_.assign(model_.skins.size(), glm::mat4(1.0f));
        const std::vector<glm::mat4>& bindGlobals = character_.skeleton.bindGlobals();
        for (std::size_t s = 0; s < model_.skins.size(); ++s) {
            const tinygltf::Skin& skin = model_.skins[s];
            if (skin.joints.empty()) {
                continue;
            }
            std::vector<glm::mat4> ibm(skin.joints.size(), glm::mat4(1.0f));
            if (skin.inverseBindMatrices >= 0) {
                const auto data = readAccessor(model_, skin.inverseBindMatrices, false, 16);
                if (!data || data.value().count < skin.joints.size()) {
                    warn("skin " + std::to_string(s) + ": unreadable inverseBindMatrices (ignored)");
                    continue;
                }
                for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                    for (int k = 0; k < 16; ++k) {
                        ibm[j][k / 4][k % 4] =
                            static_cast<float>(data.value().values[j * 16 + static_cast<std::size_t>(k)]);
                    }
                }
            }
            const auto shapeOf = [&](std::size_t j) {
                return bindGlobals[nodeToBone_[static_cast<std::size_t>(skin.joints[j])]] * ibm[j];
            };
            const glm::mat4 shape = shapeOf(0);
            float worst = 0.0f;
            for (std::size_t j = 1; j < skin.joints.size(); ++j) {
                const glm::mat4 other = shapeOf(j);
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        worst = std::max(worst, std::fabs(other[c][r] - shape[c][r]));
                    }
                }
            }
            if (worst > 1e-3f) {
                warn("skin " + std::to_string(s) +
                     ": inverseBindMatrices are inconsistent with the node hierarchy; the bind pose may look "
                     "wrong");
            } else {
                bindShape_[s] = shape;
            }
        }
    }

    // --- Matériaux -------------------------------------------------------------------------

    std::shared_ptr<const Image> loadImage(int textureIndex) {
        if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= model_.textures.size()) {
            return nullptr;
        }
        const int source = model_.textures[static_cast<std::size_t>(textureIndex)].source;
        if (source < 0 || static_cast<std::size_t>(source) >= model_.images.size()) {
            return nullptr;
        }
        auto cached = imageCache_.find(source);
        if (cached != imageCache_.end()) {
            return cached->second;
        }
        const tinygltf::Image& src = model_.images[static_cast<std::size_t>(source)];
        std::shared_ptr<const Image> result;
        const std::size_t pixels = static_cast<std::size_t>(std::max(src.width, 0)) *
                                   static_cast<std::size_t>(std::max(src.height, 0));
        if (src.bits == 8 && src.component >= 1 && src.component <= 4 && pixels > 0 &&
            src.image.size() >= pixels * static_cast<std::size_t>(src.component)) {
            auto image = std::make_shared<Image>();
            image->width = src.width;
            image->height = src.height;
            image->rgba.resize(pixels * 4);
            const std::size_t comp = static_cast<std::size_t>(src.component);
            for (std::size_t p = 0; p < pixels; ++p) {
                const unsigned char* in = src.image.data() + p * comp;
                unsigned char* out = image->rgba.data() + p * 4;
                out[0] = in[0];
                out[1] = comp >= 3 ? in[1] : in[0];
                out[2] = comp >= 3 ? in[2] : in[0];
                out[3] = comp == 4 ? in[3] : (comp == 2 ? in[1] : 255);
            }
            result = std::move(image);
        } else {
            warn("texture " + std::to_string(textureIndex) + " could not be decoded (unsupported format)");
        }
        imageCache_[source] = result;
        return result;
    }

    void loadMaterials() {
        materials_.reserve(model_.materials.size());
        for (const tinygltf::Material& src : model_.materials) {
            Material material;
            material.name = src.name.empty() ? "material" : src.name;
            const auto& pbr = src.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() >= 3) {
                material.baseColor = glm::vec3(static_cast<float>(pbr.baseColorFactor[0]),
                                               static_cast<float>(pbr.baseColorFactor[1]),
                                               static_cast<float>(pbr.baseColorFactor[2]));
            }
            material.metallic = static_cast<float>(pbr.metallicFactor);
            material.roughness = static_cast<float>(pbr.roughnessFactor);
            if (pbr.baseColorTexture.index >= 0) {
                material.baseColorTexture = loadImage(pbr.baseColorTexture.index);
            }
            materials_.push_back(std::move(material));
        }
    }

    // --- Maillages -------------------------------------------------------------------------

    Result<void> buildMeshes() {
        for (std::size_t i = 0; i < model_.nodes.size(); ++i) {
            const tinygltf::Node& node = model_.nodes[i];
            if (node.mesh < 0) {
                continue;
            }
            if (static_cast<std::size_t>(node.mesh) >= model_.meshes.size()) {
                return Error{"glTF: node refers to a missing mesh"};
            }
            if (node.skin >= 0 && static_cast<std::size_t>(node.skin) >= model_.skins.size()) {
                return Error{"glTF: node refers to a missing skin"};
            }
            if (!report(0.5f + 0.35f * static_cast<float>(i) / static_cast<float>(model_.nodes.size()),
                        "Building meshes")) {
                return cancelled();
            }
            auto mesh = buildMesh(static_cast<int>(i), model_.meshes[static_cast<std::size_t>(node.mesh)]);
            if (!mesh) {
                return mesh.error();
            }
            if (!mesh.value().vertices.empty()) {
                character_.meshes.push_back(std::move(mesh.value()));
                meshNode_.push_back(static_cast<int>(i));
                std::vector<float> defaults;
                for (const double w : model_.meshes[static_cast<std::size_t>(node.mesh)].weights) {
                    defaults.push_back(static_cast<float>(w));
                }
                meshDefaults_.push_back(std::move(defaults));
            }
        }
        return {};
    }

    Result<Mesh> buildMesh(NodeIndex nodeIndex, const tinygltf::Mesh& src) {
        const tinygltf::Node& node = model_.nodes[static_cast<std::size_t>(nodeIndex)];
        const bool skinned = node.skin >= 0;
        // Un maillage skinné ignore la transformation de son nœud (spécification glTF) ; ses sommets sont
        // ramenés dans le repère de bind du squelette (voir computeBindShapes). Un maillage statique
        // est figé dans le repère monde.
        const glm::mat4 world =
            skinned ? bindShape_[static_cast<std::size_t>(node.skin)] : globalMatrix(nodeIndex);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));

        std::vector<BoneId> skinBones;
        if (skinned) {
            for (const int joint : model_.skins[static_cast<std::size_t>(node.skin)].joints) {
                skinBones.push_back(nodeToBone_[static_cast<std::size_t>(joint)]);
            }
        }

        Mesh mesh;
        const std::string meshName = src.name.empty() ? "mesh_" + std::to_string(nodeIndex) : src.name;
        std::size_t droppedInfluences = 0;
        std::size_t noWeightVertices = 0;

        for (std::size_t p = 0; p < src.primitives.size(); ++p) {
            const tinygltf::Primitive& prim = src.primitives[p];
            const std::string primName = meshName + "." + std::to_string(p);
            auto pos = prim.attributes.find("POSITION");
            if (pos == prim.attributes.end()) {
                warn(primName + ": primitive without POSITION skipped");
                continue;
            }
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
                prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
                warn(primName + ": only triangle primitives are supported (primitive skipped)");
                continue;
            }

            const auto positions = readAccessor(model_, pos->second, false, 3);
            if (!positions) {
                return positions.error();
            }
            const std::size_t vertexCount = positions.value().count;

            const auto vertexBase = static_cast<std::uint32_t>(mesh.vertices.size());
            std::vector<Vertex> vertices(vertexCount);
            for (std::size_t v = 0; v < vertexCount; ++v) {
                const double* d = &positions.value().values[v * 3];
                const glm::vec3 local(static_cast<float>(d[0]), static_cast<float>(d[1]),
                                      static_cast<float>(d[2]));
                vertices[v].position = glm::vec3(world * glm::vec4(local, 1.0f));
            }

            bool hasNormals = false;
            if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
                const auto normals = readAccessor(model_, it->second, false, 3);
                if (!normals) {
                    return normals.error();
                }
                if (normals.value().count == vertexCount) {
                    hasNormals = true;
                    for (std::size_t v = 0; v < vertexCount; ++v) {
                        const double* d = &normals.value().values[v * 3];
                        const glm::vec3 n =
                            normalMatrix * glm::vec3(static_cast<float>(d[0]), static_cast<float>(d[1]),
                                                     static_cast<float>(d[2]));
                        const float len = glm::length(n);
                        vertices[v].normal = len > 1e-8f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                }
            }
            if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
                const auto uvs = readAccessor(model_, it->second, false, 2);
                if (!uvs) {
                    return uvs.error();
                }
                if (uvs.value().count == vertexCount) {
                    for (std::size_t v = 0; v < vertexCount; ++v) {
                        vertices[v].uv0 = glm::vec2(static_cast<float>(uvs.value().values[v * 2]),
                                                    static_cast<float>(uvs.value().values[v * 2 + 1]));
                    }
                }
            }
            if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
                const auto tangents = readAccessor(model_, it->second, false, 4);
                if (tangents && tangents.value().count == vertexCount) {
                    for (std::size_t v = 0; v < vertexCount; ++v) {
                        const double* d = &tangents.value().values[v * 4];
                        const glm::vec3 t =
                            glm::mat3(world) * glm::vec3(static_cast<float>(d[0]), static_cast<float>(d[1]),
                                                         static_cast<float>(d[2]));
                        const float len = glm::length(t);
                        if (len > 1e-8f) {
                            vertices[v].tangent = t / len;
                        }
                    }
                }
            }

            // Indices (convertis en liste de triangles).
            std::vector<std::uint32_t> raw;
            if (prim.indices >= 0) {
                const auto indices = readAccessor(model_, prim.indices, false, 1);
                if (!indices) {
                    return indices.error();
                }
                raw.reserve(indices.value().count);
                for (const double value : indices.value().values) {
                    if (value < 0.0 || value >= static_cast<double>(vertexCount)) {
                        return Error{"glTF: " + primName + ": index out of range"};
                    }
                    raw.push_back(static_cast<std::uint32_t>(value));
                }
            } else {
                raw.resize(vertexCount);
                for (std::size_t v = 0; v < vertexCount; ++v) {
                    raw[v] = static_cast<std::uint32_t>(v);
                }
            }
            std::vector<std::uint32_t> triangles;
            if (prim.mode == TINYGLTF_MODE_TRIANGLES) {
                triangles = raw;
                triangles.resize(triangles.size() / 3 * 3);
            } else if (prim.mode == TINYGLTF_MODE_TRIANGLE_STRIP) {
                for (std::size_t i = 2; i < raw.size(); ++i) {
                    const bool odd = (i % 2) == 1;
                    triangles.insert(triangles.end(),
                                     {odd ? raw[i - 1] : raw[i - 2], odd ? raw[i - 2] : raw[i - 1], raw[i]});
                }
            } else {
                for (std::size_t i = 2; i < raw.size(); ++i) {
                    triangles.insert(triangles.end(), {raw[0], raw[i - 1], raw[i]});
                }
            }

            if (!hasNormals) {
                Mesh tmp;
                tmp.vertices = vertices;
                tmp.indices = triangles;
                tmp.recomputeNormals();
                for (std::size_t v = 0; v < vertexCount; ++v) {
                    vertices[v].normal = tmp.vertices[v].normal;
                }
            }

            // Skinning.
            std::vector<SkinWeight> skin;
            if (skinned) {
                auto weights =
                    readSkin(prim, vertexCount, skinBones, primName, droppedInfluences, noWeightVertices);
                if (!weights) {
                    return weights.error();
                }
                skin = std::move(weights.value());
            } else if (prim.attributes.count("JOINTS_0") != 0) {
                warn(primName + ": JOINTS_0 ignored because the mesh node has no skin");
            }

            Submesh submesh;
            submesh.name = primName;
            submesh.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
            submesh.indexCount = static_cast<std::uint32_t>(triangles.size());
            if (prim.material >= 0 && static_cast<std::size_t>(prim.material) < materials_.size()) {
                submesh.material = materials_[static_cast<std::size_t>(prim.material)];
            }
            for (const std::uint32_t index : triangles) {
                mesh.indices.push_back(vertexBase + index);
            }
            mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());
            if (!prim.targets.empty()) {
                if (auto r = readMorphTargets(prim, src, world, vertexBase, vertexCount, mesh, primName);
                    !r) {
                    return r.error();
                }
            }
            if (skinned) {
                mesh.skinWeights.insert(mesh.skinWeights.end(), skin.begin(), skin.end());
            }
            mesh.submeshes.push_back(std::move(submesh));
        }

        for (MorphTarget& target : mesh.morphTargets)
            target.positionDeltas.resize(mesh.vertices.size(), glm::vec3(0.0f));
        if (droppedInfluences > 0) {
            warn(meshName + ": " + std::to_string(droppedInfluences) +
                 " vertices had more than 4 influences; only the 4 strongest were kept");
        }
        if (noWeightVertices > 0) {
            warn(meshName + ": " + std::to_string(noWeightVertices) +
                 " vertices had no skin weights and were bound to the first joint");
        }
        return mesh;
    }

    // Blend shapes d'une primitive : déplacements POSITION (les autres attributs de cible sont ignorés).
    Result<void> readMorphTargets(const tinygltf::Primitive& prim, const tinygltf::Mesh& src,
                                  const glm::mat4& world, std::uint32_t vertexBase, std::size_t vertexCount,
                                  Mesh& mesh, const std::string& primName) {
        if (mesh.morphTargets.size() < prim.targets.size()) {
            const tinygltf::Value& names = src.extras.Get("targetNames");
            for (std::size_t t = mesh.morphTargets.size(); t < prim.targets.size(); ++t) {
                MorphTarget target;
                target.name =
                    names.IsArray() && t < names.ArrayLen() && names.Get(static_cast<int>(t)).IsString()
                        ? names.Get(static_cast<int>(t)).Get<std::string>()
                        : "Morph " + std::to_string(t + 1);
                mesh.morphTargets.push_back(std::move(target));
            }
        }
        const std::size_t total = static_cast<std::size_t>(vertexBase) + vertexCount;
        for (std::size_t t = 0; t < prim.targets.size(); ++t) {
            const auto it = prim.targets[t].find("POSITION");
            if (it == prim.targets[t].end()) continue;
            const auto deltas = readAccessor(model_, it->second, false, 3);
            if (!deltas) return deltas.error();
            if (deltas.value().count != vertexCount) {
                warn(primName + ": morph target " + std::to_string(t) +
                     " has a wrong vertex count (ignored)");
                continue;
            }
            auto& out = mesh.morphTargets[t].positionDeltas;
            out.resize(total, glm::vec3(0.0f));
            for (std::size_t v = 0; v < vertexCount; ++v) {
                const double* d = &deltas.value().values[v * 3];
                out[vertexBase + v] =
                    glm::mat3(world) *
                    glm::vec3(static_cast<float>(d[0]), static_cast<float>(d[1]), static_cast<float>(d[2]));
            }
        }
        return {};
    }

    // Os de poids des blend shapes (un par cible), créés comme racines de contrôle : translation X = poids.
    void createMorphBones() {
        for (std::size_t m = 0; m < character_.meshes.size(); ++m) {
            Mesh& mesh = character_.meshes[m];
            const std::string base = model_.nodes[static_cast<std::size_t>(meshNode_[m])].name;
            for (std::size_t t = 0; t < mesh.morphTargets.size(); ++t) {
                Transform bind;
                bind.t.x = t < meshDefaults_[m].size() ? meshDefaults_[m][t] : 0.0f;
                const auto id = character_.skeleton.addBone(
                    "Morph_" + (base.empty() ? "mesh" + std::to_string(m) : base) + "_" +
                        mesh.morphTargets[t].name,
                    kNoBone, bind);
                if (!id) continue;
                character_.skeleton.setFlags(id.value(), BoneFlags::Control | BoneFlags::Morph);
                character_.skeleton.setLength(id.value(), 0.05f);
                mesh.morphTargets[t].weightBone = id.value();
            }
        }
    }

    Result<std::vector<SkinWeight>> readSkin(const tinygltf::Primitive& prim, std::size_t vertexCount,
                                             const std::vector<BoneId>& skinBones,
                                             const std::string& primName, std::size_t& dropped,
                                             std::size_t& noWeights) {
        struct Influence {
            BoneId bone;
            float weight;
        };
        std::vector<std::vector<Influence>> perVertex(vertexCount);
        for (int set = 0;; ++set) {
            const auto jointsIt = prim.attributes.find("JOINTS_" + std::to_string(set));
            const auto weightsIt = prim.attributes.find("WEIGHTS_" + std::to_string(set));
            if (jointsIt == prim.attributes.end() || weightsIt == prim.attributes.end()) {
                break;
            }
            const auto joints = readAccessor(model_, jointsIt->second, false, 4);
            if (!joints) {
                return joints.error();
            }
            const auto weights = readAccessor(model_, weightsIt->second, true, 4);
            if (!weights) {
                return weights.error();
            }
            if (joints.value().count != vertexCount || weights.value().count != vertexCount) {
                return Error{"glTF: " + primName + ": JOINTS/WEIGHTS count does not match POSITION"};
            }
            for (std::size_t v = 0; v < vertexCount; ++v) {
                for (std::size_t k = 0; k < 4; ++k) {
                    const double w = weights.value().values[v * 4 + k];
                    if (!(w > 0.0)) {
                        continue;
                    }
                    const double j = joints.value().values[v * 4 + k];
                    if (j < 0.0 || j >= static_cast<double>(skinBones.size())) {
                        return Error{"glTF: " + primName + ": joint index outside of the skin"};
                    }
                    perVertex[v].push_back({skinBones[static_cast<std::size_t>(j)], static_cast<float>(w)});
                }
            }
        }

        std::vector<SkinWeight> out(vertexCount);
        for (std::size_t v = 0; v < vertexCount; ++v) {
            auto& list = perVertex[v];
            if (list.empty()) {
                ++noWeights;
                out[v].joints[0] = skinBones.empty() ? 0 : skinBones.front();
                out[v].weights[0] = 1.0f;
                continue;
            }
            // Fusionne les doublons puis garde les 4 plus forts.
            std::stable_sort(list.begin(), list.end(),
                             [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
            std::vector<Influence> merged;
            for (const Influence& inf : list) {
                auto same = std::find_if(merged.begin(), merged.end(),
                                         [&](const Influence& m) { return m.bone == inf.bone; });
                if (same != merged.end()) {
                    same->weight += inf.weight;
                } else {
                    merged.push_back(inf);
                }
            }
            std::stable_sort(merged.begin(), merged.end(),
                             [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
            if (merged.size() > 4) {
                ++dropped;
            }
            for (std::size_t k = 0; k < std::min<std::size_t>(4, merged.size()); ++k) {
                out[v].joints[k] = merged[k].bone;
                out[v].weights[k] = merged[k].weight;
            }
            if (auto r = normalizeWeights(out[v]); !r) {
                return Error{"glTF: " + primName + ": " + r.error().message};
            }
        }
        return out;
    }

    // --- Animations ------------------------------------------------------------------------

    Result<void> buildAnimations() {
        std::size_t unmapped = 0;
        std::size_t morph = 0;
        for (std::size_t a = 0; a < model_.animations.size(); ++a) {
            const tinygltf::Animation& src = model_.animations[a];
            AnimationClip clip;
            clip.name = src.name.empty() ? "Animation_" + std::to_string(a) : src.name;
            for (const tinygltf::AnimationChannel& channel : src.channels) {
                const auto& target = channel.target_path;
                if (target == "weights") {
                    auto r = readWeightsChannel(src, channel, clip);
                    if (!r) return r.error();
                    if (!r.value()) ++morph;
                    continue;
                }
                if (channel.target_node < 0 ||
                    static_cast<std::size_t>(channel.target_node) >= model_.nodes.size() ||
                    nodeToBone_[static_cast<std::size_t>(channel.target_node)] == kNoBone) {
                    ++unmapped;
                    continue;
                }
                if (channel.sampler < 0 || static_cast<std::size_t>(channel.sampler) >= src.samplers.size()) {
                    return Error{"glTF: animation channel refers to a missing sampler"};
                }
                const tinygltf::AnimationSampler& sampler =
                    src.samplers[static_cast<std::size_t>(channel.sampler)];
                Interpolation mode = Interpolation::Linear;
                if (sampler.interpolation == "STEP") {
                    mode = Interpolation::Constant;
                } else if (sampler.interpolation == "CUBICSPLINE") {
                    mode = Interpolation::Bezier;
                } else if (!sampler.interpolation.empty() && sampler.interpolation != "LINEAR") {
                    return Error{"glTF: unknown interpolation '" + sampler.interpolation + "'"};
                }

                const auto times = readAccessor(model_, sampler.input, false, 1);
                if (!times) {
                    return times.error();
                }
                const bool rotation = target == "rotation";
                const int comps = rotation ? 4 : 3;
                if (!rotation && target != "translation" && target != "scale") {
                    return Error{"glTF: unknown animation path '" + target + "'"};
                }
                const auto values = readAccessor(model_, sampler.output, true, comps);
                if (!values) {
                    return values.error();
                }
                const std::size_t n = times.value().count;
                const std::size_t perKey = mode == Interpolation::Bezier ? 3 : 1;
                if (values.value().count != n * perKey) {
                    return Error{"glTF: animation '" + clip.name +
                                 "': output count does not match the keyframes"};
                }

                const BoneId targetBone = nodeToBone_[static_cast<std::size_t>(channel.target_node)];
                const Transform& fold = fold_[targetBone];
                BoneTracks& tracks = clip.tracks[targetBone];
                const auto at = [&](std::size_t key, std::size_t slot) {
                    return &values.value().values[(key * perKey + slot) * static_cast<std::size_t>(comps)];
                };
                const auto vec = [&](const double* d) {
                    return glm::vec3(static_cast<float>(d[0]), static_cast<float>(d[1]),
                                     static_cast<float>(d[2]));
                };
                const auto quat = [&](const double* d) {
                    return glm::quat(static_cast<float>(d[3]), static_cast<float>(d[0]),
                                     static_cast<float>(d[1]), static_cast<float>(d[2]));
                };
                const std::size_t valueSlot = mode == Interpolation::Bezier ? 1 : 0;
                if (rotation) {
                    tracks.rotation.keys.clear();
                } else if (target == "translation") {
                    tracks.translation.keys.clear();
                } else {
                    tracks.scale.keys.clear();
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double time = times.value().values[k];
                    if (k > 0 && time < times.value().values[k - 1]) {
                        return Error{"glTF: animation '" + clip.name +
                                     "': keyframe times must be increasing"};
                    }
                    clip.duration = std::max(clip.duration, time);
                    if (rotation) {
                        Keyframe<glm::quat> key;
                        key.time = time;
                        key.mode = mode;
                        // fold * (T R S) : la rotation finale est fold.r * R (exact, l'échelle n'y intervient
                        // pas).
                        key.value = fold.r * glm::normalize(quat(at(k, valueSlot)));
                        if (mode == Interpolation::Bezier) {
                            key.inTangent = fold.r * quat(at(k, 0));
                            key.outTangent = fold.r * quat(at(k, 2));
                        }
                        tracks.rotation.keys.push_back(key);
                    } else {
                        Keyframe<glm::vec3> key;
                        key.time = time;
                        key.mode = mode;
                        const bool isTranslation = target == "translation";
                        // Translation : fold.t + fold.r * (fold.s * v) ; échelle : fold.s * v (fold
                        // uniforme).
                        const auto compose = [&](const glm::vec3& v) {
                            return isTranslation ? fold.t + fold.r * (fold.s * v) : fold.s * v;
                        };
                        const auto composeTangent = [&](const glm::vec3& v) {
                            return isTranslation ? fold.r * (fold.s * v) : fold.s * v;
                        };
                        key.value = compose(vec(at(k, valueSlot)));
                        if (mode == Interpolation::Bezier) {
                            key.inTangent = composeTangent(vec(at(k, 0)));
                            key.outTangent = composeTangent(vec(at(k, 2)));
                        }
                        (target == "translation" ? tracks.translation : tracks.scale).keys.push_back(key);
                    }
                }
            }
            if (clip.tracks.empty()) {
                warn("animation '" + clip.name + "' does not animate any skeleton bone and was skipped");
                continue;
            }
            character_.clips.push_back(std::move(clip));
        }
        if (unmapped > 0) {
            warn(std::to_string(unmapped) +
                 " animation channels target nodes that are not skeleton bones (ignored)");
        }
        if (morph > 0) {
            warn(std::to_string(morph) +
                 " morph-target animation channels target meshes without blend shapes (ignored)");
        }
        return {};
    }

    // Canal d'animation `weights` -> pistes de translation X des os de poids. Retourne faux si le nœud n'est
    // pas un maillage.
    Result<bool> readWeightsChannel(const tinygltf::Animation& src, const tinygltf::AnimationChannel& channel,
                                    AnimationClip& clip) {
        const auto found = std::find(meshNode_.begin(), meshNode_.end(), channel.target_node);
        if (found == meshNode_.end()) return false;
        Mesh& mesh = character_.meshes[static_cast<std::size_t>(found - meshNode_.begin())];
        if (mesh.morphTargets.empty()) return false;
        if (channel.sampler < 0 || static_cast<std::size_t>(channel.sampler) >= src.samplers.size()) {
            return Error{"glTF: animation channel refers to a missing sampler"};
        }
        const tinygltf::AnimationSampler& sampler = src.samplers[static_cast<std::size_t>(channel.sampler)];
        Interpolation mode = Interpolation::Linear;
        if (sampler.interpolation == "STEP")
            mode = Interpolation::Constant;
        else if (sampler.interpolation == "CUBICSPLINE")
            mode = Interpolation::Bezier;
        const auto times = readAccessor(model_, sampler.input, false, 1);
        if (!times) return times.error();
        const auto values = readAccessor(model_, sampler.output, false, 1);
        if (!values) return values.error();
        const std::size_t n = times.value().count;
        const std::size_t targets = mesh.morphTargets.size();
        const std::size_t perKey = mode == Interpolation::Bezier ? 3 : 1;
        if (n == 0 || values.value().values.size() != n * perKey * targets) {
            return Error{"glTF: morph weights output does not match the keyframes"};
        }
        for (std::size_t t = 0; t < targets; ++t) {
            const BoneId bone = mesh.morphTargets[t].weightBone;
            if (bone == kNoBone) continue;
            auto& keys = clip.tracks[bone].translation.keys;
            keys.clear();
            for (std::size_t k = 0; k < n; ++k) {
                const double time = times.value().values[k];
                if (k > 0 && time < times.value().values[k - 1]) {
                    return Error{"glTF: animation '" + clip.name + "': keyframe times must be increasing"};
                }
                clip.duration = std::max(clip.duration, time);
                const auto at = [&](std::size_t slot) {
                    return static_cast<float>(values.value().values[(k * perKey + slot) * targets + t]);
                };
                Keyframe<glm::vec3> key;
                key.time = time;
                key.mode = mode;
                key.value = glm::vec3(at(perKey == 3 ? 1 : 0), 0.0f, 0.0f);
                if (mode == Interpolation::Bezier) {
                    key.inTangent = glm::vec3(at(0), 0.0f, 0.0f);
                    key.outTangent = glm::vec3(at(2), 0.0f, 0.0f);
                }
                keys.push_back(key);
            }
        }
        return true;
    }

    const tinygltf::Model& model_;
    const ProgressCallback& progress_;
    Character character_;
    std::vector<int> meshNode_;                     // par maillage du personnage : indice du nœud glTF
    std::vector<std::vector<float>> meshDefaults_;  // par maillage : poids par défaut des blend shapes
    std::vector<int> parent_;
    std::vector<BoneId> nodeToBone_;
    std::vector<Transform> fold_;  // par BoneId : transformation figée des ancêtres non-os (identité sinon)
    std::vector<Material> materials_;
    std::vector<glm::mat4> bindShape_;  // par skin : transformation à appliquer aux sommets skinnés
    std::map<int, std::shared_ptr<const Image>> imageCache_;
};

}  // namespace

Result<Character> importGltf(const std::filesystem::path& path, const ProgressCallback& progress) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return Error{"File not found: " + path.string()};
    }
    const std::string ext = lower(path.extension().string());
    if (ext != ".gltf" && ext != ".glb") {
        return Error{"Unsupported file type '" + ext + "' (expected .gltf or .glb)"};
    }

    const auto u8 = path.u8string();
    const std::string utf8Path(u8.begin(), u8.end());

    if (progress && !progress(0.0f, "Reading file")) {
        return Error{kImportCancelled};
    }
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warning;
    const bool ok = ext == ".glb" ? loader.LoadBinaryFromFile(&model, &err, &warning, utf8Path)
                                  : loader.LoadASCIIFromFile(&model, &err, &warning, utf8Path);
    if (!ok) {
        err = trim(err);
        return Error{"Cannot read glTF file: " +
                     (err.empty() ? std::string("invalid or corrupted file") : err)};
    }
    if (model.asset.version.empty() || model.asset.version[0] != '2') {
        return Error{"Unsupported glTF version '" + model.asset.version + "' (only glTF 2.x is supported)"};
    }
    static const std::set<std::string> kUnsupportedRequired = {
        "KHR_draco_mesh_compression", "EXT_meshopt_compression", "KHR_texture_basisu"};
    for (const std::string& required : model.extensionsRequired) {
        if (kUnsupportedRequired.count(required) != 0) {
            return Error{"Unsupported glTF extension required by the file: " + required};
        }
    }

    if (progress && !progress(0.3f, "Reading file")) {
        return Error{kImportCancelled};
    }
    Importer importer(model, progress);
    auto result = importer.run(path.stem().string());
    if (result && !trim(warning).empty()) {
        result.value().warnings.push_back(trim(warning));
    }
    return result;
}

}  // namespace rigger::io
