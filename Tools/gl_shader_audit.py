#!/usr/bin/env python3
#
# Copyright (c) 2026 Fran6nd, ZeroSpades developers.
#
# This file is part of ZeroSpades, a fork of OpenSpades.
#
# ZeroSpades is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ZeroSpades is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

"""
Audits the OpenGL renderer's GLSL programs offline, the way the game builds them.

Every `.program` under `Resources/Shaders/OpenGL` is assembled as
`GLProgramManager` does it (the settings' `#define` prelude, `#include`
expansion, the `*shadow*`-like special lines, a program file read up to its
first empty line), for every combination of the settings that change what is
built, then compiled and linked with glslang as GLSL 1.10, the version the game's
GL 2.1 context compiles its unversioned shaders as. For each linked program it
checks what a driver only reports at link time, on the machine that has the
smallest limits:

  - the samplers each stage really uses, against the GPU's texture image units
  - the samplers the program uses in all, against the texture units there are
    (the renderer gives a unit only to a texture the program samples, and a
    sampler both stages use takes one)
  - the uniform components each stage really uses

The limits default to those of the macOS GL 2.1 context, and are read from a
`SystemMessages.log` that logged them (`GL limit ...` lines) with `--log`.

Usage: Tools/gl_shader_audit.py [--log SystemMessages.log] [--all]
       [--program Water2]

Requires `glslangValidator` on the PATH.
"""

import argparse
import itertools
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
RESOURCES = os.path.join(ROOT, "Resources")

# What the macOS GL 2.1 context (Apple silicon, Metal-backed) reports, used unless
# a log gives the GPU's own.
DEFAULT_LIMITS = {
    "GL_MAX_TEXTURE_IMAGE_UNITS": 16,
    "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS": 16,
    "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS": 16,
    "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS": 4096,
    "GL_MAX_VERTEX_UNIFORM_COMPONENTS": 4096,
}

# The settings that change how a program is built, and their values.
SETTINGS = {
    "r_hdr": (0, 1),
    "r_fogShadow": (0, 1),
    "r_ssao": (0, 1),
    "r_radiosity": (0, 1, 2),
    "r_modelShadows": (0, 1),
    "r_sparseShadowMaps": (0, 1),
}

SAMPLER_TYPES = {
    0x8B5D, 0x8B5E, 0x8B5F, 0x8B60, 0x8B61, 0x8B62,  # 1D, 2D, 3D, cube, 1D/2D shadow
    0x8DC0, 0x8DC1, 0x8DC3, 0x8DC4, 0x8DC5,  # arrays, array shadows, cube shadow
}

# Uniform components of each non-sampler type, as a driver packs them: a vec3
# takes a whole vector, like a vec4.
UNIFORM_COMPONENTS = {
    0x1404: 1, 0x1406: 1, 0x8B56: 1,  # int, float, bool
    0x8B50: 2, 0x8B53: 2, 0x8B57: 2,  # vec2, ivec2, bvec2
    0x8B51: 4, 0x8B54: 4, 0x8B58: 4,  # vec3, ivec3, bvec3
    0x8B52: 4, 0x8B55: 4, 0x8B59: 4,  # vec4, ivec4, bvec4
    0x8B5A: 8, 0x8B5B: 12, 0x8B5C: 16,  # mat2, mat3, mat4
}

STAGE_VERTEX = 1
STAGE_FRAGMENT = 16

# glslang diagnostics that GLSL 1.10 itself allows: the `Lod` lookups are
# built into the vertex shader's language.
FALSE_POSITIVES = [
    re.compile(r"'texture\w*Lod' : required extension not requested: GL_ARB_shader_texture_lod"),
]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def expand_includes(source, path, included, depth=0):
    """`GLProgramManager`'s `ExpandIncludes`: paths relative to the including
    file, each file once per shader."""
    if depth > 16:
        raise RuntimeError("GLSL includes nest too deep at " + path)
    out = []
    for line in source.split("\n"):
        stripped = line.lstrip(" \t")
        if stripped.startswith("#include"):
            m = re.search(r'"([^"]+)"', stripped)
            if not m:
                raise RuntimeError("malformed #include in %s: %s" % (path, line))
            target = os.path.normpath(os.path.join(os.path.dirname(path), m.group(1)))
            if target not in included:
                included.add(target)
                out.append(expand_includes(read(target), target, included, depth + 1))
            out.append("")
            continue
        out.append(line)
    return "\n".join(out)


