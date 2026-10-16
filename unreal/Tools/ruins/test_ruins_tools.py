"""Self-test of the Jungle Ruins tools (plain python3, no dependencies):  python3 -I unreal/Tools/ruins/test_ruins_tools.py

Covers ruins_spec (frames, thinning, site plan geometry), ruins_data (the core's floor rule, traps, stone boxes, site
picking, the world.json / float32 writers, validation) and fetch_ruins' member selection. Exit code 1 on a failure."""
import json
import math
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fetch_ruins as F  # noqa: E402
import ruins_data as RD  # noqa: E402
import ruins_spec as RS  # noqa: E402


def creek_grid():
    """The CoreTests creek (test_world_water.cpp) on a 21 x 21 grid at 2 m, x / z in -20..20."""
    g = RD.Grid(21, 21, -20.0, -20.0, 2.0)
    h, w = [], []
    for k in range(21):
        for i in range(21):
            x, z = g.pos(i, k)
            base = 0.05 * x
            h.append(base - 1.2 if abs(z) <= 4.0 else base)
            w.append(base - 0.3 if abs(z) <= 4.0 else RD.NAN)
    return g, h, w


class Frames(unittest.TestCase):
    def test_pyramid_in_world_and_unreal_space(self):
        x, y, z = RS.bl_to_world(543.7, 809.8, 5.0)
        self.assertAlmostEqual(x, -56.3, places=6)
        self.assertAlmostEqual(y, 5.0, places=6)
        self.assertAlmostEqual(z, -81.8, places=6)
        X, Y, Z = RS.world_to_ue(x, y, z)
        # a Blender-local FBX vertex (bx - cx, by - cy, bz) imports at UE (100 bx, -100 by, 100 bz): the same point
        self.assertAlmostEqual(X, 100.0 * (543.7 - 600.0), places=4)
        self.assertAlmostEqual(Y, -100.0 * (809.8 - 728.0), places=4)
        self.assertAlmostEqual(Z, 500.0, places=4)
        back = RS.world_to_bl(*RS.ue_to_world(X, Y, Z))
        for a, b in zip(back, (543.7, 809.8, 5.0)):
            self.assertAlmostEqual(a, b, places=6)

    def test_yaw_is_mirrored(self):
        self.assertAlmostEqual(RS.bl_yaw_to_ue(math.pi / 2.0), -90.0)

    def test_spawn_faces_the_pyramid(self):
        sx, _, sz = RS.bl_to_world(RS.CAMERA_BL[0], RS.CAMERA_BL[1], 0.0)
        px, _, pz = RS.bl_to_world(RS.PYRAMID_BL[0], RS.PYRAMID_BL[1], 0.0)
        f = RS.facing_toward(sx, sz, px, pz)
        d = math.hypot(px - sx, pz - sz)
        self.assertAlmostEqual(sx + math.sin(f) * d, px, places=6)
        self.assertAlmostEqual(sz + math.cos(f) * d, pz, places=6)

    def test_core_is_the_sim_grid(self):
        g = RS.GRID
        x0, _, z_hi = RS.bl_to_world(RS.CORE_BL[0], RS.CORE_BL[1], 0.0)
        x1, _, z_lo = RS.bl_to_world(RS.CORE_BL[2], RS.CORE_BL[3], 0.0)
        self.assertEqual((g["x0"], g["x0"] + (g["nx"] - 1) * g["cell"]), (x0, x1))
        self.assertEqual((g["z0"], g["z0"] + (g["nz"] - 1) * g["cell"]), (z_lo, z_hi))

    def test_sites_keep_their_distance_from_the_spawn(self):
        sx, sy = RS.CAMERA_BL
        for p in RS.SITE_PLAN:
            if p["anchor"] is not None and p["mode"] == "flat":
                self.assertGreater(math.hypot(p["anchor"][0] - sx, p["anchor"][1] - sy), RS.MIN_SITE_GAP_M + p["radius"])


