// Fourfold core - port of game/actors/ai_presets.gd.
#include "AI/AiPresets.h"

#include "Data/GameData.h"
#include "ff/Types.h"
#include "Util/GdUtil.h"

#include <cctype>

namespace ff {
namespace AiPresets {

namespace {
std::string ap_strip_lower(const std::string& t) {
	size_t a = 0, b = t.size();
	while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
	std::string k = t.substr(a, b - a);
	for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return k;
}
bool ap_is_int(const std::string& k) {
	if (k.empty()) return false;
	size_t i = (k[0] == '-' || k[0] == '+') ? 1 : 0;
	if (i >= k.size()) return false;
	for (; i < k.size(); ++i)
		if (!std::isdigit(static_cast<unsigned char>(k[i]))) return false;
	return true;
}
int ap_element_index(const std::string& t) {
	const std::string k = ap_strip_lower(t);
	if (ap_is_int(k)) {
		const int i = to_int(k);
		return i >= 0 && i < 4 ? i : -1;
	}
	const Dict keys = GameData::lab().get("ai_presets").get("ELEMENT_KEYS").as_dict();
	return dint(keys, k, -1);
}
int ap_sub_index(int e, const std::string& t) {
	const std::string k = ap_strip_lower(t);
	if (ap_is_int(k)) {
		const int i = to_int(k);
		return i >= 0 && i < 4 ? i : -1;
	}
	for (int s = 0; s < 4; ++s)
		if (ap_strip_lower(std::string(SubName(e, s))) == k) return s;
	return -1;
}
}  // namespace

const Dict& TABLE() {
	static const Dict t = GameData::lab().get("ai_presets").get("TABLE").as_dict();
	return t;
}

std::vector<std::string> names() { return {"novice", "adept", "master"}; }

Dict get_preset(const std::string& name) {
	const Dict& t = TABLE();
	return (t.has(name) ? t.get(name) : t.get("adept")).as_dict().duplicate(true);
}

std::pair<int, int> parse_element_drill(const std::string& drill) {
	if (!begins_with(drill, "element:")) return {-1, -1};
	const std::string rest = drill.substr(8);
	const size_t slash = rest.find('/');
	const int e = ap_element_index(rest.substr(0, slash));
	if (e < 0) return {-1, -1};
	int s = 0;
	if (slash != std::string::npos) s = ap_sub_index(e, rest.substr(slash + 1));
	if (s < 0) return {-1, -1};
	return {e, s};
}

}  // namespace AiPresets
}  // namespace ff
