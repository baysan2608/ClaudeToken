"""Move -> clip table (MARTIAL_ARTS.md §4) for all 160 sim moves: the IDEAL clips of each move.

anim_map_gen.py resolves every clip here against the clips that are actually built (clips.json) through CLIP_FALLBACK,
so anim_map.json only ever names exported clips.  Keys are sim move ids (Source/FourfoldCore/Data/move_index.json);
the shared legacy ids use the runtime's per-element keys: "guard@<element>" (Earth 0, Water 1, Air 3 use the legacy
`guard`) and "evade@<element>" (Earth 0, Fire 2 use the legacy `evade`).  Startup "evade_*" = the directional evade
clip chosen by the runtime from the stick (FFGAnimDirector::EvadeClip).

Fields: startup / hold / release / perfect (clip names), hands (l, r), tiers {tier: {field: clip}} (cumulative from
that tier up), modes {lower-case action.data.mode: {field: clip}}.
"""


def M(startup=None, hold=None, release=None, hands=("relaxed", "relaxed"), tiers=None, modes=None, perfect=None,
      note=""):
    if isinstance(hands, str):
        hands = (hands, hands)
    d = {"startup": startup, "hold": hold, "release": release, "perfect": perfect, "hands": tuple(hands),
         "tiers": tiers or {}, "modes": modes or {}, "note": note}
    return d


def R(release=None, startup=None, hold=None, hands=None):
    d = {}
    if startup:
        d["startup"] = startup
    if hold:
        d["hold"] = hold
    if release:
        d["release"] = release
    if hands:
        d["hands"] = (hands, hands) if isinstance(hands, str) else tuple(hands)
    return d


GRIP_RIP = {"rip": R(startup="e_lift", hold="e_seize_loop", release="e_throw")}

