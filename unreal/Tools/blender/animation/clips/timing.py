"""Sim-fitted timing (contact frame, total frames) per strike clip, applied after the builder with Clip.retime().

Why: the runtime maps a clip's contact onto the move's startup (rate clamped 0.6-1.6) and the rest of the clip onto
active + recovery (same clamp).  Most strike clips were authored with ~10-frame recoveries against 14-22-frame sim
recoveries, so the recovery played at the 0.6x floor and then FROZE on the last frame while the move was still
recovering.  The lengths below are ~ contact + active + recovery of the moves that use the clip (see
`python3 ffa_audit.py --timing`), so the follow-through and the settle back to the stance play at ~1x: slower and more
deliberate in real time, nothing frozen.  Contacts move only where the anticipation was squeezed past the clamp
(water) or too short for the startup (fire palms, lightning).  Keep every contact inside
[max startup / 1.6, min startup * 1.6] of its moves (test_anim_data.py warns otherwise).
"""
TIMING = {
    # fire / lightning / combustion (Northern Shaolin)
    "f_jab": (7, 24), "f_palm_burst": (7, 28), "f_snap_kick": (10, 32), "f_needle": (8, 28),
    "f_crescent_kick": (12, 34), "f_low_sweep": (17, 44), "f_stomp": (9, 28), "l_release": (8, 28),
    "c_point": (10, 28), "c_toss": (8, 28), "l_ground": (14, 38), "l_fan": (12, 34),
    # earth (Hung Gar)
    "e_strike": (11, 34), "e_lift": (11, 33), "e_throw": (8, 30), "e_thrust": (12, 36), "e_sweep": (13, 40),
    "e_ground_slap": (14, 40), "e_heave": (18, 40), "e_pour": (13, 36), "e_sink": (8, 28), "e_disc_flick": (8, 28),
    "e_lob": (12, 32), "e_ground_rise": (15, 40), "e_chain_whirl": (14, 40), "e_spatter": (12, 34),
    # water (Tai Chi)
    "w_lash": (12, 38), "w_push": (10, 32), "w_draw": (12, 34), "w_release": (8, 30), "w_freeze": (12, 32),
    "w_press": (10, 30), "w_ground": (18, 44), "w_single_whip": (12, 38), "w_crane": (15, 40),
    # air (Baguazhang)
    "a_palm": (8, 26), "a_pierce": (10, 30), "a_low_palm": (14, 38), "a_double_palm": (10, 30), "a_clap": (6, 24),
    "a_downdraft": (6, 24), "a_updraft": (9, 26),
}
