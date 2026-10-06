// Fourfold core - port of game/ui/lab/lab_tuning.gd: live tuning of every numeric field of every move def (flat keys
// and tiers.t1..t3) and of the counter-rule cells (eff, full_at, partial_at, perfect_mult, absorb_on_fail, w.K..P).
// Process-wide state like Godot's static vars. Saved as JSON text (Session::LabSaveTuning):
//   {"schema": 1, "moves": {"<move id>/<path>": value, ...}, "rules": {"<threat|counter>#<idx>/<field>": value, ...}}
#pragma once

#include "ff/Value.h"

#include <string>
#include <vector>

namespace ff {
namespace LabTuning {

struct Field {
	std::string path;
	double value = 0.0;
	int tier = -1;
};
struct RuleCell {
	std::string key;
	int idx = 0;
	std::string id, label;
};

std::vector<Field> numeric_fields(const std::string& id);
Value get_value(const std::string& id, const std::string& path);
bool set_value(const std::string& id, const std::string& path, double value);
Value original_value(const std::string& id, const std::string& path);
bool is_changed(const std::string& id, const std::string& path);
int change_count();

std::vector<RuleCell> rule_cells();
std::vector<Field> rule_fields(const std::string& key, int idx);
bool set_rule_value(const std::string& key, int idx, const std::string& path, double value);
Value rule_original(const std::string& key, int idx, const std::string& path);
bool rule_changed(const std::string& key, int idx, const std::string& path);

void reset_all();
std::string save_json();
int load_json(const std::string& text);   // fields applied, -1 when the text is not a tuning file
std::string export_text();

}  // namespace LabTuning
}  // namespace ff
