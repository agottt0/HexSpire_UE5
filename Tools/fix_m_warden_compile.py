# Copyright Hex Spire. All Rights Reserved.
#
# 修复「换成 M_Warden 后角色不显示 / 变成默认灰白材质」。
#
# ══════════════════════════════════════════════════════════════════
# 诊断结论（来自 Saved/Logs/HexSpire.log，不是猜的）
# ══════════════════════════════════════════════════════════════════
# M_Warden 根本【没编译成功】，UE 回退到了 Default Material：
#
#   LogMaterial: Warning: M_Warden.uasset: Failed to compile Material
#                for platform PCD3D_SM5, Default Material will be used in game.
#     (Node TextureSample) Sampler type is Color, should be Masks
#         for .../texture_0_metallic
#     (Node TextureSample) Sampler type is Color, should be Masks
#         for .../texture_0_roughness
#
#   LogMaterial: Warning: M_Warden missing usage flag SkeletalMesh!
#                Default Material will be used in game.
#   LogSkeletalMesh: Warning: Material with missing usage flag was applied
#                to skeletal mesh .../01_Warden_Bined_UE4SK
#
# 所以是两个独立的错，两个都会单独导致「回退默认材质」：
#
# ① SamplerType 与贴图压缩设置不匹配
#    前一轮 fix_warden_material.py 把 metallic / roughness 改成了
#    TC_Masks（那是对的，数据贴图不该带 sRGB），
#    但材质图里那两个 TextureSample 节点的 SamplerType 还写着 Color。
#    UE 要求两者严格一致，不一致 = 编译失败（是 error，不是 warning）。
#    → 这就是「自动生成的材质能显示、手接的不能」的原因：
#      自动生成的 Material_001 用的是原始 sRGB 贴图，没有这个冲突。
#
# ② 缺 bUsedWithSkeletalMesh 使用标记
#    手动新建的材质默认只勾了静态网格体的用途。
#    从内容浏览器把材质拖到骨骼网格体上时编辑器会自动补勾并保存；
#    用蓝图/代码指派则不会 —— 于是标记一直是空的。
#
# 两个错都不影响资产在编辑器缩略图里的样子，只在实际渲染时回退，
# 这就是为什么看起来「材质是好的，但角色不显示」。
#
# 用法（必须先关掉 Unreal Editor，否则 .uasset 被锁，保存会静默失败）：
#   & "E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
#       "E:\UE_Proj\HexSpire\HexSpire.uproject" -run=pythonscript `
#       -script="E:\UE_Proj\HexSpire\Tools\fix_m_warden_compile.py"
#
# 幂等：重复运行安全。

import unreal

MAT_PATH = "/Game/ArtResource/Character/Player/Warden/Material/M_Warden"

# 贴图压缩设置 -> 该贴图的 TextureSample 节点应有的 SamplerType
#
# 只列出本材质实际用到的几种。规则本身是 UE 硬性要求：
#   TC_Default   + sRGB=True  -> SAMPLERTYPE_COLOR
#   TC_Masks                  -> SAMPLERTYPE_MASKS      （数据贴图，无 sRGB）
#   TC_Normalmap              -> SAMPLERTYPE_NORMAL
#   TC_Grayscale              -> SAMPLERTYPE_LINEAR_GRAYSCALE
COMP_TO_SAMPLER = {
    unreal.TextureCompressionSettings.TC_DEFAULT:
        unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
    unreal.TextureCompressionSettings.TC_MASKS:
        unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
    unreal.TextureCompressionSettings.TC_NORMALMAP:
        unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
    unreal.TextureCompressionSettings.TC_GRAYSCALE:
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
}


def log(msg):
    unreal.log("[HexMat] {}".format(msg))


def warn(msg):
    unreal.log_warning("[HexMat] {}".format(msg))


