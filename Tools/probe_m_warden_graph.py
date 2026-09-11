# Copyright Hex Spire. All Rights Reserved.
#
# 只读探针 ②：查清 M_Warden 的材质图到底接了哪些输入。
#
# 为什么需要这一步：
#   探针 ① 已确认网格体用的是 Interchange 生成的 Material_001
#   （父材质 FBXLegacyPhongSurfaceMaterial，Roughness=0 / Specular=0 → 黑）。
#   修法是把槽位改指向手工材质 M_Warden。
#
#   但【前提是 M_Warden 自己接对了】。如果它只接了 BaseColor 而
#   Roughness / Metallic 悬空，切过去只会把"全黑"换成"全镜面"——
#   同样不可用，而且因为改了资产，回退成本更高。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript
#       -script="E:\UE_Proj\HexSpire\Tools\probe_m_warden_graph.py"

import unreal

MAT = "/Game/ArtResource/Character/Player/Warden/Material/M_Warden"
MAT_DIR = "/Game/ArtResource/Character/Player/Warden/Material"

# 关心的材质属性（对应 UE 的 MaterialProperty 枚举）
PROPS = [
    ("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
    ("Metallic", unreal.MaterialProperty.MP_METALLIC),
    ("Specular", unreal.MaterialProperty.MP_SPECULAR),
    ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
    ("EmissiveColor", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
    ("Opacity", unreal.MaterialProperty.MP_OPACITY),
    ("Normal", unreal.MaterialProperty.MP_NORMAL),
    ("AmbientOcclusion", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
]


def log(msg):
    unreal.log("[HexProbe2] {}".format(msg))


def main():
    log("")
    log("=" * 64)
    log("M_Warden 材质图连接状况")
    log("=" * 64)

    mat = unreal.load_asset(MAT)
    if mat is None:
        unreal.log_error("[HexProbe2] 加载不到 {}".format(MAT))
        return

    # ── 各输入是否已连接
    #    get_material_property_input_node 在未连接时返回 None
    for name, prop in PROPS:
        node = None
        try:
            node = unreal.MaterialEditingLibrary.get_material_property_input_node(
                mat, prop)
        except Exception as e:
            unreal.log_warning("[HexProbe2] 读 {} 失败：{}".format(name, e))

        if node is None:
            log("  {:18s} 未连接  <- 会取材质上的常量默认值".format(name))
        else:
            desc = node.get_class().get_name()
            # 若是贴图采样，把贴图名一起打出来
            tex = None
            try:
                tex = node.get_editor_property("texture")
            except Exception:
                pass
            if tex is not None:
                desc += " -> " + tex.get_name()
            log("  {:18s} 已连接  {}".format(name, desc))

    # ── 材质上的常量默认值（未连接的输入会用这些）
    log("")
    log("常量默认值（未连接的输入取这些）：")
    for prop in ("metallic", "roughness", "specular", "opacity", "two_sided"):
        try:
            log("  {:12s} = {}".format(prop, mat.get_editor_property(prop)))
        except Exception:
            pass

    # ── 材质里引用的全部贴图
    log("")
    log("M_Warden 引用的贴图：")
    try:
        texes = unreal.MaterialEditingLibrary.get_used_textures(mat)
        for t in texes:
            log("  {}".format(t.get_path_name()))
    except Exception as e:
        unreal.log_warning("[HexProbe2] get_used_textures 失败：{}".format(e))

    # ── 贴图的压缩设置（数据贴图若带 sRGB 会算错）
    log("")
    log("=" * 64)
    log("贴图导入设置（金属度/粗糙度这类【数据】贴图不能带 sRGB）")
    log("=" * 64)
    for name in ("Material_001_Diffuse", "Material_001_Normal",
                 "texture_0_metallic", "texture_0_roughness",
                 "texture_0_emission"):
        t = unreal.load_asset("{}/{}".format(MAT_DIR, name))
        if t is None:
            log("  {} 不存在".format(name))
            continue
        comp = t.get_editor_property("compression_settings")
        srgb = t.get_editor_property("srgb")
        grp = t.get_editor_property("lod_group")
        flag = ""
        # 期望：Diffuse=TC_Default+sRGB / Normal=TC_Normalmap / 其余=TC_Masks 无 sRGB
        if name in ("texture_0_metallic", "texture_0_roughness"):
            if srgb or comp != unreal.TextureCompressionSettings.TC_MASKS:
                flag = "   <<< 需要修正（应为 TC_Masks 且 sRGB=False）"
        if name == "Material_001_Normal":
            if comp != unreal.TextureCompressionSettings.TC_NORMALMAP:
                flag = "   <<< 需要修正（应为 TC_Normalmap）"
        log("  {:22s} 压缩={:34s} sRGB={:5s} 组={}{}".format(
            name, str(comp), str(srgb), str(grp), flag))

    log("")
    log("探针结束")


main()
