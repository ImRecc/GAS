import math
import unreal

# ============================================================
# 结构1.jpg 环形立体广场一键白盒生成器 (重型 Boltgun 尺规版)
# 运行方式：在 UE5 底部 Output Log 左侧切到 Python，输入：
#   py "Scripts/build_ring_plaza.py"
# 每次运行会自动清空 RingPlaza_Auto 文件夹并重建，可反复调参！
# ============================================================

FOLDER_NAME = "RingPlaza_Auto"

# --- 核心可调尺规 (单位: cm) ---
PLAZA_SIZE = 5400.0        # 广场地面总宽/总长 (54m x 54m)
TERRACE_RADIUS = 1850.0    # 蓝色外圈弧形高台半径
TERRACE_HEIGHT = 340.0     # 蓝色高台顶面高度 (开场俯瞰制高点)
TERRACE_WIDTH = 650.0      # 蓝色高台宽度
BRIDGE_HEIGHT = 360.0      # 黑色中央立体天桥高度 (比270cm公交车高90cm，单跳直上！)
BRIDGE_WIDTH = 380.0       # 天桥宽 (兼容 1.35x 精英先锋)

# 可选：自动复用你项目里已有的湿冷沥青与远景大楼材质（找不到则自动用默认灰白，不报错）
MAT_ASPHALT_PATH = "/Game/customMaterials/LevelEnv/MI_Overpass_WetAsphalt"
MAT_BUILDING_PATH = "/Game/customMaterials/LevelEnv/M_CyberSkyscraper_Procedural"

editor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cube_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube")
mat_asphalt = unreal.EditorAssetLibrary.load_asset(MAT_ASPHALT_PATH)
mat_building = unreal.EditorAssetLibrary.load_asset(MAT_BUILDING_PATH)


def clear_old_plaza():
    removed = 0
    for actor in editor_sub.get_all_level_actors():
        if str(actor.get_folder_path()) == FOLDER_NAME:
            editor_sub.destroy_actor(actor)
            removed += 1
    unreal.log(f"[RingPlaza] 已清理旧白盒组件: {removed} 个")


def spawn_box(label, loc, size_xyz, rot_pyr=(0.0, 0.0, 0.0), material=None, collision="BlockAll"):
    """按长宽高(cm)生成一个精确碰撞方块，loc 为中心坐标，rot_pyr=(Pitch, Yaw, Roll)"""
    rot = unreal.Rotator(rot_pyr[2], rot_pyr[0], rot_pyr[1])  # UE Python: (Roll, Pitch, Yaw)
    actor = editor_sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), rot)
    actor.set_actor_label(label)
    actor.set_folder_path(FOLDER_NAME)
    comp = actor.static_mesh_component
    comp.set_static_mesh(cube_mesh)
    actor.set_actor_scale3d(unreal.Vector(size_xyz[0] / 100.0, size_xyz[1] / 100.0, size_xyz[2] / 100.0))
    comp.set_collision_profile_name(collision)
    if material:
        comp.set_material(0, material)
    return actor


def spawn_ramp_or_bridge(label, p1, p2, width, thickness=40.0, material=None):
    """在三维点 p1(x,y,z) 到 p2(x,y,z) 之间生成严丝合缝的桥面或斜坡（顶面对齐 z）"""
    dx, dy, dz = p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]
    horiz = math.hypot(dx, dy)
    length = math.hypot(horiz, dz) + 25.0  # 微量重叠消除接缝
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, horiz))
    mid = ((p1[0] + p2[0]) * 0.5, (p1[1] + p2[1]) * 0.5, (p1[2] + p2[2]) * 0.5 - thickness * 0.5)
    return spawn_box(label, mid, (length, width, thickness), (pitch, yaw, 0.0), material=material)


