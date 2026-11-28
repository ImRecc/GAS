"""
UE 5.8 环形广场雨夜无 Lumen 光影骨架一键部署脚本 (setup_rain_night_atmosphere.py)

运行方式（任选其一）：
1. UE5 编辑器底部 Output Log 左侧下拉框切到 "Python"，粘贴执行：
   exec(open(r"D:/UE5/GAS/Scripts/setup_rain_night_atmosphere.py", encoding="utf-8").read())
2. 或下拉框保留 "Cmd"，直接输入：
   py "D:/UE5/GAS/Scripts/setup_rain_night_atmosphere.py"

特性：
- 幂等执行：每次运行自动清理旧的 EnvAtmo_* / Tag=EnvAtmo_Auto 资产，支持反复微调后重跑。
- 非破坏性隔离：自动关闭关卡中原有的模板默认 DirectionalLight / SkyLight / ExponentialHeightFog，防止日光与雾效双重叠加。
- GTX 1660S 友好：关闭 Lumen、关闭 RealTimeCapture、关闭 VolumetricFog、所有霓虹 RectLight 默认不投射阴影。
"""

import unreal


ENV_FOLDER = "EnvAtmo_Auto"
ENV_TAG = "EnvAtmo_Auto"
NEON_BP_PATH = "/Game/bps/levelMap/BP_NeonLightStrip"


def _tag_and_organize(actor, label):
    actor.set_actor_label(label)
    actor.set_folder_path(ENV_FOLDER)
    tags = list(actor.tags)
    if ENV_TAG not in tags:
        tags.append(ENV_TAG)
        actor.tags = tags
    return actor


def _cleanup_previous_env(editor_sub):
    removed = 0
    for actor in list(editor_sub.get_all_level_actors()):
        if not actor:
            continue
        label = actor.get_actor_label()
        folder = str(actor.get_folder_path())
        if actor.actor_has_tag(ENV_TAG) or folder == ENV_FOLDER or label.startswith("EnvAtmo_"):
            editor_sub.destroy_actor(actor)
            removed += 1
    if removed > 0:
        unreal.log(f"[RainNightAtmo] 已清理旧的 EnvAtmo_ Actor: {removed} 个")


def _neutralize_conflicting_template_lights(editor_sub):
    disabled = []
    for actor in editor_sub.get_all_level_actors():
        if not actor or actor.actor_has_tag(ENV_TAG) or actor.get_actor_label().startswith("EnvAtmo_"):
            continue
        if isinstance(actor, unreal.DirectionalLight):
            comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
            if comp and comp.get_editor_property("affects_world"):
                comp.set_editor_property("affects_world", False)
                comp.set_visibility(False)
                disabled.append(actor.get_actor_label())
        elif isinstance(actor, unreal.SkyLight):
            comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if comp and comp.get_editor_property("affects_world"):
                comp.set_editor_property("affects_world", False)
                comp.set_visibility(False)
                disabled.append(actor.get_actor_label())
        elif isinstance(actor, unreal.ExponentialHeightFog):
            comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
            if comp and comp.get_editor_property("visible"):
                comp.set_visibility(False)
                comp.set_hidden_in_game(True)
                disabled.append(actor.get_actor_label())

    if disabled:
        unreal.log(f"[RainNightAtmo] 已停用原有非 EnvAtmo 光照/雾效以避免叠加: {', '.join(disabled)}")


def _spawn_moon_light(editor_sub):
    # UE Python Rotator(roll, pitch, yaw)
    rot = unreal.Rotator(0.0, -38.0, 135.0)
    actor = editor_sub.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1200.0), rot
    )
    _tag_and_organize(actor, "EnvAtmo_MoonLight")
    comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if comp:
        comp.set_editor_property("intensity", 0.45)
        comp.set_editor_property("light_color", unreal.Color(r=148, g=186, b=235, a=255))
        comp.set_editor_property("cast_shadows", True)
        comp.set_editor_property("atmosphere_sun_light", True)
    return actor


