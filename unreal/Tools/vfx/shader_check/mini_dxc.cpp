// Minimal `dxc` command line for check_shaders.py on macOS, where no dxc binary ships: it loads the DirectX Shader
// Compiler library Unreal bundles (Engine/Binaries/ThirdParty/ShaderConductor/Mac/libdxcompiler.dylib) and passes the
// arguments through unchanged (-T / -E / -HV / -spirv / <file>; -Fo is ignored). Exit code 0 = compiled.
// Build (see check_shaders.py --help):
//   UE="/Users/Shared/Epic Games/UE_5.8/Engine"
//   clang++ -std=c++17 -I"$UE/Source/ThirdParty/ShaderConductor/ShaderConductor/External/DirectXShaderCompiler/include" \
//     unreal/Tools/vfx/shader_check/mini_dxc.cpp -o <scratch>/mini_dxc \
//     -L"$UE/Binaries/ThirdParty/ShaderConductor/Mac" -ldxcompiler -Wl,-rpath,"$UE/Binaries/ThirdParty/ShaderConductor/Mac"
#include "dxc/dxcapi.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
	std::vector<std::wstring> args;
	std::string file;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "-Fo" && i + 1 < argc) {
			++i;
			continue;
		}
		if (a.size() > 5 && a.compare(a.size() - 5, 5, ".hlsl") == 0) file = a;   // the source (also passed on: error names)
		args.emplace_back(a.begin(), a.end());
	}
	if (file.empty()) {
		std::fprintf(stderr, "mini_dxc: no input file\n");
		return 2;
	}
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "mini_dxc: cannot read %s\n", file.c_str());
		return 2;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string src = ss.str();
	CComPtr<IDxcUtils> utils;
	CComPtr<IDxcCompiler3> compiler;
	if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
	    FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
		std::fprintf(stderr, "mini_dxc: DxcCreateInstance failed\n");
		return 3;
	}
	CComPtr<IDxcIncludeHandler> include;
	utils->CreateDefaultIncludeHandler(&include);
	DxcBuffer buf;
	buf.Ptr = src.data();
	buf.Size = src.size();
	buf.Encoding = DXC_CP_UTF8;
	std::vector<LPCWSTR> argp;
	for (const std::wstring& w : args) argp.push_back(w.c_str());
	CComPtr<IDxcResult> result;
	if (FAILED(compiler->Compile(&buf, argp.data(), static_cast<UINT32>(argp.size()), include, IID_PPV_ARGS(&result)))) {
		std::fprintf(stderr, "mini_dxc: Compile call failed\n");
		return 3;
	}
	CComPtr<IDxcBlobUtf8> errors;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	if (errors && errors->GetStringLength() > 0) {
		// the bundled library has no DXIL signer: drop that one line (it would mark every compile as a warning)
		std::string msg(errors->GetStringPointer(), errors->GetStringLength());
		const std::string sign = "warning: DXIL signing library";
		const size_t at = msg.find(sign);
		if (at != std::string::npos) {
			const size_t end = msg.find('\n', at);
			msg.erase(at, end == std::string::npos ? std::string::npos : end - at + 1);
		}
		if (!msg.empty()) std::fprintf(stderr, "%s", msg.c_str());
	}
	HRESULT status = S_OK;
	result->GetStatus(&status);
	return SUCCEEDED(status) ? 0 : 1;
}
