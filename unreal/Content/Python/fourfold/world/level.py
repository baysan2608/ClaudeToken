"""Level /Game/Fourfold/Maps/L_Lab: the arena placed from sim.json EXACTLY, scenery from level_layout.json, lighting, post, build.

Actors (all tagged "FourfoldWorld" so a rebuild can remove exactly what this script made):
  * root marker  TargetPoint "FourfoldArena" (tag FourfoldArena: the game then skips its placeholder arena)
  * solids       one StaticMeshActor per sim solid, tag FFSolid_<name>, centre = sim box centre, no collision, shadow casting on
                 (the game hides a solid's primitives from the main pass when the camera is behind it, shadows stay)
  * dressing     floor, pool basin + water, metal plate, banners, lanterns, rings (static, baked) and the scenery (movable, lit
                 dynamically by the stationary sun + the sky light / volumetric lightmap): ground, ridges, sky dome, halls, ...
  * lighting     stationary sun (warm, from the south-west, 55 degrees up, behind the default camera), stationary sky light captured from the dome, exponential
                 height fog, reflection captures, point lights at the lanterns / braziers (stationary, baked), post-process volume
                 with FIXED exposure (EV100 -0.263 => exposure scale 1: emissive 1.0 shows as 1.0, a diffuse white under 3.14 lux as 1.0),
                 Lightmass importance + character-indirect volumes
Everything is created through EditorActorSubsystem / LevelEditorSubsystem (see docs/world/API_NOTES.md)."""
import json
import math

import unreal

from . import common as C

TAG = "FourfoldWorld"
SOLID_MESH = {"north_wall": "SM_Env_WallN", "south_wall": "SM_Env_WallS", "west_wall": "SM_Env_WallW", "east_wall": "SM_Env_WallE",
              "cover_wall": "SM_Env_CoverWall", "terrace": "SM_Env_Terrace", "step_block": "SM_Env_StepBlock",
              "high_ledge": "SM_Env_HighLedge", "pillar_ne": "SM_Env_Pillar", "pillar_sw": "SM_Env_Pillar"}
FIXED_EV100 = -0.263            # exposure scale exactly 1.0  (1 / (1.2 * 2^EV))
SUN_PITCH, SUN_YAW = -42.0, -37.9          # late-afternoon sun: long shadows, warm atmosphere (fx FFKeyDir still assumes 55 deg)
SUN_COLOR = (255, 214, 168)
SUN_LUX = 10.0                             # with auto exposure (EV100 0..5): sky + clouds read physically
SKY_INTENSITY = 1.0


