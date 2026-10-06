// Fourfold core - Fire kit registration: per-move lifecycle overrides (GDScript KitFire.handle) and code hooks by
// Godot name ("FireX.method" as referenced by Data/moves.json, hooks.json and rules.json).
#include "Combat/Kits/Fire/Fire.h"
#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Kits/KitDispatch.h"
#include "Sim/Hooks.h"

namespace ff {

void RegisterFireHandlers(KitHandlerMap& h) {
	h["flame_guard"].tick = &FireFlame::guard_tick;
	h["rocket_hop"].tick = &FireFlame::hop_tick;
	KitStages& needle = h["blue_needle"];
	needle.tick = &FireBlue::needle_tick;
	needle.phase = &FireBlue::needle_phase;
	needle.interrupt = &FireBlue::needle_interrupt;
	h["spark"].phase = &FireLightning::spark_phase;
	KitStages& hand = h["conductors_hand"];
	hand.after = &FireLightning::hand_after;
	hand.tick = &FireLightning::hand_tick;
	hand.phase = &FireLightning::hand_phase;
	h["overcharge"].tick = &FireLightning::overcharge_tick;
	h["static_ward"].tick = &FireLightning::ward_tick;
	h["spark_mine"].start = &FireCombustion::mine_start;
	KitStages& fuse = h["fuse"];
	fuse.after = &FireCombustion::fuse_after;
	fuse.tick = &FireCombustion::fuse_tick_action;
	fuse.phase = &FireCombustion::fuse_phase;
	fuse.interrupt = &FireCombustion::fuse_interrupt;
	h["blast_jump"].start = &FireCombustion::jump_start;
	h["afterglow"].tick = &FireCombustion::afterglow_tick;
}

void RegisterFireHooks(HookTable& t) {
	// hook_execute
	t.exec["FireFlame.line_execute"] = &FireFlame::line_execute;
	t.exec["FireFlame.backdraft_execute"] = &FireFlame::backdraft_execute;
	t.exec["FireFlame.ground_heat_execute"] = &FireFlame::ground_heat_execute;
	t.exec["FireBlue.furrow_execute"] = &FireBlue::furrow_execute;
	t.exec["FireBlue.corona_execute"] = &FireBlue::corona_execute;
	t.exec["FireBlue.kiln_execute"] = &FireBlue::kiln_execute;
	t.exec["FireLightning.rail_execute"] = &FireLightning::rail_execute;
	t.exec["FireLightning.current_execute"] = &FireLightning::current_execute;
	t.exec["FireLightning.fan_execute"] = &FireLightning::fan_execute;
	t.exec["FireLightning.burst_execute"] = &FireLightning::burst_execute;
	t.exec["FireCombustion.pop_execute"] = &FireCombustion::pop_execute;
	t.exec["FireCombustion.mine_execute"] = &FireCombustion::mine_execute;
	t.exec["FireCombustion.chain_execute"] = &FireCombustion::chain_execute;
	t.exec["FireCombustion.scatter_execute"] = &FireCombustion::scatter_execute;
	t.exec["FireCombustion.smother_execute"] = &FireCombustion::smother_execute;
	// hook_tick
	t.tick["FireFlame.dash_tick"] = &FireFlame::dash_tick;
	t.tick["FireBlue.afterburn_tick"] = &FireBlue::afterburn_tick;
	t.tick["FireLightning.grounding_tick"] = &FireLightning::grounding_tick;
	t.tick["FireLightning.arc_step_tick"] = &FireLightning::arc_step_tick;
	// hook_impact
	t.impact["FireFlame.fireball_impact"] = &FireFlame::fireball_impact;
	t.impact["FireCombustion.mine_impact"] = &FireCombustion::mine_impact;
	// body ticks
	t.body_tick["FireFlame.fireball_tick"] = &FireFlame::fireball_tick;
	t.body_tick["FireFlame.line_tick"] = &FireFlame::line_tick;
	t.body_tick["FireBlue.rift_tick"] = &FireBlue::rift_tick;
	t.body_tick["FireLightning.current_tick"] = &FireLightning::current_tick;
	t.body_tick["FireCombustion.ember_tick"] = &FireCombustion::ember_tick;
	// zone effects
	t.zone["FireUtil.field_tick"] = &FireUtil::field_tick;
	t.zone["FireBlue.kiln_tick"] = &FireBlue::kiln_tick;
	t.zone["FireLightning.static_field_tick"] = &FireLightning::static_field_tick;
	t.zone["FireCombustion.fuse_zone_tick"] = &FireCombustion::fuse_zone_tick;
	// technique previews
	t.preview["FireBlue.smelter_preview"] = &FireBlue::smelter_preview;
	t.preview["FireLightning.hand_preview"] = &FireLightning::hand_preview;
	t.preview["FireCombustion.fuse_preview"] = &FireCombustion::fuse_preview;
	// channel hooks
	t.channel["FireRules._fire_channels"] = &FireRules::_fire_channels;
	t.channel["FireRules._current_channels"] = &FireRules::_current_channels;
	// outcome handlers ("fire_<name>" in rules.json -> FireRules.o_<name>)
	t.outcome["FireRules.o_heat"] = &FireRules::o_heat;
	t.outcome["FireRules.o_evaporate"] = &FireRules::o_evaporate;
	t.outcome["FireRules.o_burn"] = &FireRules::o_burn;
	t.outcome["FireRules.o_melt"] = &FireRules::o_melt;
	t.outcome["FireRules.o_snuffed"] = &FireRules::o_snuffed;
	t.outcome["FireRules.o_dampen"] = &FireRules::o_dampen;
	t.outcome["FireRules.o_fanned"] = &FireRules::o_fanned;
	t.outcome["FireRules.o_blown"] = &FireRules::o_blown;
	t.outcome["FireRules.o_tornado"] = &FireRules::o_tornado;
	t.outcome["FireRules.o_guard_absorb"] = &FireRules::o_guard_absorb;
	t.outcome["FireRules.o_aegis_melt"] = &FireRules::o_aegis_melt;
	t.outcome["FireRules.o_static"] = &FireRules::o_static;
	t.outcome["FireRules.o_static_full"] = &FireRules::o_static_full;
	t.outcome["FireRules.o_reactive"] = &FireRules::o_reactive;
	t.outcome["FireRules.o_counter_blast"] = &FireRules::o_counter_blast;
	t.outcome["FireRules.o_body_burst"] = &FireRules::o_body_burst;
	t.outcome["FireRules.o_charge_body"] = &FireRules::o_charge_body;
	t.outcome["FireRules.o_disrupt_zone"] = &FireRules::o_disrupt_zone;
	t.outcome["FireRules.o_fill_void"] = &FireRules::o_fill_void;
	t.outcome["FireRules.o_suppressed"] = &FireRules::o_suppressed;
	t.outcome["FireRules.o_fulgurite"] = &FireRules::o_fulgurite;
	t.outcome["FireRules.o_glassify"] = &FireRules::o_glassify;
	t.outcome["FireRules.o_conduct_owner"] = &FireRules::o_conduct_owner;
	t.outcome["FireRules.o_smother"] = &FireRules::o_smother;
}

}  // namespace ff
