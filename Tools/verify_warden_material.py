# Copyright Hex Spire. All Rights Reserved.
#
# 只读校验：M_Warden 是否真的能编译 + 网格体槽位是否指向它。
#
# 为什么单独跑一次而不是在 fix 脚本里自检：
#   fix 脚本改完后材质还在内存里，那份状态一定是"对"的。
#   编译失败是【重新加载资产时】才暴露的问题（DDC 里存的是编译产物）。
#   所以必须换一个全新进程重新 load，才算真的验过。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript
#       -script="E:\UE_Proj\HexSpire\Tools\verify_warden_material.py"

import unreal

MAT = "/Game/ArtResource/Character/Player/Warden/Material/M_Warden"
SK = "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK"

INPUTS = [
    ("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
    ("Metallic", unreal.MaterialProperty.MP_METALLIC),
    ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
    ("EmissiveColor", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
    ("Normal", unreal.MaterialProperty.MP_NORMAL),
]


def log(m):
    unreal.log("[HexVerify] {}".format(m))


def main():
    log("")
    log("=" * 64)
    log("校验 M_Warden")
    log("=" * 64)

    ok = True
    mat = unreal.load_asset(MAT)
    if mat is None:
        unreal.log_error("[HexVerify] 加载不到材质")
        return

    # ① 使用标记
    flag = mat.get_editor_property("used_with_skeletal_mesh")
    log("  bUsedWithSkeletalMesh = {}{}".format(
        flag, "" if flag else "   <<< 仍未勾选，会回退默认材质"))
    ok = ok and flag

    # ② 每个输入的 SamplerType 与贴图压缩是否一致
    log("")
    log("  输入 / 贴图 / 压缩 / SamplerType：")
    for name, prop in INPUTS:
        node = unreal.MaterialEditingLibrary.\
            get_material_property_input_node(mat, prop)
        if node is None:
            log("    {:16s} 未连接".format(name))
            continue
        tex = node.get_editor_property("texture")
        st = node.get_editor_property("sampler_type")
        comp = tex.get_editor_property("compression_settings")
        srgb = tex.get_editor_property("srgb")

        # TC_Masks 必须配 SAMPLERTYPE_MASKS，否则编译报错
        bad = (comp == unreal.TextureCompressionSettings.TC_MASKS and
               st != unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        if bad:
            ok = False
        log("    {:16s} {:22s} {:12s} sRGB={:5s} {}{}".format(
            name, tex.get_name(), comp.name, str(srgb), st.name,
            "   <<< 不匹配" if bad else ""))

    # ③ 网格体槽位
    log("")
    mesh = unreal.load_asset(SK)
    if mesh is None:
        unreal.log_error("[HexVerify] 加载不到骨骼网格体")
        return
    for i, m in enumerate(mesh.get_editor_property("materials")):
        mi = m.get_editor_property("material_interface")
        log("  槽 [{}] {} -> {}".format(
            i, m.get_editor_property("material_slot_name"),
            mi.get_path_name() if mi else "None"))

    log("")
    log("=" * 64)
    log("结论：{}".format("全部通过" if ok else "仍有问题，见上面 <<< 标记"))
    log("（若日志里没有 'Failed to compile Material' 就说明编译也过了）")
    log("=" * 64)


main()
