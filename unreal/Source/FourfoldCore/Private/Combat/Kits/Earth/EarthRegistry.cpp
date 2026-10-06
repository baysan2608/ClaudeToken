// Fourfold core - Earth kit registration: part modules (lifecycle by def `part`) and code hooks by Godot name.
// Each sub-element file contributes through the functions listed here; see docs/core/PORT_STATUS.md.
#include "Combat/Kits/Earth/Earth.h"
#include "Combat/Kits/Earth/KitEarth.h"
#include "Sim/Hooks.h"

namespace ff {

namespace {
template <typename NS_START, typename NS_AFTER, typename NS_PHASE, typename NS_TICK, typename NS_INT>
KitStages stages(NS_START s, NS_AFTER a, NS_PHASE p, NS_TICK t, NS_INT i) {
	KitStages k;
	k.start = s;
	k.after = a;
	k.phase = p;
	k.tick = t;
	k.interrupt = i;
	return k;
}

void to_satchel_ref(CombatWorld& w, ActorState& a, MatBody& b, const std::string& why) { EarthMetal::to_satchel(w, a, &b, why); }
}  // namespace

#define FF_EARTH_STAGES(NS) stages(&NS::on_start, &NS::after_startup, &NS::on_phase, &NS::on_tick, &NS::on_interrupt)

void RegisterEarthParts(EarthPartMap& m) {
	m["stone"] = FF_EARTH_STAGES(EarthStone);
	m["metal"] = FF_EARTH_STAGES(EarthMetal);
	m["sand"] = FF_EARTH_STAGES(EarthSand);
	m["magma"] = FF_EARTH_STAGES(EarthMagma);
}

#undef FF_EARTH_STAGES

ToSatchelFn EarthToSatchel() { return &to_satchel_ref; }

void RegisterEarthHooks(HookTable& t) {
	// hook_execute
	t.exec["EarthStone.fangs_execute"] = &EarthStone::fangs_execute;
	t.exec["EarthMetal.disc_execute"] = &EarthMetal::disc_execute;
	t.exec["EarthMetal.owned_execute"] = &EarthMetal::owned_execute;
	t.exec["EarthMetal.line_execute"] = &EarthMetal::line_execute;
	t.exec["EarthMetal.chain_execute"] = &EarthMetal::chain_execute;
	t.exec["EarthMetal.rod_execute"] = &EarthMetal::rod_execute;
	t.exec["EarthSand.slug_execute"] = &EarthSand::slug_execute;
	t.exec["EarthSand.blast_execute"] = &EarthSand::blast_execute;
	t.exec["EarthSand.pit_execute"] = &EarthSand::pit_execute;
	t.exec["EarthMagma.clot_execute"] = &EarthMagma::clot_execute;
	t.exec["EarthMagma.lash_execute"] = &EarthMagma::lash_execute;
	t.exec["EarthMagma.surge_execute"] = &EarthMagma::surge_execute;
	t.exec["EarthMagma.pit_execute"] = &EarthMagma::pit_execute;
	// hook_impact
	t.impact["EarthMetal.lance_impact"] = &EarthMetal::lance_impact;
	t.impact["EarthSand.slug_impact"] = &EarthSand::slug_impact;
	t.impact["EarthMagma.bomb_impact"] = &EarthMagma::bomb_impact;
	// body ticks
	t.body_tick["EarthStone.spike_tick"] = &EarthStone::spike_tick;
	t.body_tick["EarthStone.crag_tick"] = &EarthStone::crag_tick;
	t.body_tick["EarthMetal.metal_tick"] = &EarthMetal::metal_tick;
	t.body_tick["EarthSand.surge_tick"] = &EarthSand::surge_tick;
	t.body_tick["EarthSand.slug_tick"] = &EarthSand::slug_tick;
	t.body_tick["EarthSand.mud_tick"] = &EarthSand::mud_tick;
	// zone effects
	t.zone["EarthSand.cloud_effect"] = &EarthSand::cloud_effect;
	t.zone["EarthSand.quicksand_effect"] = &EarthSand::quicksand_effect;
	t.zone["EarthMagma.pool_effect"] = &EarthMagma::pool_effect;
	t.zone["EarthMagma.pit_effect"] = &EarthMagma::pit_effect;
	// technique previews
	t.preview["EarthMetal.preview"] = &EarthMetal::preview;
	t.preview["EarthSand.preview"] = &EarthSand::preview;
	t.preview["EarthMagma.preview"] = &EarthMagma::preview;
	// channel hook (untagged walls: Bulwark thickening)
	t.channel["EarthStone.bulwark_channels"] = &EarthStone::bulwark_channels;
	// outcome handlers
	t.outcome["EarthRules._o_embed"] = &EarthRules::_o_embed;
	t.outcome["EarthRules._o_stick"] = &EarthRules::_o_stick;
	t.outcome["EarthRules._o_face_heat"] = &EarthRules::_o_face_heat;
	t.outcome["EarthRules._o_absorb_face"] = &EarthRules::_o_absorb_face;
	t.outcome["EarthRules._o_feed_face"] = &EarthRules::_o_feed_face;
	t.outcome["EarthRules._o_set"] = &EarthRules::_o_set;
	t.outcome["EarthRules._o_glass_beads"] = &EarthRules::_o_glass_beads;
	t.outcome["EarthRules._o_crust"] = &EarthRules::_o_crust;
	t.outcome["EarthRules._o_mud"] = &EarthRules::_o_mud;
	t.outcome["EarthRules._o_glassify"] = &EarthRules::_o_glassify;
	t.outcome["EarthRules._o_glass_ground"] = &EarthRules::_o_glass_ground;
	t.outcome["EarthRules._o_plate_heat"] = &EarthRules::_o_plate_heat;
	t.outcome["EarthRules._o_plate_bolt"] = &EarthRules::_o_plate_bolt;
	t.outcome["EarthRules._o_magnet_catch"] = &EarthRules::_o_magnet_catch;
	t.outcome["EarthRules._o_rod_ground"] = &EarthRules::_o_rod_ground;
	t.outcome["EarthRules._o_rod_melt"] = &EarthRules::_o_rod_melt;
	t.outcome["EarthRules._o_melt_in"] = &EarthRules::_o_melt_in;
	t.outcome["EarthRules._o_bolt_grit"] = &EarthRules::_o_bolt_grit;
	t.outcome["EarthRules._o_quench"] = &EarthRules::_o_quench;
	t.outcome["EarthRules._o_smother"] = &EarthRules::_o_smother;
	t.outcome["EarthRules._o_ram_push"] = &EarthRules::_o_ram_push;
	t.outcome["EarthRules._o_ram_blocked"] = &EarthRules::_o_ram_blocked;
	t.outcome["EarthRules._o_ram_both"] = &EarthRules::_o_ram_both;
	t.outcome["EarthRules._o_wrap"] = &EarthRules::_o_wrap;
	t.outcome["EarthRules._o_spike_stop"] = &EarthRules::_o_spike_stop;
	t.outcome["EarthRules._o_glaze"] = &EarthRules::_o_glaze;
	t.outcome["EarthRules._o_drag"] = &EarthRules::_o_drag;
}

}  // namespace ff
