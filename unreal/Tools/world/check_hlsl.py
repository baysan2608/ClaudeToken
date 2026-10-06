"""Compile-check Shaders/Env/FFEnv.ush and every Custom-node body of Content/Python/fourfold/world/materials.py with DXC
(DXIL ps_6_0 and SPIR-V), wrapping each body the way Unreal generates it (named inputs, additional outputs as inout parameters).

    python3 Tools/world/check_hlsl.py [path to dxc]
Default dxc: the copy the character stream downloaded into the shared scratch (see docs/world/README.md to fetch your own:
https://github.com/microsoft/DirectXShaderCompiler/releases, linux_dxc_*.tar.gz).  Unreal's own cross-compile for Metal first
happens on the Mac."""
import importlib.util
import os
import subprocess
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
UE = os.path.abspath(os.path.join(HERE, "..", ".."))
USH_DIR = os.path.join(UE, "Shaders", "Env")
MATERIALS = os.path.join(UE, "Content", "Python", "fourfold", "world", "materials.py")
DEFAULT_DXC = "/tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/character/dxc/bin/dxc"


def load_bodies():
    """Imports materials.py with a permissive stub `unreal` (only the constant tables are needed)."""
    from unittest import mock
    stub = mock.MagicMock(name="unreal")
    sys.modules["unreal"] = stub
    pkg = types.ModuleType("fourfold"); pkg.__path__ = [os.path.join(UE, "Content", "Python", "fourfold")]
    sys.modules["fourfold"] = pkg
    sys.path.insert(0, os.path.join(UE, "Content", "Python"))
    import importlib
    m = importlib.import_module("fourfold.world.materials")
    return m.CUSTOM_BODIES, m.SCALAR_INPUTS


def wrapper(bodies, scalars):
    out = ['#include "FFEnv.ush"', "Texture2D TT; SamplerState SS;"]
    calls = []
    for name, (code, inputs, rtype, extra) in bodies.items():
        params = ", ".join(f"{'float' if i in scalars else 'float3'} {i}" for i in inputs)
        eparams = "".join(f", inout {t} {o}" for o, t in extra)
        out.append(f"{rtype} Custom_{name}({params}{eparams})\n{{\n{code}\n}}")
        args = ", ".join(("0.5" if i in scalars else "float3(0.3, 0.4, 0.5)") for i in inputs)
        decl = "".join(f"{t} v_{name}_{o} = ({t})0;\n    " for o, t in extra)
        eargs = "".join(f", v_{name}_{o}" for o, _ in extra)
        ev = "".join(f" + (float)dot(v_{name}_{o}, 1)" for o, _ in extra)
        calls.append(f"    {decl}acc += (float)dot(Custom_{name}({args}{eargs}), 1){ev};")
    out.append("float4 main(float4 pos : SV_Position) : SV_Target\n{\n    float acc = pos.x * 0.0;\n" + "\n".join(calls) +
               "\n    return float4(acc, 0, 0, 1);\n}")
    return "\n\n".join(out)


def main(dxc):
    bodies, scalars = load_bodies()
    src = wrapper(bodies, scalars)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.join(os.path.dirname(os.path.dirname(dxc)), "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    ok = True
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "FFEnv.ush"), "w") as f:
            f.write(open(os.path.join(USH_DIR, "FFEnv.ush"), encoding="utf-8").read())
        p = os.path.join(d, "t.hlsl")
        with open(p, "w") as f:
            f.write(src)
        for args in (["-T", "ps_6_0"], ["-T", "ps_6_0", "-spirv"]):
            r = subprocess.run([dxc, *args, "-E", "main", p, "-Fo", os.path.join(d, "o.bin")], env=env, capture_output=True, text=True)
            print(" ".join(args), "OK" if r.returncode == 0 else "FAILED")
            if r.returncode != 0:
                print(r.stderr[-3000:])
            ok &= r.returncode == 0
        if os.environ.get("KEEP_HLSL"):
            print(src)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DXC))
