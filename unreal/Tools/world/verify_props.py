"""Verifies the editor-Python property names the world / audio scripts set against the UE public headers (a header mirror made by the
game stream; see docs/world/API_NOTES.md).  Python name = C++ UPROPERTY name with the bool prefix `b` removed, in snake_case.

    python3 Tools/world/verify_props.py [path to Engine/Source/Runtime]
Prints every name that is NOT found (exit 1) - those are the ones that would fail on the Mac and end up in setup_report.json `notes`."""
import os
import re
import sys

DEFAULT = "/tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/game/uecheck58/ue/Engine/Source/Runtime"

PP = [  # PostProcessSettings fields (both the value and its override_ flag)
    "auto_exposure_method", "auto_exposure_min_brightness", "auto_exposure_max_brightness", "auto_exposure_bias", "bloom_intensity",
    "bloom_threshold", "motion_blur_amount", "lens_flare_intensity", "vignette_intensity", "ambient_occlusion_intensity",
    "color_saturation", "color_contrast", "scene_color_tint"]

CHECKS = {
    "directional light": (["Engine/Classes/Components/DirectionalLightComponent.h", "Engine/Classes/Components/LightComponent.h",
                           "Engine/Classes/Components/LightComponentBase.h"],
                          ["dynamic_shadow_distance_stationary_light", "dynamic_shadow_distance_movable_light", "dynamic_shadow_cascades",
                           "cascade_distribution_exponent", "cascade_transition_fraction", "light_source_angle", "atmosphere_sun_light",
                           "forward_shading_priority", "use_inset_shadows_for_movable_objects", "intensity", "light_color", "cast_shadows"]),
    "sky light": (["Engine/Classes/Components/SkyLightComponent.h", "Engine/Classes/Components/LightComponentBase.h"],
                  ["source_type", "intensity", "real_time_capture", "lower_hemisphere_is_black", "lower_hemisphere_color", "cubemap_resolution"]),
    "height fog": (["Engine/Classes/Components/ExponentialHeightFogComponent.h"],
                   ["fog_density", "fog_height_falloff", "start_distance", "fog_max_opacity", "fog_inscattering_luminance",
                    "enable_volumetric_fog", "directional_inscattering_luminance", "directional_inscattering_exponent"]),
    "point light": (["Engine/Classes/Components/PointLightComponent.h", "Engine/Classes/Components/LocalLightComponent.h",
                     "Engine/Classes/Components/LightComponentBase.h", "Engine/Classes/Components/LightComponent.h"],
                    ["intensity_units", "intensity", "light_color", "attenuation_radius", "cast_shadows", "use_inverse_squared_falloff", "source_radius"]),
    "reflection capture": (["Engine/Classes/Components/SphereReflectionCaptureComponent.h", "Engine/Classes/Components/ReflectionCaptureComponent.h"],
                           ["influence_radius"]),
    "post process volume": (["Engine/Classes/Engine/PostProcessVolume.h", "Engine/Classes/Engine/Scene.h"],
                            ["unbound", "settings"] + PP + ["override_" + p for p in PP]),
    "static mesh component": (["Engine/Classes/Components/PrimitiveComponent.h", "Engine/Classes/Components/MeshComponent.h",
                               "Engine/Classes/Components/StaticMeshComponent.h"],
                              ["cast_shadow", "cast_dynamic_shadow", "affect_distance_field_lighting", "affect_dynamic_indirect_lighting"]),
    "static mesh": (["Engine/Classes/Engine/StaticMesh.h"], ["light_map_resolution", "light_map_coordinate_index", "static_materials"]),
    "texture": (["Engine/Classes/Engine/Texture.h", "Engine/Classes/Engine/Texture2D.h"],
                ["srgb", "compression_settings", "flip_green_channel", "lod_group", "max_texture_size", "address_x", "address_y",
                 "mip_gen_settings", "compression_no_alpha"]),
    "material": (["Engine/Public/Materials/Material.h"],
                 ["blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value", "is_sky", "translucency_lighting_mode"]),
    "world settings": (["Engine/Classes/GameFramework/WorldSettings.h"],
                       ["num_indirect_lighting_bounces", "use_ambient_occlusion", "max_occlusion_distance", "fully_occluded_samples_fraction",
                        "direct_illumination_occlusion_fraction", "indirect_illumination_occlusion_fraction", "occlusion_exponent",
                        "static_lighting_level_scale", "lightmass_settings"]),
    "sound wave": (["Engine/Classes/Sound/SoundWave.h", "Engine/Classes/Sound/SoundBase.h"],
                   ["looping", "sound_group", "loading_behavior", "compression_quality", "sound_class_object"]),
    "sound class": (["Engine/Classes/Sound/SoundClass.h"], ["child_classes", "properties"]),
    "material expressions": (["Engine/Public/Materials/MaterialExpressionCustom.h"],
                             ["code", "output_type", "description", "inputs", "additional_outputs", "include_file_paths"]),
}


def snake(name):
    if re.match(r"^b[A-Z]", name):
        name = name[1:]
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.replace("__", "_").lower()


def idents(path):
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    text = re.sub(r"//.*", "", text)
    return {snake(m) for m in re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", text)}


def main(root):
    missing_total = 0
    for group, (files, names) in CHECKS.items():
        have = set()
        absent_files = []
        for f in files:
            ids = idents(os.path.join(root, f))
            if ids is None:
                absent_files.append(f)
            else:
                have |= ids
        miss = [n for n in names if n not in have]
        status = "OK" if not miss else "MISSING " + ", ".join(miss)
        extra = f" (header not in mirror: {', '.join(absent_files)})" if absent_files else ""
        print(f"{group:22s} {status}{extra}")
        missing_total += len(miss)
    return 1 if missing_total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT))
