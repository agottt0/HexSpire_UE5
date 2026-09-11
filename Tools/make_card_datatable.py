# Copyright Hex Spire. All Rights Reserved.
#
# 从导出的 CSV 建卡牌 DataTable。
#
# 用法（先跑导出，再跑这个）：
#   UnrealEditor-Cmd.exe <uproject> -run=HexExportCards
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\make_card_datatable.py"
#
# ⚠️ 这个脚本是给【首次建表】用的。表建好之后，策划的日常流程是
#    在编辑器里直接改表（或重新导入改过的 CSV），不需要再跑它。
#
# ⚠️ 幂等：表已存在时【不覆盖】，只报告。
#    覆盖会把策划已经配好的美术字段（Visual）全部冲掉 ——
#    那些字段 CSV 里是空的，重新导入等于清空。

import unreal

CSV_PATH = None  # 运行时算
DT_DIR = "/Game/HexSpire/Data"
DT_NAME = "DT_Cards"
DT_PATH = "{}/{}".format(DT_DIR, DT_NAME)


def log(msg):
    unreal.log("[HexDT] {}".format(msg))


def main():
    log("")
    log("=" * 60)
    log("建立卡牌 DataTable")
    log("=" * 60)

    if unreal.EditorAssetLibrary.does_asset_exist(DT_PATH):
        dt = unreal.load_asset(DT_PATH)
        rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("表已存在（{} 行），不覆盖：{}".format(len(rows), DT_PATH))
        log("")
        log("⚠️ 若要用新 CSV 重建，请先在编辑器里删掉该表 ——")
        log("   直接覆盖会清空策划已配好的美术字段（Visual）。")
        return

    csv_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + "Export/Cards.csv")

    if not unreal.Paths.file_exists(csv_path):
        unreal.log_error(
            "[HexDT] 找不到 CSV：{}\n"
            "        请先跑：UnrealEditor-Cmd.exe <uproject> -run=HexExportCards"
            .format(csv_path))
        return

    log("CSV：{}".format(csv_path))

    if not unreal.EditorAssetLibrary.does_directory_exist(DT_DIR):
        unreal.EditorAssetLibrary.make_directory(DT_DIR)

    # ── 导入设置：行结构必须是 FHexCardTableRow
    #
    # ⚠️ 行结构选错时 FindRow 仍会返回指针（指向按错误布局解读的字节），
    #    于是费用/伤害变成垃圾值且不报错。
    #    C++ 侧的 FHexCardTableLoader::Apply 会校验 GetRowStruct 并拒绝整张表。
    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = unreal.load_object(
        None, "/Script/HexSpire.HexCardTableRow")

    task = unreal.AssetImportTask()
    task.filename = csv_path
    task.destination_path = DT_DIR
    task.destination_name = DT_NAME
    task.replace_existing = False
    task.automated = True
    task.save = True
    task.factory = factory

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not unreal.EditorAssetLibrary.does_asset_exist(DT_PATH):
        unreal.log_error("[HexDT] 导入失败，表没有生成")
        return

    dt = unreal.load_asset(DT_PATH)
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)

    log("已建表：{}（{} 行）".format(DT_PATH, len(rows)))
    log("行名：{}".format(", ".join(str(r) for r in rows)))
    log("")
    log("接下来：")
    log("  · 策划改数值：直接在编辑器里改这张表")
    log("  · 美术挂资产：每行的 Visual 组（卡框/图标/插画/染色/光晕）")
    log("=" * 60)


main()
