// Fourfold core - embedded data loader (see GameData.h).
#include "Data/GameData.h"

#include "Data/EmbeddedData.h"
#include "ff/Json.h"
#include "Util/GdUtil.h"

namespace ff {
namespace GameData {
namespace {

struct DataStore {
	bool ok = true;
	std::string error;
	Dict files[7];
};

const char* const kDataNames[7] = {"moves", "rules", "hooks", "sim", "scenarios", "lab", "move_index"};

const DataStore& store() {
	static const DataStore s = [] {
		DataStore d;
		for (int i = 0; i < 7; ++i) {
			const std::string text = EmbeddedJson(kDataNames[i]);
			Value v;
			JsonError err;
			if (text.empty() || !ParseJson(text, v, &err) || !v.is_dict()) {
				d.ok = false;
				d.error += std::string(kDataNames[i]) + ".json: " + (text.empty() ? std::string("missing") : err.message) + "; ";
				d.files[i] = Dict();
			} else {
				d.files[i] = v.as_dict();
			}
		}
		return d;
	}();
	return s;
}

}  // namespace

bool ok() { return store().ok; }
const std::string& error() { return store().error; }
const Dict& moves() { return store().files[0]; }
const Dict& rules() { return store().files[1]; }
const Dict& hooks() { return store().files[2]; }
const Dict& sim() { return store().files[3]; }
const Dict& scenarios() { return store().files[4]; }
const Dict& lab() { return store().files[5]; }
const Dict& move_index() { return store().files[6]; }

std::string fn_name(const Value& v) {
	if (!v.is_string()) return std::string();
	const std::string& s = v.as_string();
	if (!begins_with(s, "fn:")) return std::string();
	return s.substr(3);
}

}  // namespace GameData
}  // namespace ff