class Thinning(unittest.TestCase):
    def test_keep_is_stable_and_about_one_in_n(self):
        s = RS.salt("RiverForest_Cinematic_RiverForest_01_Animated_Translucent")
        a = [RS.keep(s, j, 6) for j in range(60000)]
        self.assertEqual(a, [RS.keep(s, j, 6) for j in range(60000)])
        self.assertTrue(8500 < sum(a) < 11500, sum(a))

    def test_every_one_keeps_all_and_names_shorten(self):
        self.assertTrue(all(RS.keep(5, j, 1) for j in range(100)))
        self.assertEqual(RS.proto_key("RiverForest_01_Animated_Translucent"), "RiverForest_01")
        self.assertEqual(RS.proto_key("RiverSeedling_03_Translucent"), "RiverSeedling_03")
        self.assertEqual(RS.proto_key("Grass_B_02"), "Grass_B_02")


class Floors(unittest.TestCase):
    def test_creek_floor_matches_the_core_rule(self):
        g, h, w = creek_grid()
        fl = RD.sample_floors(h, w, 0.85)
        self.assertAlmostEqual(fl[g.idx(*g.nearest(10.0, 0.0))], 0.5 - 1.15, places=6)
        self.assertAlmostEqual(RD.floor_at(h, w, g, 0.85, 10.0, 5.0), 0.5 - 0.6, places=6)
        self.assertAlmostEqual(RD.water_at(w, g, 10.0, 0.0), 0.2, places=6)
        self.assertTrue(math.isnan(RD.water_at(w, g, 10.0, 12.0)))


class Walkability(unittest.TestCase):
    def test_a_pit_is_a_trap_and_stairs_are_not(self):
        g = RD.Grid(21, 21, -20.0, -20.0, 2.0)
        fl = [0.0] * (21 * 21)
        for k in range(3, 7):
            for i in range(3, 7):
                fl[g.idx(i, k)] = -4.0                       # 4 m deep: a 4 m rise per 2 m step cannot be climbed
        for k in range(8, 14):
            for i in range(12, 18):
                fl[g.idx(i, k)] = 1.5 * (i - 11)             # stairs, 1.5 m per 2 m step (37 degrees)
        traps = RD.traps(fl, g, g.nearest(0.0, 0.0), 1.0)
        self.assertEqual(set(traps), {g.idx(i, k) for k in range(3, 7) for i in range(3, 7)})


