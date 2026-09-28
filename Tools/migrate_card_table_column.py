# Copyright Hex Spire. All Rights Reserved.
#
# 给已存在的 DT_Cards 补【新增的列】，其余字段一个都不动。
#
# 用法（先导出 CSV 拿到权威值，再跑这个）：
#   UnrealEditor-Cmd.exe <uproject> -run=HexExportCards
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
#       -script="E:\UE_Proj\HexSpire\Tools\migrate_card_table_column.py"
#
# ══════════════════════════════════════════════════════════════════
# 为什么需要这个脚本，而不是重新导入 CSV
# ══════════════════════════════════════════════════════════════════
# 行结构加了字段（如 CastAnim）之后，老表里没有那一列。
# 覆写是【整行替换】—— 缺列的字段被冲成类型默认值，
# 代码内建的值凭空丢失（症状：角色打出卡牌时不做动作）。
#
# 但也不能简单重导 CSV：那会把美术已配好的 Visual 组全部清空 ——
# 那些字段只存在于表里，CSV 里是空的（见 make_card_datatable.py 的警告）。
#
# 所以这里走 JSON 往返做【字段级】迁移：
#   导出表为 JSON（含 Visual）→ 只改新列 → 填回去
#
# ⚠️ 用 JSON 而不是 CSV 做往返：JSON 保留嵌套结构（Visual 是个 struct，
#    里面还有 TSoftObjectPtr）。CSV 会把它压成括号字面量，
#    往返一次就可能丢精度或丢引用。
#
# ⚠️ 幂等：列已有非默认值的行会被跳过，重复跑安全。

import csv
import json

import unreal

DT_PATH = "/Game/HexSpire/Data/DT_Cards"

# 迁移哪一列，以及"默认值"长什么样（默认值 = 需要被填充的标志）
COLUMN = "CastAnim"
DEFAULT_VALUE = "None"

# ── 顶层的数组列（TArray<FName>）
#
# ⚠️ 首次建表用的 CSV 把数组导成了 "a|b|c" 管道格式 ——
#    FArrayProperty::ImportText 要求以 '(' 开头，整格解析失败，
#    于是 DT_Cards 里所有卡的 Tags 都是空数组（导入日志里只有
#    一条含糊的 Problem 警告）。这里从修正后的 CSV 补回来。
#    判据：表里是空数组 = 从未成功导入过（策划不可能配出空 Tags——
#    他们想清空也没有动机，那列从建表起就没有过值）。
LIST_COLUMNS = ("Tags",)

# ── step 级的表现字段
#
# ⚠️ 这些藏在 Effects 数组【里面】，所以顶层列检查抓不到它们：
#    表里有 Effects 列，只是列里的每个 step 缺 VfxId/SfxId ——
#    那是建表时导入的旧值（当时内建卡还没配特效）。
#    症状与缺列完全一样（事件里没有表现 id），但原因不同，
#    所以必须分开迁移。
STEP_COLUMNS = ("VfxId", "SfxId")

# 权威值来自代码内建导出的 CSV。
#
# ⚠️ 不硬编码在脚本里：那样每加一张卡都要改脚本，
#    而且脚本里的值与 HexContentLibrary.cpp 会慢慢漂移，
#    没有任何机制能发现。以导出产物为准则永远一致。
CSV_REL = "Export/Cards.csv"


def log(msg):
    unreal.log("[HexMigrate] {}".format(msg))