def _vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def _rot(pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


class Builder:
    def __init__(self, report, mirror_y=False):
        self.report = report
        self.mirror_y = mirror_y
        self.les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        self.eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.actors = {}
        self.lights = {}

    # ------------------------------------------------------------------------------------------ plumbing
    def spawn(self, cls, loc, rot=(0.0, 0.0, 0.0), label=None, tags=(), folder=None):
        a = self.eas.spawn_actor_from_class(cls, _vec(*loc), _rot(*rot))
        if a is None:
            raise RuntimeError(f"spawn_actor_from_class({cls}) returned None")
        if label:
            a.set_actor_label(label)
        all_tags = [TAG] + list(tags)
        try:
            a.set_editor_property("tags", [unreal.Name(t) for t in all_tags])
        except Exception as e:  # noqa: BLE001
            self.report["notes"].append(f"{label}: could not set tags ({e})")
        if folder:
            try:
                a.set_folder_path(folder)
            except Exception:  # noqa: BLE001
                pass
        if label:
            self.actors[label] = a
        return a

    def clear_previous(self):
        """Destroys every actor carrying the FourfoldWorld tag (a rebuild leaves the rest of the level alone)."""
        n = 0
        try:
            for a in list(self.eas.get_all_level_actors() or []):
                try:
                    if a is not None and any(str(t) == TAG for t in a.get_editor_property("tags")):
                        self.eas.destroy_actor(a)
                        n += 1
                except Exception:  # noqa: BLE001
                    continue
        except Exception as e:  # noqa: BLE001
            self.report["notes"].append(f"could not list level actors to clean up: {e}")
        if n:
            self.report["notes"].append(f"removed {n} actors of a previous build")

    def mesh_actor(self, label, mesh_path, loc, tags=(), mobility="static", cast_shadow=False, scale=(1, 1, 1), rot=(0, 0, 0), folder=None):
        mesh = unreal.load_asset(mesh_path) if C.asset_exists(mesh_path) else None
        if mesh is None:
            self.report["failed"].append({"item": label, "error": f"mesh {mesh_path} not found"})
            return None
        a = self.spawn(unreal.StaticMeshActor, loc, rot, label, tags, folder)
        comp = a.static_mesh_component
        comp.set_static_mesh(mesh)
        try:
            comp.set_mobility(unreal.ComponentMobility.STATIC if mobility == "static" else unreal.ComponentMobility.MOVABLE)
        except Exception as e:  # noqa: BLE001
            self.report["notes"].append(f"{label}: mobility not set ({e})")
        try:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:  # noqa: BLE001
            C.set_prop(comp, "collision_enabled", C.enum("CollisionEnabled", "NO_COLLISION"), self.report, quiet=True)
        try:
            comp.set_generate_overlap_events(False)
        except Exception:  # noqa: BLE001
            pass
        C.set_prop(comp, "cast_shadow", bool(cast_shadow), self.report, quiet=True)
        if mobility == "static":
            C.set_prop(comp, "cast_dynamic_shadow", False, self.report, quiet=True)        # baked: no per-frame shadow pass
        sx, sy, sz = scale
        if self.mirror_y:
            sy = -sy
        if (sx, sy, sz) != (1, 1, 1):
            a.set_actor_scale3d(_vec(sx, sy, sz))
        return a

    # ------------------------------------------------------------------------------------------ arena + scenery
    def build_arena(self, meshes, sim, layout, instances_unused=None):
        arena = sim["arena_lab"]

        def v3(d):
            return d["$v3"] if isinstance(d, dict) else d

        marker = self.spawn(unreal.TargetPoint, (0, 0, 0), label="FourfoldArena", tags=["FourfoldArena"], folder="Fourfold/Arena")
        self.report["created"].append("actor FourfoldArena")
        solids = {s["name"]: s for s in arena["solids"]}
        for sname, s in solids.items():
            mesh_name = SOLID_MESH.get(sname)
            ent = meshes.get(mesh_name)
            if ent is None:
                self.report["failed"].append({"item": f"solid {sname}", "error": f"mesh {mesh_name} not imported"})
                continue
            mn, mx = v3(s["min"]), v3(s["max"])
            c = [(mn[i] + mx[i]) * 50.0 for i in range(3)]               # centre in cm (sim axes)
            loc = (c[0], c[2], c[1])                                       # Unreal X = x, Y = z, Z = y
            scale = self._fix_scale(ent)
            a = self.mesh_actor(f"FFSolid_{sname}", ent["path"], loc, tags=[f"FFSolid_{sname}", "FourfoldArena"], mobility="static",
                                cast_shadow=True, scale=scale, folder="Fourfold/Arena/Solids")
            if a is not None:
                self.report["created"].append(f"actor FFSolid_{sname}")
        for ent_l in layout.get("actors", []):
            if ent_l.get("group") == "solid":
                continue
            if ent_l["mesh"] in ("SM_Env_Sky", "SM_Env_Ridges"):
                continue          # replaced by SkyAtmosphere + VolumetricCloud (build_lighting)
            mesh = meshes.get(ent_l["mesh"])
            if mesh is None:
                self.report["failed"].append({"item": ent_l["actor"], "error": f"mesh {ent_l['mesh']} not imported"})
                continue
            grp = ent_l.get("group", "arena")
            a = self.mesh_actor(ent_l["actor"], mesh["path"], ent_l["location"], tags=["FourfoldArena"] if grp == "arena" else [],
                                mobility=ent_l.get("mobility", "static"), cast_shadow=bool(ent_l.get("cast_shadow", False)),
                                scale=self._fix_scale(mesh), rot=ent_l.get("rotation", (0, 0, 0)),
                                folder="Fourfold/Arena" if grp == "arena" else "Fourfold/Scenery")
            if a is not None:
                if grp != "arena" and not ent_l.get("cast_shadow", False):
                    # backdrop (halls, tree rows): Lumen traces mesh distance fields for sun shadowing even when cast_shadow
                    # is off, so the tree rows striped the courtyard with 20 m shadows. Keep them out of DF lighting.
                    C.set_prop(a.static_mesh_component, "affect_distance_field_lighting", False, self.report, quiet=True)
                if ent_l["mesh"] in ("SM_Env_Sky", "SM_Env_Ridges"):
                    comp = a.static_mesh_component
                    C.set_prop(comp, "cast_shadow", False, self.report, quiet=True)
                    C.set_prop(comp, "affect_distance_field_lighting", False, self.report, quiet=True)
                    C.set_prop(comp, "affect_dynamic_indirect_lighting", False, self.report, quiet=True)
                self.report["created"].append(f"actor {ent_l['actor']}")
        return marker

    def _fix_scale(self, ent):
        """Scale that makes the imported bounds equal the expected ones (guards against a 100x unit mistake)."""
        r = ent.get("ratio")
        if r and (r < 0.2 or r > 5.0):
            self.report["notes"].append(f"{ent['path']}: imported size is {r:.3f} x expected: compensating with actor scale")
            return (1.0 / r, 1.0 / r, 1.0 / r)
        return (1.0, 1.0, 1.0)

    # ------------------------------------------------------------------------------------------ lighting
    def build_lighting(self, layout):
        rep = self.report
        # sun
        sun = self.spawn(unreal.DirectionalLight, (0, 0, 2500), (SUN_PITCH, SUN_YAW, 0), "FF_Sun", folder="Fourfold/Lighting")
        sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
        sc.set_mobility(unreal.ComponentMobility.MOVABLE)
        for k, v in (("intensity", SUN_LUX), ("light_color", unreal.Color(*SUN_COLOR, 255)), ("cast_shadows", True),
                     ("dynamic_shadow_distance_stationary_light", 3800.0), ("dynamic_shadow_distance_movable_light", 3800.0),
                     ("dynamic_shadow_cascades", 2), ("cascade_distribution_exponent", 2.2), ("cascade_transition_fraction", 0.15),
                     ("light_source_angle", 0.8), ("atmosphere_sun_light", True),
                     ("cast_cloud_shadows", True), ("use_ray_traced_distance_field_shadows", False), ("cast_shadows_on_clouds", True),
                     # per-pixel transmittance banded the courtyard floor into long dark stripes (arena sits on the planet top)
                     ("per_pixel_atmosphere_transmittance", False), ("forward_shading_priority", 1),
                     ("use_inset_shadows_for_movable_objects", True)):
            C.set_prop(sc, k, v, rep, quiet=True)
        self.lights["sun"] = sc
        # physical sky: atmosphere scattering lit by the sun + volumetric clouds
        atm = self.spawn(unreal.SkyAtmosphere, (0, 0, 0), label="FF_SkyAtmosphere", folder="Fourfold/Lighting")
        ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
        for k, v in (("mie_scattering_scale", 0.006), ("mie_anisotropy", 0.82), ("multi_scattering_factor", 1.0),
                     ("aerial_pespective_view_distance_scale", 1.6), ("height_fog_contribution", 1.0)):
            C.set_prop(ac, k, v, rep, quiet=True)
        try:
            cl = self.spawn(unreal.VolumetricCloud, (0, 0, 0), label="FF_Clouds", folder="Fourfold/Lighting")
            cc = cl.get_component_by_class(unreal.VolumetricCloudComponent)
            for k, v in (("layer_bottom_altitude", 3.5), ("layer_height", 6.0), ("view_sample_count_scale", 1.0)):
                C.set_prop(cc, k, v, rep, quiet=True)
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"volumetric clouds not created: {e}")
        # sky light: real-time capture of the atmosphere + clouds (movable, Lumen uses it for sky occlusion)
        sky = self.spawn(unreal.SkyLight, (0, 0, 2400), label="FF_SkyLight", folder="Fourfold/Lighting")
        kc = sky.get_component_by_class(unreal.SkyLightComponent)
        kc.set_mobility(unreal.ComponentMobility.MOVABLE)
        for k, v in (("source_type", C.enum("SkyLightSourceType", "SLS_CAPTURED_SCENE")), ("intensity", SKY_INTENSITY),
                     ("real_time_capture", True), ("lower_hemisphere_is_black", False),
                     ("lower_hemisphere_color", unreal.LinearColor(0.13, 0.115, 0.10, 1.0)), ("cubemap_resolution", 256)):
            C.set_prop(kc, k, v, rep, quiet=True)
        self.lights["sky"] = kc
        # fog
        fog = self.spawn(unreal.ExponentialHeightFog, (0, 0, 400), label="FF_Fog", folder="Fourfold/Lighting")
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        for k, v in (("fog_density", 0.012), ("fog_height_falloff", 0.25), ("start_distance", 600.0), ("fog_max_opacity", 0.9),
                     ("fog_inscattering_luminance", unreal.LinearColor(0.50, 0.40, 0.31, 1.0)), ("enable_volumetric_fog", True),
                     ("volumetric_fog_scattering_distribution", 0.55), ("volumetric_fog_extinction_scale", 0.6),
                     ("volumetric_fog_distance", 5000.0),
                     ("directional_inscattering_luminance", unreal.LinearColor(0.35, 0.25, 0.15, 1.0)),
                     ("directional_inscattering_exponent", 12.0)):
            C.set_prop(fc, k, v, rep, quiet=True)
        # reflection captures: Lumen reflections on the Mac; the captures are only a fallback, so none are placed
        for label, loc, radius in ():
            try:
                rc = self.spawn(unreal.SphereReflectionCapture, loc, label=label, folder="Fourfold/Lighting")
                comp = rc.get_component_by_class(unreal.SphereReflectionCaptureComponent)
                C.set_prop(comp, "influence_radius", radius, rep, quiet=True)
            except Exception as e:  # noqa: BLE001
                rep["notes"].append(f"reflection capture {label} not created: {e}")
        # point lights at the lanterns / braziers (stationary => baked into the lightmaps, cheap on iOS)
        n = 0
        for i, l in enumerate(layout.get("lights", [])):
            try:
                pl = self.spawn(unreal.PointLight, l["location"], label=f"FF_{l['kind']}_{i}", folder="Fourfold/Lighting")
                pc = pl.get_component_by_class(unreal.PointLightComponent)
                pc.set_mobility(unreal.ComponentMobility.MOVABLE)
                col = l["color"]
                for k, v in (("intensity_units", C.enum("LightUnits", "CANDELAS")), ("intensity", float(l["intensity_cd"])),
                             ("light_color", unreal.Color(int(col[0] * 255), int(col[1] * 255), int(col[2] * 255), 255)),
                             ("attenuation_radius", float(l["radius_m"]) * 100.0), ("cast_shadows", False),
                             ("volumetric_scattering_intensity", 2.0),
                             ("use_inverse_squared_falloff", True), ("source_radius", 8.0)):
                    C.set_prop(pc, k, v, rep, quiet=True)
                n += 1
            except Exception as e:  # noqa: BLE001
                rep["notes"].append(f"point light {i} not created: {e}")
        rep["notes"].append(f"{n} point lights placed")
        self._post_process()
        self._volumes()

    def _post_process(self):
        rep = self.report
        ppv = self.spawn(unreal.PostProcessVolume, (0, 0, 0), label="FF_Post", folder="Fourfold/Lighting")
        C.set_prop(ppv, "unbound", True, rep)
        try:
            st = ppv.get_editor_property("settings")
            vals = [
                ("auto_exposure_method", C.enum("AutoExposureMethod", "AEM_BASIC", "AEM_HISTOGRAM")),
                ("auto_exposure_min_brightness", 0.0), ("auto_exposure_max_brightness", 5.0),
                ("auto_exposure_bias", 0.3), ("auto_exposure_speed_up", 2.0), ("auto_exposure_speed_down", 1.5),
                ("bloom_intensity", 0.55), ("bloom_threshold", 1.0),
                ("motion_blur_amount", 0.35), ("motion_blur_max", 2.0), ("lens_flare_intensity", 0.0),
                ("vignette_intensity", 0.32),
                ("ambient_occlusion_intensity", 0.6), ("ambient_occlusion_radius", 120.0),
                ("lumen_final_gather_quality", 2.0), ("lumen_reflection_quality", 2.0), ("lumen_scene_lighting_quality", 2.0),
                ("film_toe", 0.6), ("film_shoulder", 0.26), ("film_slope", 0.86),
                ("white_temp", 6200.0),
                ("color_saturation", unreal.Vector4(1.05, 1.05, 1.05, 1.0)),
                ("color_contrast", unreal.Vector4(1.06, 1.06, 1.06, 1.0)),
                ("scene_color_tint", unreal.LinearColor(1.0, 0.985, 0.96, 1.0)),
            ]
            for k, v in vals:
                if v is None:
                    continue
                C.set_prop(st, "override_" + k, True, rep, quiet=True)
                C.set_prop(st, k, v, rep, quiet=True)
            ppv.set_editor_property("settings", st)
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"post-process settings not applied: {e}")

    def _volumes(self):
        rep = self.report
        for cls_name, label, scale in (("LightmassImportanceVolume", "FF_LightmassImportance", (44.0, 44.0, 9.0)),
                                       ("LightmassCharacterIndirectDetailVolume", "FF_CharacterIndirect", (40.0, 40.0, 6.0))):
            cls = getattr(unreal, cls_name, None)
            if cls is None:
                rep["notes"].append(f"{cls_name} not available")
                continue
            try:
                v = self.spawn(cls, (0, 0, 450), label=label, folder="Fourfold/Lighting")
                v.set_actor_scale3d(_vec(*scale))
            except Exception as e:  # noqa: BLE001
                rep["notes"].append(f"{label} not created: {e}")

    # ------------------------------------------------------------------------------------------ world settings
    def world_settings(self):
        rep = self.report
        try:
            world = None
            try:
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            except Exception:  # noqa: BLE001
                world = unreal.EditorLevelLibrary.get_editor_world()          # older API, deprecated since 5.0
            ws = world.get_world_settings()
            lm = ws.get_editor_property("lightmass_settings")
            for k, v in (("num_indirect_lighting_bounces", 2), ("use_ambient_occlusion", True), ("max_occlusion_distance", 220.0),
                         ("fully_occluded_samples_fraction", 0.85), ("direct_illumination_occlusion_fraction", 0.5),
                         ("indirect_illumination_occlusion_fraction", 1.0), ("occlusion_exponent", 1.0),
                         ("static_lighting_level_scale", 1.0)):
                C.set_prop(lm, k, v, rep, quiet=True)
            ws.set_editor_property("lightmass_settings", lm)
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"world / lightmass settings not applied: {e}")

    # ------------------------------------------------------------------------------------------ lighting build + fallback
    def build_lighting_bake(self, mode):
        """mode 'baked' | 'dynamic' | 'auto'.  Returns the mode actually used ('baked' or 'dynamic')."""
        rep = self.report
        if mode in ("dynamic", "auto"):
            # Desktop-first look: everything movable, Lumen GI. Lightmass started from a script call also times out
            # ("Timed out waiting for the recipient"), so "baked" is opt-in only.
            self.make_dynamic()
            return "dynamic"
        if "-run=" in unreal.SystemLibrary.get_command_line().lower():
            # A commandlet has no renderer: Lightmass asserts in the render graph (InDesc.IsValid) and crashes.
            rep["notes"].append("lighting not built (commandlet, no renderer): run the setup in the editor to bake")
            return "unbuilt"
        try:
            for k in ("sky",):
                comp = self.lights.get(k)
                if comp is not None:
                    try:
                        comp.recapture_sky()
                    except Exception:  # noqa: BLE001
                        pass
            q = C.enum("LightingBuildQuality", "QUALITY_MEDIUM", "QUALITY_PREVIEW")
            ok = self.les.build_light_maps(q, True)
            if ok is False:
                raise RuntimeError("build_light_maps returned False")
            rep["notes"].append("lighting built (quality medium)")
            return "baked"
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"lighting build failed ({e}): falling back to fully dynamic lighting")
            self.make_dynamic()
            return "dynamic"

    def make_dynamic(self):
        """Fallback when baking is unavailable: movable sun + sky light with real-time capture, static meshes cast dynamic shadows."""
        rep = self.report
        for key in ("sun", "sky"):
            comp = self.lights.get(key)
            if comp is not None:
                try:
                    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                except Exception as e:  # noqa: BLE001
                    rep["notes"].append(f"{key}: could not set Movable ({e})")
        sk = self.lights.get("sky")
        if sk is not None:
            C.set_prop(sk, "real_time_capture", True, rep, quiet=True)
        for label, a in self.actors.items():
            try:
                comp = a.static_mesh_component
            except Exception:  # noqa: BLE001
                continue
            if label.startswith("FFSolid_") or label in ("FFArena_Floor", "FFArena_PoolBasin"):
                C.set_prop(comp, "cast_dynamic_shadow", True, rep, quiet=True)
                C.set_prop(comp, "cast_shadow", True, rep, quiet=True)
            if label in ("FFArena_Floor", "FFArena_PoolBasin"):
                # a 34 m zero-thickness plane has no usable distance field
                C.set_prop(comp, "affect_distance_field_lighting", False, rep, quiet=True)

    def save(self):
        try:
            self.les.save_current_level()
            return True
        except Exception as e:  # noqa: BLE001
            self.report["failed"].append({"item": C.MAP_PATH, "error": f"save_current_level: {e}"})
            return False


