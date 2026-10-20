from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridgeDemo"
TARGET_MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
ANIMATION_PATH = (
    "/Game/MotionBridge/Retargeted/A_MotionBridgeSource_Manny"
)


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def spawn_actor(
    actor_class: type,
    location: unreal.Vector,
    rotation: unreal.Rotator | None = None,
) -> unreal.Actor:
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class,
        location,
        rotation or unreal.Rotator(),
    )
    if actor is None:
        raise RuntimeError(f"could not spawn actor: {actor_class}")
    return actor


def main() -> None:
    target_mesh = require_asset(TARGET_MESH_PATH)
    animation = require_asset(ANIMATION_PATH)

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_subsystem.new_level(LEVEL_PATH):
        if not level_subsystem.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not create or load level: {LEVEL_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    generated_labels = {
        "MotionBridge_Manny_Retargeted",
        "MotionBridge_Floor",
        "MotionBridge_KeyLight",
        "MotionBridge_SkyLight",
        "MotionBridge_OverviewCamera",
    }
    generated_actors = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() in generated_labels
    ]
    if generated_actors:
        actor_subsystem.destroy_actors(generated_actors)

    character = spawn_actor(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
    character.set_actor_label("MotionBridge_Manny_Retargeted")
    skeletal_component = character.get_editor_property(
        "skeletal_mesh_component"
    )
    # Manny 自带的后处理动画蓝图依赖完整角色模板环境；本演示只验证重定向骨骼动画。
    skeletal_component.set_editor_property(
        "disable_post_process_blueprint",
        True,
    )
    skeletal_component.set_enable_gravity(False)
    skeletal_component.set_skeletal_mesh_asset(target_mesh)
    # PlayAnimation 只修改瞬时 AnimInstance；OverrideAnimationData 才会随关卡序列化。
    skeletal_component.override_animation_data(
        animation,
        True,
        True,
        0.0,
        1.0,
    )
    unreal.log("MOTIONBRIDGE_DEMO_STEP=character_ready")

    floor_mesh = require_asset("/Engine/BasicShapes/Cube")
    floor = spawn_actor(unreal.StaticMeshActor, unreal.Vector(0, 0, -7.5))
    floor.set_actor_label("MotionBridge_Floor")
    floor.set_actor_scale3d(unreal.Vector(5.0, 5.0, 0.1))
    floor.get_editor_property("static_mesh_component").set_static_mesh(
        floor_mesh
    )
    unreal.log("MOTIONBRIDGE_DEMO_STEP=floor_ready")

    key_light = spawn_actor(
        unreal.DirectionalLight,
        unreal.Vector(0, 0, 300),
        unreal.Rotator(-45, -30, 0),
    )
    key_light.set_actor_label("MotionBridge_KeyLight")
    key_light.get_editor_property("directional_light_component").set_editor_property(
        "intensity",
        4.0,
    )
    unreal.log("MOTIONBRIDGE_DEMO_STEP=key_light_ready")

    sky_light = spawn_actor(unreal.SkyLight, unreal.Vector(0, 0, 200))
    sky_light.set_actor_label("MotionBridge_SkyLight")
    unreal.log("MOTIONBRIDGE_DEMO_STEP=sky_light_ready")

    camera = spawn_actor(
        unreal.CameraActor,
        unreal.Vector(300, 300, 160),
        unreal.Rotator(-10, -135, 0),
    )
    camera.set_actor_label("MotionBridge_OverviewCamera")
    camera.set_editor_property(
        "auto_activate_for_player",
        unreal.AutoReceiveInput.PLAYER0,
    )
    unreal.log("MOTIONBRIDGE_DEMO_STEP=camera_ready")

    if not level_subsystem.save_current_level():
        raise RuntimeError(f"could not save level: {LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_DEMO_STEP=level_saved")

    unreal.log(f"MOTIONBRIDGE_DEMO_LEVEL={LEVEL_PATH}")
    unreal.log(f"MOTIONBRIDGE_DEMO_MESH={target_mesh.get_path_name()}")
    unreal.log(f"MOTIONBRIDGE_DEMO_ANIMATION={animation.get_path_name()}")
    unreal.log("MOTIONBRIDGE_DEMO_LEVEL=PASS")


main()
