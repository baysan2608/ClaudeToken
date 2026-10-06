"""Event -> sound rule data for Content/Fourfold/Data/sfx_manifest.json (port of the Godot AudioDirector / FxDirector / FxCues
cue logic, extended for the Unreal build).  build_manifest.py merges this with the generated sound index and validates it.

Rule language (interpreted by Source/FourfoldAudio/Private/Logic/AudioRules.*):

  events[<ff::Event.type>] = [ rule, ... ]          rules run in order; every matching rule fires; "stop": true ends the list
  rule   = { "id", "when": [cond...], "play": [play...], "stop": bool }
  cond   = { "f": field, <op>: arg }   ops: eq ne gt gte lt lte in nin has nhas truthy falsy
           field = a key of ff::Event.data, or a derived "$name":
             $player       the event's actor is the local player           $el        element name of the event / actor (earth|water|fire|air)
             $weight       "heavy" when data.heavy or data.tier >= 2 else "light"       $body_mat   fx mat of data.body
             $threat_mat / $counter_mat    material of an interaction's threat / counter (class_mat table, then body / actor)
             $surface      surface class of the actor (stone water puddle metal ice mud sand wood)
             $involves_player   the player is the threat or counter actor of an interaction
  play   = { "sound": ref, "at": [source...], "gain_db", "pitch", "chance", "limit", "gain_map", "two_d" }
           ref    "name" | "tmpl_{field}" (fields substituted) | ["a","b"] (round robin) | {"table","key","default"} (key: field or [fields]
                  joined with "_")          at: actor hand feet body pos at path_end (first available; none = 2D at the listener)
           gain_map: {"f": field, "in": [lo, hi], "db": [lo, hi]} linear
           limit: a named limiter of mix.limiters (gap seconds, optionally per actor / body)
  loops.bodies / loops.actors = [ { "id", "when", "sound", "gain_db", "fade": "body"|"zone", "at" } ]  evaluated every frame per body / actor
  loops.events   = [ { "event", "when", "sound", "key": [fields], "hold_s", "gain_db", "at", "fade" } ]  refreshed by events, expire on their own
"""
from __future__ import annotations


def C(f, **op):
    d = {"f": f}
    d.update(op)
    return d


def P(sound, at=("actor",), gain=0.0, pitch=1.0, chance=1.0, limit=None, gain_map=None, two_d=False, expect=None, delay=0.0):
    d = {"sound": sound}
    if delay:
        d["delay_s"] = delay
    if at:
        d["at"] = list(at)
    if gain:
        d["gain_db"] = gain
    if pitch != 1.0:
        d["pitch"] = pitch
    if chance < 1.0:
        d["chance"] = chance
    if limit:
        d["limit"] = limit
    if gain_map:
        d["gain_map"] = gain_map
    if two_d:
        d["two_d"] = True
    if expect:
        d["expect"] = expect
    return d


def R(rid, play, when=(), stop=False):
    d = {"id": rid, "play": play if isinstance(play, list) else [play]}
    if when:
        d["when"] = list(when)
    if stop:
        d["stop"] = True
    return d


MATS = ["stone", "metal", "sand", "glass", "magma", "water", "ice", "mist", "steam", "plant", "flame", "blue", "lightning",
        "blast", "wind", "vortex", "vacuum", "sound"]
ELS = ["earth", "water", "fire", "air"]

# ---------------------------------------------------------------------------------------------------------------------
# tables
# ---------------------------------------------------------------------------------------------------------------------