class Boxes(unittest.TestCase):
    def test_stone_columns_merge_into_one_box_below_the_surface(self):
        g = RD.Grid(11, 11, 0.0, 0.0, 2.0)
        h_ter = [1.0] * 121
        h_all = list(h_ter)
        mask = [False] * 121
        for k in range(2, 6):
            for i in range(3, 9):
                h_all[g.idx(i, k)] = 5.4
                mask[g.idx(i, k)] = True
        boxes = RD.stone_boxes(h_all, h_ter, g, mask)
        self.assertEqual(len(boxes), 1)
        b = boxes[0]
        self.assertEqual((b["kind"], b["surface"]), ("wall", "stone"))
        self.assertAlmostEqual(b["max"][1], 5.0)              # floor((5.4 - 0.3) / 0.5) * 0.5: below the walkable top
        self.assertAlmostEqual(b["min"][1], 0.0)              # bare terrain 1.0, sunk 1 m
        self.assertEqual((b["min"][0], b["max"][0]), (5.5, 16.5))
        self.assertEqual((b["min"][2], b["max"][2]), (3.5, 10.5))

    def _assert_sound(self, boxes, cap):
        self.assertLessEqual(len(boxes), cap)
        for b in boxes:
            for a in range(3):
                self.assertLess(b["min"][a], b["max"][a], b)
        self.assertEqual([b["name"] for b in boxes], [f"stone_{n}" for n in range(len(boxes))])

    def test_a_coarser_quantum_merges_uneven_columns_and_never_inverts(self):
        g = RD.Grid(41, 41, 0.0, 0.0, 2.0)
        h_ter = [1.0] * (41 * 41)
        h_all = [5.4 if (j % 41 + j // 41) % 2 == 0 else 5.9 for j in range(41 * 41)]     # checkerboard: tops 5.0 / 5.5 at 0.5
        boxes = RD.stone_boxes(h_all, h_ter, g, [True] * (41 * 41))
        self._assert_sound(boxes, 400)
        self.assertEqual(len(boxes), 1)                                  # at quantum 1 both tops are 5.0: one rectangle
        self.assertAlmostEqual(boxes[0]["max"][1], 5.0)

    def test_the_box_cap_holds_and_low_columns_never_make_inverted_boxes(self):
        g = RD.Grid(51, 51, 0.0, 0.0, 2.0)
        h_ter = [2.0] * (51 * 51)
        h_all = list(h_ter)
        mask = [False] * (51 * 51)
        for k in range(0, 51, 2):
            for i in range(0, 51, 2):                                    # 676 isolated 1 m columns: nothing to merge
                h_all[g.idx(i, k)] = 3.0
                mask[g.idx(i, k)] = True
        boxes = RD.stone_boxes(h_all, h_ter, g, mask)
        self._assert_sound(boxes, 400)
        self.assertEqual(len(boxes), 400)                                # truncated, not silently emptied
        for b in boxes:
            self.assertGreaterEqual(b["max"][1] - b["min"][1], 0.25)
        small = RD.stone_boxes(h_all, h_ter, g, mask, max_boxes=50)
        self._assert_sound(small, 50)
        self.assertEqual(len(small), 50)

    def test_a_fine_grid_still_gives_boxes_a_footprint(self):
        g = RD.Grid(9, 9, 0.0, 0.0, 1.0)                                 # inset 0.5 would leave a 1 m cell zero width
        h_ter = [0.0] * 81
        h_all = list(h_ter)
        mask = [False] * 81
        h_all[g.idx(4, 4)] = 3.0
        mask[g.idx(4, 4)] = True
        boxes = RD.stone_boxes(h_all, h_ter, g, mask)
        self._assert_sound(boxes, 400)
        self.assertEqual(len(boxes), 1)

    def test_coarse_quanta_drop_columns_that_would_sit_below_their_own_floor(self):
        g = RD.Grid(11, 11, 0.0, 0.0, 2.0)
        h_ter = [2.0] * 121
        h_all = list(h_ter)
        mask = [False] * 121
        h_all[g.idx(5, 5)] = 3.0
        mask[g.idx(5, 5)] = True
        # quantum 4: floor((3.0 - 0.3) / 4) * 4 = 0 < terrain 2.0 - 1: no box (rather than an inverted one)
        self.assertEqual(RD.stone_boxes(h_all, h_ter, g, mask, quant=4.0), [])


class Sites(unittest.TestCase):
    def test_flat_site_prefers_the_flattest_sample(self):
        g = RD.Grid(11, 11, 0.0, 0.0, 2.0)
        fl = [0.6 * (j % 11) for j in range(121)]
        for k in range(4, 7):
            for i in range(6, 9):
                fl[g.idx(i, k)] = 4.0
        ok = [True] * 121
        self.assertEqual(RD.pick_flat(fl, g, ok, 10.0, 10.0, 8.0), (7, 5))

    def test_bank_site_keeps_off_the_water(self):
        g, h, w = creek_grid()
        fl = RD.sample_floors(h, w, 0.85)
        ok = [g.inside(*g.pos(j % g.nx, j // g.nx), 1.0) for j in range(len(fl))]
        i, k = RD.pick_bank(fl, w, h, g, ok)
        self.assertTrue(3.0 <= min(abs(g.pos(i, k)[1] - 4.0), abs(g.pos(i, k)[1] + 4.0)) <= 10.0)


class Writers(unittest.TestCase):
    def _world(self, g, sites):
        return RD.world_dict(g, (0.0, 0.0, 10.0), math.pi, sites, [RD.make_shrine("shrine_t", "Test Shrine", (0.0, 0.0, 6.0))],
                             [], [], {}, RS.EDGE_MARGIN, RS.WADE_DEPTH, {"credit": RS.CREDIT})

    def test_world_files_round_trip_in_the_core_format(self):
        g, h, w = creek_grid()
        world = self._world(g, [])
        with tempfile.TemporaryDirectory() as d:
            paths = RD.write_world(d, world, h, w)
            with open(paths["world.json"]) as f:
                back = json.load(f)
            self.assertEqual((back["terrain"]["nx"], back["terrain"]["nz"], back["terrain"]["cell"]), (21, 21, 2.0))
            self.assertEqual((back["terrain"]["edge_margin"], back["terrain"]["editable"]), (RS.EDGE_MARGIN, False))
            self.assertEqual((back["terrain"]["heights"], back["water"]["layer"]), ("heights.f32", "water.f32"))
            self.assertNotIn("level", back["water"])
            with open(paths["heights.f32"], "rb") as f:
                hb = RD.unpack_f32(f.read())
            with open(paths["water.f32"], "rb") as f:
                wb = RD.unpack_f32(f.read())
            self.assertEqual((len(hb), len(wb)), (441, 441))
            self.assertAlmostEqual(hb[0], h[0], places=5)
            self.assertTrue(math.isnan(wb[0]))
            self.assertAlmostEqual(wb[g.idx(10, 10)], -0.3, places=5)

    def test_validate_flags_a_site_in_the_water_and_passes_a_good_world(self):
        g, h, w = creek_grid()
        good = self._world(g, [RD.make_site("s_a", "A", "earth", 0, (-10.0, 0.0, 10.0), 0.0, "novice", 9.0),
                               RD.make_site("s_b", "B", "water", 1, (10.0, 0.5, -10.0), 0.0, "adept", 9.0)])
        self.assertEqual(RD.validate(good, h, w, g), [])
        bad = self._world(g, [RD.make_site("s_a", "A", "earth", 0, (-10.0, 0.0, 10.0), 0.0, "novice", 9.0),
                              RD.make_site("s_w", "Wet", "water", 1, (0.0, 0.0, 0.0), 0.0, "adept", 9.0)])
        self.assertTrue(any("in the water" in e for e in RD.validate(bad, h, w, g)))


class Fetch(unittest.TestCase):
    def test_selection_keeps_the_pipeline_inputs_only(self):
        names = ["JungleRuins/", "JungleRuins/credits_license.txt", "JungleRuins/Blender/JungleRuins_Main.blend",
                 "JungleRuins/USD/JungleRuins_Karma.usda", "JungleRuins/USD/elements/Pyramid/Pyramid.usd",
                 "JungleRuins/USD/elements/Terrain/Terrain_Cinematic.usd", "JungleRuins/USD/elements/Terrain/Terrain_Extended.usd",
                 "JungleRuins/USD/elements/Banyan/Banyan.usd", "JungleRuins/USD/elements/QueenForest/PI_S_QueenForest.usd",
                 "JungleRuins/USD/elements/Pyramid_Moss/PI_S_Moss.usd", "JungleRuins/USD/elements/RiverForest/riverforest_classes.usda",
                 "JungleRuins/textures/Terrain/Terrain_Cinematic_Normal_4_3.jpg", "JungleRuins/textures/Terrain/Terrain_BaseColor_0_0.jpg",
                 "JungleRuins/textures/Trees/PX_Prunus_x_yedoensis_Leaf_02_albedo.jpg", "JungleRuins/textures/Plants/Shrub_04_BaseColor.tif",
                 "JungleRuins/../escape.txt", "Other/credits_license.txt"]
        self.assertEqual(F.select_members(names), [
            "JungleRuins/credits_license.txt", "JungleRuins/USD/JungleRuins_Karma.usda",
            "JungleRuins/USD/elements/Pyramid/Pyramid.usd", "JungleRuins/USD/elements/Terrain/Terrain_Cinematic.usd",
            "JungleRuins/USD/elements/RiverForest/riverforest_classes.usda",
            "JungleRuins/textures/Terrain/Terrain_Cinematic_Normal_4_3.jpg", "JungleRuins/textures/Plants/Shrub_04_BaseColor.tif"])

    def test_destinations_stay_inside_the_extract_folder(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue(F.safe_dest(d, "JungleRuins/USD/a.usd").startswith(os.path.abspath(d)))
            with self.assertRaises(ValueError):
                F.safe_dest(d, "JungleRuins/../../evil.txt")


if __name__ == "__main__":
    unittest.main()
