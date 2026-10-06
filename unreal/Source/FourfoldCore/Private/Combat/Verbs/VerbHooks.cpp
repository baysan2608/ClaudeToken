// Fourfold core - code hooks of the generic verbs referenced by name in the data.
#include "Combat/Verbs.h"
#include "Sim/Hooks.h"

namespace ff {

void RegisterCoreHooks(HookTable& t) { t.body_tick["VerbVolume.fuse_tick"] = &VerbVolume::fuse_tick; }

}  // namespace ff