def build_level(force, meshes, lighting, report):
    """Creates (or rebuilds) L_Lab.  Returns the lighting mode used or None when nothing was built."""
    paths = C.project_paths()
    sim = C.load_json(paths["sim_json"], report, "sim.json")
    layout = C.load_json(paths["layout_json"], report, "level_layout.json") or {}
    if not sim:
        report["failed"].append({"item": C.MAP_PATH, "error": "sim.json not readable: arena cannot be placed"})
        return None
    C.make_dirs(C.MAP_DIR)
    b = Builder(report, mirror_y=False)
    try:
        exists = C.asset_exists(C.MAP_PATH)
        if exists:
            b.les.load_level(C.MAP_PATH)
            has_arena = any(a is not None and any(str(t) == "FourfoldArena" for t in a.get_editor_property("tags"))
                            for a in (b.eas.get_all_level_actors() or []))
            if has_arena and not force:
                report["skipped"].append(C.MAP_PATH)
                return None
            b.clear_previous()
        else:
            if not b.les.new_level(C.MAP_PATH):
                report["failed"].append({"item": C.MAP_PATH, "error": "LevelEditorSubsystem.new_level returned False"})
                return None
            report["notes"].append("new level created")
    except Exception as e:  # noqa: BLE001
        import traceback
        report["failed"].append({"item": C.MAP_PATH, "error": f"level create / load failed: {e}\n{traceback.format_exc()}"})
        return None
    from . import meshes as M
    b.mirror_y = M.detect_mirror_y(meshes, report)
    try:
        b.build_arena(meshes, sim, layout)
        b.build_lighting(layout)
        b.world_settings()
    except Exception as e:  # noqa: BLE001
        import traceback
        report["failed"].append({"item": "level actors", "error": f"{e}\n{traceback.format_exc()}"})
    b.save()
    mode = b.build_lighting_bake(lighting)
    b.save()
    report["created"].append(C.MAP_PATH)
    report["notes"].append(f"lighting mode: {mode}")
    return mode
