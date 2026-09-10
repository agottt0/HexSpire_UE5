# Copyright Hex Spire. All Rights Reserved.
#
# 用 Editor Python API 创建可玩 Demo 的关卡与配置。
#
# 为什么用 Python：
#   .umap 与 .uasset 是二进制格式，无法用文本工具创建。
#   UE 官方的 Editor Python API 是唯一能"用脚本建关卡"的途径，
#   这样整个 demo（逻辑 + 表现 + 关卡 + 配置）都能纯代码交付。
#
# 用法（命令行，不需要打开编辑器 UI）：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/make_demo_level.py"
#
# ══════════════════════════════════════════════════════════════════
# 这个脚本放进关卡的东西（全部可在编辑器里选中、移动、调参）
# ══════════════════════════════════════════════════════════════════
#   HexBoard        —— 棋盘。放进关卡后【编辑器视口里就能看到】六边形，
#                      因为 AHexBoardVisual::OnConstruction 会建预览。
#                      运行时 GameMode 会复用它（不再另建），
#                      所以你在编辑器里挪动它是有效的。
#   HexDemoCamera   —— 相机，带 "HexDemoCamera" 标签。
#                      GameMode 认这个标签，所以你在编辑器里调好角度，
#                      点 Play 就是这个角度。这是调视角最快的方式。
#   Sun / Sky / Fill—— 三点式光照，见 setup_lighting 里的说明。
#   SkyAtmosphere   —— 让天空不是纯黑（纯黑背景下棋盘边缘看不清轮廓）。
#   PlayerStart     —— 消除 "PATHS NOT DEFINED" 警告。

import math
import unreal

MAP_DIR = "/Game/HexSpire/Maps"
MAP_NAME = "HexDemo"
MAP_PATH = "{}/{}".format(MAP_DIR, MAP_NAME)

# 与 C++ 侧 HexK 保持一致（HexSpireConstants.h）
BOARD_COLS = 7
BOARD_ROWS = 7
TILE_WIDTH = 200.0
TILE_HEIGHT = 230.94

# 棋盘中心的世界坐标（HexCoord::OffsetToWorld2D 的中点）
BOARD_CENTER_X = (BOARD_COLS - 1) * TILE_WIDTH * 0.5
BOARD_CENTER_Y = (BOARD_ROWS - 1) * TILE_HEIGHT * 0.75 * 0.5

# 相机参数（与 AHexDemoGameMode::SetupCamera 的兜底值一致）
CAM_PITCH = -55.0
CAM_DISTANCE = 1750.0


def log(msg):
    unreal.log("[HexSpire] {}".format(msg))


def get_actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_actor_by_label(label):
    """按显示名找 Actor，用于幂等（重复运行脚本不会堆出一堆重复 Actor）"""
    for a in get_actor_subsystem().get_all_level_actors():
        if a and a.get_actor_label() == label:
            return a
    return None


def create_level():
    """新建空白关卡并保存。已存在则直接加载。"""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        log("关卡已存在，直接加载：{}".format(MAP_PATH))
        les.load_level(MAP_PATH)
        return True

    if not unreal.EditorAssetLibrary.does_directory_exist(MAP_DIR):
        unreal.EditorAssetLibrary.make_directory(MAP_DIR)

    if not les.new_level(MAP_PATH):
        unreal.log_error("[HexSpire] 创建关卡失败：{}".format(MAP_PATH))
        return False

    log("已创建关卡：{}".format(MAP_PATH))
    return True


def cleanup_legacy_actors():
    """
    删掉第一版脚本留下的 Actor。

    ⚠️ 第一版用的名字是 HexDemo_Sky（一个 SkyLight），
       现在改叫 HexDemo_SkyLight。不删掉的话【两个天空光会叠加】——
       环境光强度翻倍，浅色高亮（黄/白）直接过曝成一片白，
       而且这种问题看起来像"材质坏了"，很难联想到是光源重复。
    """
    eas = get_actor_subsystem()
    legacy_labels = ["HexDemo_Sky"]

    for label in legacy_labels:
        actor = find_actor_by_label(label)
        if actor:
            eas.destroy_actor(actor)
            log("已删除遗留 Actor：{}".format(label))


