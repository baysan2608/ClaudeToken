"""Compile-check the FX HLSL exactly the way the materials use it (stream `fx`).

    python3 unreal/Tools/vfx/shader_check/check_shaders.py --dxc <path to dxc> [--params <params.json>]

For every master material of Content/Python/fourfold/fx/spec.py and every Custom node in it, this writes one HLSL file
that includes the node's include files (the /Fourfold virtual path mapped to unreal/Shaders), wraps the node's code in a
function shaped like Unreal's generated CustomExpressionN (inputs typed from the graph sources, texture objects as
Texture2D + SamplerState, additional outputs as inout) and calls it from a vertex or pixel entry point. Each file is
compiled with DXC as DXIL (vs/ps_6_0) and SPIR-V (the first step of Unreal's Metal path), in HLSL 2018 and 2021.
Also lints the shared files against the portability rules of Shaders/Common/FFCommon.ush, and checks that every
parameter name a material declares is a name the C++ logic knows (Private/Logic/FxTypes.h). With --params (written by
ffx_core_soak --dump-params), it also checks that every parameter the logic sets on a material slot is declared by that
slot's master.
DXC: https://github.com/microsoft/DirectXShaderCompiler/releases (linux_dxc_*.tar.gz; LD_LIBRARY_PATH = its lib/).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SHADERS = os.path.join(UNREAL, "Shaders")


def _load_spec():
    """Loads fourfold/fx/spec.py by path (the package __init__ imports `unreal`)."""
    import importlib.util
    path = os.path.join(UNREAL, "Content", "Python", "fourfold", "fx", "spec.py")
    s = importlib.util.spec_from_file_location("ff_fx_spec", path)
    m = importlib.util.module_from_spec(s)
    s.loader.exec_module(m)
    return m


spec = _load_spec()

FX_OWNED = [os.path.join(SHADERS, "Common"), os.path.join(SHADERS, "FX")]

SWIZZLE = re.compile(r"\.\s*([xyzwrgba]{2,4})\b")
SINGLE_CTOR = re.compile(r"\b(float|half|int|uint)([234])\s*\(\s*([A-Za-z_0-9.]+)\s*\)")
# HLSL keywords that are legal C++ identifiers (the shim would accept them, DXC does not).
HLSL_RESERVED = ["line", "point", "triangle", "lineadj", "triangleadj", "sample", "centroid", "linear", "precise",
                 "nointerpolation", "noperspective", "groupshared", "uniform", "vector", "matrix", "texture", "sampler",
                 "shared", "string", "snorm", "unorm", "in", "out", "inout", "pass", "compile", "export", "register",
                 "half", "dword", "fixed", "cbuffer", "tbuffer"]
RESERVED_DECL = re.compile(r"\b(?:float|int|uint|bool|float[234])\s+(" + "|".join(HLSL_RESERVED) + r")\b")
FORBIDDEN = [r"\bddx\b", r"\bddy\b", r"\bfwidth\b", r"\bWave[A-Z]\w*", r"\bQuad[A-Z]\w*", r"\bfloat[234]x[234]\b",
             r"\bhalf\b", r"\bmin16float\b", r"\[branch\]", r"\[loop\]"]


def lint():
    problems = []
    for d in FX_OWNED:
        for name in sorted(os.listdir(d)):
            if not name.endswith(".ush"):
                continue
            path = os.path.join(d, name)
            with open(path, encoding="utf-8") as f:
                for i, line in enumerate(f, 1):
                    code = line.split("//", 1)[0]
                    if SWIZZLE.search(code):
                        problems.append(f"{path}:{i}: multi-letter swizzle (shim rule): {line.strip()}")
                    m = SINGLE_CTOR.search(code)
                    if m:
                        problems.append(f"{path}:{i}: single-argument vector constructor: {m.group(0)}")
                    m = RESERVED_DECL.search(code)
                    if m:
                        problems.append(f"{path}:{i}: HLSL reserved word as a name: {m.group(1)}")
                    for pat in FORBIDDEN:
                        if re.search(pat, code):
                            problems.append(f"{path}:{i}: forbidden in shared code ({pat}): {line.strip()}")
    return problems


def cpp_param_names():
    with open(os.path.join(UNREAL, "Source", "FourfoldFX", "Private", "Logic", "FxTypes.h"), encoding="utf-8") as f:
        src = f.read()
    names = set()
    for arr in ("kParamNames", "kVParamNames"):
        m = re.search(arr + r"\s*=\s*\{(.*?)\};", src, re.S)
        names.update(re.findall(r'"(\w+)"', m.group(1)))
    return names


def stage_tree(tmp):
    root = os.path.join(tmp, "Fourfold")
    shutil.copytree(SHADERS, root)
    for dp, _, fs in os.walk(root):
        for fn in fs:
            if fn.endswith(".ush"):
                p = os.path.join(dp, fn)
                with open(p, encoding="utf-8") as f:
                    s = f.read()
                s = s.replace('#include "/Fourfold/', f'#include "{root}/')
                with open(p, "w", encoding="utf-8") as f:
                    f.write(s)
    return root


def arg_expr(t, k):
    sw = {"float": f"T{k % 4}.x", "float2": f"T{k % 4}.xy", "float3": f"T{k % 4}.xyz", "float4": f"T{k % 4}"}
    return sw[t]


def wrapper(root, mat_key, mat, node_id, node):
    lines = [f"// {mat['asset']} / {node_id}"]
    for inc in mat["includes"]:
        lines.append('#include "' + inc.replace("/Fourfold/", root + "/") + '"')
    lines.append("struct FMaterialParameters { float4 Dummy; };")
    params, call, globals_ = ["FMaterialParameters Parameters"], ["P"], []
    for k, (name, src) in enumerate(node["inputs"].items()):
        t = spec.source_type(src)
        if t == "Texture2D":
            params += [f"Texture2D {name}", f"SamplerState {name}Sampler"]
            globals_ += [f"Texture2D G_{name};", f"SamplerState G_{name}Sampler;"]
            call += [f"G_{name}", f"G_{name}Sampler"]
        elif src.startswith("node:"):
            other = mat["nodes"][src[5:]]
            params.append(f"{other['type']} {name}")
            call.append(arg_expr(other["type"], k))
        else:
            params.append(f"{t} {name}")
            call.append(arg_expr(t, k))
    for oname, otype in node["outputs"].items():
        params.append(f"inout {otype} {oname}")
        call.append(oname)
    lines += globals_
    lines.append(f"{node['type']} CustomExpression0({', '.join(params)})")
    lines.append("{")
    lines += ["\t" + ln for ln in node["code"].splitlines()]
    lines.append("}")
    vs = node["stage"] == "vs"
    if vs:
        lines.append("float4 main(float4 T0 : POSITION, float4 T1 : TEXCOORD0, float4 T2 : TEXCOORD1, float4 T3 : TEXCOORD2)"
                     " : SV_Position")
    else:
        lines.append("float4 main(float4 T0 : SV_Position, float4 T1 : TEXCOORD0, float4 T2 : TEXCOORD1, "
                     "float4 T3 : TEXCOORD2) : SV_Target")
    lines.append("{")
    lines.append("\tFMaterialParameters P;")
    lines.append("\tP.Dummy = T1;")
    acc = []
    for oname, otype in node["outputs"].items():
        zero = {"float": "0.0", "float2": "float2(0.0, 0.0)", "float3": "float3(0.0, 0.0, 0.0)"}[otype]
        lines.append(f"\t{otype} {oname} = {zero};")
        acc.append(oname)
    lines.append(f"\t{node['type']} R = CustomExpression0({', '.join(call)});")
    s = "float4(R, 1.0)" if node["type"] == "float3" else "float4(R, R, R, 1.0)"
    for o in acc:
        t = node["outputs"][o]
        s += f" + float4({o}, 0.0)" if t == "float3" else f" + float4({o}, {o}, {o}, 0.0)"
    lines.append(f"\treturn {s};")
    lines.append("}")
    return "\n".join(lines) + "\n", ("vs_6_0" if vs else "ps_6_0")


def compile_all(dxc, tmp, root):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.join(os.path.dirname(os.path.dirname(dxc)), "lib")
    failures, count, warnings = [], 0, []
    for key, mat in spec.MATERIALS.items():
        for node_id, node in mat["nodes"].items():
            src, profile = wrapper(root, key, mat, node_id, node)
            path = os.path.join(tmp, f"{key}_{node_id}.hlsl")
            with open(path, "w", encoding="utf-8") as f:
                f.write(src)
            for hv in ("2018", "2021"):
                for extra in ([], ["-spirv"]):
                    args = [dxc, "-T", profile, "-E", "main", "-HV", hv, *extra, path, "-Fo", path + ".bin"]
                    r = subprocess.run(args, env=env, capture_output=True, text=True)
                    count += 1
                    out = (r.stdout + r.stderr).strip()
                    tag = f"{mat['asset']}/{node_id} {profile} HV{hv}{' spirv' if extra else ''}"
                    if r.returncode != 0:
                        failures.append(f"{tag}:\n{out}")
                    elif "warning" in out:
                        warnings.append(f"{tag}:\n{out}")
    return count, failures, warnings


def check_params(params_json):
    problems = []
    known = cpp_param_names()
    for key, mat in spec.MATERIALS.items():
        for n in list(mat.get("scalars", {})) + list(mat.get("vectors", {})):
            if n not in known and n not in spec.TUNING_ONLY:
                problems.append(f"{mat['asset']}: parameter {n} is not a logic parameter name (FxTypes.h) nor TUNING_ONLY")
        for node_id, node in mat["nodes"].items():
            for src in node["inputs"].values():
                kind, _, name = src.partition(":")
                if kind == "p" and name not in mat["scalars"]:
                    problems.append(f"{mat['asset']}/{node_id}: input p:{name} not declared in scalars")
                if kind == "v" and name not in mat["vectors"]:
                    problems.append(f"{mat['asset']}/{node_id}: input v:{name} not declared in vectors")
                if kind == "tex" and name not in mat.get("textures", {}):
                    problems.append(f"{mat['asset']}/{node_id}: input tex:{name} not declared in textures")
                if kind == "nmap" and name not in mat.get("normal_maps", {}):
                    problems.append(f"{mat['asset']}/{node_id}: input nmap:{name} not declared in normal_maps")
    if params_json:
        with open(params_json, encoding="utf-8") as f:
            used = json.load(f)
        for slot, names in used.items():
            mat = spec.MATERIALS.get(slot)
            if mat is None:
                problems.append(f"slot {slot} has no master material in spec.MATERIALS")
                continue
            declared = set(mat.get("scalars", {})) | set(mat.get("vectors", {}))
            for n in names:
                if n == "Flipbook":
                    if "Flipbook" not in mat.get("textures", {}):
                        problems.append(f"{mat['asset']}: the logic sets the Flipbook texture but the master has none")
                elif n not in declared:
                    problems.append(f"{mat['asset']}: the logic sets {n} but the master does not declare it")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxc", required=True)
    ap.add_argument("--params", help="JSON {slot: [param names]} from ffx_core_soak --dump-params")
    ap.add_argument("--keep", help="keep the generated HLSL in this directory")
    a = ap.parse_args()
    ok = True
    lp = lint()
    print(f"lint: {len(lp)} problem(s)")
    for p in lp:
        print("  " + p)
    ok &= not lp
    pp = check_params(a.params)
    print(f"parameters: {len(pp)} problem(s)")
    for p in pp:
        print("  " + p)
    ok &= not pp
    tmp = a.keep or tempfile.mkdtemp(prefix="ff_shader_check_")
    os.makedirs(tmp, exist_ok=True)
    if os.path.exists(os.path.join(tmp, "Fourfold")):
        shutil.rmtree(os.path.join(tmp, "Fourfold"))
    root = stage_tree(tmp)
    count, failures, warnings = compile_all(a.dxc, tmp, root)
    print(f"dxc: {count} compiles, {len(failures)} failed, {len(warnings)} with warnings")
    for f in failures:
        print("FAIL " + "\n".join(f.splitlines()[:8]))
    for w in warnings:
        print("WARN " + w)
    ok &= not failures
    if not a.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
