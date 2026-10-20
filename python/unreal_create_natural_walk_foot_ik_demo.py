from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkFootIK"
SOURCE_BLUEPRINT_PATH = "/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK"
BLUEPRINT_PATH = "/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK"
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeNaturalWalkCharacter"


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
) -> unreal.StaticMeshActor:
    actor = spawn(unreal.StaticMeshActor, location)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    actor.get_editor_property("static_mesh_component").set_static_mesh(
        require_asset("/Engine/BasicShapes/Cube")
    )
    return actor


def ensure_natural_walk_blueprint() -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(BLUEPRINT_PATH):
        return
    if not unreal.EditorAssetLibrary.duplicate_asset(
        SOURCE_BLUEPRINT_PATH,
        BLUEPRINT_PATH,
    ):
        raise RuntimeError("could not duplicate natural-walk AnimBlueprint")
    unreal.EditorAssetLibrary.save_asset(BLUEPRINT_PATH)


def main() -> None:
    ensure_natural_walk_blueprint()

    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not levels.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not load {LEVEL_PATH}")
    elif not levels.new_level(LEVEL_PATH):
        raise RuntimeError(f"could not create {LEVEL_PATH}")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = actors.get_all_level_actors()
    if existing:
        actors.destroy_actors(existing)

    character_class = unreal.load_class(None, CHARACTER_CLASS_PATH)
    if character_class is None:
        raise RuntimeError(f"missing C++ class: {CHARACTER_CLASS_PATH}")
    character = spawn(character_class, unreal.Vector(-170.0, 0.0, 96.0))
    character.set_actor_label("MotionBridge_NaturalWalkFootIKCharacter")

    # 第一次创建动画蓝图时，角色类默认对象可能早于资产加载。这里显式设置，
    # 保证同一次无界面执行也能把正确 AnimInstance 类保存进关卡。
    generated_class = unreal.load_class(
        None,
        f"{BLUEPRINT_PATH}.{BLUEPRINT_PATH.rsplit('/', 1)[-1]}_C",
    )
    if generated_class is None:
        raise RuntimeError("natural-walk AnimBlueprint generated class is missing")
    mesh = character.get_editor_property("mesh")
    mesh.set_anim_instance_class(generated_class)
    mesh.set_relative_location(unreal.Vector(0.0, 0.0, -96.0), False, False)

    for component in character.get_components_by_class(unreal.ActorComponent):
        if component.get_name() == "FootIKComponent":
            component.set_editor_property("draw_debug", False)

    # 地面顶面 Z=0。角色从 X=-170 向 +X 行走，在 X=80 处踏上完整宽度的
    # 10 cm 台阶；不再把竖直棱边固定夹在两脚或同一只脚的前后掌之间。
    add_cube(
        "MotionBridge_NaturalWalkBase",
        unreal.Vector(40.0, 0.0, -10.0),
        unreal.Vector(6.0, 4.0, 0.2),
    )
    add_cube(
        "MotionBridge_NaturalWalkStep",
        unreal.Vector(290.0, 0.0, 5.0),
        unreal.Vector(4.2, 4.0, 0.1),
    )

    light = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 400.0))
    light.set_actor_rotation(unreal.Rotator(pitch=-50.0, yaw=-35.0), False)
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 5.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 250.0))

    # 侧前方观察整段步态，能同时看到前进、上台阶以及脚跟/前掌接触。
    camera = spawn(unreal.CameraActor, unreal.Vector(120.0, -520.0, 145.0))
    camera.set_actor_rotation(unreal.Rotator(pitch=-7.0, yaw=88.0), False)
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 62.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_NATURAL_WALK_LEVEL={LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_NATURAL_WALK_LEVEL=PASS")


main()
