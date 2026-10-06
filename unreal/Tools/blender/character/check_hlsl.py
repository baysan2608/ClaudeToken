"""Compile-check unreal/Shaders/Character/FFFighter.ush with DXC (DXIL ps_6_0 and SPIR-V), wrapping each helper the
way the material Custom nodes call it (additional outputs are `inout` parameters of the generated function).

    python3 unreal/Tools/blender/character/check_hlsl.py <path to dxc binary>
DXC: https://github.com/microsoft/DirectXShaderCompiler/releases (linux_dxc_*.tar.gz; set LD_LIBRARY_PATH to its lib/).
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
USH = os.path.abspath(os.path.join(HERE, "..", "..", "..", "Shaders", "Character", "FFFighter.ush"))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "..", "Content", "Python")))

WRAPPER = r'''
#include "FFFighter.ush"
Texture2D T; SamplerState S;
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 n : NORMAL; };
float3 CustomStatus(float3 Base, float Rough, float Rim, float Wet, float Frost, float Burn, float Glow,
                    float3 ElementColor, float3 Noise, float T, float Porosity, inout float OutRough, inout float3 OutEmissive)
{
%STATUS%
}
float3 CustomTint(float3 Neutral, float Mask, float3 Main, float3 Accent, float3 Trim, float3 Select, float Gain)
{ return FFTint(Neutral, Mask, Main, Accent, Trim, Select, Gain); }
float3 CustomSheen(float3 Base, float Rim, float Strength) { return FFClothSheen(Base, Rim, Strength); }
float3 CustomScatter(float3 Base, float Mask, float Rim, float3 ScatterColor, float Strength)
{ return FFSkinScatter(Base, Mask, Rim, ScatterColor, Strength); }
float3 CustomDetail(float3 BaseN, float3 DetailN, float Strength) { return FFBlendDetailNormal(BaseN, DetailN, Strength); }
float4 main(PSIn i) : SV_Target
{
    float3 base = T.Sample(S, i.uv).rgb;
    float rim = pow(1.0 - saturate(i.n.z), 3.0);
    float r = 0; float3 e = 0;
    float3 t = CustomTint(base, 1.0, float3(0.1,0.2,0.3), float3(0.5,0.3,0.1), float3(0.8,0.7,0.6), float3(1,0,0), 2.0);
    float3 b = CustomStatus(CustomSheen(t, rim, 0.25), 0.6, rim, 0.3, 0.2, 0.4, 0.5, float3(1,0.5,0.1),
                            T.Sample(S, i.uv * 6).rgb, 1.0, 1.0, r, e);
    float3 n = CustomDetail(normalize(i.n), float3(0.1, 0.2, 0.97), 0.5);
    return float4(b + e + CustomScatter(base, 0.5, rim, float3(0.9,0.3,0.2), 0.2) + n * 0.001, r);
}
'''


def main(dxc):
    import importlib.util
    spec = importlib.util.spec_from_file_location("m", os.path.join(HERE, "..", "..", "..", "Content", "Python",
                                                                     "fourfold", "character", "materials.py"))
    src = open(spec.origin, encoding="utf-8").read()
    code = src.split('STATUS_CODE = """', 1)[1].split('"""', 1)[0]
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "FFFighter.ush"), "w") as f:
            f.write(open(USH, encoding="utf-8").read())
        p = os.path.join(d, "t.hlsl")
        with open(p, "w") as f:
            f.write(WRAPPER.replace("%STATUS%", code))
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = os.path.join(os.path.dirname(os.path.dirname(dxc)), "lib")
        ok = True
        for args in (["-T", "ps_6_0"], ["-T", "ps_6_0", "-spirv"]):
            r = subprocess.run([dxc, *args, "-E", "main", p, "-Fo", os.path.join(d, "o.bin")], env=env,
                               capture_output=True, text=True)
            print(" ".join(args), "OK" if r.returncode == 0 else "FAILED", r.stderr.strip()[-2000:])
            ok &= r.returncode == 0
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
