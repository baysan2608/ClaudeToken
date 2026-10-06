// FourfoldFX - integration soak: the real ff::Session plays every move (Lab try: 4 elements x 4 subs x 10 slots, tap
// and T3) and an AI duel; every frame goes through the FX director. Fails on unmapped bodies, invalid meshes,
// duplicate keys, over-budget lights or NaN parameters; prints coverage and timing. Only built by tests/CMakeLists.
#if defined(FF_LOGIC_TESTS)
#include "FxDirector.h"

#include "ff/FourfoldCore.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ffx;

namespace {

struct Totals {
	int frames = 0, failures = 0, maxItems = 0, maxTris = 0, maxLights = 0, maxOneShots = 0, maxViews = 0;
	double totalMs = 0.0, maxMs = 0.0;
	std::string maxWhere;
	int over2ms = 0;
	std::map<std::string, int> events;
	std::set<std::string> unmapped;
	std::map<std::string, int> kinds;   // view kinds seen
	// per material slot: parameter ranges, vector params, flipbooks and vertex streams seen (--params)
	struct SlotUse {
		int items = 0;
		std::map<int, std::pair<float, float>> ranges;
		std::set<int> vparams;
		std::set<int> flipbooks;
		std::set<int> assets;
		bool uv1 = false, uv2 = false, col = false, attached = false, shadow = false;
	};
	std::array<SlotUse, kNumMatSlots> slots;
};

void Tally(const DrawItem& it, Totals& t) {
	Totals::SlotUse& u = t.slots[static_cast<size_t>(it.mat)];
	++u.items;
	for (int i = 0; i < kNumParams; ++i) {
		if (!it.params.Has(static_cast<P>(i))) continue;
		const float v = it.params.s[static_cast<size_t>(i)];
		auto f = u.ranges.find(i);
		if (f == u.ranges.end()) u.ranges[i] = {v, v};
		else f->second = {std::min(f->second.first, v), std::max(f->second.second, v)};
	}
	for (int i = 0; i < kNumVParams; ++i)
		if (it.params.Has(static_cast<PV>(i))) u.vparams.insert(i);
	if (it.params.flipbook != Flipbook::None) u.flipbooks.insert(static_cast<int>(it.params.flipbook));
	if (it.asset != MeshAsset::None) u.assets.insert(static_cast<int>(it.asset));
	if (it.mesh) {
		u.uv1 = u.uv1 || !it.mesh->uv1.empty();
		u.uv2 = u.uv2 || !it.mesh->uv2.empty();
		u.col = u.col || !it.mesh->col.empty();
	}
	u.attached = u.attached || it.attachActor >= 0;
	u.shadow = u.shadow || it.castShadow;
}

void PrintSlots(const Totals& t) {
	for (int s = 0; s < kNumMatSlots; ++s) {
		const Totals::SlotUse& u = t.slots[static_cast<size_t>(s)];
		std::printf("slot %s: %d items%s%s%s%s%s\n", std::string(kMatSlotNames[static_cast<size_t>(s)]).c_str(), u.items,
		            u.uv1 ? " uv1" : "", u.uv2 ? " uv2" : "", u.col ? " col" : "", u.attached ? " attached" : "",
		            u.shadow ? " shadow" : "");
		for (const auto& kv : u.ranges)
			std::printf("    %-14s %9.3f .. %9.3f\n", std::string(kParamNames[static_cast<size_t>(kv.first)]).c_str(),
			            static_cast<double>(kv.second.first), static_cast<double>(kv.second.second));
		std::printf("    vec:");
		for (int v : u.vparams) std::printf(" %s", std::string(kVParamNames[static_cast<size_t>(v)]).c_str());
		std::printf("  flipbooks:");
		for (int f : u.flipbooks) std::printf(" %s", std::string(kFlipbookNames[static_cast<size_t>(f)]).c_str());
		std::printf("  assets:");
		for (int a : u.assets) std::printf(" %s", std::string(kMeshAssetNames[static_cast<size_t>(a)]).c_str());
		std::printf("\n");
	}
}

// {slot: [parameter names set by the logic (+ "Flipbook")]} for Tools/vfx/shader_check/check_shaders.py --params.
bool DumpParams(const Totals& t, const char* path) {
	FILE* f = std::fopen(path, "w");
	if (!f) return false;
	std::fprintf(f, "{\n");
	bool firstSlot = true;
	for (int s = 0; s < kNumMatSlots; ++s) {
		const Totals::SlotUse& u = t.slots[static_cast<size_t>(s)];
		if (u.items == 0) continue;
		std::fprintf(f, "%s  \"%s\": [", firstSlot ? "" : ",\n", std::string(kMatSlotNames[static_cast<size_t>(s)]).c_str());
		firstSlot = false;
		bool first = true;
		auto put = [&](std::string_view n) {
			std::fprintf(f, "%s\"%s\"", first ? "" : ", ", std::string(n).c_str());
			first = false;
		};
		for (const auto& kv : u.ranges) put(kParamNames[static_cast<size_t>(kv.first)]);
		for (int v : u.vparams) put(kVParamNames[static_cast<size_t>(v)]);
		if (!u.flipbooks.empty()) put("Flipbook");
		std::fprintf(f, "]");
	}
	std::fprintf(f, "\n}\n");
	std::fclose(f);
	return true;
}

bool Finite(float v) { return v == v && std::fabs(v) < 1e8f; }

void Check(const DrawList& dl, Totals& t, const char* where) {
	std::set<uint32_t> keys;
	for (const DrawItem& it : dl.items) {
		Tally(it, t);
		if (!keys.insert(it.key).second) {
			++t.failures;
			std::printf("  duplicate key %u (%s)\n", it.key, where);
		}
		if (it.mesh && !it.mesh->Valid()) {
			++t.failures;
			std::printf("  invalid mesh key %u mat %d (%s)\n", it.key, static_cast<int>(it.mat), where);
		}
		if (!it.mesh) {
			++t.failures;
			std::printf("  item without mesh / fallback key %u (%s)\n", it.key, where);
		}
		for (int i = 0; i < kNumParams; ++i)
			if (it.params.Has(static_cast<P>(i)) && !Finite(it.params.s[static_cast<size_t>(i)])) {
				++t.failures;
				std::printf("  non-finite param %s key %u (%s)\n", std::string(kParamNames[static_cast<size_t>(i)]).c_str(), it.key, where);
			}
		const Vec3& p = it.xform.pos;
		if (!Finite(p.x) || !Finite(p.y) || !Finite(p.z)) {
			++t.failures;
			std::printf("  non-finite position key %u (%s)\n", it.key, where);
		}
		if (it.mesh)
			for (const Vec3& v : it.mesh->pos)
				if (!Finite(v.x) || !Finite(v.y) || !Finite(v.z)) {
					++t.failures;
					std::printf("  non-finite vertex key %u mat %d (%s)\n", it.key, static_cast<int>(it.mat), where);
					break;
				}
	}
	if (static_cast<int>(dl.lights.size()) > 4) {
		++t.failures;
		std::printf("  %zu lights (%s)\n", dl.lights.size(), where);
	}
}

void RunTicks(ff::Session& s, FxDirector& dir, int ticks, Totals& t, const char* where, int quality) {
	ff::Snapshot prev = s.GetSnapshot();
	std::vector<ff::Event> events;
	for (int i = 0; i < ticks; ++i) {
		ff::InputFrame in;
		s.Step(in, 0.0f);
		events.clear();
		s.TakeEvents(events);
		for (const ff::Event& e : events) ++t.events[e.type];
		const ff::Snapshot& cur = s.GetSnapshot();
		for (const ff::BodyView& b : cur.bodies) ++t.kinds[std::string(ViewKindName(SelectView(b).kind))];
		// two rendered frames per tick at 120 Hz-ish alphas exercise the interpolation path
		for (int f = 0; f < 2; ++f) {
			FxFrameIn fi;
			fi.prev = &prev;
			fi.curr = &cur;
			fi.alpha = f == 0 ? 0.5f : 1.0f;
			fi.events = f == 0 ? &events : nullptr;
			fi.dt = 1.0f / 120.0f;
			fi.arena = &s.Arena();
			fi.quality = quality;
			fi.cam.pos = Vec3(0.0f, 4.0f, 12.0f);
			fi.cam.fwd = Norm(Vec3(0.0f, -0.3f, -1.0f));
			const auto t0 = std::chrono::steady_clock::now();
			const DrawList& dl = dir.Update(fi);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			t.totalMs += ms;
			if (ms > t.maxMs) {
				t.maxMs = ms;
				t.maxWhere = where;
			}
			if (ms > 2.0) ++t.over2ms;
			++t.frames;
			const FxStats& st = dir.Stats();
			t.maxItems = std::max(t.maxItems, st.items);
			t.maxTris = std::max(t.maxTris, st.triangles);
			t.maxLights = std::max(t.maxLights, st.lights);
			t.maxOneShots = std::max(t.maxOneShots, st.oneShots);
			t.maxViews = std::max(t.maxViews, st.views);
			for (const std::string& u : st.unmapped)
				if (t.unmapped.insert(u).second) {
					std::printf("  unmapped %s first seen in %s\n", u.c_str(), where);
					for (const ff::BodyView& b : cur.bodies)
						if (b.form == ff::Form::Zone && SelectView(b).kind == ViewKind::None)
							std::printf("    zone body %d tag '%s' fx_mat '%s' props %s\n", b.id, b.tag.c_str(), b.fx_mat.c_str(),
							            b.props.to_string().c_str());
				}
			Check(dl, t, where);
		}
		prev = cur;
	}
}

}  // namespace

