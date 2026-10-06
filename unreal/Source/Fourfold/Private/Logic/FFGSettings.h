// Fourfold game logic island - player settings data: clamping and JSON (port of game/ui/game_settings.gd + the UE
// additions of FFourfoldSettings). UFourfoldSettingsSubsystem copies FFourfoldSettings <-> SettingsData and persists the
// JSON text under Saved/Fourfold/settings.json. Parsing uses ff::ParseJson (FJsonObject changed in UE 5.8).
#pragma once

#include <string>

namespace ffg {

struct SettingsData {
	float control_scale = 1.0f;       // 0.8 .. 1.4
	float control_opacity = 0.8f;     // 0.3 .. 1.0
	std::string layout_preset = "default";
	bool left_handed = false;
	bool strong_labels = false;
	int touch_ui_mode = 0;            // 0 auto, 1 always, 2 never
	float camera_sensitivity = 1.0f;  // 0.3 .. 2.5
	bool invert_y = false;
	float screen_shake = 1.0f;
	float flashes = 1.0f;
	bool haptics = true;
	bool reduced_motion = false;
	bool slowmo_assist = false;
	float master_volume = 1.0f;
	float sfx_volume = 1.0f;
	float ambience_volume = 0.8f;
	float ui_volume = 0.8f;
	int quality = -1;                 // -1 auto, 0 low, 1 medium, 2 high
	int frame_rate_cap = 60;          // 30 | 60 | 120
	bool show_debug = false;
};

// Clamps every field into its legal range (a hand-edited file can never break the layout).
SettingsData ClampSettings(const SettingsData& s);
// Pretty JSON text (schema "fourfold.settings/1").
std::string SettingsToJson(const SettingsData& s);
// Missing / damaged text or fields fall back to the defaults (field by field); the result is clamped.
// Returns false when the text could not be parsed at all (out = defaults).
bool SettingsFromJson(const std::string& text, SettingsData& out);
bool SettingsEqual(const SettingsData& a, const SettingsData& b);
// The auto quality tier from a coarse device class: 0 low, 1 medium, 2 high.
int AutoQualityFor(bool is_mobile, int cpu_cores, double memory_gb);

}  // namespace ffg