# M_Warden 实际接了贴图的五个输入。
#
# ⚠️ 为什么不直接遍历 Material.expressions：
#    UE 5.8 里该属性（含 5.1+ 的 expression_collection.expressions）在
#    Python 侧是 protected，读取会抛
#    "Property 'Expressions' ... is protected and cannot be read"。
#    改用 get_material_property_input_node 逐个输入取节点 ——
#    这条路 probe_m_warden_graph.py 已经验证过可用。
#    代价是取不到「悬空没接到输出的节点」，但那种节点不参与编译，
#    也就不会造成 SamplerType 报错，正好不需要管。
INPUT_PROPS = [
    ("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
    ("Metallic", unreal.MaterialProperty.MP_METALLIC),
    ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
    ("EmissiveColor", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
    ("Normal", unreal.MaterialProperty.MP_NORMAL),
    ("Specular", unreal.MaterialProperty.MP_SPECULAR),
    ("Opacity", unreal.MaterialProperty.MP_OPACITY),
    ("AmbientOcclusion", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
]


def get_texture_nodes(mat):
    """取所有【接到材质输出上】的贴图采样节点，按节点去重。"""
    nodes = []
    seen = set()

    for name, prop in INPUT_PROPS:
        try:
            node = unreal.MaterialEditingLibrary.                get_material_property_input_node(mat, prop)
        except Exception as e:
            warn("读 {} 输入失败：{}".format(name, e))
            continue

        if node is None:
            continue
        if not isinstance(node, unreal.MaterialExpressionTextureBase):
            log("  {:18s} 接的不是贴图采样（{}），跳过".format(
                name, node.get_class().get_name()))
            continue

        key = node.get_path_name()
        if key in seen:
            continue
        seen.add(key)
        nodes.append((name, node))

    return nodes


# ═══════════════════════════════════════ ① SamplerType 对齐贴图压缩设置

def fix_sampler_types(mat):
    changed = 0

    for prop_name, node in get_texture_nodes(mat):
        tex = node.get_editor_property("texture")
        if tex is None:
            warn("{} 上的采样节点没设贴图，跳过".format(prop_name))
            continue

        comp = tex.get_editor_property("compression_settings")
        want = COMP_TO_SAMPLER.get(comp)
        if want is None:
            warn("{}：压缩设置 {} 没有对应规则，请手工确认".format(
                tex.get_name(), comp))
            continue

        cur = node.get_editor_property("sampler_type")
        if cur == want:
            log("  {:22s} SamplerType 已正确（{}）".format(
                tex.get_name(), want.name))
            continue

        log("  {:22s} SamplerType {} -> {}   （贴图压缩={}）".format(
            tex.get_name(), cur.name, want.name, comp.name))
        node.set_editor_property("sampler_type", want)
        changed += 1

    return changed


# ═══════════════════════════════════════ ② 补上骨骼网格体使用标记

def fix_usage_flags(mat):
    """
    bUsedWithSkeletalMesh 不勾，材质就【不会为骨骼网格体编译对应 shader 变体】，
    运行时只能回退 Default Material —— 无论材质图本身接得多对。

    只勾骨骼网格体这一项。每个 usage flag 都会多编一套 shader 变体，
    无脑全勾会让编译时间和包体都涨。
    """
    if mat.get_editor_property("used_with_skeletal_mesh"):
        log("  bUsedWithSkeletalMesh 已勾选")
        return False

    log("  bUsedWithSkeletalMesh False -> True")
    mat.set_editor_property("used_with_skeletal_mesh", True)
    return True


def main():
    log("")
    log("=" * 64)
    log("修复 M_Warden 编译失败（角色回退默认材质 / 不显示）")
    log("=" * 64)

    mat = unreal.load_asset(MAT_PATH)
    if mat is None:
        unreal.log_error("[HexMat] 加载不到材质：{}".format(MAT_PATH))
        return

    log("")
    log("① SamplerType 与贴图压缩设置对齐")
    n_sampler = fix_sampler_types(mat)

    log("")
    log("② 使用标记（Usage Flags）")
    n_usage = fix_usage_flags(mat)

    if n_sampler == 0 and not n_usage:
        log("")
        log("两项都已是目标状态，无需改动")
        log("=" * 64)
        return

    # 改完必须重编译，否则 DDC 里还是那份编译失败的结果
    log("")
    log("重新编译材质…")
    unreal.MaterialEditingLibrary.recompile_material(mat)

    # ⚠️ 一定要检查 save 的返回值。编辑器开着时 .uasset 被锁，
    #    UE 重试后让 save 返回 False，但内存里的改动是成功的 ——
    #    任何"改完再读一遍"的自检都会通过，下次重启才发现白干了。
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat, False):
        unreal.log_error(
            "[HexMat] 保存失败 —— 几乎总是因为 Unreal Editor 正开着锁住了文件。"
            "请关闭编辑器后重跑本脚本。")
        return

    log("")
    log("=" * 64)
    log("完成：SamplerType 改动={} 个节点，使用标记改动={}".format(
        n_sampler, n_usage))
    log("材质已重编译并保存")
    log("=" * 64)


main()
