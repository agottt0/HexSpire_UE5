# Copyright Hex Spire. All Rights Reserved.
#
# 检查 demo 关卡的配置是否正确。
# 这个脚本【只读】，不修改任何东西。
#
# 用法：
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/check_demo_level.py"

import unreal

MAP_PATH = "/Game/HexSpire/Maps/HexDemo"


def log(msg):
    unreal.log("[HexSpire] {}".format(msg))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP_PATH)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = eas.get_all_level_actors()

    log("=" * 56)
    log("关卡内容：{}".format(MAP_PATH))
    log("=" * 56)

    for a in actors:
        if not a:
            continue
        label = a.get_actor_label()
        cls = a.get_class().get_name()
        loc = a.get_actor_location()
        extra = ""

        # 光源强度
        for comp_cls in (unreal.DirectionalLightComponent,
                         unreal.SkyLightComponent):
            comp = a.get_component_by_class(comp_cls)
            if comp:
                extra = " 强度={:.1f}".format(
                    comp.get_editor_property("intensity"))
                break

        # 相机标签（GameMode 靠它复用）
        tags = a.get_editor_property("tags")
        if tags:
            extra += " 标签={}".format([str(t) for t in tags])

        log("  {:24s} {:22s} ({:.0f},{:.0f},{:.0f}){}".format(
            label, cls, loc.x, loc.y, loc.z, extra))

    # GameMode
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ues.get_editor_world()
    if world:
        ws = world.get_world_settings()
        gm = ws.get_editor_property("default_game_mode")
        log("")
        log("  GameMode: {}".format(gm.get_name() if gm else "（未设置）"))

    log("=" * 56)


main()
