from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkSlope"
BLUEPRINT_PATH = "/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK"
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeNaturalWalkCharacter"
SLOPE_DEGREES = 10.0


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def spawn(
    actor_class: type,
    location: unreal.Vector,
    rotation: unreal.Rotator | None = None,
) -> unreal.Actor:
    actor = unreal.get_editor_subsystem(
        unreal.EditorActorSubsystem
    ).spawn_actor_from_class(
        actor_class,
        location,
        rotation or unreal.Rotator(),
    )
    if actor is None:
        raise RuntimeError(f"could not spawn {actor_class}")
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

    # 角色从斜坡低端开始。完整周期约前进 660 cm，坡面留出足够前后余量。
    character = spawn(character_class, unreal.Vector(-400.0, 0.0, 100.0))
    character.set_actor_label("MotionBridge_NaturalWalkSlopeCharacter")

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

    # 10 度足以观察脚掌法线对齐，同时远低于 CharacterMovement 的可行走坡度。
    slope = spawn(
        unreal.StaticMeshActor,
        unreal.Vector(100.0, 0.0, 80.0),
        unreal.Rotator(pitch=SLOPE_DEGREES),
    )
    slope.set_actor_label("MotionBridge_NaturalWalkSlope")
    slope.set_actor_scale3d(unreal.Vector(12.0, 8.0, 0.2))
    slope.get_editor_property("static_mesh_component").set_static_mesh(
        require_asset("/Engine/BasicShapes/Cube")
    )

    light = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 450.0))
    light.set_actor_rotation(unreal.Rotator(pitch=-45.0, yaw=-35.0), False)
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 5.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 300.0))

    # 固定侧视覆盖完整斜坡，不用跟随相机掩盖角色实际上升量。
    camera = spawn(unreal.CameraActor, unreal.Vector(-60.0, -1050.0, 190.0))
    camera.set_actor_rotation(unreal.Rotator(pitch=-5.0, yaw=90.0), False)
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 58.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_NATURAL_WALK_SLOPE_LEVEL={LEVEL_PATH}")
    unreal.log(
        f"MOTIONBRIDGE_NATURAL_WALK_SLOPE_LEVEL=PASS|slope_degrees={SLOPE_DEGREES}"
    )


main()