def prelude(settings):
    """`GLProgramManager::CreateShader`'s defines, after the version the GL 2.1
    context compiles an unversioned shader as."""
    hdr = settings["r_hdr"]
    radiosity = settings["r_radiosity"]
    return "".join([
        "#version 110\n",
        "#define USE_HDR %d\n" % hdr,
        "#define LINEAR_FRAMEBUFFER %d\n" % hdr,
        "#define USE_VOLUMETRIC_FOG %d\n" % settings["r_fogShadow"],
        "#define USE_SSAO %d\n" % settings["r_ssao"],
        "#define USE_RADIOSITY %d\n" % (2 if radiosity >= 2 else 1 if radiosity >= 1 else 0),
    ])


def shadow_shaders(settings, variance=False, skip_ssao=False, facing_up=False):
    """`GLShadowShader::RegisterShader`."""
    s = ["Shadow/CommonNoSSAO.fs" if skip_ssao else "Shadow/Common.fs", "Shadow/Common.vs"]
    s += ["Shadow/MapVariance.fs", "Shadow/MapVariance.vs"] if variance else ["Shadow/Map.fs", "Shadow/Map.vs"]
    if settings["r_modelShadows"]:
        if settings["r_sparseShadowMaps"]:
            s += ["Shadow/ModelSparse.fs", "Shadow/ModelSparse.vs"]
        else:
            s += ["Shadow/Model.fs", "Shadow/Model.vs"]
    else:
        s += ["Shadow/ModelNull.fs", "Shadow/ModelNull.vs"]
    if settings["r_radiosity"]:
        s += ["Shadow/MapRadiosityUp.fs" if facing_up else "Shadow/MapRadiosity.fs",
              "Shadow/MapRadiosity.vs"]
    else:
        s += ["Shadow/MapRadiosityNull.fs", "Shadow/MapRadiosityNull.vs"]
    return ["Shaders/OpenGL/" + x for x in s]


SPECIAL = {
    "*shadow*": lambda st: shadow_shaders(st),
    "*shadow-water*": lambda st: shadow_shaders(st, skip_ssao=True, facing_up=True),
    "*shadow-variance*": lambda st: shadow_shaders(st, variance=True),
    "*dlight*": lambda st: ["Shaders/OpenGL/DynamicLight/" + x for x in
                            ("Lights.fs", "Common.fs", "Common.vs", "MapNull.fs", "MapNull.vs")],
    "*shadowmap*": lambda st: ["Shaders/OpenGL/ShadowMap/" + x for x in
                               ("Common.fs", "Common.vs", "Basic.fs", "Basic.vs")],
}


def program_shaders(program_path, settings):
    """`GLProgramManager::CreateProgram`: the shaders a program attaches."""
    shaders = []
    for line in read(program_path).split("\n"):
        text = line.strip()
        if not text:
            break  # as the game reads it
        if text in SPECIAL:
            shaders += SPECIAL[text](settings)
        elif text.startswith("*"):
            raise RuntimeError("unknown special shader %s in %s" % (text, program_path))
        elif text.startswith("#"):
            continue
        else:
            shaders.append(text)
    return shaders


def link(shaders, settings, workdir):
    """Compiles and links `shaders` with glslang; returns its diagnostics and
    reflection."""
    files = []
    for i, name in enumerate(shaders):
        path = os.path.join(RESOURCES, name)
        ext = ".frag" if name.endswith(".fs") else ".vert" if name.endswith(".vs") else None
        if ext is None:
            raise RuntimeError("unknown shader stage of " + name)
        source = prelude(settings) + expand_includes(read(path), os.path.normpath(path),
                                                     {os.path.normpath(path)})
        out = os.path.join(workdir, "%02d_%s%s" % (i, os.path.basename(name).replace(".", "_"), ext))
        with open(out, "w", encoding="utf-8") as f:
            f.write(source)
        files.append((out, name))
    result = subprocess.run(["glslangValidator", "-l", "-q"] + [f for f, _ in files],
                            capture_output=True, text=True)
    output = result.stdout + result.stderr
    for out, name in files:
        output = output.replace(out, name)
    return output


def parse_reflection(output):
    uniforms = []
    section = None
    for line in output.split("\n"):
        if line.endswith("reflection:"):
            section = line
            continue
        if section == "Uniform reflection:":
            m = re.match(r"(.+?): offset -?\d+, type ([0-9a-f]+), size (\d+), .* stages (\d+)", line)
            if m:
                uniforms.append((m.group(1), int(m.group(2), 16), int(m.group(3)), int(m.group(4))))
    return uniforms


