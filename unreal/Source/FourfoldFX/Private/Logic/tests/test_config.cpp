// FourfoldFX logic island - config tests. Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxConfig.h"
#include "FxDrawList.h"

#include <fstream>
#include <sstream>

using namespace ffx;

FXT_TEST(config_roundtrip_and_override) {
	FxConfig a;
	const std::string js = a.ToJson();
	FXT_CHECK(js.find("fourfold.fx_config/1") != std::string::npos);
	FxConfig b;
	b.mat[0] = Color(0, 0, 0, 1);
	b.quality[1].cloudOuter = 99;
	std::string err, warn;
	FXT_CHECK(b.LoadJson(js, &err, &warn));
	FXT_CHECK(warn.empty());
	FXT_NEAR(b.mat[0].r, a.mat[0].r, 1e-3);
	FXT_CHECK(b.quality[1].cloudOuter == a.quality[1].cloudOuter);
	FXT_CHECK(b.ToJson() == js);
	// partial override
	FxConfig c;
	FXT_CHECK(c.LoadJson(R"({"palette": {"mat": {"flame": [0.1, 0.2, 0.3]}}, "quality": [{"max_lights": 1}],
	                        "bursts": {"dust": {"speed": 3.5, "bogus": 1}}, "lights": {"intensity_scale": 10}})",
	                     &err, &warn));
	FXT_NEAR(c.MatColor(Fam::Flame).g, 0.2f, 1e-6);
	FXT_CHECK(c.Q(0).maxLights == 1);
	FXT_CHECK(c.Q(1).maxLights == a.Q(1).maxLights);
	FXT_NEAR(c.Burst(Burst::Dust).speed, 3.5f, 1e-6);
	FXT_CHECK(warn.find("bogus") != std::string::npos);
	FXT_NEAR(c.lights.intensityScale, 10.0f, 1e-6);
	// malformed
	FxConfig d;
	FXT_CHECK(!d.LoadJson("{ \"palette\": ", &err));
	FXT_CHECK(!err.empty());
	FXT_CHECK(d.materials[0] == "/Game/Fourfold/FX/Materials/M_FX_Rock");
	FXT_CHECK(d.materials[static_cast<size_t>(MatSlot::LavaStrip)] == "/Game/Fourfold/FX/Materials/M_FX_LavaStrip");
	FXT_CHECK(d.meshes[static_cast<size_t>(MeshAsset::Rock3)] == "/Game/Fourfold/FX/Meshes/SM_FX_rock_3");
}

FXT_TEST(config_committed_file_matches_defaults) {
	// The committed runtime file must be the generated default (regenerate: ffx_logic_tests --write-config <path>).
	std::ifstream f(std::string(FFX_UNREAL_DIR) + "/Content/Fourfold/Data/fx_config.json", std::ios::binary);
	FXT_CHECK(f.good());
	if (!f.good()) return;
	std::stringstream ss;
	ss << f.rdbuf();
	FxConfig c;
	std::string err, warn;
	FXT_CHECK(c.LoadJson(ss.str(), &err, &warn));
	FXT_CHECK(warn.empty());
	FXT_CHECK(ss.str() == FxConfig().ToJson());
}

FXT_TEST(linear_conversion) {
	const Color l = Linear(Color(0.5f, 1.0f, 0.0f, 0.3f));
	FXT_NEAR(l.r, 0.214f, 2e-3);
	FXT_NEAR(l.g, 1.0f, 1e-6);
	FXT_NEAR(l.b, 0.0f, 1e-6);
	FXT_NEAR(l.a, 0.3f, 1e-6);
}

FXT_TEST(select_lights_budget) {
	std::vector<LightReq> ls;
	for (int i = 0; i < 7; ++i) {
		LightReq r;
		r.key = static_cast<uint32_t>(i);
		r.intensity = static_cast<float>(i);
		r.priority = 1.0f;
		ls.push_back(r);
	}
	SelectLights(ls, 4, Vec3());
	FXT_CHECK(ls.size() == 4u);
	FXT_CHECK(ls[0].key == 6u && ls[3].key == 3u);
	SelectLights(ls, 0, Vec3());
	FXT_CHECK(ls.empty());
}

#endif
