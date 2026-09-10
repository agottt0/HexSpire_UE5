# Copyright Hex Spire. All Rights Reserved.
#
# 只读探针：验证模板的角色/动画资产能否被我们的 C++ 系统直接使用。
#
# ══════════════════════════════════════════════════════════════════
# 为什么必须先跑这个脚本，而不是直接写代码
# ══════════════════════════════════════════════════════════════════
# 动画能否跨模型复用，唯一的判据是【两个 SkeletalMesh 是否共用同一个
# Skeleton 资产】。这件事无法从文件名推断 —— 目录里那个
# "01_Warden_Bined_UE4SK" 的名字【看起来】像是绑到 UE4 骨架了，
# 但命名只是作者的自述，不是引擎的事实。
#
# 如果实际 Skeleton 不同，那就要走 IK Retarget（工作量差一个数量级），
# 整个接入方案要重写。所以先问引擎要答案。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/probe_template_assets.py"

import unreal


def log(msg):
    unreal.log("[HexSpire] {}".format(msg))


def get_asset(path):
    """加载资产，不存在返回 None（不抛异常，方便批量探测）"""
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def probe_skeletons():
    """
    核心问题：Warden 和模板 Mannequin 是不是同一个 Skeleton？
    """
    log("=" * 60)
    log("① 骨架一致性 —— 决定动画能否直接复用")
    log("=" * 60)

    targets = {
        "模板 Mannequin(男)":
            "/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin",
        "模板 Mannequin(女)":
            "/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin_Female",
        "我们的 Warden":
            "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK",
    }

    skeletons = {}
    for name, path in targets.items():
        mesh = get_asset(path)
        if mesh is None:
            log("  {:20s} 资产不存在：{}".format(name, path))
            continue

        skel = mesh.get_editor_property("skeleton")
        skel_path = skel.get_path_name() if skel else "（无）"
        skeletons[name] = skel_path

        bone_count = -1
        try:
            bone_count = len(skel.get_editor_property("bone_tree")) if skel else -1
        except Exception:
            pass

        log("  {:20s} 骨架={}".format(name, skel_path))
        if bone_count >= 0:
            log("  {:20s} 骨骼数={}".format("", bone_count))

    uniq = set(skeletons.values())
    log("")
    if len(uniq) == 1 and len(skeletons) > 1:
        log("  >>> 结论：全部共用同一骨架，动画可直接复用，无需重定向。")
    else:
        log("  >>> 结论：骨架不一致（{} 种），需要 IK Retarget。".format(len(uniq)))
        for name, sp in skeletons.items():
            log("        {} -> {}".format(name, sp))

    return skeletons


def probe_animations():
    """列出可用动画，并确认它们挂在哪个骨架上。"""
    log("")
    log("=" * 60)
    log("② 动画资产 —— 我们真正需要的只有 5 个状态")
    log("=" * 60)

    # 我们的战斗表现只需要这几个状态（回合制不需要复杂混合）
    wanted = {
        "Idle": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_Idle",
        "Walk": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_Walk",
        "Attack": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_SwordsmanCombo1",
        "Cast": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_MageAttack",
        "Shoot": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_ArcherShoot",
        "GetHit": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_GetHit",
        "Die": "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_Die",
    }

    for name, path in sorted(wanted.items()):
        anim = get_asset(path)
        if anim is None:
            log("  {:8s} 缺失：{}".format(name, path))
            continue

        skel = anim.get_editor_property("skeleton")
        length = -1.0
        try:
            length = anim.get_editor_property("sequence_length")
        except Exception:
            try:
                length = anim.get_play_length()
            except Exception:
                pass

        log("  {:8s} 时长={:5.2f}s  骨架={}".format(
            name, length, skel.get_name() if skel else "?"))


def probe_notifies():
    """
    检查动画上的 Notify。

    ⚠️ 这决定了"伤害在动画的哪一帧结算"能否被 C++ 接管。
       模板把结算写在蓝图 Notify 里；我们的伤害由 C++ 逻辑层算，
       所以只需要知道 Notify 的【时间点】，用它来对齐飘字/特效。
    """
    log("")
    log("=" * 60)
    log("③ 攻击动画的 Notify 时间点 —— 用于对齐伤害飘字")
    log("=" * 60)

    paths = [
        "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_SwordsmanCombo1",
        "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_ArcherShoot",
        "/Game/TurnBasedStrategyRPGTemplate/Animations/AS_MageAttack",
    ]

    for path in paths:
        anim = get_asset(path)
        if anim is None:
            continue
        try:
            notifies = anim.get_editor_property("notifies")
            log("  {} —— {} 个 Notify".format(anim.get_name(), len(notifies)))
            for n in notifies:
                t = n.get_editor_property("trigger_time_offset")
                link = n.get_editor_property("link_value")
                log("      t={:.3f}  {}".format(
                    link if link else t, n.get_editor_property("notify_name")))
        except Exception as e:
            log("  {} —— 无法读取 Notify（{}）".format(anim.get_name(), e))


def probe_materials():
    """确认换色材质是否可用（我们靠颜色区分敌我，与模板做法一致）。"""
    log("")
    log("=" * 60)
    log("④ 材质")
    log("=" * 60)

    paths = [
        "/Game/TurnBasedStrategyRPGTemplate/Materials/M_Male_Body",
        "/Game/TurnBasedStrategyRPGTemplate/Materials/M_RedBody",
        "/Game/TurnBasedStrategyRPGTemplate/Materials/M_GreenBody",
        "/Game/ArtResource/Character/Player/Warden/Material/M_Warden",
    ]

    for p in paths:
        mat = get_asset(p)
        log("  {:8s} {}".format("OK" if mat else "缺失", p))


def probe_hex_scale():
    """
    量出 Mannequin 的实际身高，用来判断它跟我们的格子尺寸配不配。

    ⚠️ 这是个容易被忽略但会毁掉画面的问题：
       我们的格子宽 200uu，而 UE4 Mannequin 身高约 180uu。
       如果直接 1:1 放上去，人几乎和格子一样宽，
       棋盘会被角色完全盖住，看不出站位 —— 而站位是这个游戏的核心。
    """
    log("")
    log("=" * 60)
    log("⑤ 尺寸校验 —— 角色会不会盖住棋盘")
    log("=" * 60)

    mesh = get_asset("/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin")
    if mesh is None:
        log("  Mannequin 缺失，跳过")
        return

    try:
        bounds = mesh.get_bounds()
        ext = bounds.box_extent
        log("  Mannequin 包围盒半长: X={:.1f} Y={:.1f} Z={:.1f}".format(
            ext.x, ext.y, ext.z))
        log("  => 高约 {:.0f}uu, 宽约 {:.0f}uu".format(ext.z * 2, ext.x * 2))
        log("  我们的格子宽 200uu / 高 230.9uu")
    except Exception as e:
        log("  无法读取包围盒：{}".format(e))

    warden = get_asset(
        "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK")
    if warden:
        try:
            b = warden.get_bounds()
            e = b.box_extent
            log("  Warden 包围盒半长:     X={:.1f} Y={:.1f} Z={:.1f}".format(
                e.x, e.y, e.z))
            log("  => 高约 {:.0f}uu".format(e.z * 2))
        except Exception as ex:
            log("  Warden 包围盒读取失败：{}".format(ex))


def main():
    log("")
    log("#" * 60)
    log("# 模板角色/动画资产探针（只读）")
    log("#" * 60)

    probe_skeletons()
    probe_animations()
    probe_notifies()
    probe_materials()
    probe_hex_scale()

    log("")
    log("#" * 60)
    log("# 探针结束")
    log("#" * 60)


main()