def _spawn_sky_light(editor_sub):
    actor = editor_sub.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 900.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
    _tag_and_organize(actor, "EnvAtmo_SkyLight")
    comp = actor.get_component_by_class(unreal.SkyLightComponent)
    if comp:
        comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
        comp.set_editor_property("real_time_capture", False)
        comp.set_editor_property("intensity", 0.2)
        comp.set_editor_property("light_color", unreal.Color(r=88, g=168, b=205, a=255))
        comp.recapture_sky()
    return actor


def _srgb_u8_to_linear_channel(c_u8):
    c = c_u8 / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _spawn_height_fog(editor_sub):
    actor = editor_sub.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
    _tag_and_organize(actor, "EnvAtmo_HeightFog")
    comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if comp:
        # #08111A -> LinearColor
        fog_color = unreal.LinearColor(
            _srgb_u8_to_linear_channel(8),
            _srgb_u8_to_linear_channel(17),
            _srgb_u8_to_linear_channel(26),
            1.0,
        )
        comp.set_editor_property("fog_inscattering_luminance", fog_color)
        comp.set_editor_property("fog_density", 0.02)
        comp.set_editor_property("start_distance", 500.0)
        comp.set_editor_property("enable_volumetric_fog", False)
    return actor


def _spawn_post_process(editor_sub):
    actor = editor_sub.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 300.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
    _tag_and_organize(actor, "EnvAtmo_PostProcess")
    actor.set_editor_property("unbound", True)
    actor.set_editor_property("priority", 10.0)
    actor.set_editor_property("enabled", True)

    settings = actor.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 0.0)
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 0.0)
    settings.set_editor_property("override_bloom_intensity", True)
    settings.set_editor_property("bloom_intensity", 0.7)
    settings.set_editor_property("override_vignette_intensity", True)
    settings.set_editor_property("vignette_intensity", 0.35)
    actor.set_editor_property("settings", settings)
    return actor


def _spawn_reflection_captures(editor_sub):
    captures = [
        ("EnvAtmo_Reflection_Center", unreal.Vector(0.0, 0.0, 260.0), 2200.0),
        ("EnvAtmo_Reflection_WestTerrace", unreal.Vector(-1500.0, 0.0, 420.0), 1800.0),
        ("EnvAtmo_Reflection_EastArena", unreal.Vector(1350.0, 0.0, 320.0), 1800.0),
    ]
    spawned = []
    for label, loc, radius in captures:
        actor = editor_sub.spawn_actor_from_class(
            unreal.SphereReflectionCapture, loc, unreal.Rotator(0.0, 0.0, 0.0)
        )
        _tag_and_organize(actor, label)
        comp = actor.get_component_by_class(unreal.SphereReflectionCaptureComponent)
        if comp:
            comp.set_editor_property("influence_radius", radius)
            comp.set_editor_property("brightness", 1.0)
        spawned.append(actor)
    return spawned


