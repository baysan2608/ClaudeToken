// FourfoldFX logic island - draw list helpers. Owner: stream `fx`.
#include "FxDrawList.h"

#include <algorithm>

namespace ffx {

void SelectLights(std::vector<LightReq>& lights, int maxLights, const Vec3& camPos) {
	if (maxLights <= 0) {
		lights.clear();
		return;
	}
	if (static_cast<int>(lights.size()) <= maxLights) return;
	std::stable_sort(lights.begin(), lights.end(), [&camPos](const LightReq& a, const LightReq& b) {
		const float sa = a.priority * a.intensity, sb = b.priority * b.intensity;
		if (sa != sb) return sa > sb;
		return a.pos.distance_squared_to(camPos) < b.pos.distance_squared_to(camPos);
	});
	lights.resize(static_cast<size_t>(maxLights));
}

}  // namespace ffx
