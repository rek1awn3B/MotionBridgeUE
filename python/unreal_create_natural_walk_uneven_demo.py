from __future__ import annotations

import math

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkUneven"
BLUEPRINT_PATH = "/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK"
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeNaturalWalkCharacter"
SURFACE_THICKNESS = 20.0


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


def add_surface_segment(
    label: str,
    start_x: float,
    length: float,
    start_height: float,
    pitch_degrees: float,
) -> tuple[float, float]:
    """增加一段连续表面，并返回下一段的起点和高度。"""
    pitch_radians = math.radians(pitch_degrees)
    end_height = start_height + math.tan(pitch_radians) * length
    top_center_height = 0.5 * (start_height + end_height)
    actor_center_height = (
        top_center_height
        - 0.5 * SURFACE_THICKNESS * math.cos(pitch_radians)
    )

    surface = spawn(
        unreal.StaticMeshActor,
        unreal.Vector(start_x + 0.5 * length, 0.0, actor_center_height),
        unreal.Rotator(pitch=pitch_degrees),
    )
    surface.set_actor_label(label)
    surface.set_actor_scale3d(
        unreal.Vector(length / 100.0, 8.0, SURFACE_THICKNESS / 100.0)
    )
    surface.get_editor_property("static_mesh_component").set_static_mesh(
        require_asset("/Engine/BasicShapes/Cube")
    )
    return start_x + length, end_height


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
    character = spawn(character_class, unreal.Vector(-420.0, 0.0, 98.0))
    character.set_actor_label("MotionBridge_NaturalWalkUnevenCharacter")

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

    # 两个约 4.9 cm 高/深的小起伏。每个斜面长 70 cm、坡度 4 度，
    # 只考查地形贴合，不要求普通走路动画生成额外抬腿轨迹。
    x = -470.0
    height = 0.0
    segments = [
        ("FlatLeadIn", 120.0, 0.0),
        ("SmallRise", 70.0, 4.0),
        ("SmallFall", 70.0, -4.0),
        ("FlatMiddle", 100.0, 0.0),
        ("SmallDip", 70.0, -4.0),
        ("SmallRecovery", 70.0, 4.0),
        ("FlatExit", 350.0, 0.0),
    ]
    for name, length, pitch in segments:
        x, height = add_surface_segment(
            f"MotionBridge_Uneven_{name}",
            x,
            length,
            height,
            pitch,
        )

    light = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 420.0))
    light.set_actor_rotation(unreal.Rotator(pitch=-50.0, yaw=-35.0), False)
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 5.0
    )
    spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 280.0))

    camera = spawn(unreal.CameraActor, unreal.Vector(-80.0, -980.0, 145.0))
    camera.set_actor_rotation(unreal.Rotator(pitch=-3.0, yaw=90.0), False)
    camera.get_editor_property("camera_component").set_editor_property(
        "field_of_view", 56.0
    )
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )

    if not levels.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_NATURAL_WALK_UNEVEN_LEVEL={LEVEL_PATH}")
    unreal.log(
        "MOTIONBRIDGE_NATURAL_WALK_UNEVEN_LEVEL=PASS|"
        "amplitude_cm=4.895|slope_degrees=4.0"
    )


main()