TABLES = {
    # FxCues.CLS_MAT: threat / counter class -> material family
    "class_mat": {
        "stone": "stone", "stone_heavy": "stone", "boulder": "stone", "hot_rock": "magma", "magma": "magma",
        "lava_wave": "magma", "metal": "metal", "molten_metal": "metal", "sand": "sand", "sand_cloud": "sand",
        "sand_surge": "sand", "glass": "glass", "water": "water", "water_wave": "water", "puddle": "water", "pool": "water",
        "ice": "ice", "mist": "mist", "steam": "steam", "vine": "plant", "flame": "flame", "blue_fire": "blue",
        "fire_field": "flame", "ember": "blast", "lightning": "lightning", "blast": "blast", "gust": "wind",
        "tornado": "vortex", "vacuum": "vacuum", "sound": "sound", "frost": "ice",
        "guard_earth": "stone", "wall_stone": "stone", "wall_obsidian": "stone", "wall_glass": "glass",
        "wall_sand": "sand", "wall_mud": "stone", "wall_ice": "ice", "wall_vine": "plant", "plate_metal": "metal",
        "shield_water": "water", "screen_steam": "steam", "fog": "mist", "aura_flame": "flame", "aura_blue": "blue",
        "ward_static": "lightning", "guard_blast": "blast", "guard_wind": "wind", "wall_vortex": "vortex",
        "bubble_null": "vacuum", "barrier_sound": "sound", "spikes": "stone", "rod": "metal", "anchor": "stone",
        "swallow": "stone", "quicksand": "sand", "melt_pit": "magma", "heat_grip": "flame", "heat_ranged": "flame",
        "draw_heat": "flame", "freeze": "ice", "condense": "water", "wave_water": "water", "wave_sand": "sand",
        "wave_lava": "magma", "rime": "ice", "vacuum_well": "vacuum", "suction": "vacuum", "water_jet": "water",
        "spray": "water", "plate": "metal", "arena_wall": "stone", "ground": "stone",
    },
    # FxCues.MAT_RELEASE_SFX
    "mat_release": {
        "stone": "stone_launch", "metal": "metal_scrape", "sand": "sand_hiss", "glass": "glass_shatter",
        "magma": "magma_glob", "water": "water_whip", "ice": "frost_hiss", "mist": "steam_hiss", "steam": "steam_jet",
        "plant": "vine_creak", "flame": "fireball_whoosh", "blue": "blue_ignite", "lightning": "spark_snap",
        "blast": "explosion_small", "wind": "crescent_whoosh", "vortex": "crescent_whoosh", "vacuum": "vacuum_implode",
        "sound": "echo_ping",
    },
    "beam": {"blue": "blue_ignite", "flame": "fire_release", "sand": "sand_hiss", "water": "water_jet", "sound": "sonic_boom",
             "vacuum": "vacuum_implode"},
    "erupt": {"stone": "wall_raise", "magma": "magma_surge", "water": "geyser", "steam": "geyser", "mist": "geyser",
              "ice": "ice_wall_raise", "flame": "fire_release", "blue": "fire_release", "plant": "vine_creak",
              "sand": "sand_burst"},
    "burst_simple": {"sand": "sand_burst", "stone": "stone_impact_1", "water": "water_splash", "steam": "steam_jet",
                     "mist": "steam_jet", "ice": "ice_shatter", "glass": "glass_shatter", "metal": "metal_clang",
                     "sound": "sonic_boom", "vacuum": "vacuum_implode"},
    # FxCues.interaction: transform stinger by the resulting state
    "transform_sting": {"steam": "steam_hiss", "glass": "glass_fuse", "ice": "frost_hiss", "snow": "frost_hiss",
                        "rock": "lava_hiss_quench", "obsidian": "obsidian_set", "lava": "magma_surge",
                        "molten_metal": "magma_surge", "mud": "water_splash", "ash": "vine_burn", "water": "ice_melt_drip",
                        "mist": "steam_hiss", "hot_rock": "melt_rise", "sandstone": "sand_burst"},
    "break": {"stone": "wall_crumble", "sand": "wall_crumble", "metal": "wall_crumble", "plant": "vine_snap"},
    # `launch` events: the projectile kind (body tag / material)
    "launch_kind": {"stream": "water_whip", "water": "water_whip", "ice": "freeze", "seed": "vine_snap",
                    "disc_storm": "metal_scrape", "grip": "stone_rip", "split": "stone_launch", "hot_stone": "fire_ignite",
                    "fireball": "fireball_whoosh", "comet": "fireball_whoosh", "ember": "fireball_whoosh",
                    "bomb": "fireball_whoosh", "glob": "magma_glob", "disc": "metal_scrape", "lance": "metal_scrape",
                    "rod": "metal_scrape", "plate": "metal_scrape", "caltrops": "metal_scrape", "crescent": "crescent_whoosh",
                    "twister": "crescent_whoosh", "spiral": "crescent_whoosh", "needle": "spark_snap",
                    "molten_stone": "magma_glob", "molten_metal": "magma_glob", "molten_sand": "magma_glob"},
    # a body's fx mat -> its landing / hitting sounds (round robin)
    "body_impact": {"stone": ["stone_impact_1", "stone_impact_2"], "metal": ["impact_metal_heavy"],
                    "sand": ["impact_sand_heavy"], "glass": ["impact_glass_heavy"], "ice": ["impact_ice_heavy"],
                    "water": ["impact_water_heavy"], "magma": ["lava_splat"], "plant": ["impact_plant_heavy"],
                    "flame": ["impact_fire_heavy"], "blue": ["impact_fire_heavy"], "blast": ["impact_fire_heavy"],
                    "mist": ["impact_water_light"], "steam": ["impact_water_light"], "wind": ["impact_wind_heavy"],
                    "vortex": ["impact_wind_heavy"], "vacuum": ["impact_wind_heavy"], "sound": ["impact_wind_heavy"],
                    "lightning": ["spark_snap"]},
    # `hit` events: the threat's material -> the material layer under hit_light / hit_heavy
    "hit_layer_light": {"stone": "impact_stone_light", "metal": "impact_metal_light", "sand": "impact_sand_light",
                        "glass": "impact_glass_light", "ice": "impact_ice_light", "water": "impact_water_light",
                        "mist": "impact_water_light", "steam": "impact_water_light", "plant": "impact_plant_light",
                        "flame": "impact_fire_light", "blue": "impact_fire_light", "blast": "impact_fire_light",
                        "magma": "impact_fire_light", "wind": "impact_wind_light", "vortex": "impact_wind_light",
                        "vacuum": "impact_wind_light", "sound": "impact_wind_light", "lightning": "spark_snap"},
    "hit_layer_heavy": {"stone": "impact_stone_heavy", "metal": "impact_metal_heavy", "sand": "impact_sand_heavy",
                        "glass": "impact_glass_heavy", "ice": "impact_ice_heavy", "water": "impact_water_heavy",
                        "mist": "impact_water_heavy", "steam": "impact_water_heavy", "plant": "impact_plant_heavy",
                        "flame": "impact_fire_heavy", "blue": "impact_fire_heavy", "blast": "impact_fire_heavy",
                        "magma": "impact_fire_heavy", "wind": "impact_wind_heavy", "vortex": "impact_wind_heavy",
                        "vacuum": "impact_wind_heavy", "sound": "impact_wind_heavy", "lightning": "lightning_redirect"},
    "land_surface": {"stone": "land", "sand": "land_sand", "wood": "land_wood", "metal": "land_metal", "ice": "land_ice",
                     "mud": "land_mud", "water": "land_water", "puddle": "land_water"},
    # statuses turning on
    "status_on": {"chilled": "frost_hiss", "frozen": "frost_hiss", "shocked": "spark_snap", "charged": "spark_snap",
                  "overcharged": "spark_snap", "rooted": "vine_creak", "icegrip": "ice_crack", "scalded": "steam_hiss",
                  "wet": "status_wet_on", "anchored": "armor_up", "armored": "armor_up"},
    "status_on_gain": {"icegrip": -4.0, "scalded": -6.0},
    # zone opening: a short accent per kind (the long loops start from the body state)
    "zone_open": {"sand_cloud": "sand_burst", "sandstorm": "sand_burst", "quicksand": "sand_burst", "fog": "steam_jet",
                  "mist": "steam_jet", "steam": "steam_jet", "steam_screen": "steam_jet", "geyser": "geyser",
                  "fire_field": "fire_ignite", "melt_pit": "magma_glob", "lava_pool": "magma_glob", "mine": "fuse_tick",
                  "fuse": "fuse_tick", "ice_floor": "frost_hiss", "vacuum_well": "vacuum_implode", "null_bubble": "vacuum_implode",
                  "static_field": "spark_snap", "corona": "blue_ignite", "sound_barrier": "echo_ping", "briar": "vine_creak",
                  "caltrops": "metal_clang", "tornado": "crescent_whoosh", "eddy": "crescent_whoosh", "vortex_wall": "crescent_whoosh",
                  "wind_guard": "air_push", "mud": "water_splash"},
    "element_ui": {"0": "element_earth", "1": "element_water", "2": "element_fire", "3": "element_air"},
}