MOVES = {
    # ---------------------------------------------------------------- 4.1 Earth / Stone (Hung Gar, rooted)
    "earth_attack": M("e_lift", release="e_strike", hands=("tiger", "fist"),
                      tiers={1: R("e_heave"), 2: R("e_heave"), 3: R("e_heave")}, note="stomp-lift, iron-bridge drive; T1+ heave"),
    "spear_stone": M("e_thrust", hands="tiger"),
    "rising_fangs": M("e_ground_rise", hands="palm"),
    "rubble_fan": M("e_sweep", hands="crane"),
    "guard@0": M("e_wall", hold="e_guard", hands="palm", perfect="deflect"),
    "ram_wall": M("e_push", hands="tiger"),
    "swallow": M("e_sink", hands="palm"),
    "earth_tech": M(None, hold="e_seize_loop", release="e_throw", hands="spread", modes=GRIP_RIP),
    "evade@0": M("evade_*"),
    "stone_skin": M(None, hold="e_stone_skin", hands="fist",
                    modes={"burrow": R(startup="e_burrow", hands="relaxed"), "skin": R(hold="e_stone_skin")}),
    # ---------------------------------------------------------------- 4.2 Earth / Metal
    "razor_disc": M("e_disc_flick", hands="crane"),
    "iron_lance": M("e_thrust", hands="crane"),
    "lodestone_line": M("e_ground_slap", hands="palm"),
    "chain_arc": M("e_chain_whirl", hands="fist"),
    "aegis_plate": M(None, hold="e_guard", hands="spread", perfect="deflect"),
    "plate_rush": M("e_disc_flick", hands="crane"),
    "rod_plant": M("e_overhead_slam", hands="fist"),
    "lodestone_grip": M(None, hold="e_seize_loop", release="e_throw", hands="spread"),
    "magnet_glide": M("a_dash"),
    "iron_stance": M(None, hold="e_stone_skin", hands="fist", note="pigeon-toed iron stance"),
    # ---------------------------------------------------------------- 4.3 Earth / Sand
    "grit_shot": M("e_lift", release="e_strike", hands="tiger"),
    "sandblast": M("e_thrust", hands="palm", note="held jet: the runtime holds the contact pose while active"),
    "sand_surge": M("e_ground_slap", hands="palm"),
    "veil_of_grit": M("e_sweep", hands="crane"),
    "dune_wall": M("e_wall", hold="e_guard", hands="palm", perfect="deflect"),
    "dune_push": M("e_push", hands="tiger"),
    "quicksand": M("e_sink", hands="palm"),
    "sandform": M(None, hold="e_seize_loop", release="e_throw", hands="cup", modes=GRIP_RIP),
    "sand_surf": M("e_burrow"),
    "sand_ride": M(None, hold="surf"),
    # ---------------------------------------------------------------- 4.4 Earth / Magma
    "ember_clot": M("e_lob", hands="cup"),
    "lava_lash": M("e_chain_whirl", hands="fist"),
    "magma_surge": M("e_pour", hands="palm"),
    "spatter_arc": M("e_spatter", hands="spread"),
    "magma_curtain": M("e_wall", hold="e_guard", hands="palm", perfect="deflect"),
    "slag_wave": M("e_pour", hands="palm"),
    "melt_pit": M("e_sink", hands="palm"),
    "magma_hold": M(None, hold="e_magma_hold", release="e_pour", hands="cup"),
    "cinder_step": M("evade_fwd"),
    "lava_wade": M(None, hold="walk", note="heavy wading walk (walk loop, slow)"),
    # ---------------------------------------------------------------- 4.5 Water / Water (Tai Chi)
    "water_attack": M("w_lash", hands="willow",
                      tiers={1: R("w_freeze", hands="fist"), 2: R("w_push", hands="palm"), 3: R("w_maelstrom", hands="willow")},
                      note="Part the Mane; T1 Pipa clench; T2 Push; T3 turning whip"),
    "water_bullet": M("w_press", hands="palm"),
    "tidal_rush": M("w_ground", hands="palm"),
    "spray_fan": M("w_single_whip", hands=("willow", "crane")),
    "guard@1": M(None, hold="w_shield", hands="willow", perfect="deflect"),
    "surge_orb": M("w_push", hands="palm"),
    "slick": M("w_snake", hands="willow"),
    "water_tech": M("w_draw", hold="w_hold", release="w_release", hands="cup"),
    "riptide_step": M("w_repulse", hands="willow"),
    "wave_ride": M(None, hold="surf", hands="willow"),
    # ---------------------------------------------------------------- 4.6 Water / Ice
    "frost_shard": M("w_freeze", hands="fist"),
    "icicle_volley": M("w_press", hands="sword"),
    "rime_path": M("w_ground", hands="palm"),
    "hoarfrost_fan": M("w_single_whip", hands="willow"),
    "ice_wall": M("w_freeze", hold="w_shield", hands="palm", perfect="deflect"),
    "glacier_shove": M("w_heel_kick", hands="palm"),
    "frost_floor": M("w_snake", hands="palm"),
    "freeze_draw": M("w_draw", hold="w_hold", release="w_release", hands="cup",
                     modes={"rip": R(startup="w_draw")}),
    "ice_glide": M("evade_*", hands="willow"),
    "skate": M(None, hold="skate"),
    # ---------------------------------------------------------------- 4.7 Water / Mist
    "scald_puff": M("w_push", hands="willow", note="Brush Knee and Push, short"),
    "fog_lance": M("w_press", hands="willow"),
    "creeping_fog": M("w_crane", hands="willow"),
    "veil": M("w_single_whip", hands="willow"),
    "steam_screen": M(None, hold="w_shield", hands="willow", perfect="deflect"),
    "steam_blast": M("w_push", hands="palm"),
    "dew_fall": M("w_snake", hands="willow"),
    "vapor_draw": M("w_draw", hold="w_hold", release="w_release", hands="cup"),
    "mist_step": M("w_repulse", hands="willow"),
    "fog_walk": M(None, hold="walk", hands="willow"),
    # ---------------------------------------------------------------- 4.8 Water / Plant
    "bramble_lash": M("w_lash", hands="crane", tiers={3: R("w_maelstrom")}),
    "burr_shot": M("w_press", hands="crane"),
    "root_snare": M("w_ground", hands="spread"),
    "thicket_fan": M("w_single_whip", hands="spread"),
    "living_lattice": M(None, hold="w_shield", hands="spread", perfect="deflect"),
    "lattice_roll": M("w_shuttle", hands="palm"),
    "deep_roots": M("w_snake", hands="spread"),
    "vinegrip": M("w_draw", hold="w_hold", release="w_release", hands="spread"),
    "vine_swing": M("evade_*", hands="crane"),
    "canopy": M(None, hold="hover"),
    # ---------------------------------------------------------------- 4.9 Fire / Flame (Northern Shaolin)
    "fire_attack": M("f_jab", hold="f_charge", hands="fist",
                     tiers={1: R("f_palm_burst", hands="palm"), 2: R("f_column", hands="palm"), 3: R("f_inferno", hands="palm")},
                     note="jab; T1 charge -> palm burst; T2 rising palm; T3 circle + double palm"),
    "fireball": M("f_snap_kick", hands="fist"),
    "fire_line": M("f_low_sweep", hands="fist"),
    "fire_fan": M("f_crescent_kick", hands="palm", tiers={3: R("f_tornado_kick")}),
    "flame_guard": M(None, hold="guard", hands="palm", perfect="deflect"),
    "backdraft": M("f_palm_burst", hands="palm"),
    "ground_heat": M("f_stomp", hands="palm"),
    "fire_tech": M(None, hold="f_thermal_hold", release="f_pour", hands="cup",
                   modes={"heat": R(hold="f_thermal_hold", release="f_pour", hands="cup"),
                          "draw": R(hold="f_heat_draw", release="f_palm_burst", hands="spread"),
                          "scorch": R(hold="f_heat_draw", release="f_palm_burst", hands="spread"),
                          "vent": R(startup="f_palm_burst", release="f_palm_burst", hands="palm")}),
    "evade@2": M("evade_*", hands="fist"),
    "rocket_hop": M("f_hop", hands="palm"),
    # ---------------------------------------------------------------- 4.10 Fire / Blue
    "blue_needle": M("f_needle", hold="f_charge", hands="sword"),
    "comet_flame": M("f_snap_kick", hands="sword"),
    "blue_furrow": M("f_low_sweep", hands="sword"),
    "corona": M("f_corona", hands="sword"),
    "blue_aegis": M(None, hold="guard", hands="palm", perfect="deflect"),
    "flash_over": M("f_palm_burst", hands="palm"),
    "kiln": M(None, hold="f_heat_draw", hands="sword"),
    "smelter": M(None, hold="f_heat_draw", hands="sword"),
    "shimmer_step": M("f_dash", hands="sword"),
    "afterburn": M(None, hold="run", hands="fist"),
    # ---------------------------------------------------------------- 4.11 Fire / Lightning
    "spark": M("f_jab", hold="l_charge", hands="sword",
               tiers={1: R("l_release"), 2: R("l_release"), 3: R("l_skybreak")},
               note="T0 sword-finger jab; T1+ circular charge -> two-finger release; T3 sky strike"),
    "rail_arc": M("l_release", hands="sword"),
    "ground_current": M("l_ground", hands="palm"),
    "arc_fan": M("l_fan", hands="spread"),
    "static_ward": M(None, hold="guard", hands="sword", perfect="l_redirect", note="perfect guard = Return Current"),
    "static_burst": M("f_palm_burst", hands="spread"),
    "grounding": M("f_stomp", hands="palm"),
    "conductors_hand": M(None, hold="l_charge", release="l_release", hands="sword"),
    "arc_step": M("f_dash", hands="sword"),
    "overcharge": M(None, hold="run", hands="sword"),
    # ---------------------------------------------------------------- 4.12 Fire / Combustion
    "pop": M("f_jab", hands="palm", tiers={1: R("c_point", startup="c_point", hands=("sword", "fist"))},
             note="T0 palm pop; T1+ point -> fist snap"),
    "spark_mine": M("c_toss", hands="crane"),
    "chain_blasts": M("c_chain_stomp", hands="fist"),
    "scatter_charges": M("c_toss", hands="spread"),
    "reactive_blast": M(None, hold="guard", hands="palm", perfect="deflect"),
    "shockwave": M("f_palm_burst", hands="palm"),
    "smother_blast": M("f_hop", hands="palm"),
    "fuse": M(None, hold="c_fuse_loop", release="c_point", hands="sword"),
    "blast_jump": M("f_hop", hands="fist"),
    "afterglow": M(None, hold="hover", hands="palm"),
    # ---------------------------------------------------------------- 4.13 Air / Gust (Baguazhang)
    "air_attack": M("a_palm", hands="oxtongue",
                    tiers={1: R("a_double_palm"), 2: R("a_double_palm"), 3: R("a_hurricane")},
                    note="single palm; T1-T2 double palm change; T3 turning double palm"),
    "gust_crescent": M("a_pierce", hands="oxtongue"),
    "gust_dust_line": M("a_low_palm", hands="oxtongue"),
    "gust_crosswind": M("a_turn_palm", hands="oxtongue"),
    "guard@3": M(None, hold="a_guard", hands="oxtongue", perfect="deflect"),
    "gust_wall": M("a_wall_push", hands="palm"),
    "gust_downdraft": M("a_downdraft", hands="palm"),
    "air_tech": M("a_updraft", hands="oxtongue",
                  modes={"wind grip": R(startup="a_pluck", hold="w_hold", release="a_palm", hands="oxtongue")}),
    "air_dash": M("a_dash", hands="oxtongue"),
    "gust_tailwind": M(None, hold="run", hands="oxtongue",
                       note="x1.3 run: stride-matched run (a_circle_walk is a fixed-rate loop; see REQUESTS.md)"),
    # ---------------------------------------------------------------- 4.14 Air / Vortex
    "vortex_twister": M("a_spin", hands="oxtongue"),
    "vortex_spiral": M("a_pierce", hands="oxtongue"),
    "vortex_funnel": M("a_low_palm", hands="oxtongue"),
    "vortex_eddy": M("a_spin", hands="oxtongue"),
    "vortex_wall": M("a_rising_guard", hold="a_guard", hands="oxtongue", perfect="deflect"),
    "vortex_unleash": M("a_wall_push", hands="palm"),
    "vortex_funnel_down": M("a_downdraft", hands="palm"),
    "vortex_eye": M("a_gather", hands="oxtongue", note="steer while held: the runtime holds the gather's end pose"),
    "vortex_spin_step": M("a_spin", hands="oxtongue"),
    "vortex_whirl": M(None, hold="hover", hands="oxtongue"),
    # ---------------------------------------------------------------- 4.15 Air / Vacuum
    "vacuum_palm": M("a_palm", hands="palm",
                     tiers={1: R("a_double_palm"), 2: R("a_hurricane"), 3: R("a_hurricane")}),
    "vacuum_suction": M("a_pluck", hands="spread"),
    "vacuum_mine": M("a_low_palm", hands="palm"),
    "vacuum_arc": M("a_turn_palm", hands="oxtongue"),
    "vacuum_bubble": M(None, hold="a_guard", hands="oxtongue", perfect="deflect"),
    "vacuum_wave": M("a_wall_push", hands="palm"),
    "vacuum_anchor": M("a_downdraft", hold="e_stone_skin", hands="palm"),
    "vacuum_well": M("a_gather", hands="spread"),
    "vacuum_hop": M("a_updraft", hands="oxtongue"),
    "vacuum_slipstream": M(None, hold="run", hands="oxtongue"),
    # ---------------------------------------------------------------- 4.16 Air / Sound
    "sound_clap": M("a_clap", hands="palm", tiers={1: R("a_roar", startup="a_roar")}),
    "sound_lance": M("a_pierce", hands="oxtongue"),
    "sound_tremor": M("f_stomp", hands="palm"),
    "sound_echo_ring": M("a_spin", hands="palm"),
    "sound_barrier": M(None, hold="a_guard", hands="palm", perfect="deflect"),
    "sound_thunder_step": M("a_dash", hands="palm"),
    "sound_ping": M("f_stomp", hands="palm"),
    "sound_flight": M("a_updraft", hold="flight", hands="oxtongue"),
    "sound_boom_step": M("a_dash", hands="oxtongue"),
    "sound_hover": M(None, hold="hover"),
}