def _spawn_neon_strips(editor_sub):
    neon_cls = unreal.EditorAssetLibrary.load_blueprint_class(NEON_BP_PATH)
    if not neon_cls:
        raise RuntimeError(f"无法加载霓虹灯条蓝图类: {NEON_BP_PATH}")

    cyan = unreal.LinearColor(0.10, 0.85, 1.0, 1.0)
    amber = unreal.LinearColor(1.0, 0.45, 0.08, 1.0)
    magenta = unreal.LinearColor(1.0, 0.12, 0.58, 1.0)

    # 对应 build_ring_plaza.py 尺规：外圈弧形高台(R=1850, H=340) 与中央天桥(H=360) 关键节点
    # 4 处冷青主调 + 2 处暖橙/品红视觉引导点
    strips = [
        {
            "label": "EnvAtmo_NeonStrip_01_TerraceNW",
            "loc": unreal.Vector(-1460.0, 1020.0, 390.0),
            "rot": unreal.Rotator(0.0, 0.0, -35.0),
            "preset": unreal.NeonColorPreset.CYBER_CYAN,
            "color": cyan,
            "emissive": 16.0,
            "light_int": 1600.0,
            "radius": 1100.0,
            "length": 500.0,
            "pitch_offset": -25.0,
        },
        {
            "label": "EnvAtmo_NeonStrip_02_TerraceSW",
            "loc": unreal.Vector(-1460.0, -1020.0, 390.0),
            "rot": unreal.Rotator(0.0, 0.0, 35.0),
            "preset": unreal.NeonColorPreset.CYBER_CYAN,
            "color": cyan,
            "emissive": 16.0,
            "light_int": 1600.0,
            "radius": 1100.0,
            "length": 500.0,
            "pitch_offset": -25.0,
        },
        {
            "label": "EnvAtmo_NeonStrip_03_OverpassNorth",
            "loc": unreal.Vector(450.0, 550.0, 315.0),
            "rot": unreal.Rotator(0.0, 0.0, -90.0),
            "preset": unreal.NeonColorPreset.CYBER_CYAN,
            "color": cyan,
            "emissive": 15.0,
            "light_int": 1500.0,
            "radius": 1000.0,
            "length": 450.0,
            "pitch_offset": -55.0,
        },
        {
            "label": "EnvAtmo_NeonStrip_04_OverpassEast",
            "loc": unreal.Vector(950.0, 50.0, 315.0),
            "rot": unreal.Rotator(0.0, 0.0, 180.0),
            "preset": unreal.NeonColorPreset.CYBER_CYAN,
            "color": cyan,
            "emissive": 15.0,
            "light_int": 1500.0,
            "radius": 1000.0,
            "length": 400.0,
            "pitch_offset": -55.0,
        },
        {
            "label": "EnvAtmo_NeonStrip_05_GuideAmberSouth",
            "loc": unreal.Vector(-365.0, -1720.0, 260.0),
            "rot": unreal.Rotator(0.0, 0.0, 90.0),
            "preset": unreal.NeonColorPreset.NEON_AMBER,
            "color": amber,
            "emissive": 18.0,
            "light_int": 1800.0,
            "radius": 1050.0,
            "length": 360.0,
            "pitch_offset": -35.0,
        },
        {
            "label": "EnvAtmo_NeonStrip_06_GuideMagentaNW",
            "loc": unreal.Vector(-650.0, 500.0, 280.0),
            "rot": unreal.Rotator(0.0, 0.0, -15.0),
            "preset": unreal.NeonColorPreset.HOT_MAGENTA,
            "color": magenta,
            "emissive": 18.0,
            "light_int": 1800.0,
            "radius": 1050.0,
            "length": 360.0,
            "pitch_offset": -35.0,
        },
    ]

    spawned = []
    for cfg in strips:
        actor = editor_sub.spawn_actor_from_class(neon_cls, cfg["loc"], cfg["rot"])
        _tag_and_organize(actor, cfg["label"])
        actor.set_editor_property("color_preset", cfg["preset"])
        actor.set_editor_property("neon_color", cfg["color"])
        actor.set_editor_property("emissive_intensity", cfg["emissive"])
        actor.set_editor_property("light_intensity", cfg["light_int"])
        actor.set_editor_property("light_attenuation_radius", cfg["radius"])
        actor.set_editor_property("strip_length_cm", cfg["length"])
        actor.set_editor_property("cast_light_shadows", False)
        actor.set_editor_property("enable_light", True)
        actor.set_editor_property("light_pitch_offset_deg", cfg["pitch_offset"])
        actor.apply_visual_settings()
        spawned.append(actor)
    return spawned


def run():
    unreal_editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    editor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal_editor_sub.get_editor_world()
    if not world:
        raise RuntimeError("未检测到当前打开的编辑器关卡！")

    with unreal.ScopedEditorTransaction("Setup Rain Night Atmosphere"):
        _cleanup_previous_env(editor_sub)
        _neutralize_conflicting_template_lights(editor_sub)

        _spawn_moon_light(editor_sub)
        _spawn_sky_light(editor_sub)
        _spawn_height_fog(editor_sub)
        _spawn_post_process(editor_sub)
        refls = _spawn_reflection_captures(editor_sub)
        strips = _spawn_neon_strips(editor_sub)

    unreal.log(
        f"[RainNightAtmo] 雨夜无 Lumen 光影骨架已部署至关卡 '{world.get_name()}' "
        f"(Folder: {ENV_FOLDER}, ReflectionCaptures: {len(refls)}, NeonStrips: {len(strips)})"
    )


run()
