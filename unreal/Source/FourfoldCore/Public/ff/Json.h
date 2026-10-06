// Fourfold core - JSON <-> ff::Value (engine-free, no exceptions, locale-independent).
// FROZEN CONTRACT (architect). Owner after creation: stream `core`.
// UE modules (Fourfold, FourfoldFX, FourfoldAudio) parse their runtime JSON (clips.json, anim_map.json,
// sfx_manifest.json, ...) with these functions instead of FJsonObject, whose API changed in UE 5.8.
//
// Tagged objects written by the Godot exporter (unreal/Tools/godot_export/export_core_data.gd) are decoded:
//   {"$v2":[x,y]} -> Vec2      {"$v3":[x,y,z]} -> Vec3      {"$v3a":[[x,y,z],...]} -> Array of Vec3
//   {"$color":[r,g,b,a]} -> Array of 4 floats              {"$map":[[k,v],...]} -> Dict (keys stringified)
//   {"$f":"inf"|"-inf"|"nan"} -> Float                    {"$fn":"Class.method"} -> String "fn:Class.method"
// Numbers containing '.', 'e' or 'E' become Float, all others Int. Objects keep their key order.
#pragma once

#include "ff/Config.h"
#include "ff/Value.h"

#include <string>
#include <string_view>

namespace ff {

struct JsonError {
	size_t offset = 0;
	int line = 0;
	int column = 0;
	std::string message;
};

// Parses `text` into `out`. Returns false (and fills `err` if given) on malformed input; `out` is then Nil.
FOURFOLDCORE_API bool ParseJson(std::string_view text, Value& out, JsonError* err = nullptr);
// Serializes `v`. indent < 0: compact one line; indent >= 0: pretty with that many spaces per level.
// Vec2/Vec3 are written tagged ({"$v3":[...]}) so ParseJson round-trips them; floats always contain '.' or 'e'.
FOURFOLDCORE_API std::string ToJson(const Value& v, int indent = -1);

}  // namespace ff
