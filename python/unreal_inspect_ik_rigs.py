from __future__ import annotations

import unreal


RIG_PATHS = (
    "/Game/Characters/Mannequins/Rigs/IK_Mannequin",
    "/Game/MotionBridge/100StyleRigs/IK_MotionBridge100StyleSource",
    "/Game/MotionBridge/100StyleRigs/IK_UE5Manny100Style",
)


def vector(value: unreal.Vector) -> str:
    return f"({value.x:.4f},{value.y:.4f},{value.z:.4f})"


def inspect_rig(path: str) -> None:
    rig = unreal.load_asset(path)
    if rig is None:
        unreal.log_warning(f"MOTIONBRIDGE_IK_RIG_MISSING={path}")
        return

    controller = unreal.IKRigController.get_controller(rig)
    unreal.log(
        f"MOTIONBRIDGE_IK_RIG={path}|root={controller.get_retarget_root()}|"
        f"solvers={controller.get_num_solvers()}"
    )
    for index in range(controller.get_num_solvers()):
        solver = controller.get_solver_controller(index)
        unreal.log(
            "MOTIONBRIDGE_IK_SOLVER="
            f"{path}|{index}|class={solver.get_class().get_name()}|"
            f"start={controller.get_start_bone(index)}|"
            f"end={controller.get_end_bone(index)}|"
            f"enabled={controller.get_solver_enabled(index)}"
        )

    for goal in controller.get_all_goals():
        name = str(goal.get_editor_property("goal_name"))
        bone = str(controller.get_bone_for_goal(name))
        connected = [
            str(index)
            for index in range(controller.get_num_solvers())
            if controller.is_goal_connected_to_solver(name, index)
        ]
        unreal.log(
            f"MOTIONBRIDGE_IK_GOAL={path}|{name}|bone={bone}|"
            f"solvers={','.join(connected)}"
        )

    for chain in controller.get_retarget_chains():
        chain_name = str(chain.get_editor_property("chain_name"))
        start = str(chain.get_editor_property("start_bone"))
        end = str(chain.get_editor_property("end_bone"))
        goal = str(chain.get_editor_property("ik_goal_name"))
        unreal.log(
            f"MOTIONBRIDGE_IK_CHAIN={path}|{chain_name}|"
            f"{start}|{end}|goal={goal}"
        )

    for bone in (
        "pelvis",
        "thigh_l",
        "calf_l",
        "foot_l",
        "ball_l",
        "thigh_r",
        "calf_r",
        "foot_r",
        "ball_r",
        "Hips",
        "LeftHip",
        "LeftKnee",
        "LeftAnkle",
        "LeftToe",
        "RightHip",
        "RightKnee",
        "RightAnkle",
        "RightToe",
    ):
        transform = controller.get_ref_pose_transform_of_bone(bone)
        if not transform.translation.is_nearly_zero() or bone in {"pelvis", "Hips"}:
            unreal.log(
                f"MOTIONBRIDGE_IK_REF={path}|{bone}|"
                f"position={vector(transform.translation)}"
            )


for rig_path in RIG_PATHS:
    inspect_rig(rig_path)


retargeter = unreal.load_asset(
    "/Game/MotionBridge/100StyleRigs/RTG_MotionBridge100StyleToManny"
)
if retargeter is not None:
    retarget_controller = unreal.IKRetargeterController.get_controller(retargeter)
    for side in (
        unreal.RetargetSourceOrTarget.SOURCE,
        unreal.RetargetSourceOrTarget.TARGET,
    ):
        unreal.log(
            f"MOTIONBRIDGE_RETARGET_POSE={side}|"
            f"{retarget_controller.get_current_retarget_pose_name(side)}"
        )
        for bone in (
            "LeftHip",
            "LeftKnee",
            "LeftAnkle",
            "RightHip",
            "RightKnee",
            "RightAnkle",
            "thigh_l",
            "calf_l",
            "foot_l",
            "thigh_r",
            "calf_r",
            "foot_r",
        ):
            quat = retarget_controller.get_rotation_offset_for_retarget_pose_bone(
                bone, side
            )
            rotator = quat.rotator()
            if (
                abs(rotator.roll) > 0.0001
                or abs(rotator.pitch) > 0.0001
                or abs(rotator.yaw) > 0.0001
            ):
                unreal.log(
                    f"MOTIONBRIDGE_RETARGET_OFFSET={side}|{bone}|"
                    f"pitch={rotator.pitch:.5f}|yaw={rotator.yaw:.5f}|"
                    f"roll={rotator.roll:.5f}"
                )
