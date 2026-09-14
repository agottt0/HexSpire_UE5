# Copyright Hex Spire. All Rights Reserved.
#
# 只读探针：查 /Game/ArtResource/UI 下头像与字样贴图的实际尺寸与压缩设置。
#
# 用法（脚本路径必须【绝对】—— 相对路径会被解析到引擎 Binaries 目录）：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:/UE_Proj/HexSpire/Tools/probe_ui_textures.py"
#
# 为什么要先探：控件尺寸必须按贴图【原始比例】定。
# 比例填错的表现是贴图被拉伸变形，而美术会以为是自己导错了图。
#
# ⚠️ 不用 AssetRegistry 扫目录。commandlet 启动时注册表的目录扫描
#    可能还没覆盖到这个新目录，get_assets_by_path 会返回空 ——
#    表现是"脚本执行成功但什么都没打"，看不出是漏扫还是真没资产。
#    直接按路径 load_asset 则确定性地成功或报错。

import unreal

UI_DIR = "/Game/ArtResource/UI"

# 四个角色 × 头像/字样。注意 Revenant 的两张【拼写不一致】：
#   UI_Revanat_头像 与 UI_Revenat_字样
NAMES = [
    "UI_Warden_头像", "UI_Warden_字样",
    "UI_Medium_头像", "UI_Medium_字样",
    "UI_Scrivener_头像", "UI_Scrivener_字样",
    "UI_Revanat_头像", "UI_Revenat_字样",
]


def log(msg):
    unreal.log("[HexProbe] {}".format(msg))


def main():
    log("=" * 72)
    log("UI 贴图尺寸探测")
    log("=" * 72)

    for name in NAMES:
        path = "{}/{}".format(UI_DIR, name)

        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            log("{:<24} 【不存在】{}".format(name, path))
            continue

        tex = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            log("{:<24} 不是 Texture2D，实际是 {}".format(
                name, type(tex).__name__))
            continue

        w = tex.blueprint_get_size_x()
        h = tex.blueprint_get_size_y()
        ratio = (float(w) / float(h)) if h else 0.0

        log("{:<24} {:>5} x {:<5} 宽高比 {:.3f}  压缩={}  LOD组={}  sRGB={}"
            .format(name, int(w), int(h), ratio,
                    str(tex.compression_settings).split(".")[-1],
                    str(tex.lod_group).split(".")[-1],
                    tex.srgb))

    log("")
    log("探测完成")


main()
