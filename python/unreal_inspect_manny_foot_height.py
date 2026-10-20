from __future__ import annotations

import unreal


MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"


def main() -> None:
    """量出 Manny 参考姿势中 foot / ball 骨骼枢轴到参考地面的高度。"""

    mesh = unreal.load_asset(MESH_PATH)
    if mesh is None:
        raise RuntimeError(f"missing skeletal mesh: {MESH_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(
        unreal.EditorActorSubsystem
    )
    actor = actor_subsystem.spawn_actor_from_class(
        unreal.SkeletalMeshActor,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(),
    )
    if actor is None:
        raise RuntimeError("could not spawn temporary skeletal mesh actor")

    try:
        component = actor.get_editor_property("skeletal_mesh_component")
        component.set_skeletal_mesh_asset(mesh)
        component.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        component.set_editor_property("animation_data", unreal.SingleAnimationPlayData())

        left_foot = component.get_socket_location("foot_l")
        right_foot = component.get_socket_location("foot_r")
        left_ball = component.get_socket_location("ball_l")
        right_ball = component.get_socket_location("ball_r")
        left_foot_transform = component.get_socket_transform(
            "foot_l", unreal.RelativeTransformSpace.RTS_WORLD
        )
        right_foot_transform = component.get_socket_transform(
            "foot_r", unreal.RelativeTransformSpace.RTS_WORLD
        )
        unreal.log(
            "MOTIONBRIDGE_MANNY_FOOT_HEIGHT="
            f"left_from_reference_ground={left_foot.z:.3f}|"
            f"right_from_reference_ground={right_foot.z:.3f}|"
            f"left_ball_from_reference_ground={left_ball.z:.3f}|"
            f"right_ball_from_reference_ground={right_ball.z:.3f}|"
            f"left_foot_z={left_foot.z:.3f}|"
            f"right_foot_z={right_foot.z:.3f}|"
            f"left_ball_z={left_ball.z:.3f}|"
            f"right_ball_z={right_ball.z:.3f}"
        )
        unreal.log(
            "MOTIONBRIDGE_MANNY_FOOT_POINTS="
            f"left_foot={left_foot}|left_ball={left_ball}|"
            f"right_foot={right_foot}|right_ball={right_ball}"
        )
        unreal.log(
            "MOTIONBRIDGE_MANNY_FOOT_ROTATIONS="
            f"left={left_foot_transform.rotation}|"
            f"right={right_foot_transform.rotation}"
        )
    finally:
        actor_subsystem.destroy_actor(actor)


main()
