# Copyright Hex Spire. All Rights Reserved.
#
# 首次创建 DA_UnitVisual_* 外观资产（/Game/HexSpire/Data/Visuals/）。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\make_unit_visualsets.py"
#
# ⚠️ 初始内容【镜像】HexUnitAppearance.cpp 的占位映射
#    （warden 用自有模型，杂兵用 Mannequin，精英/Boss 用 Female），
#    所以建完的第一帧画面与建之前逐像素相同 —— 这是刻意的：
#    管线迁移与美术升级是两件事，一次只做一件，出问题才知道怪谁。
#
# ⚠️ 幂等：资产已存在时不覆盖 —— 美术改过的槽位不能被脚本冲掉。

import unreal

VIS_DIR = "/Game/HexSpire/Data/Visuals"

MESH_WARDEN = "/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK.01_Warden_Bined_UE4SK"
MESH_GRUNT = "/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin.SK_Mannequin"
MESH_ELITE = "/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin_Female.SK_Mannequin_Female"

# 模板动画（全部 S_Mannequin 骨架，与上面三个模型互换无需重定向）
ANIM_DIR = "/Game/TurnBasedStrategyRPGTemplate/Animations"
ANIMS = {
    unreal.HexUnitAnim.IDLE: "AS_Idle",
    unreal.HexUnitAnim.WALK: "AS_Walk",
    unreal.HexUnitAnim.ATTACK: "AS_SwordsmanCombo1",
    unreal.HexUnitAnim.CAST: "AS_MageAttack",
    unreal.HexUnitAnim.SHOOT: "AS_ArcherShoot",
    unreal.HexUnitAnim.GET_HIT: "AS_GetHit",
    unreal.HexUnitAnim.DIE: "AS_Die",
}

# UnitId → 模型（UnitId 必须等于 DT_Heroes / DT_Enemies 的 RowName）
UNITS = (
    ("warden", MESH_WARDEN),
    ("biting_hound", MESH_GRUNT),
    ("stone_slinger", MESH_GRUNT),
    ("stone_golem", MESH_ELITE),   # 精英 → Female 剪影（区分度，非性别设定）
    ("siege_worm", MESH_ELITE),    # Boss → 同上
)


def log(msg):
    unreal.log("[HexVis] {}".format(msg))


def make_visual(unit_id, mesh_path):
    name = "DA_UnitVisual_{}".format(unit_id)
    asset_path = "{}/{}".format(VIS_DIR, name)

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        log("已存在，不覆盖：{}".format(asset_path))
        return True

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.HexUnitVisualSet)

    da = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, VIS_DIR, unreal.HexUnitVisualSet, factory)
    if da is None:
        unreal.log_error("[HexVis] 创建失败：{}".format(asset_path))
        return False

    # ⚠️ UnitId 是两边对上的唯一纽带，这一行错了资产就是死的。
    da.set_editor_property("unit_id", unit_id)

    mesh = unreal.load_asset(mesh_path.split(".")[0])
    if mesh is None:
        unreal.log_error("[HexVis] 模型加载失败：{}".format(mesh_path))
        return False
    da.set_editor_property("skeletal_mesh", mesh)

    anims = {}
    for key, asset_name in ANIMS.items():
        seq = unreal.load_asset("{}/{}".format(ANIM_DIR, asset_name))
        if seq is None:
            unreal.log_error("[HexVis] 动画加载失败：{}/{}".format(ANIM_DIR, asset_name))
            return False
        anims[key] = seq
    da.set_editor_property("anims", anims)

    if not unreal.EditorAssetLibrary.save_loaded_asset(da):
        unreal.log_error("[HexVis] 保存失败：{}".format(asset_path))
        return False

    log("已创建：{}（UnitId={}）".format(asset_path, unit_id))
    return True


def main():
    log("")
    log("=" * 60)
    log("创建单位外观资产")
    log("=" * 60)

    if not unreal.EditorAssetLibrary.does_directory_exist(VIS_DIR):
        unreal.EditorAssetLibrary.make_directory(VIS_DIR)

    ok = all([make_visual(uid, mesh) for uid, mesh in UNITS])

    log("")
    if ok:
        log("完成。美术后续在这些资产里换模型/动画/挂件即可，不用碰代码。")
        log("注意：全部动画必须与模型共用同一套骨架（见 HexUnitVisualSet.h）。")
    else:
        unreal.log_error("[HexVis] 有资产创建失败，见上方日志")
    log("=" * 60)


main()