# ---------------------------------------------------------------------------------------------------------------------
# events
# ---------------------------------------------------------------------------------------------------------------------

EV: dict[str, list] = {}


def ev(name, *rules):
    EV.setdefault(name, []).extend(rules)


# ---- combat results (FxDirector._event) ---------------------------------------------------------------------------
ev("hit",
   R("hit.heavy", P("hit_heavy"), [C("result", eq="knockdown")]),
   R("hit.heavy_dmg", P("hit_heavy"), [C("result", ne="knockdown"), C("damage", gte=15)]),
   R("hit.light", P("hit_light"), [C("result", ne="knockdown"), C("damage", lt=15)]),
   R("hit.lava", P("lava_splat"), [C("kind", eq="lava")]),
   # material layer: the threat's material under the generic body hit (stone hits stay as hit_*: the layer is subtle)
   R("hit.layer_heavy", P({"table": "hit_layer_heavy", "key": "mat"}, gain=-3.0),
     [C("mat", nin=["", "magma"]), C("$weight", eq="heavy")]),
   R("hit.layer_light", P({"table": "hit_layer_light", "key": "mat"}, gain=-2.0),
     [C("mat", nin=["", "magma"]), C("$weight", eq="light")]),
   R("hit.knockdown", P("knockdown"), [C("result", eq="knockdown")]),
   # the victim's breath leaves them
   R("hit.breath_heavy", P("breath_exhale_heavy", gain=-5.0, chance=0.7, limit="breath"),
     [C("$weight", eq="heavy")]),
   R("hit.breath_light", P("breath_exhale_light", gain=-4.0, chance=0.5, limit="breath"),
     [C("$weight", eq="light")]))

ev("block",
   R("block.thud", P("block", at=("actor", "body")), []),
   R("block.steam", P("steam_hiss", at=("actor", "body")), [C("kind", eq="fire_water")]))
ev("guard_break", R("guard_break", P("guard_break")))
ev("deflect", R("deflect", P("deflect")))
ev("perfect_deflect", R("perfect_deflect", P("perfect_deflect")))
ev("intercept", R("intercept", P("block", at=("body", "actor"), gain=-4.0)))
EV["control_won"] = [R("control_won.seize", P("stone_rip", at=("body", "actor"), gain=-6.0),
                       [C("verb", **{"in": ["seize", "magma_grip", "acquire"]})])]
ev("control_fail", R("control_fail.mass", P("control_lost"), [C("reason", eq="mass")]))
ev("control_lost", R("control_lost", P("control_lost", at=("body", "actor"))))
ev("conversion_interrupted", R("conversion_interrupted", P("control_lost", at=("body", "actor"), gain=-4.0)))
ev("insufficient", R("insufficient", P("challenge_fail", at=(), gain=-8.0, two_d=True, limit="insufficient"), [C("$player", truthy=True)]))
ev("rip", R("rip", P("stone_rip", at=("body", "actor"))))
ev("launch",
   R("launch.kind", P({"table": "launch_kind", "key": "kind", "default": "stone_launch"}, at=("body", "actor")), []),
   R("launch.whoosh_heavy", P("whoosh_heavy", at=("body", "actor"), gain=-3.0),
     [C("kind", nin=["stream", "water", "ice", "seed", "fireball", "comet", "ember", "bomb", "glob", "crescent", "twister", "spiral", "needle"]),
      C("heavy", truthy=True)]),
   R("launch.whoosh_light", P("whoosh_light", at=("body", "actor"), gain=-3.0),
     [C("kind", nin=["stream", "water", "ice", "seed", "fireball", "comet", "ember", "bomb", "glob", "crescent", "twister", "spiral", "needle"]),
      C("heavy", falsy=True)]))
ev("impact",
   R("impact.body", P({"table": "body_impact", "key": "$body_mat", "default": "stone_impact_1"}, at=("body",),
                      gain_map={"f": "speed", "in": [3.0, 16.0], "db": [-9.0, 0.0]}),
     [C("speed", gt=3.0)]))
ev("wall", R("wall", P("wall_raise", at=("body", "actor"))))
ev("wall_crumble", R("wall_crumble", P("wall_crumble", at=("body",))))
ev("wall_cut", R("wall_cut", P("wall_crumble", at=("body",), gain=-6.0)))
ev("wall_crack", R("wall_crack", P("wall_crumble", at=("body",), gain=-8.0)))
ev("transform",
   R("transform.molten", P("melt_rise", at=("at", "body", "actor")), [C("to", eq="molten")]),
   R("transform.wave", P("lava_splat", at=("at", "body", "actor")), [C("to", eq="wave")]),
   R("transform.rock", [P("crust_hiss", at=("at", "body", "actor")), P("cool_crack", at=("at", "body", "actor"), gain=-3.0)],
     [C("to", eq="rock")]),
   R("transform.ice", P("freeze", at=("at", "body", "actor")), [C("to", eq="ice")]),
   R("transform.frozen", P("freeze", at=("at", "body", "actor"), gain=-3.0), [C("to", eq="frozen")]),
   R("transform.water", P("ice_melt_drip", at=("at", "body", "actor")), [C("to", eq="water")]),
   R("transform.puddle", P("water_splash", at=("at", "body", "actor"), gain=-4.0), [C("to", eq="puddle")]))
ev("shatter",
   R("shatter.glass", P("glass_shatter", at=("body",)), [C("$body_mat", eq="glass")], stop=True),
   R("shatter.ice", P("ice_shatter", at=("body",)), []))
