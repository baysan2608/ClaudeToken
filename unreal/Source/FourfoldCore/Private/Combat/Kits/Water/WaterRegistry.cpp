// Fourfold core - Water kit registration: per-move lifecycle overrides (GDScript KitWater.handle) and code hooks by
// Godot name ("WaterX.method" as referenced by Data/moves.json, hooks.json and rules.json).
#include "Combat/Kits/KitDispatch.h"
#include "Combat/Kits/Water/Water.h"
#include "Sim/Hooks.h"

namespace ff {

void RegisterWaterHandlers(KitHandlerMap& h) {
	// Water
	KitStages& bullet = h["water_bullet"];
	bullet.tick = &WaterWater::bullet_tick;
	bullet.phase = &WaterWater::bullet_phase;
	bullet.interrupt = &WaterWater::bullet_interrupt;
	KitStages& slick = h["slick"];
	slick.start = &WaterWater::slick_start;
	slick.after = &WaterWater::after_active;
	KitStages& rip = h["riptide_step"];
	rip.start = &WaterWater::riptide_start;
	rip.after = &WaterWater::after_active;
	rip.tick = &WaterWater::evade_tick;
	rip.phase = &WaterWater::evade_phase;
	// Ice
	h["ice_wall"].start = &WaterIce::wall_start;
	KitStages& glide = h["ice_glide"];
	glide.start = &WaterIce::glide_start;
	glide.after = &WaterWater::after_active;
	glide.tick = &WaterWater::evade_tick;
	glide.phase = &WaterWater::evade_phase;
	h["freeze_draw"].tick = &WaterIce::freeze_draw_tick;
	KitStages& skate = h["skate"];
	skate.after = &WaterIce::skate_after;
	skate.phase = &WaterIce::skate_phase;
	skate.interrupt = &WaterIce::skate_interrupt;
	// Mist
	h["vapor_draw"].tick = &WaterMist::vapor_tick;
	KitStages& step = h["mist_step"];
	step.start = &WaterMist::step_start;
	step.after = &WaterWater::after_active;
	step.tick = &WaterWater::evade_tick;
	step.phase = &WaterMist::step_phase;
	KitStages& walk = h["fog_walk"];
	walk.after = &WaterMist::walk_after;
	walk.phase = &WaterMist::walk_phase;
	walk.interrupt = &WaterMist::walk_interrupt;
	// Plant
	KitStages& lattice = h["living_lattice"];
	lattice.start = &WaterPlant::lattice_start;
	lattice.tick = &WaterPlant::lattice_tick;
	h["deep_roots"].tick = &WaterPlant::roots_stance_tick;
	h["vinegrip"].tick = &WaterPlant::vinegrip_tick;
	KitStages& swing = h["vine_swing"];
	swing.start = &WaterPlant::swing_start;
	swing.after = &WaterWater::after_active;
	swing.tick = &WaterWater::evade_tick;
	swing.phase = &WaterPlant::swing_phase;
	h["canopy"].tick = &WaterPlant::canopy_tick;
}

void RegisterWaterHooks(HookTable& t) {
	// hook_execute
	t.exec["WaterWater.bullet_execute"] = &WaterWater::bullet_execute;
	t.exec["WaterWater.tidal_execute"] = &WaterWater::tidal_execute;
	t.exec["WaterWater.spray_execute"] = &WaterWater::spray_execute;
	t.exec["WaterWater.orb_execute"] = &WaterWater::orb_execute;
	t.exec["WaterIce.rime_execute"] = &WaterIce::rime_execute;
	t.exec["WaterIce.hoarfrost_execute"] = &WaterIce::hoarfrost_execute;
	t.exec["WaterIce.shove_execute"] = &WaterIce::shove_execute;
	t.exec["WaterMist.puff_execute"] = &WaterMist::puff_execute;
	t.exec["WaterMist.lance_execute"] = &WaterMist::lance_execute;
	t.exec["WaterMist.fog_execute"] = &WaterMist::fog_execute;
	t.exec["WaterMist.veil_execute"] = &WaterMist::veil_execute;
	t.exec["WaterMist.blast_execute"] = &WaterMist::blast_execute;
	t.exec["WaterMist.dew_execute"] = &WaterMist::dew_execute;
	t.exec["WaterPlant.lash_execute"] = &WaterPlant::lash_execute;
	t.exec["WaterPlant.burr_execute"] = &WaterPlant::burr_execute;
	t.exec["WaterPlant.roots_execute"] = &WaterPlant::roots_execute;
	t.exec["WaterPlant.thicket_execute"] = &WaterPlant::thicket_execute;
	t.exec["WaterPlant.roll_execute"] = &WaterPlant::roll_execute;
	// hook_tick
	t.tick["WaterWater.ride_tick"] = &WaterWater::ride_tick;
	// hook_impact
	t.impact["WaterWater.orb_impact"] = &WaterWater::orb_impact;
	t.impact["WaterIce.spear_impact"] = &WaterIce::spear_impact;
	t.impact["WaterMist.ball_impact"] = &WaterMist::ball_impact;
	t.impact["WaterPlant.seed_impact"] = &WaterPlant::seed_impact;
	// counter_scale
	t.counter_scale["WaterWater.tidal_scale"] = &WaterWater::tidal_scale;
	// body ticks
	t.body_tick["WaterWater.wave_tick"] = &WaterWater::wave_tick;
	t.body_tick["WaterIce.wall_tick"] = &WaterIce::wall_tick;
	t.body_tick["WaterIce.rime_tick"] = &WaterIce::rime_tick;
	t.body_tick["WaterMist.geyser_tick"] = &WaterMist::geyser_tick;
	t.body_tick["WaterPlant.vine_tick"] = &WaterPlant::vine_tick;
	t.body_tick["WaterPlant.roots_tick"] = &WaterPlant::roots_tick;
	// zone effects
	t.zone["WaterWater.slick_zone"] = &WaterWater::slick_zone;
	t.zone["WaterRules.fog_zone"] = &WaterRules::fog_zone;
	t.zone["WaterRules.ice_floor_zone"] = &WaterRules::ice_floor_zone;
	t.zone["WaterRules.steam_zone"] = &WaterRules::steam_zone;
	// technique previews
	t.preview["WaterWater.tech_preview"] = &WaterWater::tech_preview;
	// outcome handlers
	t.outcome["WaterRules.o_carry"] = &WaterRules::o_carry;
	t.outcome["WaterRules.o_ridge"] = &WaterRules::o_ridge;
	t.outcome["WaterRules.o_freeze"] = &WaterRules::o_freeze;
	t.outcome["WaterRules.o_skin"] = &WaterRules::o_skin;
	t.outcome["WaterRules.o_hot_block"] = &WaterRules::o_hot_block;
	t.outcome["WaterRules.o_dampen"] = &WaterRules::o_dampen;
	t.outcome["WaterRules.o_condense_in"] = &WaterRules::o_condense_in;
	t.outcome["WaterRules.o_brittle"] = &WaterRules::o_brittle;
	t.outcome["WaterRules.o_feed"] = &WaterRules::o_feed;
	t.outcome["WaterRules.o_drown"] = &WaterRules::o_drown;
	t.outcome["WaterRules.o_melt"] = &WaterRules::o_melt;
	t.outcome["WaterRules.o_quench"] = &WaterRules::o_quench;
	t.outcome["WaterRules.o_burn"] = &WaterRules::o_burn;
	t.outcome["WaterRules.o_sling"] = &WaterRules::o_sling;
}

}  // namespace ff
