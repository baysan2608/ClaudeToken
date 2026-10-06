// Fourfold core - Air kit registration: per-move lifecycle overrides (GDScript KitAir.handle) and code hooks by Godot
// name ("AirX.method" as referenced by Data/moves.json, hooks.json and rules.json).
#include "Combat/Kits/Air/Air.h"
#include "Combat/Kits/Air/AirGust.h"
#include "Combat/Kits/KitDispatch.h"
#include "Sim/Hooks.h"

namespace ff {

void RegisterAirHandlers(KitHandlerMap& h) {
	// Vortex
	KitStages& eye = h["vortex_eye"];
	eye.after = &AirVortex::eye_after;
	eye.tick = &AirVortex::eye_tick;
	KitStages& whirl = h["vortex_whirl"];
	whirl.after = &AirVortex::whirl_after;
	whirl.phase = &AirVortex::whirl_phase;
	whirl.interrupt = &AirVortex::whirl_interrupt;
	// Vacuum
	KitStages& well = h["vacuum_well"];
	well.phase = &AirVacuum::well_phase;
	well.interrupt = &AirVacuum::well_interrupt;
	h["vacuum_hop"].phase = &AirVacuum::hop_phase;
	h["vacuum_slipstream"].tick = &AirVacuum::slipstream_tick;
	// Sound
	h["sound_thunder_step"].phase = &AirSound::boom_phase;
	KitStages& flight = h["sound_flight"];
	flight.start = &AirSound::flight_start;
	flight.after = &AirSound::flight_after;
	h["sound_boom_step"].phase = &AirSound::boom_phase;
	h["sound_hover"].after = &AirSound::hover_after;
}

void RegisterAirHooks(HookTable& t) {
	// hook_execute
	t.exec["AirGust.crescent_execute"] = &AirGust::crescent_execute;
	t.exec["AirGust.crosswind_execute"] = &AirGust::crosswind_execute;
	t.exec["AirGust.downdraft_execute"] = &AirGust::downdraft_execute;
	t.exec["AirVortex.twister_execute"] = &AirVortex::twister_execute;
	t.exec["AirVortex.unleash_execute"] = &AirVortex::unleash_execute;
	t.exec["AirVortex.funnel_down_execute"] = &AirVortex::funnel_down_execute;
	t.exec["AirVacuum.palm_execute"] = &AirVacuum::palm_execute;
	t.exec["AirVacuum.suction_execute"] = &AirVacuum::suction_execute;
	t.exec["AirVacuum.mine_execute"] = &AirVacuum::mine_execute;
	t.exec["AirVacuum.wave_execute"] = &AirVacuum::wave_execute;
	t.exec["AirSound.clap_execute"] = &AirSound::clap_execute;
	t.exec["AirSound.lance_execute"] = &AirSound::lance_execute;
	t.exec["AirSound.echo_execute"] = &AirSound::echo_execute;
	t.exec["AirSound.ping_execute"] = &AirSound::ping_execute;
	// hook_tick
	t.tick["AirGust.grip_tick"] = &AirGust::grip_tick;
	t.tick["AirVortex.spin_tick"] = &AirVortex::spin_tick;
	// body ticks
	t.body_tick["AirGust.crescent_tick"] = &AirGust::crescent_tick;
	t.body_tick["AirVortex.twister_tick"] = &AirVortex::twister_tick;
	t.body_tick["AirVacuum.well_tick"] = &AirVacuum::well_tick;
	t.body_tick["AirSound.tremor_tick"] = &AirSound::tremor_tick;
	// zone effects
	t.zone["AirVortex.tornado_effect"] = &AirVortex::tornado_effect;
	t.zone["AirVortex.wall_effect"] = &AirVortex::wall_effect;
	t.zone["AirVortex.eddy_effect"] = &AirVortex::eddy_effect;
	t.zone["AirVacuum.well_effect"] = &AirVacuum::well_effect;
	t.zone["AirVacuum.bubble_effect"] = &AirVacuum::bubble_effect;
	t.zone["AirVacuum.mine_effect"] = &AirVacuum::mine_effect;
	t.zone["AirSound.flight_effect"] = &AirSound::flight_effect;
	// technique previews
	t.preview["AirGust.tech_preview"] = &AirGust::tech_preview;
	t.preview["AirVortex.tech_preview"] = &AirVortex::tech_preview;
	t.preview["AirVacuum.tech_preview"] = &AirVacuum::tech_preview;
	t.preview["AirSound.tech_preview"] = &AirSound::tech_preview;
	// outcome handlers
	t.outcome["AirOutcomes.o_cool"] = &AirOutcomes::o_cool;
	t.outcome["AirOutcomes.o_cut"] = &AirOutcomes::o_cut;
	t.outcome["AirOutcomes.o_split"] = &AirOutcomes::o_split;
	t.outcome["AirOutcomes.o_shrink"] = &AirOutcomes::o_shrink;
	t.outcome["AirOutcomes.o_shatter"] = &AirOutcomes::o_shatter;
	t.outcome["AirVortex.o_infuse"] = &AirVortex::o_infuse;
	t.outcome["AirVortex.o_catch"] = &AirVortex::o_catch;
	t.outcome["AirVortex.o_slow_bend"] = &AirVortex::o_slow_bend;
	t.outcome["AirVortex.o_spatter"] = &AirVortex::o_spatter;
	t.outcome["AirVortex.o_contest"] = &AirVortex::o_contest;
	t.outcome["AirVacuum.o_compress"] = &AirVacuum::o_compress;
	t.outcome["AirVacuum.o_snuff"] = &AirVacuum::o_snuff;
	t.outcome["AirVacuum.o_spit"] = &AirVacuum::o_spit;
	t.outcome["AirSound.o_pop"] = &AirSound::o_pop;
	t.outcome["AirSound.o_still"] = &AirSound::o_still;
}

}  // namespace ff
