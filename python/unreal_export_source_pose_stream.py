from __future__ import annotations

import json
import math
import os

import unreal


ANIMATION_PATH = (
    "/Game/MotionBridge/100StyleSource/"
    "A_MotionBridge100StyleSource_Anim"
)
MESH_PATH = (
    "/Game/MotionBridge/100StyleSource/"
    "SK_MotionBridge100StyleSource"
)
OUTPUT_PATH = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "..",
        "artifacts",
        "realtime",
        "source_reference_stream.json",
    )
)


def vector3(value: unreal.Vector) -> list[float]:
    result = [value.x, value.y, value.z]
    if not all(math.isfinite(component) for component in result):
        raise RuntimeError(f"non-finite vector: {result}")
    return result


def quaternion(value: unreal.Quat) -> list[float]:
    result = [value.x, value.y, value.z, value.w]
    if not all(math.isfinite(component) for component in result):
        raise RuntimeError(f"non-finite quaternion: {result}")
    return result


def main() -> None:
    animation = unreal.load_asset(ANIMATION_PATH)
    mesh = unreal.load_asset(MESH_PATH)
    if animation is None or mesh is None:
        raise RuntimeError("100STYLE source animation or mesh is missing")

    model = animation.data_model_interface
    frame_rate = model.get_frame_rate()
    fps = frame_rate.numerator / frame_rate.denominator
    frame_count = model.get_number_of_frames() + 1

    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = mesh
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True

    frames: list[dict] = []
    bone_names: list[str] | None = None
    for frame_index in range(frame_count):
        pose = animation.get_anim_pose_at_frame(frame_index, options)
        if not pose.is_valid():
            raise RuntimeError(f"invalid pose at frame {frame_index}")

        current_bone_names = [str(name) for name in pose.get_bone_names()]
        if bone_names is None:
            bone_names = current_bone_names
        elif current_bone_names != bone_names:
            raise RuntimeError("bone order changed between source frames")

        translations: list[list[float]] = []
        rotations: list[list[float]] = []
        for bone_name in bone_names:
            transform = pose.get_bone_pose(
                bone_name,
                unreal.AnimPoseSpaces.LOCAL,
            )
            translations.append(vector3(transform.translation))
            rotations.append(quaternion(transform.rotation))

        root_index = bone_names.index("Hips")
        frames.append(
            {
                "frame_index": frame_index,
                "timestamp": frame_index / fps,
                "root_position": translations[root_index],
                "root_rotation": rotations[root_index],
                "bone_translations": translations,
                "bone_rotations": rotations,
            }
        )

    stream = {
        "protocol_version": 1,
        "stream_id": "100style-source-reference",
        "skeleton": "100style-bvh-source",
        "fps": fps,
        "bones": bone_names,
        "frames": frames,
    }
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as output_file:
        json.dump(stream, output_file, separators=(",", ":"))

    unreal.log(
        "MOTIONBRIDGE_SOURCE_STREAM_EXPORT=PASS|"
        f"frames={frame_count}|bones={len(bone_names or [])}|"
        f"fps={fps:.6f}|path={OUTPUT_PATH}"
    )


main()