# missing clip -> stand-in (MARTIAL_ARTS.md §4 "fallback" notes + the runtime's built-in chain); resolved repeatedly
CLIP_FALLBACK = {
    # shared
    "strafe_l": "walk", "strafe_r": "walk", "walk_back": "walk", "turn_l90": "idle", "turn_r90": "idle",
    "evade_l": "evade_fwd", "evade_r": "evade_fwd", "evade_back": "evade_fwd", "evade_fwd": "jump",
    "hit_light_back": "hit_light_front", "hit_heavy": "hit_light_front", "guard_break": "stagger",
    "stagger": "hit_light_front", "knockdown": "hit_heavy", "getup": "idle", "block_impact": "hit_light_front",
    "deflect": "block_impact", "salute": "idle", "glide": "fall", "flight": "glide", "hover": "fall",
    "skate": "surf", "surf": "glide", "fall": "idle", "land": "idle", "jump": "idle", "run": "walk",
    "guard": "f_stance",
    # earth
    "e_thrust": "e_strike", "e_ground_rise": "e_lift", "e_ground_slap": "e_pour", "e_sweep": "e_strike",
    "e_push": "e_throw", "e_sink": "e_pour", "e_overhead_slam": "e_heave", "e_disc_flick": "e_strike",
    "e_chain_whirl": "e_sweep", "e_lob": "e_lift", "e_magma_hold": "e_seize_loop", "e_spatter": "e_sweep",
    "e_burrow": "evade_fwd", "e_stone_skin": "e_guard", "e_shadowless_kick": "f_snap_kick", "e_guard": "e_stance",
    "e_wall": "e_lift",
    # water
    "w_lash": "w_push", "w_freeze": "w_push", "w_press": "w_push", "w_maelstrom": "w_lash", "w_ground": "w_push",
    "w_single_whip": "w_lash", "w_shield": "w_stance", "w_snake": "w_ground", "w_draw": "w_push", "w_hold": "w_stance",
    "w_release": "w_push", "w_clench": "w_freeze", "w_heel_kick": "w_push", "w_crane": "w_ground",
    "w_repulse": "evade_back", "w_shuttle": "w_push",
    # fire
    "f_jab": "f_cross", "f_cross": "f_jab", "f_charge": "f_stance", "f_palm_burst": "f_cross", "f_column": "f_palm_burst",
    "f_inferno": "f_palm_burst", "f_snap_kick": "f_jab", "f_low_sweep": "f_snap_kick", "f_crescent_kick": "f_snap_kick",
    "f_tornado_kick": "f_crescent_kick", "f_stomp": "e_wall", "f_heat_draw": "f_thermal_hold", "f_thermal_hold": "f_charge",
    "f_pour": "e_pour", "f_dash": "a_dash", "f_hop": "jump", "f_needle": "f_jab", "f_corona": "f_crescent_kick",
    "l_charge": "f_charge", "l_release": "f_jab", "l_redirect": "deflect", "l_skybreak": "l_release",
    "l_ground": "f_stomp", "l_fan": "l_release", "c_point": "f_jab", "c_toss": "e_disc_flick",
    "c_fuse_loop": "l_charge", "c_chain_stomp": "f_stomp",
    # air
    "a_palm": "f_jab", "a_double_palm": "a_palm", "a_hurricane": "a_double_palm", "a_pierce": "a_palm",
    "a_low_palm": "a_palm", "a_turn_palm": "a_palm", "a_wall_push": "a_double_palm", "a_downdraft": "e_sink",
    "a_updraft": "jump", "a_spin": "a_turn_palm", "a_circle_walk": "run", "a_dash": "evade_fwd",
    "a_gather": "w_draw", "a_pluck": "w_draw", "a_guard": "a_stance", "a_rising_guard": "a_guard",
    "a_clap": "a_palm", "a_roar": "a_clap",
}

LOCOMOTION = {"idle": "idle", "stance": ["e_stance", "w_stance", "f_stance", "a_stance"], "walk": "walk", "run": "run",
              "strafe_l": "strafe_l", "strafe_r": "strafe_r", "back": "walk_back"}
REACTIONS = {"light": "hit_light_front", "light_back": "hit_light_back", "heavy": "hit_heavy",
             "knockdown": "knockdown", "getup": "getup", "guard_break": "guard_break", "bound": "stagger",
             "stagger": "stagger", "block": "block_impact", "perfect": "deflect"}
AIR = {"jump": "jump", "fall": "fall", "land": "land", "glide": "glide"}
GUARD = {"default": "e_guard", "by_element": ["e_guard", "w_shield", "guard", "a_guard"]}
MODES = {"flight": "flight", "hover": "hover", "skate": "skate", "surf": "surf"}
SLOT_FALLBACKS = {"strike": "f_jab", "thrust": "a_pierce", "ground": "e_ground_slap", "sweep": "e_sweep",
                  "guard": "e_wall", "push": "w_push", "sink": "e_sink", "tech": "w_hold",
                  "evade": "evade_fwd", "evade_hold": "run"}