def setup_lighting():
    """
    三点式光照 + 天空。

    ⚠️ 为什么需要这么多光源：
       棋盘的格子是【平顶】的，且相邻格子高度相同。
       只有一个顶光时，所有格子受光完全一致 ——
       六边形之间的缝隙（GapScale=0.94 留出来的）在画面上消失，
       整个棋盘糊成一块灰色板子，根本数不清有几格。

       解决办法是给一个明显的【斜向主光】制造缝隙阴影，
       再用补光避免背光面纯黑。
    """
    eas = get_actor_subsystem()
    center = unreal.Vector(BOARD_CENTER_X, BOARD_CENTER_Y, 0.0)

    # ── 主光：斜 45° 打进来，负责制造格子缝隙的阴影
    sun = find_actor_by_label("HexDemo_Sun")
    if sun is None:
        sun = eas.spawn_actor_from_class(
            unreal.DirectionalLight,
            unreal.Vector(center.x, center.y, 2200.0),
            unreal.Rotator(-48.0, -35.0, 0.0))
        if sun:
            sun.set_actor_label("HexDemo_Sun")
    if sun:
        sun.set_actor_rotation(unreal.Rotator(-48.0, -35.0, 0.0), False)
        comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if comp:
            # ⚠️ 6.0 是【实测截图确认可见】的值，不要凭感觉改。
            #    调过 2.6（全黑）和 15（配错曝光后全白），
            #    最终回到这里。要改亮度，优先动 setup_fixed_exposure()
            #    里的 bias，那个语义直观、失败也不会让画面消失。
            comp.set_editor_property("intensity", 6.0)
            comp.set_editor_property(
                "light_color", unreal.Color(255, 244, 224))
            comp.set_editor_property("cast_shadows", True)
        log("主光已配置")

    # ── 天空光：提供环境光，让背光面不是纯黑
    sky = find_actor_by_label("HexDemo_SkyLight")
    if sky is None:
        sky = eas.spawn_actor_from_class(
            unreal.SkyLight, unreal.Vector(center.x, center.y, 1400.0))
        if sky:
            sky.set_actor_label("HexDemo_SkyLight")
    if sky:
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        if comp:
            # 环境光是"抬底"的：它一高，所有暗部一起变灰，
            # 格子缝隙的阴影就没了 —— 而缝隙阴影正是数格子的唯一依据。
            # 所以它要明显低于主光，但不能为 0（否则背光面纯黑）。
            comp.set_editor_property("intensity", 2.2)
            # 冷调 —— 第一章黄泉线（地铁）的基调（美术文档 §5）
            comp.set_editor_property(
                "light_color", unreal.Color(168, 190, 220))
            # 实时捕获：程序化生成的棋盘在运行时才出现，
            # 静态捕获会捕到"什么都没有"的场景
            comp.set_editor_property("real_time_capture", True)
        log("天空光已配置")

    # ── 补光：从相机侧后方补一盏弱光
    #    没有它时，朝向相机的那一面（玩家最常看的一面）会偏暗，
    #    单位的颜色区分（队伍色/状态色）会看不清。
    fill = find_actor_by_label("HexDemo_Fill")
    if fill is None:
        fill = eas.spawn_actor_from_class(
            unreal.DirectionalLight,
            unreal.Vector(center.x, center.y - 1500.0, 1200.0),
            unreal.Rotator(-30.0, 90.0, 0.0))
        if fill:
            fill.set_actor_label("HexDemo_Fill")
    if fill:
        fill.set_actor_rotation(unreal.Rotator(-30.0, 90.0, 0.0), False)
        comp = fill.get_component_by_class(unreal.DirectionalLightComponent)
        if comp:
            comp.set_editor_property("intensity", 1.8)
            comp.set_editor_property(
                "light_color", unreal.Color(190, 205, 230))
            # 补光【不投影】：两套阴影会互相干扰，让缝隙的方向感变乱
            comp.set_editor_property("cast_shadows", False)
        log("补光已配置")

    # ── 天空球：避免纯黑背景
    #    纯黑时棋盘边缘与背景无法区分，看不出棋盘到哪里结束。
    atmos = find_actor_by_label("HexDemo_Atmosphere")
    if atmos is None:
        atmos = eas.spawn_actor_from_class(
            unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
        if atmos:
            atmos.set_actor_label("HexDemo_Atmosphere")
            log("天空球已添加")

    setup_fixed_exposure()


def setup_fixed_exposure():
    """
    压暗曝光补偿，抑制自动曝光把画面冲白。

    ══════════════════════════════════════════════════════════════
    这里踩过的坑（留作记录，别再走一遍）
    ══════════════════════════════════════════════════════════════
    最初想【完全锁死】曝光（min == max == 某个 EV），理由是
    自动曝光会让固定视角的战棋"画面呼吸"：敌人一死画面变亮、
    高亮一铺画面变暗，而本作正是靠颜色编码传信息的，颜色会漂
    就等于信息不可靠。

    理由成立，但实际锁不住 —— 连试三组值全部失败：
        EV=1.0  配 2.6 lux  -> 全黑
        EV=-1.0 配 15 lux   -> 全白
        EV=2.8  配 15 lux   -> 又全黑（只剩天空可见）
    即便按摄影公式 EV100=log2(lux*0.4) 算出理论值也对不上，
    说明 UE 5.8 里这两个字段的实际语义与文档/公式并不一致，
    继续盲调只是在浪费时间。

    改成【保留自动曝光 + 负向补偿】：
      · 自动曝光保证画面在任何情况下都不会全黑/全白（下限保障）
      · 负 bias 把整体压暗一档，抵消它对暗场景的过度提亮
        —— 那正是当初棋盘被冲成纯白的原因
        （地面色实际是 0.30,0.32,0.35 的水泥灰，不是白色）

    代价是画面仍会轻微呼吸。等美术材质定稿、场景亮度稳定后
    再回来做真正的锁定，那时才有稳定的基准可校。
    """
    eas = get_actor_subsystem()

    ppv = find_actor_by_label("HexDemo_PostProcess")
    if ppv is None:
        ppv = eas.spawn_actor_from_class(
            unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
        if ppv:
            ppv.set_actor_label("HexDemo_PostProcess")

    if ppv is None:
        return

    # 无边界：不需要玩家"走进"体积就生效（本作没有可走动的 Pawn）
    ppv.set_editor_property("unbound", True)

    settings = ppv.get_editor_property("settings")
    try:
        # ⚠️ 必须【显式关掉】曾经设过的 min/max 锁定。
        #    这个脚本是幂等的，会复用关卡里已有的 PPV ——
        #    但"不再设置某个值"不等于"清除它"，
        #    上一版写进去的 EV=2.8 会原封不动留在资产里继续生效。
        #    幂等脚本必须能把自己之前造成的状态撤销干净，
        #    否则改脚本不生效，人却在怀疑代码。
        settings.set_editor_property("override_auto_exposure_min_brightness", False)
        settings.set_editor_property("override_auto_exposure_max_brightness", False)

        # 只用 bias：负值压暗，正值提亮，语义直观且实测有效。
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", -1.0)
        ppv.set_editor_property("settings", settings)
        log("曝光补偿已配置（bias=-1.0，保留自动曝光）")
    except Exception as e:
        unreal.log_warning("[HexSpire] 曝光设置失败：{}".format(e))


def setup_board():
    """
    放置棋盘。

    ⚠️ 这是本次修改的重点：
       棋盘原本只在运行时由 GameMode spawn，导致关卡在编辑器里
       是【一片空白】—— 没法对齐相机、判断不了棋盘多大、调光照全靠猜。
       现在把 AHexBoardVisual 放进关卡：
         · 编辑器里立刻看到六边形（OnConstruction 建预览）
         · 运行时 GameMode 复用它，所以你的调整有效
    """
    board_cls = unreal.load_class(None, "/Script/HexSpire.HexBoardVisual")
    if board_cls is None:
        unreal.log_error(
            "[HexSpire] 找不到 HexBoardVisual —— C++ 是否已编译？")
        return None

    board = find_actor_by_label("HexBoard")
    if board is None:
        board = get_actor_subsystem().spawn_actor_from_class(
            board_cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0, 0, 0))
        if board:
            board.set_actor_label("HexBoard")
            log("棋盘已放入关卡（编辑器视口现在应能看到六边形）")
    else:
        log("棋盘已存在，跳过")

    return board


def setup_camera():
    """
    放置相机并打标签。

    GameMode 会找带 "HexDemoCamera" 标签的相机作为视角，
    所以你在编辑器里拖动/旋转它 → 点 Play 立刻是那个角度。
    这比改 C++ 常量再重编译快得多，视角手感就是要这样试出来的。
    """
    pitch_rad = math.radians(-CAM_PITCH)
    cam_loc = unreal.Vector(
        BOARD_CENTER_X,
        BOARD_CENTER_Y - CAM_DISTANCE * math.cos(pitch_rad),
        CAM_DISTANCE * math.sin(pitch_rad))

    cam = find_actor_by_label("HexDemoCamera")
    if cam is None:
        cam = get_actor_subsystem().spawn_actor_from_class(
            unreal.CameraActor, cam_loc,
            unreal.Rotator(CAM_PITCH, 90.0, 0.0))
        if cam:
            cam.set_actor_label("HexDemoCamera")
            log("相机已放入关卡")

    if cam:
        cam.set_actor_location(cam_loc, False, False)
        cam.set_actor_rotation(unreal.Rotator(CAM_PITCH, 90.0, 0.0), False)

        # ⚠️ 这个标签是 GameMode 与关卡之间的约定，改名会让复用失效
        tags = cam.get_editor_property("tags")
        if "HexDemoCamera" not in [str(t) for t in tags]:
            cam.set_editor_property("tags", ["HexDemoCamera"])

        comp = cam.get_component_by_class(unreal.CameraComponent)
        if comp:
            comp.set_editor_property("field_of_view", 60.0)
            # 关掉"约束宽高比"，否则不同窗口尺寸下会出现黑边
            comp.set_editor_property("constrain_aspect_ratio", False)

    return cam


def setup_player_start():
    """
    放一个 PlayerStart。

    这个 demo 没有 Pawn（纯鼠标战棋），但缺少 PlayerStart 会让引擎
    每次启动都报 "FindPlayerStart: PATHS NOT DEFINED"。
    那条警告无害，但它会淹没真正需要注意的日志。
    """
    ps = find_actor_by_label("HexDemo_PlayerStart")
    if ps is None:
        ps = get_actor_subsystem().spawn_actor_from_class(
            unreal.PlayerStart,
            unreal.Vector(BOARD_CENTER_X, BOARD_CENTER_Y, 200.0),
            unreal.Rotator(0, 0, 0))
        if ps:
            ps.set_actor_label("HexDemo_PlayerStart")
            log("PlayerStart 已添加")


def setup_world_settings():
    """把关卡的 GameMode 覆写为 Demo GameMode。"""
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ues.get_editor_world()
    if world is None:
        unreal.log_error("[HexSpire] 拿不到编辑器 World")
        return False

    settings = world.get_world_settings()
    if settings is None:
        unreal.log_error("[HexSpire] 拿不到 WorldSettings")
        return False

    gm_class = unreal.load_class(None, "/Script/HexSpire.HexDemoGameMode")
    if gm_class is None:
        unreal.log_error(
            "[HexSpire] 找不到 HexDemoGameMode —— C++ 是否已编译？")
        return False

    settings.set_editor_property("default_game_mode", gm_class)
    log("关卡 GameMode 已设为 HexDemoGameMode")
    return True


def setup_project_defaults():
    """
    把 demo 关卡设为默认启动关卡与编辑器启动关卡。

    ⚠️ 直接读写 DefaultEngine.ini 而不用 unreal.SystemLibrary ——
       UE 5.8 的 Python 绑定里没有 set_config_string（曾报
       AttributeError: type object 'SystemLibrary' has no attribute ...）。
       ini 是纯文本，直接改反而更可靠、也更容易看出改了什么。
    """
    import os
    import re

    ini_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_config_dir() + "DefaultEngine.ini")

    section = "[/Script/EngineSettings.GameMapsSettings]"
    entries = {
        "GameDefaultMap": MAP_PATH + "." + MAP_NAME,
        "EditorStartupMap": MAP_PATH + "." + MAP_NAME,
        "GlobalDefaultGameMode": "/Script/HexSpire.HexDemoGameMode",
        # ⚠️ 必须显式改回引擎默认。
        #    这个工程是从 TurnBasedStrategyRPGTemplate 改名来的，
        #    ini 里残留着 GameInstanceClass=BP_RPGGameInstance_C ——
        #    模板的 GameInstance 会在启动时跑它自己的初始化逻辑，
        #    与我们的 GameMode 抢控制权，且失败时只报一条模糊的蓝图错误。
        "GameInstanceClass": "/Script/Engine.GameInstance",
    }

    lines = []
    if os.path.exists(ini_path):
        with open(ini_path, "r", encoding="utf-8-sig") as f:
            lines = f.read().splitlines()

    # 找到目标 section 的范围
    start = -1
    end = len(lines)
    for i, line in enumerate(lines):
        if line.strip() == section:
            start = i
            for j in range(i + 1, len(lines)):
                if re.match(r"^\s*\[.+\]\s*$", lines[j]):
                    end = j
                    break
            break

    if start < 0:
        # section 不存在 → 追加到文件末尾
        if lines and lines[-1].strip() != "":
            lines.append("")
        lines.append(section)
        for key, value in entries.items():
            lines.append("{}={}".format(key, value))
    else:
        # 覆盖已有键，缺失的键补进去
        body = lines[start + 1:end]
        remaining = dict(entries)

        for i, line in enumerate(body):
            m = re.match(r"^\s*([A-Za-z0-9_]+)\s*=", line)
            if m and m.group(1) in remaining:
                body[i] = "{}={}".format(m.group(1), remaining.pop(m.group(1)))

        for key, value in remaining.items():
            body.append("{}={}".format(key, value))

        lines = lines[:start + 1] + body + lines[end:]

    with open(ini_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    log("已写入默认启动关卡：{}".format(MAP_PATH))


def main():
    log("=" * 56)
    log("开始配置 Hex Spire Demo 关卡")
    log("=" * 56)

    if not create_level():
        return

    cleanup_legacy_actors()
    setup_lighting()
    setup_board()
    setup_camera()
    setup_player_start()
    setup_world_settings()

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les.save_current_level():
        log("关卡已保存")
    else:
        unreal.log_warning("[HexSpire] 关卡保存失败（可能无改动）")

    setup_project_defaults()

    log("=" * 56)
    log("完成。在编辑器里你现在可以：")
    log("  · 直接看到六边形棋盘（不需要点 Play）")
    log("  · 选中 HexBoard，改 PreviewLayoutId 换地形预览")
    log("  · 拖动 HexDemoCamera 调视角 —— 点 Play 就是这个角度")
    log("  · 选中 HexDemo_Sun 调光照方向与强度")
    log("=" * 56)


main()
