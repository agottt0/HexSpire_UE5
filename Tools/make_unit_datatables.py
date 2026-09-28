# Copyright Hex Spire. All Rights Reserved.
#
# 从导出的 CSV 建英雄/敌人 DataTable（DT_Heroes / DT_Enemies）。
#
# 用法（先跑导出，再跑这个）：
#   UnrealEditor-Cmd.exe <uproject> -run=HexExportUnits
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\make_unit_datatables.py"
#
# ⚠️ 这个脚本是给【首次建表】用的。表建好之后，策划的日常流程是
#    在编辑器里直接改表，不需要再跑它。
#
# ⚠️ 幂等：表已存在时【不覆盖】，只报告。
#    单位表没有美术字段（资产引用全在 DA_UnitVisual_*，见
#    HexHeroTableRow.h 顶部的分界线说明），所以覆盖不会像卡表那样
#    冲掉美术数据 —— 但会冲掉策划已调过的数值，同样不能自动做。

import unreal

DT_DIR = "/Game/HexSpire/Data"

# (CSV 文件名, 表名, 行结构路径)
TABLES = (
    ("Heroes.csv", "DT_Heroes", "/Script/HexSpire.HexHeroTableRow"),
    ("Enemies.csv", "DT_Enemies", "/Script/HexSpire.HexEnemyTableRow"),
)


def log(msg):
    unreal.log("[HexDT] {}".format(msg))


def make_table(csv_name, dt_name, row_struct_path):
    dt_path = "{}/{}".format(DT_DIR, dt_name)

    if unreal.EditorAssetLibrary.does_asset_exist(dt_path):
        dt = unreal.load_asset(dt_path)
        rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("表已存在（{} 行），不覆盖：{}".format(len(rows), dt_path))
        return True

    csv_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + "Export/" + csv_name)

    if not unreal.Paths.file_exists(csv_path):
        unreal.log_error(
            "[HexDT] 找不到 CSV：{}\n"
            "        请先跑：UnrealEditor-Cmd.exe <uproject> -run=HexExportUnits"
            .format(csv_path))
        return False

    log("CSV：{}".format(csv_path))

    if not unreal.EditorAssetLibrary.does_directory_exist(DT_DIR):
        unreal.EditorAssetLibrary.make_directory(DT_DIR)

    # ⚠️ 行结构选错时 FindRow 仍会返回指针（按错误布局解读的字节），
    #    HP/ATK 变垃圾值且不报错。C++ 侧 FHexUnitTableLoader 会校验
    #    GetRowStruct 并拒绝整张表，这里选对只是让它不用拒绝。
    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = unreal.load_object(
        None, row_struct_path)

    task = unreal.AssetImportTask()
    task.filename = csv_path
    task.destination_path = DT_DIR
    task.destination_name = dt_name
    task.replace_existing = False
    task.automated = True
    task.save = True
    task.factory = factory

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not unreal.EditorAssetLibrary.does_asset_exist(dt_path):
        unreal.log_error("[HexDT] 导入失败，表没有生成：{}".format(dt_path))
        return False

    dt = unreal.load_asset(dt_path)
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)

    log("已建表：{}（{} 行）".format(dt_path, len(rows)))
    log("行名：{}".format(", ".join(str(r) for r in rows)))
    return True


def main():
    log("")
    log("=" * 60)
    log("建立英雄/敌人 DataTable")
    log("=" * 60)

    ok = True
    for csv_name, dt_name, row_struct in TABLES:
        ok = make_table(csv_name, dt_name, row_struct) and ok
        log("")

    if ok:
        log("接下来：")
        log("  · 策划改数值：直接在编辑器里改这两张表")
        log("  · 美术挂资产：{}/Visuals 下的 DA_UnitVisual_<UnitId>".format(DT_DIR))
    log("=" * 60)


main()
