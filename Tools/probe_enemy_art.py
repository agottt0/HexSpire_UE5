# Copyright Hex Spire. All Rights Reserved.
#
# 探查 Content/ArtResource/Character/Enemy/ 里的资产到底是什么类型、
# 骨架是哪一套 —— 决定它们能不能配进 DA_UnitVisual_*。
#
# ⚠️ 为什么必须先探查而不是直接按文件名猜：
#    动画与模型必须共用【同一套骨架】。骨架不一致时资产能加载成功，
#    但播放时姿态错乱（手脚扭曲）——不报任何错。
#    模板动画基于 S_Mannequin（108 骨骼），
#    Siren 那批若是自带骨架，就不能和模板动画混用。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\probe_enemy_art.py"

import unreal

DIRS = [
    "/Game/ArtResource/Character/Enemy",
    "/Game/ArtResource/Character/Player/Warden",
    "/Game/TurnBasedStrategyRPGTemplate/Meshes",
]


_LINES = []


def log(msg):
    # ⚠️ 同时走 unreal.log 与 print，并且落盘。
    #    -run=pythonscript 下 unreal.log 不一定出现在 stdout
    #    （实测被吞），只靠它会让探查"成功执行但没有结果"。
    unreal.log("[HexProbe] {}".format(msg))
    print("[HexProbe] {}".format(msg))
    _LINES.append(str(msg))


def flush():
    out = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + "Export/probe_enemy_art.txt")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(_LINES) + "\n")
    print("[HexProbe] 结果已写入 {}".format(out))


def skeleton_of(asset):
    """取骨骼网格体/动画的骨架路径，拿不到返回 None"""
    try:
        skel = asset.get_editor_property("skeleton")
        return skel.get_path_name() if skel else None
    except Exception:
        return None


def main():
    log("")
    log("=" * 70)

    reg = unreal.AssetRegistryHelpers.get_asset_registry()

    for d in DIRS:
        log("")
        log("目录 {}".format(d))
        log("-" * 70)

        f = unreal.ARFilter(package_paths=[d], recursive_paths=True)
        found = reg.get_assets(f)

        if not found:
            log("  (空)")
            continue

        for data in found:
            name = str(data.asset_name)
            cls = str(data.asset_class_path.asset_name)

            # 只详查我们关心的三类，其余只报类型
            if cls not in ("SkeletalMesh", "AnimSequence", "Skeleton", "StaticMesh"):
                log("  {:<44} {}".format(name, cls))
                continue

            asset = data.get_asset()
            extra = ""

            if cls in ("SkeletalMesh", "AnimSequence"):
                sk = skeleton_of(asset)
                extra = "  骨架={}".format(sk.split("/")[-1] if sk else "?")

                if cls == "AnimSequence":
                    try:
                        extra += "  时长={:.2f}s".format(
                            asset.get_editor_property("sequence_length"))
                    except Exception:
                        pass

            if cls == "Skeleton":
                try:
                    extra = "  骨骼数={}".format(len(asset.get_editor_property("bone_tree")))
                except Exception:
                    pass

            log("  {:<44} {}{}".format(name, cls, extra))

    log("")
    log("=" * 70)
    log("判读：骨架相同的模型与动画才能混用。")
    log("      骨架不同的那批需要 IK 重定向，或只配自带动画。")
    log("=" * 70)

    flush()


main()