def build_ring_plaza():
    clear_old_plaza()

    # 1. 主广场湿沥青地面 (顶面严格 Z = 0)
    spawn_box("Plaza_Ground", (0, 0, -50), (PLAZA_SIZE, PLAZA_SIZE, 100), material=mat_asphalt)

    # 2. 外圈弧形高台 (对应 结构1.jpg 蓝色区域：4段折弧俯瞰全场)
    arc_angles = [130, 160, 190, 220, 250]
    for i in range(len(arc_angles) - 1):
        a1 = math.radians(arc_angles[i])
        a2 = math.radians(arc_angles[i + 1])
        am = (a1 + a2) * 0.5
        seg_len = 2.0 * TERRACE_RADIUS * math.sin((a2 - a1) * 0.5) + 180.0
        cx = TERRACE_RADIUS * math.cos(am)
        cy = TERRACE_RADIUS * math.sin(am)
        yaw = math.degrees(am) + 90.0
        spawn_box(f"Blue_OuterTerrace_{i+1}", (cx, cy, TERRACE_HEIGHT * 0.5),
                  (seg_len, TERRACE_WIDTH, TERRACE_HEIGHT), (0, yaw, 0))

    # 3. 绿色长楼梯 / 宽斜坡 (连接蓝色高台与广场地面，坡度 ~20.5° 兼容 AI 寻路)
    spawn_ramp_or_bridge("Green_LongStairRamp_South", (-850, -1720, TERRACE_HEIGHT), (120, -1720, 0), width=460)
    spawn_ramp_or_bridge("Green_LongStairRamp_North", (-1180, 1450, TERRACE_HEIGHT), (-350, 1850, 0), width=420)

    # 4. 中央蜿蜒黑色立体天桥 + 黄色桥墩支柱 (对应 结构1.jpg 黑色天桥 + 黄柱)
    bridge_pts = [
        (-750,  450, 0),               # 西北上桥坡脚
        (-350,  350, BRIDGE_HEIGHT),   # 拐点 1
        ( 450,  550, BRIDGE_HEIGHT),   # 拐点 2 (北弧)
        ( 950,   50, BRIDGE_HEIGHT),   # 拐点 3 (东端)
        ( 550, -650, BRIDGE_HEIGHT),   # 拐点 4 (南弧)
        (-250, -550, BRIDGE_HEIGHT),   # 拐点 5
        (-850, -350, 0),               # 西南下桥坡脚
    ]
    for i in range(len(bridge_pts) - 1):
        spawn_ramp_or_bridge(f"Black_Overpass_Seg_{i+1}", bridge_pts[i], bridge_pts[i + 1],
                             width=BRIDGE_WIDTH, thickness=45.0)
    # 黄色桥墩支柱 (立在桥面拐点下方)
    for idx in [1, 2, 3, 4, 5]:
        px, py, pz = bridge_pts[idx]
        spawn_box(f"Yellow_Pillar_{idx}", (px, py, (pz - 45.0) * 0.5), (100, 100, pz - 45.0))

    # 5. 橙色公交车临时高地 + 贴边踏脚隔离墩 (0 -> 100 -> 185 -> 270车顶 -> 360天桥！)
    buses = [
        ("Orange_Bus_1", ( 200, -1100, 135),  15.0),
        ("Orange_Bus_2", (1250,  -400, 135),  80.0),
        ("Orange_Bus_3", ( 150,  1100, 135), -20.0),
    ]
    for name, (bx, by, bz), byaw in buses:
        spawn_box(name, (bx, by, bz), (900, 260, 270), (0, byaw, 0))
        rad = math.radians(byaw)
        # 车头侧 100cm 隔离墩 + 185cm 踏箱，重型单跳(108cm)顺畅登顶
        spawn_box(f"{name}_StepLow", (bx + math.cos(rad) * 560, by + math.sin(rad) * 560, 50), (220, 120, 100), (0, byaw, 0))
        spawn_box(f"{name}_StepMid", (bx + math.cos(rad) * 350 - math.sin(rad) * 200,
                                      by + math.sin(rad) * 350 + math.cos(rad) * 200, 92.5), (200, 140, 185), (0, byaw, 0))

    # 6. 白色外围楼房 + 离地 250cm 可踩跳雨棚 (对应 结构1.jpg 白色布景楼)
    buildings = [
        ("White_Bldg_N1", (-1200,  2200, 1100), (1100, 900, 2200),   0, True,  (0, -560)),
        ("White_Bldg_N2", (  400,  2300,  750), (1200, 900, 1500),  -8, True,  (0, -560)),
        ("White_Bldg_E1", ( 2250,   900,  900), (1000, 1100, 1800), 10, True,  (-620, 0)),
        ("White_Bldg_E2", ( 2350,  -950,  650), (1000, 1100, 1300), -5, False, (0, 0)),
        ("White_Bldg_S1", (  600, -2300, 1000), (1300, 900, 2000),   5, True,  (0, 560)),
        ("White_Bldg_W1", (-2450,  -600, 1300), (1000, 1200, 2600),  0, False, (0, 0)),
    ]
    for name, loc, size, yaw, has_canopy, c_off in buildings:
        spawn_box(name, loc, size, (0, yaw, 0), material=mat_building)
        if has_canopy:
            spawn_box(f"{name}_Canopy", (loc[0] + c_off[0], loc[1] + c_off[1], 250), (550, 280, 35), (0, yaw, 0))

    unreal.log("[RingPlaza] 结构1.jpg 环形立体广场白盒生成完毕！请拖入 NavMeshBoundsVolume 与 PlayerStart 测试。")


build_ring_plaza()