def diagnostics(output):
    found = []
    for line in output.split("\n"):
        if not line.startswith("ERROR") and not line.startswith("WARNING"):
            continue
        if "compilation terminated" in line or re.search(r"\d+ compilation errors", line):
            continue
        if any(p.search(line) for p in FALSE_POSITIVES):
            continue
        found.append(line.strip())
    return found


def limits_from_log(path):
    limits = dict(DEFAULT_LIMITS)
    pattern = re.compile(r"GL limit (GL_[A-Z0-9_]+): (\d+)")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                limits[m.group(1)] = int(m.group(2))
    return limits


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("--log", help="a SystemMessages.log with the GPU's `GL limit` lines")
    parser.add_argument("--program", help="only the programs whose path contains this")
    parser.add_argument("--all", action="store_true",
                        help="list every program and settings combination, not only failures")
    args = parser.parse_args()

    if not shutil.which("glslangValidator"):
        sys.exit("glslangValidator is not on the PATH")

    limits = limits_from_log(args.log) if args.log else dict(DEFAULT_LIMITS)
    print("Limits: " + ", ".join("%s %d" % kv for kv in sorted(limits.items())))

    programs = []
    for dirpath, _, names in os.walk(os.path.join(RESOURCES, "Shaders", "OpenGL")):
        for n in sorted(names):
            if n.endswith(".program"):
                path = os.path.join(dirpath, n)
                if not args.program or args.program in path:
                    programs.append(path)

    keys = list(SETTINGS)
    combos = [dict(zip(keys, values)) for values in itertools.product(*(SETTINGS[k] for k in keys))]

    # Findings, each with the settings it happens with
    findings = {}
    peak = {}
    with tempfile.TemporaryDirectory() as workdir:
        for program in sorted(programs):
            name = os.path.relpath(program, RESOURCES)
            for settings in combos:
                try:
                    shaders = program_shaders(program, settings)
                    output = link(shaders, settings, workdir)
                except (OSError, RuntimeError) as e:
                    findings.setdefault((name, "assembly: %s" % e), []).append(settings)
                    continue

                for d in diagnostics(output):
                    findings.setdefault((name, d), []).append(settings)

                uniforms = parse_reflection(output)

                units = sorted(u[0] for u in uniforms if u[1] in SAMPLER_TYPES)
                key = (name, "program")
                peak[key] = (max(peak.get(key, (0,))[0], len(units)), 0, None)
                if len(units) > limits["GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS"]:
                    findings.setdefault(
                        (name, "program uses %d texture units, above "
                         "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS %d: %s" %
                         (len(units), limits["GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS"],
                          ", ".join(units))), []).append(settings)
                for stage, label, sampler_limit, component_limit in (
                    (STAGE_VERTEX, "vertex", "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS",
                     "GL_MAX_VERTEX_UNIFORM_COMPONENTS"),
                    (STAGE_FRAGMENT, "fragment", "GL_MAX_TEXTURE_IMAGE_UNITS",
                     "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS"),
                ):
                    used = [u for u in uniforms if u[3] & stage]
                    samplers = sorted(u[0] for u in used if u[1] in SAMPLER_TYPES)
                    components = sum(UNIFORM_COMPONENTS.get(u[1], 4) * u[2]
                                     for u in used if u[1] not in SAMPLER_TYPES)
                    key = (name, label)
                    best = peak.get(key, (0, 0, None))
                    peak[key] = (max(best[0], len(samplers)), max(best[1], components),
                                 best[2] if best[0] >= len(samplers) else settings)
                    if len(samplers) > limits[sampler_limit]:
                        findings.setdefault(
                            (name, "%s stage uses %d samplers, above %s %d: %s" %
                             (label, len(samplers), sampler_limit, limits[sampler_limit],
                              ", ".join(samplers))), []).append(settings)
                    if components > limits[component_limit]:
                        findings.setdefault(
                            (name, "%s stage uses %d uniform components, above %s %d" %
                             (label, components, component_limit, limits[component_limit])),
                            []).append(settings)

    if args.all:
        print("\nPeak use per program (samplers, uniform components):")
        for (name, label), (samplers, components, settings) in sorted(peak.items()):
            print("  %-60s %-8s %2d samplers %5d components" % (name, label, samplers, components))

    if not findings:
        print("\nNo findings in %d programs over %d settings combinations." %
              (len(programs), len(combos)))
        return 0

    print("\n%d findings:" % len(findings))
    for (name, what), settings_list in sorted(findings.items()):
        print("\n%s\n  %s\n  with %d of %d settings combinations, e.g. %s" %
              (name, what, len(settings_list), len(combos),
               " ".join("%s=%d" % kv for kv in settings_list[0].items())))
    return 1


if __name__ == "__main__":
    sys.exit(main())
