from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkFlat"
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


def main() -> None:
    require_asset(BLUEPRINT_PATH)
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
    character = spawn(character_class, unreal.Vector(-330.0, 0.0, 96.0))
    character.set_actor_label("MotionBridge_NaturalWalkFlatCharacter")

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

    # 单一完整平面，只验证动画步频、Actor 前进速度和脚接触；暂不混入台阶。
    add_cube(
        "MotionBridge_NaturalWalkFlatGround",
        unreal.Vector(0.0, 0.0, -10.0),
        unreal.Vector(9.0, 4.0, 0.2),
    )

    light = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 400.0))
    light.set_actor_rotation(unreal.Rotator(pitch=-50.0, yaw=-35.0), False)
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 5.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 250.0))

    # 严格侧视一轮完整步态，避免透视把前后距离误读成左右滑动。
    camera = spawn(unreal.CameraActor, unreal.Vector(0.0, -900.0, 135.0))
    camera.set_actor_rotation(unreal.Rotator(pitch=-3.0, yaw=90.0), False)
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 52.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_NATURAL_WALK_FLAT_LEVEL={LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_NATURAL_WALK_FLAT_LEVEL=PASS")


main()