int main(int argc, char** argv) {
	std::string err;
	if (!ff::Session::DataOk(&err)) {
		std::printf("core data failed: %s\n", err.c_str());
		return 1;
	}
	bool quick = false, params = false;
	const char* dumpPath = nullptr;
	for (int a = 1; a < argc; ++a) {
		quick = quick || std::strcmp(argv[a], "--quick") == 0;
		params = params || std::strcmp(argv[a], "--params") == 0;
		if (std::strcmp(argv[a], "--dump-params") == 0 && a + 1 < argc) dumpPath = argv[++a];
	}
	Totals t;
	FxDirector dir;
	{
		ff::Session s;
		if (!s.LoadScenario("lab")) {
			std::printf("lab scenario failed\n");
			return 1;
		}
		int tries = 0;
		for (int e = 0; e < 4; ++e)
			for (int sub = 0; sub < 4; ++sub)
				for (int slot = 0; slot < 10; ++slot)
					for (int tier : {0, 3}) {
						if (quick && (slot % 3 != 0)) continue;
						s.LabClear();
						s.LabHeal();
						s.LabTry(e, sub, static_cast<ff::Slot>(slot), tier);
						char where[128];
						std::snprintf(where, sizeof(where), "lab e%d s%d slot%d t%d (%s)", e, sub, slot, tier,
						              s.ResolveMove(e, sub, static_cast<ff::Slot>(slot)).c_str());
						RunTicks(s, dir, tier > 0 ? 240 : 150, t, where, (e + sub + slot) % 3);
						++tries;
					}
		std::printf("lab tries: %d\n", tries);
	}
	{
		ff::Session s;
		ff::ScenarioOptions o;
		o.autoplay = "duel";
		o.spar_kit = "all";
		if (s.LoadScenario("spar", o)) {
			dir.Reset();
			RunTicks(s, dir, quick ? 600 : 60 * 90, t, "spar duel", 2);
		} else {
			std::printf("spar scenario failed\n");
			++t.failures;
		}
	}
	std::printf("frames %d, avg %.3f ms, max %.3f ms per Update; max items %d, tris %d, lights %d, one-shots %d, views %d\n",
	            t.frames, t.totalMs / std::max(t.frames, 1), t.maxMs, t.maxItems, t.maxTris, t.maxLights, t.maxOneShots,
	            t.maxViews);
	std::printf("slowest Update in: %s; frames over 2 ms: %d\n", t.maxWhere.c_str(), t.over2ms);
	std::printf("event types seen (%zu):", t.events.size());
	for (const auto& kv : t.events) std::printf(" %s=%d", kv.first.c_str(), kv.second);
	std::printf("\nview kinds seen:");
	for (const auto& kv : t.kinds) std::printf(" %s=%d", kv.first.c_str(), kv.second);
	std::printf("\n");
	if (!t.unmapped.empty()) {
		std::printf("UNMAPPED bodies:");
		for (const std::string& u : t.unmapped) std::printf(" %s", u.c_str());
		std::printf("\n");
		++t.failures;
	}
	if (params) PrintSlots(t);
	if (dumpPath && !DumpParams(t, dumpPath)) {
		std::printf("could not write %s\n", dumpPath);
		++t.failures;
	}
	std::printf("%s (%d failures)\n", t.failures == 0 ? "OK" : "FAILED", t.failures);
	return t.failures == 0 ? 0 : 1;
}
#endif
