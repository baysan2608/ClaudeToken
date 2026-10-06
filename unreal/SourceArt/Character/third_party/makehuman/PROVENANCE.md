# MakeHuman CC0 data (pinned)

Downloaded from `https://raw.githubusercontent.com/makehumancommunity/makehuman/a8bc2d54ff0ac92e78ff71431b1023eda42bf482/makehuman/data/<path>`
(the paths below this folder mirror `makehuman/data/`). Licence: CC0 1.0 (`LICENSE.ASSETS.md`).

Only the files the build uses are kept (`ch_body.used_target_files()` + the measurement pairs of
`solve_proportions.py` + `targets/ears/{l,r}-ear-trans-backward.target`, which `ch_regions.landmarks()` reads only as
a vertex mask so the hair stays off the ears). To try other targets, fetch them with
`/home/user/tools/bpyenv/bin/python unreal/Tools/blender/character/fetch_makehuman.py targets/<group>/<name>.target ...`.
