from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeFootIKSlopeTest"
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeRootMotionCharacter"


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
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not levels.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not load {LEVEL_PATH}")
    elif not levels.new_level(LEVEL_PATH):
        raise RuntimeError(f"could not create {LEVEL_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing_actors = actor_subsystem.get_all_level_actors()
    if existing_actors:
        actor_subsystem.destroy_actors(existing_actors)

    character_class = unreal.load_class(None, CHARACTER_CLASS_PATH)
    if character_class is None:
        raise RuntimeError(f"missing C++ class: {CHARACTER_CLASS_PATH}")

    character = spawn(character_class, unreal.Vector(0.0, 0.0, 110.0))
    character.set_actor_label("MotionBridge_FootIKSlopeCharacter")
    character.set_editor_property("apply_root_motion_to_actor", False)

    # 12 度斜坡足够让法线明显偏离世界 Up，同时仍在 CharacterMovement
    # 默认可行走角度内。大平面保证整段短 Root Motion 都不会走出边缘。
    slope = spawn(
        unreal.StaticMeshActor,
        unreal.Vector(0.0, 0.0, -10.0),
        unreal.Rotator(pitch=12.0),
    )
    slope.set_actor_label("MotionBridge_FootIKSlope")
    slope.set_actor_scale3d(unreal.Vector(5.0, 4.0, 0.1))
    slope.get_editor_property("static_mesh_component").set_static_mesh(
        require_asset("/Engine/BasicShapes/Cube")
    )

    light = spawn(
        unreal.DirectionalLight,
        unreal.Vector(0.0, 0.0, 300.0),
        unreal.Rotator(pitch=-45.0, yaw=-35.0),
    )
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 4.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 200.0))

    camera = spawn(
        unreal.CameraActor,
        unreal.Vector(185.0, 185.0, 135.0),
        unreal.Rotator(pitch=-10.0, yaw=-135.0),
    )
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 75.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_FOOT_IK_SLOPE_LEVEL={LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_FOOT_IK_SLOPE_LEVEL=PASS")


main()
