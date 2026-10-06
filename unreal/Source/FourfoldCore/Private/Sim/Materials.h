// Fourfold core - port of game/core/materials.gd: per-material properties (COMBAT_SPEC "Engine" E5).
// Thermal constants are game rules (HU, degrees C); hardness feeds the counter rule (barrier CP = mass x hardness).
#pragma once

#include "ff/Types.h"
#include "ff/Value.h"

#include <string_view>

namespace ff {

class MatBody;

namespace Materials {

inline constexpr double FOG_CONDUCTION = 0.6;

struct Props {
	double c, melt, latent, max_temp, hardness, hardness_frozen, ignite, burn_rate, fire_decay;
	bool conductive, brittle, porous, flammable, magnetic, insulator;
};
const Props& props(Mat m);
// Materials.prop(mat, key, default) for data-driven callers (Lab, kits).
Value prop(int mat, std::string_view key, const Value& def = Value());

bool is_fusible(Mat m);
inline bool is_fusible(int m) { return is_fusible(static_cast<Mat>(m)); }
double c(Mat m);
double melt(Mat m);
double latent(Mat m);
double max_temp(Mat m);

double tag_hardness(std::string_view tag, bool* found);
double hardness(const MatBody& b);
bool is_brittle(const MatBody& b);
bool conducts(const MatBody& b);
double conduction_factor(const MatBody& b);
bool insulates(const MatBody& b);
bool is_flammable(const MatBody& b);
bool is_magnetic(const MatBody& b);

bool is_brittle_tag(std::string_view tag);
bool is_conductive_tag(std::string_view tag);
bool is_insulating_tag(std::string_view tag);

}  // namespace Materials
}  // namespace ff
