"""
UE5 远景黑夜冷雨城市天际线一键生成脚本 (setup_night_skyline.py)

在 UE5 编辑器底部 Output Log 左侧将下拉框切换为 "Python"，粘贴执行下面这一行命令即可：
exec(open(r"d:/UE5/GAS/Scripts/setup_night_skyline.py", encoding="utf-8").read())
"""

import math
import random
import unreal


SKYLINE_FOLDER = "Skyline_Auto"
MAT_PATH = "/Game/customMaterials/LevelEnv/M_CyberSkyscraper_Procedural"
CUBE_PATH = "/Engine/BasicShapes/Cube"

BUILDING_COUNT = 28
INNER_RADIUS = 5000.0
OUTER_RADIUS = 11000.0
RANDOM_SEED = 20261127


def _set_cold_night_lighting(world):
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_sub.get_all_level_actors()

    for actor in actors:
        if isinstance(actor, unreal.DirectionalLight):
            light_comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
            if light_comp:
                light_comp.set_editor_property("intensity", 0.35)
                light_comp.set_editor_property(
                    "light_color", unreal.Color(r=140, g=195, b=255, a=255)
                )
        elif isinstance(actor, unreal.SkyLight):
            sky_comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if sky_comp:
                sky_comp.set_editor_property("intensity", 0.15)
                sky_comp.set_editor_property(
                    "light_color", unreal.Color(r=110, g=155, b=220, a=255)
                )
                sky_comp.recapture_sky()
        elif isinstance(actor, unreal.ExponentialHeightFog):
            fog_comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
            if fog_comp:
                fog_comp.set_editor_property(
                    "fog_inscattering_luminance",
                    unreal.LinearColor(r=0.01, g=0.025, b=0.04, a=1.0),
                )
                fog_comp.set_editor_property("fog_density", 0.025)

    unreal.log(f"[setup_night_skyline] 已校准冷雨夜光照与高度雾 (World: {world.get_name()})")


def _clear_previous_skyline():
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for actor in actor_sub.get_all_level_actors():
        if str(actor.get_folder_path()) == SKYLINE_FOLDER:
            actor_sub.destroy_actor(actor)
            removed += 1
    if removed > 0:
        unreal.log(f"[setup_night_skyline] 已清理旧的 {SKYLINE_FOLDER} 建筑: {removed} 栋")


def _spawn_skyscrapers():
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube_mesh = unreal.EditorAssetLibrary.load_asset(CUBE_PATH)
    sky_mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)

    if not cube_mesh or not sky_mat:
        raise RuntimeError(f"无法加载资产: cube={cube_mesh}, mat={sky_mat}")

    rng = random.Random(RANDOM_SEED)
    base_angle_step = (2.0 * math.pi) / BUILDING_COUNT

    for idx in range(BUILDING_COUNT):
        angle = idx * base_angle_step + rng.uniform(-0.08, 0.08)
        radius = rng.uniform(INNER_RADIUS, OUTER_RADIUS)
        x = math.cos(angle) * radius
        y = math.sin(angle) * radius

        width_x = rng.uniform(1100.0, 2400.0)
        width_y = rng.uniform(1100.0, 2400.0)
        height_z = rng.uniform(4500.0, 14500.0)
        base_z = rng.uniform(-1200.0, -400.0)
        center_z = base_z + height_z * 0.5

        loc = unreal.Vector(x, y, center_z)
        rot = unreal.Rotator(0.0, rng.uniform(0.0, 360.0), 0.0)

        actor = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
        actor.set_actor_label(f"Skyscraper_{idx + 1:02d}")
        actor.set_folder_path(SKYLINE_FOLDER)
        actor.set_actor_scale3d(
            unreal.Vector(width_x / 100.0, width_y / 100.0, height_z / 100.0)
        )

        smc = actor.static_mesh_component
        smc.set_static_mesh(cube_mesh)
        smc.set_material(0, sky_mat)
        smc.set_collision_profile_name("NoCollision")
        smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        smc.set_cast_shadow(False)

    unreal.log(f"[setup_night_skyline] 已生成 {BUILDING_COUNT} 栋远景摩天楼至 {SKYLINE_FOLDER}")


def run():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world:
        raise RuntimeError("未检测到当前打开的编辑器关卡！")

    with unreal.ScopedEditorTransaction("Setup Night Skyline"):
        _set_cold_night_lighting(world)
        _clear_previous_skyline()
        _spawn_skyscrapers()


run()
