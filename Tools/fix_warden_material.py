# Copyright Hex Spire. All Rights Reserved.
#
# 修复「角色很黑」。
#
# ══════════════════════════════════════════════════════════════════
# 诊断结论（由 probe_warden_material.py / probe_m_warden_graph.py 实测）
# ══════════════════════════════════════════════════════════════════
# 这【不是】光照问题。三盏灯（主光 6.0 / 天空光 2.2 / 补光 1.8）都正常，
# 棋盘在同样的光下亮度合理 —— 只有角色是黑的。
#
# 真正的原因是【材质槽指错了】：
#
#   骨骼网格体的槽 [0] 指向 Material_001
#     └ 父材质 = /InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial
#        · Roughness = 0.0   ← 完全光滑
#        · Specular  = 0.0   ← 无高光
#        · BaseColor 标量 = 0.0
#     这是 FBX 导入时 Interchange 自动生成的「legacy Phong」实例。
#     Roughness=0 + Specular=0 的组合在 PBR 下的表现就是
#     「只反射环境、自身不漫反射」→ 在没有反射探针的场景里 = 纯黑。
#
#   而工程里【已经有一份接对了的手工材质】M_Warden，五个输入全接好：
#     BaseColor -> Material_001_Diffuse     Metallic  -> texture_0_metallic
#     Roughness -> texture_0_roughness      Normal    -> Material_001_Normal
#     Emissive  -> texture_0_emission
#   它只是从来没被指派给网格体，白白躺在目录里。
#
# 所以修法是【把槽位改指向 M_Warden】，而不是去调灯。
#
# ⚠️ 不要用"把灯调亮"来治这个症状：
#    Roughness=0 的材质无论加多少光都不会变亮（它不做漫反射），
#    只会把棋盘和 UI 一起冲白 —— 这正是 make_demo_level.py 里
#    记录过的那轮"全白/全黑"反复调参的成因。
#
# ══════════════════════════════════════════════════════════════════
# 顺带修的第二件事：数据贴图的 sRGB
# ══════════════════════════════════════════════════════════════════
# texture_0_metallic / texture_0_roughness 被导入成 TC_Default + sRGB=True。
# 它们是【数据】贴图（金属度、粗糙度），不是颜色贴图。
# 带 sRGB 会让采样值先过一遍 gamma 解码，粗糙度整体偏移 ——
# 实测 roughness 贴图均值 145/255，带 sRGB 解码后约 0.28，
# 不带则约 0.57。差一倍，直接决定角色是"塑料感"还是"布料感"。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript
#       -script="E:\UE_Proj\HexSpire\Tools\fix_warden_material.py"
#
# 幂等：重复运行安全（已是目标状态时跳过并说明）。

import unreal

SK_PATH = "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK"
MAT_DIR = "/Game/ArtResource/Character/Player/Warden/Material"
TARGET_MAT = MAT_DIR + "/M_Warden"

# 需要改成数据贴图的那两张（金属度 / 粗糙度）
DATA_TEXTURES = ("texture_0_metallic", "texture_0_roughness")


def log(msg):
    unreal.log("[HexFix] {}".format(msg))


def warn(msg):
    unreal.log_warning("[HexFix] {}".format(msg))


# ════════════════════════════════════════════ ① 材质槽改指向 M_Warden

def fix_mesh_material():
    mesh = unreal.load_asset(SK_PATH)
    if mesh is None:
        unreal.log_error("[HexFix] 加载不到骨骼网格体：{}".format(SK_PATH))
        return False

    target = unreal.load_asset(TARGET_MAT)
    if target is None:
        unreal.log_error("[HexFix] 加载不到目标材质：{}".format(TARGET_MAT))
        return False

    mats = mesh.get_editor_property("materials")
    if len(mats) == 0:
        unreal.log_error("[HexFix] 网格体没有材质槽")
        return False

    current = mats[0].get_editor_property("material_interface")
    cur_path = current.get_path_name() if current else "None"

    if current is not None and current.get_path_name().startswith(TARGET_MAT):
        log("材质槽已指向 M_Warden，跳过")
        return False

    log("材质槽 [0]：{}".format(cur_path))
    log("        -> {}".format(target.get_path_name()))

    # ⚠️ 必须【整个数组回写】。
    #    直接改 mats[0] 只动了取出来的副本，
    #    不 set_editor_property 回去的话资产纹丝不动 —— 且不报错。
    new_mats = []
    for i, m in enumerate(mats):
        # 保留原槽名：动画/LOD 的 LODMaterialMap 按槽名对应，改名会错位
        slot = m.get_editor_property("material_slot_name")
        entry = unreal.SkeletalMaterial()
        entry.set_editor_property(
            "material_interface", target if i == 0 else
            m.get_editor_property("material_interface"))
        entry.set_editor_property("material_slot_name", slot)
        new_mats.append(entry)

    mesh.set_editor_property("materials", new_mats)

    # ⚠️ 必须检查 save 的返回值。
    #    第一版没检查，于是脚本打印"已保存"而实际【一个字节都没写】——
    #    因为编辑器开着时会锁住 .uasset，UE 重试 10 次后报
    #    Error Code 32（MoveFile 失败）并让 save 返回 False。
    #    修改在内存里是成功的，所以任何"改完再读一次"的自检也会通过，
    #    只有下次重启编辑器才会发现改动没了。这类假成功比直接报错更难查。
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
        unreal.log_error(
            "[HexFix] 骨骼网格体保存失败 —— "
            "几乎总是因为 Unreal Editor 正开着锁住了该文件。"
            "请关闭编辑器后重跑本脚本。")
        return False

    log("骨骼网格体已保存")
    return True


