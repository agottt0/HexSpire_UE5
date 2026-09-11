# Copyright Hex Spire. All Rights Reserved.
#
# 只读探针：查清「角色很黑」到底是材质没接对，还是光照太暗。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/probe_warden_material.py"
#
# 这个脚本【不修改任何资产】。它回答三个问题：
#   ① 骨骼网格体的材质槽实际指向谁（M_Warden 还是 Interchange 生成的 Material_001）
#   ② 那个材质的 BaseColor / Metallic / Roughness 实际取值
#   ③ 关卡里三盏灯的实际强度与曝光补偿
#
# ⚠️ 为什么必须先探再改：
#    "角色黑" 有至少四个独立成因（材质没接、贴图本身黑、金属度=1 无环境反射、
#    光照太暗）。盲调其中任何一个都可能"看起来好了一点"却没治本，
#    而每轮盲调都要重启编辑器，代价很高。

import unreal

SK_PATH = "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK"
MAP_PATH = "/Game/HexSpire/Maps/HexDemo"

MAT_DIR = "/Game/ArtResource/Character/Player/Warden/Material"


def log(msg):
    unreal.log("[HexProbe] {}".format(msg))


def sep(title):
    log("")
    log("=" * 64)
    log(title)
    log("=" * 64)


# ══════════════════════════════════════════════════ ① 材质槽指向谁

def probe_mesh_slots():
    sep("① 骨骼网格体的材质槽")

    mesh = unreal.load_asset(SK_PATH)
    if mesh is None:
        unreal.log_error("[HexProbe] 加载不到骨骼网格体：{}".format(SK_PATH))
        return None

    mats = mesh.get_editor_property("materials")
    log("材质槽数量：{}".format(len(mats)))

    used = []
    for i, m in enumerate(mats):
        iface = m.get_editor_property("material_interface")
        slot = m.get_editor_property("material_slot_name")
        if iface is None:
            log("  [{}] 槽名={} -> None（空槽，渲染成默认灰白）".format(i, slot))
            continue
        log("  [{}] 槽名={} -> {}  ({})".format(
            i, slot, iface.get_path_name(), iface.get_class().get_name()))
        used.append(iface)

    return used


# ══════════════════════════════════════════════════ ② 材质参数

def describe_material(iface, indent="  "):
    """把一个 Material / MaterialInstance 的关键取值打出来。"""
    cls = iface.get_class().get_name()
    log("{}资产：{}  类型={}".format(indent, iface.get_path_name(), cls))

    # ── MaterialInstance：列出被覆写的参数 + 父材质
    if isinstance(iface, unreal.MaterialInstance):
        parent = iface.get_editor_property("parent")
        log("{}父材质：{}".format(
            indent, parent.get_path_name() if parent else "None（无父材质 -> 渲染异常）"))

        try:
            # UE 5.x：MaterialEditingLibrary 能取到实例上的覆写值
            for kind, getter in (
                ("标量", unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value),
                ("向量", unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value),
            ):
                for name in ("Metallic", "Roughness", "Specular", "BaseColor",
                             "DiffuseColor", "EmissiveColor", "Opacity"):
                    try:
                        v = getter(iface, name)
                        log("{}  {}参数 {} = {}".format(indent, kind, name, v))
                    except Exception:
                        pass

            tex_names = ("DiffuseColorMap", "BaseColorMap", "NormalMap",
                         "EmissiveColorMap", "SpecularMap", "Texture")
            for name in tex_names:
                try:
                    t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                        iface, name)
                    if t:
                        log("{}  贴图参数 {} = {}".format(indent, name, t.get_path_name()))
                except Exception:
                    pass
        except Exception as e:
            unreal.log_warning("[HexProbe] 读实例参数失败：{}".format(e))
        return

    # ── Material：列出 shading model / blend mode / 已连接的输入
    if isinstance(iface, unreal.Material):
        for prop in ("blend_mode", "shading_model", "material_domain",
                     "two_sided", "metallic", "roughness", "specular"):
            try:
                log("{}  {} = {}".format(indent, prop, iface.get_editor_property(prop)))
            except Exception:
                pass


def probe_materials(used):
    sep("② 材质本体与参数")

    if used:
        log("—— 网格体【实际使用】的材质 ——")
        for iface in used:
            describe_material(iface)
            log("")

    log("—— 目录里【存在】的材质资产（对比用）——")
    for name in ("M_Warden", "Material_001"):
        asset = unreal.load_asset("{}/{}".format(MAT_DIR, name))
        if asset is None:
            log("  {} 不存在".format(name))
            continue
        describe_material(asset)
        log("")


# ══════════════════════════════════════════════════ ③ 贴图亮度

def probe_textures():
    sep("③ 贴图本身的亮度（排除「贴图就是黑的」）")

    for name in ("Material_001_Diffuse", "texture_0_metallic",
                 "texture_0_roughness", "texture_0_emission"):
        t = unreal.load_asset("{}/{}".format(MAT_DIR, name))
        if t is None:
            log("  {} 不存在".format(name))
            continue

        try:
            w = t.blueprint_get_size_x()
            h = t.blueprint_get_size_y()
        except Exception:
            w = h = -1

        comp = t.get_editor_property("compression_settings")
        srgb = t.get_editor_property("srgb")
        log("  {:24s} {}x{}  压缩={}  sRGB={}".format(name, w, h, comp, srgb))


# ══════════════════════════════════════════════════ ④ 光照

def probe_lighting():
    sep("④ 关卡光照与曝光")

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP_PATH)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    for a in eas.get_all_level_actors():
        if not a:
            continue
        label = a.get_actor_label()

        for comp_cls, kind in ((unreal.DirectionalLightComponent, "方向光"),
                               (unreal.SkyLightComponent, "天空光"),
                               (unreal.PointLightComponent, "点光"),
                               (unreal.SpotLightComponent, "聚光")):
            comp = a.get_component_by_class(comp_cls)
            if not comp:
                continue
            try:
                inten = comp.get_editor_property("intensity")
                color = comp.get_editor_property("light_color")
                rot = a.get_actor_rotation()
                log("  {:22s} {:6s} 强度={:8.2f} 颜色=({},{},{}) 朝向=({:.0f},{:.0f})".format(
                    label, kind, inten, color.r, color.g, color.b, rot.pitch, rot.yaw))
            except Exception as e:
                unreal.log_warning("[HexProbe] 读光源失败 {}：{}".format(label, e))

        if isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property("settings")
            log("  {:22s} 后处理  unbound={}".format(
                label, a.get_editor_property("unbound")))
            for key in ("auto_exposure_method",
                        "override_auto_exposure_bias", "auto_exposure_bias",
                        "override_auto_exposure_min_brightness",
                        "auto_exposure_min_brightness",
                        "override_auto_exposure_max_brightness",
                        "auto_exposure_max_brightness"):
                try:
                    log("      {} = {}".format(key, s.get_editor_property(key)))
                except Exception:
                    pass


def main():
    log("")
    log("Warden 材质与光照探针（只读）")

    used = probe_mesh_slots()
    probe_materials(used or [])
    probe_textures()
    probe_lighting()

    sep("探针结束")


main()
