#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET

"""Génère les fichiers glTF synthétiques de tests/data (cube, bras 2 os, accessor sparse, >4 influences).

Usage : python3 tools/make_test_gltf.py [--out tests/data]
Les fichiers sont déterministes ; ils couvrent : byteStride, JOINTS_0 UNSIGNED_BYTE, WEIGHTS_0
UNSIGNED_SHORT normalisés, interpolations LINEAR / STEP / CUBICSPLINE, accessors sparse.
"""
import argparse
import base64
import json
import math
import struct
from pathlib import Path

FLOAT, UBYTE, USHORT = 5126, 5121, 5123
ARRAY_BUFFER, ELEMENT_BUFFER = 34962, 34963


class Builder:
    def __init__(self):
        self.data = bytearray()
        self.views, self.accessors = [], []

    def view(self, payload, target=None, stride=None):
        while len(self.data) % 4:
            self.data.append(0)
        v = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target:
            v["target"] = target
        if stride:
            v["byteStride"] = stride
        self.data += payload
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, view, ctype, count, atype, offset=0, **extra):
        a = {"bufferView": view, "byteOffset": offset, "componentType": ctype, "count": count, "type": atype}
        a.update(extra)
        self.accessors.append(a)
        return len(self.accessors) - 1

    def finish(self, doc, path):
        doc["asset"] = {"version": "2.0", "generator": "rigger/tools/make_test_gltf.py"}
        doc["buffers"] = [{"byteLength": len(self.data),
                           "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(self.data)).decode()}]
        doc["bufferViews"], doc["accessors"] = self.views, self.accessors
        Path(path).write_text(json.dumps(doc, indent=1))


def cube(out):
    b = Builder()
    corners = [(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
    pos = b.accessor(b.view(b"".join(struct.pack("<3f", *c) for c in corners), ARRAY_BUFFER), FLOAT, 8, "VEC3",
                     min=[-0.5] * 3, max=[0.5] * 3)
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    idx = [i for q in quads for i in (q[0], q[1], q[2], q[0], q[2], q[3])]
    ind = b.accessor(b.view(struct.pack("<36H", *idx), ELEMENT_BUFFER), USHORT, 36, "SCALAR")
    b.finish({"scene": 0, "scenes": [{"nodes": [0]}],
              "nodes": [{"name": "Cube", "mesh": 0, "translation": [0, 1, 0]}],
              "materials": [{"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1]}}],
              "meshes": [{"name": "Cube", "primitives": [{"attributes": {"POSITION": pos}, "indices": ind,
                                                          "material": 0}]}]}, out / "cube.gltf")


def arm(out):
    """Cylindre-bras de 2 m le long de Y, deux os (épaule en y=0, coude en y=1)."""
    b = Builder()
    rings, seg = 5, 6
    inter, joints, weights = bytearray(), bytearray(), bytearray()
    count = 0
    for r in range(rings):
        y = 2.0 * r / (rings - 1)
        w1 = min(max(y - 0.5, 0.0), 1.0)  # 0 sous 0.5, 1 au-dessus de 1.5
        for s in range(seg):
            a = 2 * math.pi * s / seg
            n = (math.cos(a), 0.0, math.sin(a))
            inter += struct.pack("<6f", 0.1 * n[0], y, 0.1 * n[2], *n)
            joints += struct.pack("<4B", 0, 1, 0, 0)
            w_forearm = int(round(w1 * 65535))
            weights += struct.pack("<4H", 65535 - w_forearm, w_forearm, 0, 0)
            count += 1
    view = b.view(bytes(inter), ARRAY_BUFFER, stride=24)
    pos = b.accessor(view, FLOAT, count, "VEC3", 0, min=[-0.1, 0, -0.1], max=[0.1, 2, 0.1])
    nor = b.accessor(view, FLOAT, count, "VEC3", 12)
    jnt = b.accessor(b.view(bytes(joints), ARRAY_BUFFER), UBYTE, count, "VEC4")
    wgt = b.accessor(b.view(bytes(weights), ARRAY_BUFFER), USHORT, count, "VEC4", normalized=True)
    idx = []
    for r in range(rings - 1):
        for s in range(seg):
            n = (s + 1) % seg
            lo, lon, up, upn = r * seg + s, r * seg + n, (r + 1) * seg + s, (r + 1) * seg + n
            idx += [lo, lon, upn, lo, upn, up]
    ind = b.accessor(b.view(struct.pack(f"<{len(idx)}H", *idx), ELEMENT_BUFFER), USHORT, len(idx), "SCALAR")
    ibm = b.accessor(b.view(struct.pack("<32f",
                                        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                                        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1)), FLOAT, 2, "MAT4")
    # Animation : rotation LINEAR du coude (0 -> 90 degres autour de Z en 1 s), translation STEP de
    # l'épaule, échelle CUBICSPLINE de l'épaule (tangentes nulles).
    times = b.accessor(b.view(struct.pack("<3f", 0.0, 0.5, 1.0)), FLOAT, 3, "SCALAR", min=[0.0], max=[1.0])
    s45, c45 = math.sin(math.radians(45) / 2), math.cos(math.radians(45) / 2)
    s90, c90 = math.sin(math.radians(90) / 2), math.cos(math.radians(90) / 2)
    rot = b.accessor(b.view(struct.pack("<12f", 0, 0, 0, 1, 0, 0, s45, c45, 0, 0, s90, c90)), FLOAT, 3, "VEC4")
    tr = b.accessor(b.view(struct.pack("<9f", 0, 0, 0, 0.5, 0, 0, 0, 0, 0)), FLOAT, 3, "VEC3")
    scale = []
    for sx in (1.0, 2.0, 1.0):
        scale += [0, 0, 0, sx, 1, 1, 0, 0, 0]  # in-tangent, valeur, out-tangent
    sc = b.accessor(b.view(struct.pack("<27f", *scale)), FLOAT, 9, "VEC3")
    b.finish({
        "scene": 0, "scenes": [{"nodes": [0, 2]}],
        "nodes": [{"name": "shoulder", "children": [1]},
                  {"name": "elbow", "translation": [0, 1, 0]},
                  {"name": "ArmMesh", "mesh": 0, "skin": 0}],
        "skins": [{"name": "ArmSkin", "joints": [0, 1], "inverseBindMatrices": ibm, "skeleton": 0}],
        "materials": [{"name": "Skin", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.6, 0.9, 1]}}],
        "meshes": [{"name": "Arm", "primitives": [{"attributes": {"POSITION": pos, "NORMAL": nor,
                                                                   "JOINTS_0": jnt, "WEIGHTS_0": wgt},
                                                   "indices": ind, "material": 0}]}],
        "animations": [{"name": "Bend",
                        "samplers": [{"input": times, "output": rot, "interpolation": "LINEAR"},
                                     {"input": times, "output": tr, "interpolation": "STEP"},
                                     {"input": times, "output": sc, "interpolation": "CUBICSPLINE"}],
                        "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}},
                                     {"sampler": 1, "target": {"node": 0, "path": "translation"}},
                                     {"sampler": 2, "target": {"node": 0, "path": "scale"}}]}],
    }, out / "arm.gltf")


def sparse(out):
    """Quad dont POSITION n'a pas de bufferView : tout vient de l'accessor sparse (4 sommets sur 4)."""
    b = Builder()
    sidx = b.view(struct.pack("<4B", 0, 1, 2, 3))
    quad = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    sval = b.view(b"".join(struct.pack("<3f", *p) for p in quad))
    pos = len(b.accessors)
    b.accessors.append({"componentType": FLOAT, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0],
                        "sparse": {"count": 4, "indices": {"bufferView": sidx, "componentType": UBYTE},
                                   "values": {"bufferView": sval}}})
    ind = b.accessor(b.view(struct.pack("<6H", 0, 1, 2, 0, 2, 3), ELEMENT_BUFFER), USHORT, 6, "SCALAR")
    b.finish({"scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "Quad", "mesh": 0}],
              "meshes": [{"name": "Quad", "primitives": [{"attributes": {"POSITION": pos}, "indices": ind}]}]},
             out / "sparse.gltf")


def influences(out):
    """Triangle skinné par 6 joints (JOINTS_0 + JOINTS_1) : l'import doit garder les 4 plus forts."""
    b = Builder()
    pos = b.accessor(b.view(b"".join(struct.pack("<3f", *p) for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]),
                            ARRAY_BUFFER), FLOAT, 3, "VEC3", min=[0, 0, 0], max=[1, 1, 0])
    j0 = b.accessor(b.view(struct.pack("<12B", *([0, 1, 2, 3] * 3)), ARRAY_BUFFER), UBYTE, 3, "VEC4")
    j1 = b.accessor(b.view(struct.pack("<12B", *([4, 5, 0, 0] * 3)), ARRAY_BUFFER), UBYTE, 3, "VEC4")
    w0 = b.accessor(b.view(struct.pack("<12f", *([0.3, 0.25, 0.2, 0.1] * 3)), ARRAY_BUFFER), FLOAT, 3, "VEC4")
    w1 = b.accessor(b.view(struct.pack("<12f", *([0.1, 0.05, 0.0, 0.0] * 3)), ARRAY_BUFFER), FLOAT, 3, "VEC4")
    ind = b.accessor(b.view(struct.pack("<3H", 0, 1, 2), ELEMENT_BUFFER), USHORT, 3, "SCALAR")
    nodes = [{"name": f"j{i}", "children": [i + 1]} for i in range(5)] + [{"name": "j5"}]
    nodes.append({"name": "Tri", "mesh": 0, "skin": 0})
    b.finish({"scene": 0, "scenes": [{"nodes": [0, 6]}], "nodes": nodes,
              "skins": [{"joints": [0, 1, 2, 3, 4, 5]}],
              "meshes": [{"name": "Tri", "primitives": [{"attributes": {
                  "POSITION": pos, "JOINTS_0": j0, "JOINTS_1": j1, "WEIGHTS_0": w0, "WEIGHTS_1": w1},
                  "indices": ind}]}]}, out / "influences.gltf")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tests/data")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cube(out)
    arm(out)
    sparse(out)
    influences(out)
