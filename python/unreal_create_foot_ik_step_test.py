from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeFootIKStepTest"
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeRootMotionCharacter"


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def spawn(actor_class: type, location: unreal.Vector) -> unreal.Actor:
    actor = unreal.get_editor_subsystem(
        unreal.EditorActorSubsystem
    ).spawn_actor_from_class(actor_class, location, unreal.Rotator())
    if actor is None:
        raise RuntimeError(f"could not spawn {actor_class}")
    return actor


def add_cube(
    label: str,
    location: unreal.Vector,
    scale: unreal.Vector,
    ignore_pawn: bool = False,
) -> unreal.StaticMeshActor:
    actor = spawn(unreal.StaticMeshActor, location)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(require_asset("/Engine/BasicShapes/Cube"))
    if ignore_pawn:
        # 台阶只作为脚底射线的测试表面，不抬高整个胶囊；否则无法区分
        # “胶囊被碰撞顶起”与“两只脚获得不同 IK 目标”。
        component.set_collision_profile_name("Custom")
        component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
        component.set_collision_response_to_all_channels(
            unreal.CollisionResponseType.ECR_IGNORE
        )
        component.set_collision_response_to_channel(
            unreal.CollisionChannel.ECC_VISIBILITY,
            unreal.CollisionResponseType.ECR_BLOCK,
        )
    return actor


def main() -> None:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not levels.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not load {LEVEL_PATH}")
    elif not levels.new_level(LEVEL_PATH):
        raise RuntimeError(f"could not create {LEVEL_PATH}")

    # 专用自动验收关卡：重复执行时先清掉上一次生成的 Actor，避免多个
    # 台阶、角色或相机叠在一起改变碰撞结果。
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing_actors = actor_subsystem.get_all_level_actors()
    if existing_actors:
        actor_subsystem.destroy_actors(existing_actors)

    character_class = unreal.load_class(None, CHARACTER_CLASS_PATH)
    if character_class is None:
        raise RuntimeError(f"missing C++ class: {CHARACTER_CLASS_PATH}")
    character = spawn(character_class, unreal.Vector(0.0, 0.0, 100.0))
    character.set_actor_label("MotionBridge_FootIKStepCharacter")
    character.set_editor_property("apply_root_motion_to_actor", False)

    # 基础地面顶面 Z=-5；右半平面的测试台顶面 Z=10，高差 15 cm。
    add_cube(
        "MotionBridge_FootIKStepBase",
        unreal.Vector(0.0, 0.0, -10.0),
        unreal.Vector(5.0, 4.0, 0.1),
    )
    add_cube(
        "MotionBridge_FootIKRaisedStep",
        unreal.Vector(0.0, 48.5, 5.0),
        # 高台覆盖 Y=[-3, 100]，边界位于实测左右脚落点之间。
        # X 方向覆盖完整短 Root Motion 路径。
        unreal.Vector(5.0, 1.03, 0.1),
        ignore_pawn=True,
    )

    light = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 300.0))
    light.set_actor_rotation(unreal.Rotator(pitch=-45.0, yaw=-35.0), False)
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 4.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 200.0))
    # 从角色正前方沿世界 -X 直拍；画面横向只对应角色的左右方向，
    # 不再像斜侧相机那样把前后距离投影成“左右交换”。
    camera = spawn(unreal.CameraActor, unreal.Vector(260.0, 0.0, 135.0))
    camera.set_actor_rotation(unreal.Rotator(pitch=-10.0, yaw=180.0), False)
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 75.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_FOOT_IK_STEP_LEVEL={LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_FOOT_IK_STEP_LEVEL=PASS")


main()
