# Copyright Hex Spire. All Rights Reserved.
#
# 一次性验证脚本：确认数组列真的进了表（不是又静默变成空数组）。
# 跑完即可删；留着也无害，它只读不写。

import json

import unreal

CHECKS = (
    # (表, 列, 至少几行该列非空)
    ("/Game/HexSpire/Data/DT_Cards", "Tags", 13),
    ("/Game/HexSpire/Data/DT_Heroes", "CornerstoneCardIds", 1),
    ("/Game/HexSpire/Data/DT_Heroes", "CardPoolTags", 1),
    ("/Game/HexSpire/Data/DT_Heroes", "PassiveRules", 1),
)


def main():
    failed = False
    for dt_path, col, want_min in CHECKS:
        dt = unreal.load_asset(dt_path)
        rows = json.loads(dt.export_to_json_string())
        non_empty = [r["Name"] for r in rows if r.get(col)]
        ok = len(non_empty) >= want_min
        failed = failed or not ok
        unreal.log("[HexVerifyRT] {} {}.{}: {}/{} 行非空 {}".format(
            "PASS" if ok else "FAIL", dt_path.split("/")[-1], col,
            len(non_empty), len(rows), non_empty))

    # 敌人表：SkillIds 目前内建全空是预期（留空 = profile 默认攻击），
    # 改验数值列有没有正确落进来。
    dt = unreal.load_asset("/Game/HexSpire/Data/DT_Enemies")
    rows = {r["Name"]: r for r in json.loads(dt.export_to_json_string())}
    worm = rows.get("siege_worm", {})
    ok = (worm.get("BaseHP") == 120 and worm.get("KnockbackResistOverride") == 999
          and worm.get("bIsBoss") is True)
    failed = failed or not ok
    unreal.log("[HexVerifyRT] {} DT_Enemies.siege_worm: HP={} KBResist={} Boss={}".format(
        "PASS" if ok else "FAIL", worm.get("BaseHP"),
        worm.get("KnockbackResistOverride"), worm.get("bIsBoss")))

    if failed:
        unreal.log_error("[HexVerifyRT] 往返验证失败")
    else:
        unreal.log("[HexVerifyRT] 全部通过")


main()
