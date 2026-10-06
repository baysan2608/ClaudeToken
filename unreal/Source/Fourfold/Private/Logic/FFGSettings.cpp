// Fourfold game logic island - player settings data (see FFGSettings.h).
#include "FFGSettings.h"

#include "FFGMath.h"
#include "ff/Json.h"
#include "ff/Value.h"

#include <cmath>

namespace ffg {

SettingsData ClampSettings(const SettingsData& in) {
	SettingsData s = in;
	auto fin = [](float v, float def) { return std::isfinite(v) ? v : def; };
	s.control_scale = Clampf(fin(s.control_scale, 1.0f), 0.8f, 1.4f);
	s.control_opacity = Clampf(fin(s.control_opacity, 0.8f), 0.3f, 1.0f);
	if (s.layout_preset != "default" && s.layout_preset != "compact" && s.layout_preset != "wide") s.layout_preset = "default";
	s.touch_ui_mode = Clampi(s.touch_ui_mode, 0, 2);
	s.camera_sensitivity = Clampf(fin(s.camera_sensitivity, 1.0f), 0.3f, 2.5f);
	s.screen_shake = Clampf(fin(s.screen_shake, 1.0f), 0.0f, 1.0f);
	s.flashes = Clampf(fin(s.flashes, 1.0f), 0.0f, 1.0f);
	s.master_volume = Clampf(fin(s.master_volume, 1.0f), 0.0f, 1.0f);
	s.sfx_volume = Clampf(fin(s.sfx_volume, 1.0f), 0.0f, 1.0f);
	s.ambience_volume = Clampf(fin(s.ambience_volume, 0.8f), 0.0f, 1.0f);
	s.ui_volume = Clampf(fin(s.ui_volume, 0.8f), 0.0f, 1.0f);
	s.quality = Clampi(s.quality, -1, 2);
	if (s.frame_rate_cap != 30 && s.frame_rate_cap != 60 && s.frame_rate_cap != 120)
		s.frame_rate_cap = s.frame_rate_cap < 45 ? 30 : (s.frame_rate_cap < 90 ? 60 : 120);
	return s;
}

std::string SettingsToJson(const SettingsData& in) {
	const SettingsData s = ClampSettings(in);
	ff::Dict d;
	d.set("schema", "fourfold.settings/1");
	d.set("control_scale", static_cast<double>(s.control_scale));
	d.set("control_opacity", static_cast<double>(s.control_opacity));
	d.set("layout_preset", s.layout_preset);
	d.set("left_handed", s.left_handed);
	d.set("strong_labels", s.strong_labels);
	d.set("touch_ui_mode", s.touch_ui_mode);
	d.set("camera_sensitivity", static_cast<double>(s.camera_sensitivity));
	d.set("invert_y", s.invert_y);
	d.set("screen_shake", static_cast<double>(s.screen_shake));
	d.set("flashes", static_cast<double>(s.flashes));
	d.set("haptics", s.haptics);
	d.set("reduced_motion", s.reduced_motion);
	d.set("slowmo_assist", s.slowmo_assist);
	d.set("master_volume", static_cast<double>(s.master_volume));
	d.set("sfx_volume", static_cast<double>(s.sfx_volume));
	d.set("ambience_volume", static_cast<double>(s.ambience_volume));
	d.set("ui_volume", static_cast<double>(s.ui_volume));
	d.set("quality", s.quality);
	d.set("frame_rate_cap", s.frame_rate_cap);
	d.set("show_debug", s.show_debug);
	return ff::ToJson(ff::Value(d), 2);
}

namespace {
float SetGetF(const ff::Value& v, const char* k, float def) {
	const ff::Value& x = v[k];
	return x.is_number() ? static_cast<float>(x.as_float()) : def;
}
bool SetGetB(const ff::Value& v, const char* k, bool def) {
	const ff::Value& x = v[k];
	if (x.is_bool()) return x.as_bool();
	if (x.is_number()) return x.as_float() != 0.0;
	return def;
}
int SetGetI(const ff::Value& v, const char* k, int def) {
	const ff::Value& x = v[k];
	if (!x.is_number()) return def;
	const double d = x.as_float();
	if (!std::isfinite(d)) return def;
	return static_cast<int>(std::lround(Clampf(static_cast<float>(d), -1000.0f, 1000.0f)));
}
}  // namespace

bool SettingsFromJson(const std::string& text, SettingsData& out) {
	out = SettingsData();
	ff::Value v;
	if (!ff::ParseJson(text, v) || !v.is_dict()) return false;
	const SettingsData d;
	out.control_scale = SetGetF(v, "control_scale", d.control_scale);
	out.control_opacity = SetGetF(v, "control_opacity", d.control_opacity);
	if (v["layout_preset"].is_string()) out.layout_preset = v["layout_preset"].as_string();
	out.left_handed = SetGetB(v, "left_handed", d.left_handed);
	out.strong_labels = SetGetB(v, "strong_labels", d.strong_labels);
	out.touch_ui_mode = SetGetI(v, "touch_ui_mode", d.touch_ui_mode);
	out.camera_sensitivity = SetGetF(v, "camera_sensitivity", d.camera_sensitivity);
	out.invert_y = SetGetB(v, "invert_y", d.invert_y);
	out.screen_shake = SetGetF(v, "screen_shake", d.screen_shake);
	out.flashes = SetGetF(v, "flashes", d.flashes);
	out.haptics = SetGetB(v, "haptics", d.haptics);
	out.reduced_motion = SetGetB(v, "reduced_motion", d.reduced_motion);
	out.slowmo_assist = SetGetB(v, "slowmo_assist", d.slowmo_assist);
	out.master_volume = SetGetF(v, "master_volume", d.master_volume);
	out.sfx_volume = SetGetF(v, "sfx_volume", d.sfx_volume);
	out.ambience_volume = SetGetF(v, "ambience_volume", d.ambience_volume);
	out.ui_volume = SetGetF(v, "ui_volume", d.ui_volume);
	out.quality = SetGetI(v, "quality", d.quality);
	out.frame_rate_cap = SetGetI(v, "frame_rate_cap", d.frame_rate_cap);
	out.show_debug = SetGetB(v, "show_debug", d.show_debug);
	out = ClampSettings(out);
	return true;
}

bool SettingsEqual(const SettingsData& a, const SettingsData& b) {
	auto eq = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
	return eq(a.control_scale, b.control_scale) && eq(a.control_opacity, b.control_opacity) && a.layout_preset == b.layout_preset &&
	       a.left_handed == b.left_handed && a.strong_labels == b.strong_labels && a.touch_ui_mode == b.touch_ui_mode &&
	       eq(a.camera_sensitivity, b.camera_sensitivity) && a.invert_y == b.invert_y && eq(a.screen_shake, b.screen_shake) &&
	       eq(a.flashes, b.flashes) && a.haptics == b.haptics && a.reduced_motion == b.reduced_motion &&
	       a.slowmo_assist == b.slowmo_assist && eq(a.master_volume, b.master_volume) && eq(a.sfx_volume, b.sfx_volume) &&
	       eq(a.ambience_volume, b.ambience_volume) && eq(a.ui_volume, b.ui_volume) && a.quality == b.quality &&
	       a.frame_rate_cap == b.frame_rate_cap && a.show_debug == b.show_debug;
}

int AutoQualityFor(bool is_mobile, int cpu_cores, double memory_gb) {
	if (!is_mobile) return 2;
	// iPhone 12 (A14, 4 GB, 6 cores) is the floor target: medium. 6 GB+ devices (iPhone 13 Pro / 14 / 15 / iPad Air 5+): high.
	if (memory_gb >= 5.5 && cpu_cores >= 6) return 2;
	if (memory_gb >= 3.5) return 1;
	return 0;
}

}  // namespace ffg
