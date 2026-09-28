# Copyright Hex Spire. All Rights Reserved.
#
# 从导出的 CSV 建符文 DataTable（DT_Runes）。
#
# 用法（先跑导出，再跑这个）：
#   UnrealEditor-Cmd.exe <uproject> -run=HexExportRunes
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\make_rune_datatable.py"
#
# ⚠️ 幂等：表已存在时【不覆盖】，只报告（同 make_card_datatable.py）。

import unreal

DT_DIR = "/Game/HexSpire/Data"
DT_NAME = "DT_Runes"
DT_PATH = "{}/{}".format(DT_DIR, DT_NAME)


def log(msg):
    unreal.log("[HexDT] {}".format(msg))


def main():
    log("")
    log("=" * 60)
    log("建立符文 DataTable")
    log("=" * 60)

    if unreal.EditorAssetLibrary.does_asset_exist(DT_PATH):
        dt = unreal.load_asset(DT_PATH)
        rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("表已存在（{} 行），不覆盖：{}".format(len(rows), DT_PATH))
        return

    csv_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + "Export/Runes.csv")

    if not unreal.Paths.file_exists(csv_path):
        unreal.log_error(
            "[HexDT] 找不到 CSV：{}\n"
            "        请先跑：UnrealEditor-Cmd.exe <uproject> -run=HexExportRunes"
            .format(csv_path))
        return

    log("CSV：{}".format(csv_path))

    if not unreal.EditorAssetLibrary.does_directory_exist(DT_DIR):
        unreal.EditorAssetLibrary.make_directory(DT_DIR)

    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = unreal.load_object(
        None, "/Script/HexSpire.HexRuneTableRow")

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
    log("接下来：策划在编辑器里改表加行；符文目标 90–120 个（§6.1）")
    log("=" * 60)


main()