# ════════════════════════════════════════════ ② 数据贴图去掉 sRGB

def fix_data_textures():
    changed = 0

    for name in DATA_TEXTURES:
        path = "{}/{}".format(MAT_DIR, name)
        tex = unreal.load_asset(path)
        if tex is None:
            warn("加载不到贴图：{}".format(path))
            continue

        srgb = tex.get_editor_property("srgb")
        comp = tex.get_editor_property("compression_settings")

        want_comp = unreal.TextureCompressionSettings.TC_MASKS
        if not srgb and comp == want_comp:
            log("{} 已是数据贴图设置，跳过".format(name))
            continue

        log("{}：sRGB {} -> False，压缩 {} -> TC_Masks".format(name, srgb, comp))

        # sRGB=False：数据贴图不做 gamma 解码
        tex.set_editor_property("srgb", False)
        # TC_Masks：非颜色数据的标准压缩，各通道独立且不做 sRGB
        tex.set_editor_property("compression_settings", want_comp)

        unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
        changed += 1

    return changed


# ════════════════════════════════════════════ ③ 补一盏反射探针

def add_reflection_capture():
    """
    给关卡加一个 SphereReflectionCapture。

    ⚠️ 为什么需要它：
       M_Warden 的 Metallic 是【贴图驱动】的，金属部分（护甲扣件之类）
       在 PBR 下【只能靠反射环境成像】—— 没有反射源时金属永远是黑的。
       天空光的 real_time_capture 能提供一部分，但棋盘这种
       近距离环境（地面颜色）只有反射探针能捕到。

       这也是为什么单纯"调亮灯"治不了金属部分的黑。
    """
    MAP_PATH = "/Game/HexSpire/Maps/HexDemo"

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP_PATH)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # 与 make_demo_level.py 保持一致的棋盘中心
    BOARD_COLS, BOARD_ROWS = 7, 7
    TILE_WIDTH, TILE_HEIGHT = 200.0, 230.94
    cx = (BOARD_COLS - 1) * TILE_WIDTH * 0.5
    cy = (BOARD_ROWS - 1) * TILE_HEIGHT * 0.75 * 0.5

    label = "HexDemo_ReflectionCapture"

    for a in eas.get_all_level_actors():
        if a and a.get_actor_label() == label:
            log("反射探针已存在，跳过")
            return False

    cap = eas.spawn_actor_from_class(
        unreal.SphereReflectionCapture,
        unreal.Vector(cx, cy, 400.0))
    if cap is None:
        warn("反射探针生成失败")
        return False

    cap.set_actor_label(label)
    comp = cap.get_component_by_class(unreal.SphereReflectionCaptureComponent)
    if comp:
        # 半径要盖住整个棋盘（7 格 × 200 ≈ 1400，留余量）
        comp.set_editor_property("influence_radius", 2000.0)
    log("反射探针已添加（半径 2000）")

    if les.save_current_level():
        log("关卡已保存")
    return True


def main():
    log("")
    log("=" * 64)
    log("修复 Warden 材质（角色发黑）")
    log("=" * 64)

    a = fix_mesh_material()
    b = fix_data_textures()
    c = add_reflection_capture()

    log("")
    log("=" * 64)
    log("完成：材质槽改动={} 贴图改动={} 反射探针={}".format(a, b, c))
    log("")
    log("验证方式：")
    log("  UnrealEditor-Cmd.exe <uproject> -run=pythonscript \\")
    log("      -script=\"Tools/probe_warden_material.py\"")
    log("  槽 [0] 应显示 -> M_Warden")
    log("=" * 64)


main()