def read_authoritative_values():
    """从导出的 CSV 读 {RowName: 列值}"""
    csv_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + CSV_REL)

    if not unreal.Paths.file_exists(csv_path):
        unreal.log_error(
            "[HexMigrate] 找不到 CSV：{}\n"
            "        请先跑：UnrealEditor-Cmd.exe <uproject> -run=HexExportCards"
            .format(csv_path))
        return None

    # utf-8-sig：导出器刻意带 BOM（否则 Excel 打开中文是乱码），
    # 不剥掉 BOM 会让第一列列名变成 '\ufeffName' 而匹配不上。
    with open(csv_path, "r", encoding="utf-8-sig", newline="") as f:
        rows = [r for r in csv.reader(f) if r]

    if not rows:
        unreal.log_error("[HexMigrate] CSV 是空的")
        return None

    header = rows[0]
    if "Name" not in header or COLUMN not in header:
        unreal.log_error(
            "[HexMigrate] CSV 缺 Name 或 {} 列 —— 导出器是不是也漏改了？"
            .format(COLUMN))
        return None

    name_idx = header.index("Name")
    col_idx = header.index(COLUMN)

    out = {}
    for parts in rows[1:]:
        # ⚠️ 必须用 csv 模块而不是 line.split(",")：
        #    Tags / TargetSpec / Effects 都是带引号的复合列，里面有逗号。
        #    裸 split 会让引号列之后的所有下标整体错位 —— CastAnim
        #    恰好排在 Tags 之后，第一个被错位打中的就是本脚本要迁移的列。
        if len(parts) <= col_idx:
            continue
        out[parts[name_idx]] = parts[col_idx]

    return out


def read_temp_table_values():
    """从代码内建读 step 级字段与顶层数组列的权威值。

    返回 (steps, lists)：
      steps = {RowName: [{VfxId,SfxId}, ...]}
      lists = {RowName: {Tags: [...], ...}}

    ⚠️ 不从 CSV 读：Effects 列是带引号的括号字面量，里面还有
       转义的双引号（VfxId=""vfx_slash""）。用正则去解析它
       是在重新实现 UE 的 struct 文本解析器 —— 那东西的边界情况
       （嵌套括号、转义、空值）足够写出一堆静默错误。

    改为直接问引擎：把【代码内建】的卡池导成 JSON 没有现成入口，
    但有一个等价且更可靠的办法 —— 内建值就是导出 CSV 的来源，
    而我们真正需要的只是 "id → 每个 step 的 vfx/sfx"。
    所以这里用引擎自己的 struct 解析：先把 CSV 导入一张
    【临时表】，再从临时表读 JSON。解析由引擎负责，不会漂移。
    """
    csv_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + CSV_REL)

    if not unreal.Paths.file_exists(csv_path):
        unreal.log_error("[HexMigrate] 找不到 CSV：{}".format(csv_path))
        return None

    tmp_dir = "/Game/HexSpire/Data/_Migrate"
    tmp_name = "DT_Cards_Migrate_Tmp"
    tmp_path = "{}/{}".format(tmp_dir, tmp_name)

    if unreal.EditorAssetLibrary.does_asset_exist(tmp_path):
        unreal.EditorAssetLibrary.delete_asset(tmp_path)

    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = unreal.load_object(
        None, "/Script/HexSpire.HexCardTableRow")

    task = unreal.AssetImportTask()
    task.filename = csv_path
    task.destination_path = tmp_dir
    task.destination_name = tmp_name
    task.replace_existing = True
    task.automated = True
    task.save = False
    task.factory = factory

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not unreal.EditorAssetLibrary.does_asset_exist(tmp_path):
        unreal.log_error("[HexMigrate] 临时表导入失败，无法读取 step 级权威值")
        return None

    tmp_dt = unreal.load_asset(tmp_path)
    try:
        tmp_rows = json.loads(tmp_dt.export_to_json_string())
    except ValueError as e:
        unreal.log_error("[HexMigrate] 临时表 JSON 解析失败：{}".format(e))
        return None

    steps = {}
    lists = {}
    for r in tmp_rows:
        name = r.get("Name")
        if name is None:
            continue
        steps[name] = [
            {k: e.get(k, DEFAULT_VALUE) for k in STEP_COLUMNS}
            for e in (r.get("Effects") or [])
        ]
        lists[name] = {col: (r.get(col) or []) for col in LIST_COLUMNS}

    # 临时表用完即删 —— 留着会让内容浏览器里多一张看起来像正式表的资产，
    # 而它的数据是某次迁移时的快照，被人误当成配表入口就麻烦了。
    unreal.EditorAssetLibrary.delete_asset(tmp_path)
    if unreal.EditorAssetLibrary.does_directory_exist(tmp_dir):
        unreal.EditorAssetLibrary.delete_directory(tmp_dir)

    return steps, lists