ev("wave_blocked", R("wave_blocked", P("lava_splat", at=("body",))))
ev("wave_drop", R("wave_drop", P("lava_splat", at=("body",))))
ev("steam", R("steam", P("steam_hiss", at=("body", "actor"), gain=-3.0)))
ev("steam_block", R("steam_block", P("steam_hiss", at=("body", "actor"), gain=-3.0)))
ev("flare",
   R("flare.heavy", P("fire_release", at=("hand", "actor")), [C("heavy", truthy=True)]),
   R("flare.light", P("fire_jab", at=("hand", "actor")), [C("heavy", falsy=True)]))
ev("vent", R("vent", P("vent_heat")))
ev("lightning", R("lightning", P("lightning_strike", at=("path_end", "actor"))))
ev("lightning_redirect", R("lightning_redirect", P("lightning_redirect")))
ev("conduct", R("conduct", P("conduct_buzz")))
ev("gust",
   R("gust.heavy", P("air_gust"), [C("heavy", truthy=True)]),
   R("gust.light", P("air_push"), [C("heavy", falsy=True)]))
ev("lash", R("lash", P("water_whip")))
ev("shield", R("shield", P("water_splash", gain=-6.0)))
ev("evade",
   R("evade.dash", P("dash_air"), [C("dash", truthy=True)]),
   R("evade.step", P("evade_whoosh"), [C("dash", falsy=True)]),
   R("evade.cloth", P("cloth_flap", gain=-6.0, limit="cloth")),
   R("evade.water", P("weight_shift", gain=-2.0, limit="cloth"), [C("dash", falsy=True), C("$el", eq="water")]),
   R("evade.air", P("swirl_air", gain=-3.0, limit="cloth"), [C("dash", falsy=True), C("$el", eq="air")]))
ev("updraft", R("updraft", P("air_gust", gain=-4.0)))
ev("land",
   R("land.surface", P({"table": "land_surface", "key": "$surface", "default": "land"}, at=("feet", "actor"),
                       gain_map={"f": "speed", "in": [3.0, 14.0], "db": [-6.0, 0.0]}), [C("speed", gt=3.0)]))
ev("element", R("element", P({"table": "element_ui", "key": "element"}, at=(), two_d=True), [C("$player", truthy=True)]))
ev("burn", R("burn", P("fire_ignite", gain=-6.0)))
ev("getup", R("getup", P("cloth_wrap", gain=-3.0, limit="cloth")))
ev("slam", R("slam", P("stone_impact_2")))
ev("thunder", R("thunder", P("thunderclap", at=("pos", "actor"), gain=-2.0)))
ev("mine_detonate", R("mine_detonate", P("explosion_small")))
ev("mine_burst", R("mine_burst", P("explosion_small", at=("pos", "body"), gain=-2.0)))
ev("collapse", R("collapse", P("vacuum_implode", at=("pos", "body"))))
ev("magma_surge", R("magma_surge", P("magma_surge")))
ev("magma_rift", R("magma_rift", P("magma_surge", at=("body", "actor"), gain=-3.0)))
ev("fire_column", R("fire_column", P("fire_release")))
ev("fire_burst",
   R("fire_burst.blue", P("blue_ignite", at=("pos", "body"), gain=-3.0), [C("blue", truthy=True)]),
   R("fire_burst.flame", P("fire_ignite", at=("pos", "body"), gain=-3.0), [C("blue", falsy=True)]))
ev("fire_ring", R("fire_ring", P("fire_release", at=("pos", "actor"), gain=-3.0)))
ev("ember_pop", R("ember_pop", P("spark_snap", at=("pos", "body"), gain=-4.0, limit="pops")))
ev("static_burst", R("static_burst", P("spark_snap", gain=-3.0, limit="pops")))
ev("discharge", R("discharge", P("conduct_buzz", at=("body", "actor"), gain=-4.0)))
ev("jet", R("jet.on", P("water_jet", at=("hand", "actor"), gain=-3.0), [C("on", truthy=True)]))
ev("dive", R("dive.on", P("water_splash", gain=-3.0), [C("on", truthy=True)]))
ev("revealed", R("revealed", P("echo_ping", gain=-6.0)))
ev("recall", R("recall", P("metal_recall", at=("body", "actor"))))
ev("hook", R("hook", P("vine_snap")))
ev("reactive_blast", R("reactive_blast", P("explosion_small", gain=-2.0)))
ev("grapple", R("grapple", P("dash_air", gain=-3.0)))
ev("vacuum_catch", R("vacuum_catch", P("vacuum_implode", at=("body", "actor"), gain=-6.0)))
ev("kiln", R("kiln", P("crust_hiss", at=("body", "actor"), gain=-6.0)))
ev("zone",
   R("zone.open", P({"table": "zone_open", "key": "kind"}, at=("pos", "body"), gain=-5.0, limit="zone"), [C("phase", eq="open")]),
   R("zone.close", P("status_off", at=("pos", "body"), gain=-5.0, limit="zone"), [C("phase", eq="close")]))
ev("clash",
   R("clash.energy", P("clash_energy", at=("pos",)),
     [C("mat", **{"in": ["flame", "blue", "lightning", "blast", "vacuum", "sound", "wind", "vortex"]})], stop=True),
   R("clash.solid", P("clash_solid", at=("pos",))))

# ---- martial swings: from the action events (phase "active" = the limb is moving) -------------------------------------
ev("action",
   R("action.swing", P("swing_{$el}_{$weight}", at=("hand", "actor"), limit="swing", expect={"$el": ELS, "$weight": ["light", "heavy"]}),
     [C("phase", eq="active"), C("slot", **{"in": ["strike", "thrust", "push"]}), C("$el", ne="")]),
   R("action.kick", P("kick_{$el}", at=("feet", "actor"), limit="swing", expect={"$el": ELS}),
     [C("phase", eq="active"), C("slot", eq="sweep"), C("$el", ne="")]),
   R("action.stomp_earth", P("stomp_earth", at=("feet", "actor"), limit="swing"),
     [C("phase", eq="active"), C("slot", eq="ground"), C("$el", eq="earth")], stop=True),
   R("action.stomp", P("stomp_light", at=("feet", "actor"), limit="swing"),
     [C("phase", eq="active"), C("slot", eq="ground")]),
   R("action.snap_fire", P("cloth_snap", at=("hand", "actor"), gain=-3.0, limit="cloth"),
     [C("phase", eq="recovery"), C("$el", eq="fire"), C("slot", **{"in": ["strike", "thrust"]})]),
   R("action.breath_fire", P("breath_sharp", gain=-2.0, limit="breath"),
     [C("phase", eq="startup"), C("$el", eq="fire"), C("slot", **{"in": ["strike", "thrust", "sweep"]})]),
   R("action.breath_inhale", P("breath_inhale", gain=-3.0, limit="breath"),
     [C("phase", eq="charge"), C("tier", eq=0)]))