def main():
    log("")
    log("=" * 60)
    log("迁移 DT_Cards 的 {} 列".format(COLUMN))
    log("=" * 60)

    if not unreal.EditorAssetLibrary.does_asset_exist(DT_PATH):
        log("表不存在，无需迁移：{}".format(DT_PATH))
        return

    authoritative = read_authoritative_values()
    if authoritative is None:
        return

    dt = unreal.load_asset(DT_PATH)

    as_json = dt.export_to_json_string()
    try:
        rows = json.loads(as_json)
    except ValueError as e:
        unreal.log_error("[HexMigrate] 表的 JSON 解析失败：{}".format(e))
        return

    temp_vals = read_temp_table_values()
    if temp_vals is None:
        return
    step_authoritative, list_authoritative = temp_vals

    changed = 0
    skipped = 0
    missing = 0

    for row in rows:
        row_name = row.get("Name")
        if row_name is None:
            continue

        # ── 顶层列（CastAnim）
        want = authoritative.get(row_name)
        if want is None:
            missing += 1
        else:
            # 已经有非默认值 → 那是策划自己配的，不覆盖
            current = row.get(COLUMN, DEFAULT_VALUE)
            if current != DEFAULT_VALUE:
                skipped += 1
            elif want != DEFAULT_VALUE:
                row[COLUMN] = want
                changed += 1
                log("  {} {} : {} → {}".format(row_name, COLUMN, current, want))

        # ── 顶层数组列（Tags）
        #
        # ⚠️ 首次建表的 CSV 把数组导成了管道格式，整列解析失败为空。
        #    空数组 = 从未成功导入过（Tags 从建表起就没有过值），
        #    从修正后的 CSV 补回；非空 = 已修过或策划配过，不动。
        want_lists = list_authoritative.get(row_name) or {}
        for col in LIST_COLUMNS:
            cur_list = row.get(col) or []
            want_list = want_lists.get(col) or []
            if not cur_list and want_list:
                row[col] = want_list
                changed += 1
                log("  {} {} : [] → {}".format(row_name, col, want_list))

        # ── step 级字段（Effects[i].VfxId / SfxId）
        #
        # ⚠️ 按【下标】对应而不是按 Op 匹配：同一张卡可以有两个
        #    同 Op 的 step（连击 + 追加伤害），按 Op 匹配会串。
        #    下标对应的前提是表与内建的 step 数一致 —— 不一致时
        #    说明策划改过效果结构，此时跳过并报告，不强行猜。
        want_steps = step_authoritative.get(row_name)
        effects = row.get("Effects") or []

        if want_steps is not None and len(want_steps) == len(effects):
            for i, eff in enumerate(effects):
                for key in STEP_COLUMNS:
                    cur = eff.get(key, DEFAULT_VALUE)
                    wnt = want_steps[i].get(key, DEFAULT_VALUE)
                    if cur == DEFAULT_VALUE and wnt != DEFAULT_VALUE:
                        eff[key] = wnt
                        changed += 1
                        log("  {} Effects[{}].{} : → {}".format(
                            row_name, i, key, wnt))
        elif want_steps is not None and len(want_steps) != len(effects):
            log("  {} 的 step 数与内建不一致（表 {} vs 内建 {}），跳过 step 迁移"
                .format(row_name, len(effects), len(want_steps)))

    if changed == 0:
        log("没有需要迁移的字段（已有值 {} 行，CSV 里查不到 {} 行）"
            .format(skipped, missing))
        return

    # 填回去。
    #
    # ⚠️ fill_from_json_string 是【整表替换】，所以上面必须是
    #    "导出的完整 JSON 改一个字段"，而不是只构造改动的那几行 ——
    #    只填改动行会把其余行全部删掉。
    if not dt.fill_from_json_string(json.dumps(rows, ensure_ascii=False)):
        unreal.log_error("[HexMigrate] 回填失败，表未改动")
        return

    unreal.EditorAssetLibrary.save_asset(DT_PATH)

    log("")
    log("已迁移 {} 行，跳过 {} 行（已有值）".format(changed, skipped))
    log("⚠️ 请确认美术字段（Visual）没有丢 —— JSON 往返应当保留它们。")
    log("=" * 60)


main()