# ---- fx cues (FxCues.fx_event) ---------------------------------------------------------------------------------------
ev("fx",
   # release: material sound at -2 dB, a heavy whoosh under a big one
   R("fx.release", P({"table": "mat_release", "key": "mat", "default": "whoosh_light"}, at=("pos", "actor"), gain=-2.0),
     [C("fx", eq="release")]),
   R("fx.release_heavy", P("whoosh_heavy", at=("pos", "actor"), gain=-4.0), [C("fx", eq="release"), C("tier", gte=3)]),
   # cone
   R("fx.cone.flame", P("fire_release", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="flame")]),
   R("fx.cone.blue", P("blue_ignite", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="blue")]),
   R("fx.cone.wind_big", P("air_gust", at=("pos", "actor")),
     [C("fx", eq="cone"), C("mat", **{"in": ["wind", "vortex"]}), C("tier", gte=2)]),
   R("fx.cone.wind", P("air_push", at=("pos", "actor")),
     [C("fx", eq="cone"), C("mat", **{"in": ["wind", "vortex"]}), C("tier", lt=2)]),
   R("fx.cone.sand", P("sand_burst", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="sand")]),
   R("fx.cone.water", P("water_splash", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="water")]),
   R("fx.cone.steam", P("steam_jet", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", **{"in": ["steam", "mist"]})]),
   R("fx.cone.ice", P("frost_hiss", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="ice")]),
   R("fx.cone.sound_big", P("roar_wave", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="sound"), C("tier", gte=2)]),
   R("fx.cone.sound", P("echo_ping", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="sound"), C("tier", lt=2)]),
   R("fx.cone.vacuum", P("vacuum_implode", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="vacuum")]),
   R("fx.cone.lightning", P("spark_snap", at=("pos", "actor")), [C("fx", eq="cone"), C("mat", eq="lightning")]),
   # beam
   R("fx.beam.skybreak", P("thunderclap", at=("pos", "actor")), [C("fx", eq="beam"), C("mat", eq="lightning"), C("shape", eq="down")]),
   R("fx.beam.lightning", P("lightning_strike", at=("path_end", "pos", "actor"), gain=-2.0),
     [C("fx", eq="beam"), C("mat", eq="lightning"), C("shape", ne="down")]),
   R("fx.beam.other", P({"table": "beam", "key": "mat"}, at=("pos", "actor"), gain=-2.0),
     [C("fx", eq="beam"), C("mat", **{"in": ["blue", "flame", "sand", "water", "sound", "vacuum"]})]),
   # ring
   R("fx.ring.sonic", P("sonic_boom", at=("pos", "actor")), [C("fx", eq="ring"), C("mat", eq="sound"), C("tier", gte=2)], stop=True),
   R("fx.ring", P({"table": "mat_release", "key": "mat", "default": "air_push"}, at=("pos", "actor")), [C("fx", eq="ring")]),
   # burst
   R("fx.burst.big_radius", P("explosion_large", at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", **{"in": ["blast", "flame", "blue"]}), C("radius", gte=2.0)], stop=True),
   R("fx.burst.big_blast_power", P("explosion_large", at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", eq="blast"), C("power", gte=30.0)], stop=True),
   R("fx.burst.big_flame_power", P("explosion_large", at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", **{"in": ["flame", "blue"]}), C("radius", gte=1.2), C("power", gte=30.0)], stop=True),
   R("fx.burst.blast", P("explosion_small", at=("pos", "actor")), [C("fx", eq="burst"), C("mat", eq="blast")], stop=True),
   R("fx.burst.flame_mid", P("explosion_small", at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", **{"in": ["flame", "blue"]}), C("radius", gte=1.2)], stop=True),
   R("fx.burst.flame_small", P("fire_ignite", at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", **{"in": ["flame", "blue"]}), C("radius", lt=1.2)], stop=True),
   R("fx.burst.lightning", P("thunderclap", at=("pos", "actor"), gain=-3.0), [C("fx", eq="burst"), C("mat", eq="lightning")], stop=True),
   R("fx.burst.simple", P({"table": "burst_simple", "key": "mat"}, at=("pos", "actor")),
     [C("fx", eq="burst"), C("mat", **{"in": ["sand", "stone", "water", "steam", "mist", "ice", "glass", "metal", "sound", "vacuum"]})]),
   # erupt
   R("fx.erupt", P({"table": "erupt", "key": "mat"}, at=("pos", "actor")),
     [C("fx", eq="erupt"), C("mat", **{"in": ["stone", "magma", "water", "steam", "mist", "ice", "flame", "blue", "plant", "sand"]})]),
   # splash is silent in the Godot build (the hit / launch cues carry the sound)
   )

# ---- interaction outcomes (FxCues.interaction): one stinger per outcome, <= 1 per 0.1 s ---------------------------------
_ST = "sting"
ev("interaction",
   R("ix.perfect", P("counter_success", at=("pos", "actor"), limit=_ST), [C("perfect", truthy=True)], stop=True),
   R("ix.block_metal", P("metal_clang", at=("pos",), limit=_ST),
     [C("outcome", eq="block"), C("$threat_mat", eq="metal")], stop=True),
   R("ix.block_metal2", P("metal_clang", at=("pos",), limit=_ST),
     [C("outcome", eq="block"), C("$counter_mat", eq="metal")], stop=True),
   R("ix.deflect", P("deflect", at=("pos", "actor"), limit=_ST), [C("outcome", **{"in": ["deflect", "redirect", "reflect"]})]),
   R("ix.reclaim_metal", P("metal_recall", at=("pos",), limit=_ST), [C("outcome", eq="reclaim"), C("$threat_mat", eq="metal")], stop=True),
   R("ix.reclaim", P("stone_rip", at=("pos",), limit=_ST), [C("outcome", eq="reclaim")]),
   R("ix.absorb_vacuum", P("vacuum_implode", at=("pos",), limit=_ST),
     [C("outcome", **{"in": ["absorb", "capture"]}), C("$counter_mat", eq="vacuum")], stop=True),
   R("ix.absorb", P("water_splash", at=("pos",), limit=_ST), [C("outcome", **{"in": ["absorb", "capture"]})]),
   R("ix.transform", P({"table": "transform_sting", "key": "to", "default": "melt_rise"}, at=("pos",), limit=_ST),
     [C("outcome", eq="transform")]),
   R("ix.shatter_glass", P("glass_shatter", at=("pos",), limit=_ST), [C("outcome", eq="shatter"), C("$threat_mat", eq="glass")], stop=True),
   R("ix.shatter_ice", P("ice_shatter", at=("pos",), limit=_ST), [C("outcome", eq="shatter"), C("$threat_mat", eq="ice")], stop=True),
   R("ix.shatter", P("stone_impact_2", at=("pos",), limit=_ST), [C("outcome", eq="shatter")]),
   R("ix.sink", P("stone_impact_1", at=("pos",), limit=_ST), [C("outcome", eq="sink")]),
   R("ix.conduct", P("conduct_buzz", at=("pos",), limit=_ST), [C("outcome", **{"in": ["ground", "conduct"]})]),
   R("ix.extinguish", P("steam_hiss", at=("pos",), limit=_ST),
     [C("outcome", **{"in": ["extinguish", "neutralize", "disperse"]}), C("$threat_mat", **{"in": ["flame", "blue"]})]),
   R("ix.amplify", P("fire_ignite", at=("pos",), limit=_ST), [C("outcome", eq="amplify")]),
   R("ix.overwhelm", [P("counter_fail", at=("pos",), limit=_ST),
                      P({"table": "break", "key": "$counter_mat"}, at=("pos",))],
     [C("outcome", eq="overwhelm")]),
   R("ix.disrupt", P("echo_ping", at=("pos",), limit=_ST), [C("outcome", eq="disrupt")]))

ev("charge",
   R("charge.tier", P("charge_{$el}_t{tier}", at=("hand", "actor"), gain=-3.0, expect={"$el": ELS, "tier": ["1", "2", "3"]}),
     [C("tier", gte=1), C("$el", ne="")]))

ev("charge", R("charge.generic", P("charge_t{tier}", at=("hand", "actor"), gain=-3.0, expect={"tier": ["1", "2", "3"]}),
               [C("tier", gte=1), C("$el", eq="")]))

ev("status",
   R("status.on", P({"table": "status_on", "key": "status"}, at=("actor",), limit="status"),
     [C("on", truthy=True), C("status", **{"in": ["chilled", "frozen", "shocked", "charged", "overcharged", "rooted", "wet", "anchored", "armored"]})]),
   R("status.on_quiet", P({"table": "status_on", "key": "status"}, at=("actor",), gain=-5.0, limit="status"),
     [C("on", truthy=True), C("status", **{"in": ["icegrip", "scalded"]})]),
   R("status.off", P("status_off", at=("actor",), gain=-4.0, limit="status"),
     [C("on", falsy=True), C("status", **{"in": ["burning", "frozen", "chilled", "rooted", "shocked", "anchored", "armored", "blinded",
                                                "concealed", "slowed", "muddy", "levitating", "wet"]})]))

ev("stance",
   R("stance.heavy", P("armor_up", at=("actor",), limit="status"),
     [C("on", truthy=True), C("stance", **{"in": ["stone_skin", "iron", "anchor", "roots"]})], stop=True),
   R("stance.on", P("stance_settle", at=("actor",), limit="status"), [C("on", truthy=True), C("stance", ne="")]))
ev("mode", R("mode.on", P("cloth_flap", at=("actor",), gain=-5.0, limit="cloth"), [C("on", truthy=True)]))
ev("weave", R("weave", P("charge_t1", at=("hand", "actor"), gain=-6.0, limit=_ST)))
ev("slump", R("slump", P("melt_rise", at=("body", "actor"))))
ev("convert", R("convert.glass", P("glass_fuse", at=("body", "actor")), [C("to", **{"in": ["glass", "Glass", "GLASS"]})]))
ev("ricochet", R("ricochet", P("metal_clang", at=("at",))))
ev("inrush", R("inrush", P("vacuum_implode", at=("pos",), gain=-3.0)))
ev("extinguish",
   R("extinguish.time", P("steam_hiss", at=("body",), gain=-6.0), [C("by", eq="time")], stop=True),
   R("extinguish.quench", P("lava_hiss_quench", at=("body",), gain=-6.0)))

# ---- session / app events ---------------------------------------------------------------------------------------------
ev("app_ko",
   R("app_ko.bell", P("ko_bell", at=(), two_d=True)),
   R("app_ko.victory", P("victory", at=(), two_d=True, delay=0.9), [C("$player", falsy=True)]))
ev("app_round_reset", R("app_round_reset", P("round_start", at=(), two_d=True, limit="scene")))
ev("app_scenario_loaded", R("app_scenario_loaded", P("scenario_start", at=(), two_d=True, limit="scene", delay=0.08)))
ev("target", R("target.lock", P("ui_ring_pick", at=(), gain=-9.0, two_d=True, limit="ui_tick"), [C("$player", truthy=True), C("target", gte=0)]))
ev("whiff", R("whiff", P("status_off", at=("actor",), gain=-2.0, limit="status")))
ev("swing", R("swing.grapple", P("dash_air", at=("actor",), gain=-4.0)))
ev("app_toast", R("app_toast.mastery", P("unlock", at=(), two_d=True), [C("kind", eq="mastery")]))
ev("app_challenge", R("app_challenge.done", P("challenge_done", at=(), two_d=True), [C("done", truthy=True)]))
ev("app_combo",
   R("combo.started", P("combo_start", at=(), two_d=True), [C("state", eq="started")]),
   R("combo.step", P("combo_step", at=(), two_d=True), [C("state", eq="step")]),
   R("combo.success", P("combo_success", at=(), two_d=True), [C("state", eq="success")]),
   R("combo.fail", P("combo_fail", at=(), two_d=True), [C("state", eq="fail")]))

# ---------------------------------------------------------------------------------------------------------------------
# state-driven loops (FxDirector.update_continuous / FxCues.update), one voice per body / actor
# ---------------------------------------------------------------------------------------------------------------------

ZONE_LOOPS = ["tornado_loop", "sandstorm_loop", "quicksand_loop", "mist_loop", "fire_field_loop", "static_crackle_loop",
              "vacuum_loop", "wave_rush_loop", "sand_surge_loop", "geyser_loop"]

BODY_TAG_LOOPS = {  # FxCues.LOOPS (zone / body tag -> loop), played at -4 dB while the body lives
    "sand_cloud": "sandstorm_loop", "sandstorm": "sandstorm_loop", "quicksand": "quicksand_loop",
    "fire_field": "fire_field_loop", "tornado": "tornado_loop", "vortex_wall": "tornado_loop", "eddy": "tornado_loop",
    "vacuum_well": "vacuum_loop", "null_bubble": "vacuum_loop", "fog": "mist_loop", "mist": "mist_loop",
    "static_field": "static_crackle_loop", "corona": "blue_roar_loop", "disc": "disc_whirr_loop",
    "water_wave": "wave_rush_loop", "sand_surge": "sand_surge_loop", "rod": "rod_hum_loop", "geyser": "geyser_loop",
    "flight_field": "flight_loop", "fuse": "fuse_loop",
}

LOOPS = {
    "bodies": (
        [{"id": f"tag.{tag}", "when": [C("tag", eq=tag)], "sound": snd, "gain_db": -4.0,
          "fade": "zone" if snd in ZONE_LOOPS else "body", "at": "body"} for tag, snd in BODY_TAG_LOOPS.items()]
        + [
            {"id": "lava.wave", "when": [C("mat", eq="stone"), C("form", eq="wave"), C("$speed", gt=0.3), C("tag", eq="")],
             "sound": "lava_wave_loop", "gain_db": 0.0, "fade": "body", "at": "body"},
            {"id": "lava.blob", "when": [C("mat", eq="stone"), C("form", eq="blob"), C("liquid", gt=0.3)],
             "sound": "lava_bubble_loop", "gain_db": 0.0, "fade": "body", "at": "body"},
            {"id": "stone.roll", "when": [C("mat", eq="stone"), C("form", eq="chunk"), C("on_ground", truthy=True), C("$speed", gt=2.5), C("liquid", lt=0.2)],
             "sound": "stone_roll_loop", "gain_db": -2.0, "fade": "body", "at": "body"},
            {"id": "stone.hot", "when": [C("mat", eq="stone"), C("controller", gte=0), C("temp", gt=350.0), C("liquid", lt=0.3)],
             "sound": "heat_crackle_loop", "gain_db": -4.0, "fade": "body", "at": "body"},
        ]),
    "actors": [
        {"id": "charge.bolt", "when": [C("action.id", eq="fire_attack"), C("action.phase", eq="charge"), C("action.data.bolt_ready", truthy=True)],
         "sound": "lightning_charge_loop", "gain_db": -4.0, "fade": "body", "at": "chest"},
        {"id": "charge.fire", "when": [C("action.id", eq="fire_attack"), C("action.phase", eq="charge"), C("action.data.bolt_ready", falsy=True)],
         "sound": "fire_charge_loop", "gain_db": -4.0, "fade": "body", "at": "chest"},
        {"id": "glide", "when": [C("gliding", truthy=True)], "sound": "glide_loop", "gain_db": 0.0, "fade": "body", "at": "chest"},
        {"id": "flight", "when": [C("flying", truthy=True)], "sound": "flight_loop", "gain_db": -3.0, "fade": "body", "at": "chest"},
        {"id": "water.draw", "when": [C("action.id", eq="water_tech"), C("action.phase", eq="channel")],
         "sound": "water_draw_loop", "gain_db": -6.0, "fade": "body", "at": "chest"},
        {"id": "ice.slide", "when": [C("surface", eq="zone:ice"), C("grounded", truthy=True), C("$speed", gt=1.5)],
         "sound": "ice_slide_loop", "gain_db": -2.0, "fade": "body", "at": "feet"},
        {"id": "water.shield", "when": [C("element", eq="water"), C("guarding", truthy=True)], "sound": "water_shield_loop",
         "gain_db": -4.0, "fade": "body", "at": "chest"},
        {"id": "burning", "when": [C("$status", has="burning")], "sound": "status_burn_loop", "gain_db": 0.0, "fade": "body", "at": "chest"},
    ],
    "events": [
        {"event": "heating", "sound": "heat_crackle_loop", "key": ["heat", "body"], "hold_s": 0.35, "gain_db": -2.0, "at": "body", "fade": "body"},
        {"event": "drawing", "sound": "heat_draw_loop", "key": ["draw", "body"], "hold_s": 0.2, "gain_db": 0.0, "at": "body", "fade": "body"},
        {"event": "draw_water", "sound": "water_draw_loop", "key": ["wdraw", "actor"], "hold_s": 0.3, "gain_db": -6.0, "at": "actor", "fade": "body"},
    ],
}

# ---------------------------------------------------------------------------------------------------------------------
# footsteps, ambience, UI, mix
# ---------------------------------------------------------------------------------------------------------------------

STEPS = {
    "min_speed": 0.8,
    "cadence_base": 1.2,          # step accumulator += dt * (cadence_base + speed * cadence_per_speed); a step each 1.0 (Godot)
    "cadence_per_speed": 0.45,
    "gain_db": [-1.5, 0.5],      # random per step (Godot: randf_range(-1.5, 0.5))
    "speed_gain": {"in": [0.8, 5.5], "db": [-5.0, 0.0]},
    "pitch_var": 0.04,
    "default": "stone",
    "surfaces": {   # surface class -> variants (round robin, never the same twice in a row)
        "stone": ["step_stone_1", "step_stone_2", "step_stone_3", "step_stone_4"],
        "sand": ["step_sand_1", "step_sand_2", "step_sand_3"],
        "wood": ["step_wood_1", "step_wood_2", "step_wood_3"],
        "metal": ["step_metal_1", "step_metal_2", "step_metal_3"],
        "water": ["step_water_1", "step_water_2", "step_water_3"],
        "puddle": ["step_puddle_1", "step_puddle_2"],
        "ice": ["step_ice_1", "step_ice_2"],
        "mud": ["step_mud_1", "step_mud_2"],
    },
    # actor.surface string -> class: exact first, then the first matching prefix / substring rule
    "surface_exact": {"stone": "stone", "water": "water", "puddle": "puddle", "metal": "metal"},
    "surface_contains": [["ice", "ice"], ["mud", "mud"], ["quicksand", "mud"], ["swamp", "mud"], ["sand", "sand"], ["wood", "wood"],
                         ["plate", "metal"], ["metal", "metal"], ["water", "water"]],
    "cloth_every": 3,           # every n-th step adds a cloth rustle (quiet) while running
    "cloth_sounds": ["cloth_rustle_1", "cloth_rustle_2", "cloth_rustle_3"],
}

AMBIENCE = {
    "beds": [
        {"sound": "amb_courtyard_air_loop", "gain_db": 0.0},
        {"sound": "amb_wind_trees_loop", "gain_db": -2.0},
        {"sound": "amb_water_distant_loop", "gain_db": -2.0},
        {"sound": "amb_courtyard_loop", "gain_db": -9.0},
    ],
    "accents": [
        {"sounds": ["chime_wind_1", "chime_wind_2", "chime_wind_3", "chime_wind_4"], "min_gap_s": 14.0, "max_gap_s": 38.0,
         "gain_db": 0.0, "pitch_var": 0.0},
        {"sounds": ["bamboo_knock"], "min_gap_s": 35.0, "max_gap_s": 80.0, "gain_db": 0.0, "pitch_var": 0.02},
    ],
    "fade_in_s": 2.5,
}

UI = {  # OnUiCue names (FourfoldSimSubsystem.h) -> sound
    "ui_tap": "ui_tap", "ui_select": "ui_select", "ui_back": "ui_back", "ui_open": "ui_open", "ui_close": "ui_close",
    "ui_toggle": "ui_toggle", "ui_ring_open": "ui_ring_open", "ui_ring_pick": "ui_ring_pick", "ui_error": "ui_error",
    "ui_pause": "ui_pause", "ui_resume": "ui_resume", "ui_toast": "ui_toast",
}

MIX = {
    "global_voices": 28,
    "loop_voices": 14,
    "steal_fade_s": 0.03,
    "bus_gain_db": {"sfx": 0.0, "ui": 0.0, "ambience": -4.0},
    "fade_db_per_s": {"body_in": 90.0, "body_out": 60.0, "zone_in": 30.0, "zone_out": 24.0, "floor_db": -40.0},
    "limiters": {
        "sting": {"gap_s": 0.1},
        "swing": {"gap_s": 0.16, "per": "actor"},
        "breath": {"gap_s": 0.45, "per": "actor"},
        "cloth": {"gap_s": 0.3, "per": "actor"},
        "status": {"gap_s": 0.2, "per": "actor"},
        "insufficient": {"gap_s": 0.25},
        "pops": {"gap_s": 0.08},
        "scene": {"gap_s": 1.5},
        "zone": {"gap_s": 0.12},
        "ui_tick": {"gap_s": 0.15},
    },
    # distance classes: inner radius (full volume), outer radius (cm), attenuation at the outer radius (dB)
    "attenuation": {
        "near": {"inner_cm": 600.0, "max_cm": 2800.0, "db_at_max": -22.0},
        "mid": {"inner_cm": 1000.0, "max_cm": 4500.0, "db_at_max": -24.0},
        "far": {"inner_cm": 1800.0, "max_cm": 8000.0, "db_at_max": -26.0},
        "feel": {"inner_cm": 2500.0, "max_cm": 6500.0, "db_at_max": -9.0},
    },
    # ambience ducks under the loudest events (Godot AUDIO.md: 3-4 dB under guard_break / knockdown / lightning_strike / fire_release)
    "duck": {"bus": "ambience", "db": -4.0, "attack_s": 0.04, "hold_s": 0.5, "release_s": 0.9,
             "triggers": ["guard_break", "knockdown", "lightning_strike", "fire_release", "thunderclap", "explosion_large", "ko_bell"]},
}

# sounds that read as parry / feedback: positional but nearly distance-free so they always read in a 32 m arena
FEEL_SOUNDS = {"deflect", "perfect_deflect", "block", "counter_success", "counter_fail", "guard_break", "control_lost",
               "clash_solid", "clash_energy", "challenge_fail"}
FAR_SOUNDS = {"explosion_large", "explosion_small", "thunderclap", "lightning_strike", "lightning_redirect", "sonic_boom",
              "tornado_loop", "sandstorm_loop", "vacuum_loop", "fire_field_loop", "wave_rush_loop", "roar_wave", "magma_surge",
              "geyser", "geyser_loop", "vacuum_implode", "stomp_earth", "fire_release", "knockdown"}
NEAR_PREFIX = ("step_", "cloth_", "breath_", "land", "weight_", "swirl_", "stance_", "status_", "armor_", "swing_", "kick_",
               "stomp_light", "fuse_", "water_draw", "fire_charge", "lightning_charge", "heat_", "ice_slide", "glide")
TWO_D_NAMES = {"ko_bell", "round_start", "round_reset", "victory", "scenario_start", "unlock", "challenge_done", "challenge_fail"}
