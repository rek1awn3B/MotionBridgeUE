from __future__ import annotations

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Vector


def coordinate_extent(coordinates: list[Vector]) -> tuple[float, float, float]:
    minimum = Vector(
        (min(value[index] for value in coordinates) for index in range(3))
    )
    maximum = Vector(
        (max(value[index] for value in coordinates) for index in range(3))
    )
    return tuple(round(value, 4) for value in maximum - minimum)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_fbx", type=Path)
    parser.add_argument("--expected-bones", type=int, default=55)
    parser.add_argument("--expected-root-name")
    parser.add_argument("--expected-samples", type=int, default=69)
    if "--" not in sys.argv:
        raise RuntimeError("pass script arguments after Blender's -- separator")
    arguments = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    result = bpy.ops.import_scene.fbx(filepath=str(arguments.source_fbx.resolve()))
    if "FINISHED" not in result:
        raise RuntimeError(f"FBX import failed: {result}")

    armatures = [
        scene_object
        for scene_object in bpy.context.scene.objects
        if scene_object.type == "ARMATURE"
    ]
    if len(armatures) != 1:
        raise RuntimeError(
            f"expected exactly one armature, got {len(armatures)}"
        )

    armature = armatures[0]
    mesh_objects = [
        scene_object
        for scene_object in bpy.context.scene.objects
        if scene_object.type == "MESH"
    ]
    if len(mesh_objects) != 1:
        raise RuntimeError(
            f"expected exactly one debug mesh, got {len(mesh_objects)}"
        )
    debug_mesh = mesh_objects[0]
    action = armature.animation_data.action
    if action is None:
        raise RuntimeError("reimported FBX has no animation action")

    raw_frame_range = tuple(float(value) for value in action.frame_range)
    frame_start = round(raw_frame_range[0])
    frame_end = round(raw_frame_range[1])
    bpy.context.scene.frame_set(frame_start)
    bpy.context.view_layer.update()
    dimensions = tuple(round(value, 4) for value in armature.dimensions)
    rest_extent = coordinate_extent(
        [
            coordinate
            for bone in armature.data.bones
            for coordinate in (bone.head_local, bone.tail_local)
        ]
    )
    pose_extent = coordinate_extent(
        [
            armature.matrix_world @ coordinate
            for bone in armature.pose.bones
            for coordinate in (bone.head, bone.tail)
        ]
    )
    joint_head_extent = coordinate_extent(
        [
            armature.matrix_world @ bone.head
            for bone in armature.pose.bones
        ]
    )

    if len(armature.data.bones) != arguments.expected_bones:
        raise RuntimeError(
            f"expected {arguments.expected_bones} bones, "
            f"got {len(armature.data.bones)}"
        )
    frame_interval_count = frame_end - frame_start
    expected_interval_count = arguments.expected_samples - 1
    if frame_interval_count != expected_interval_count:
        raise RuntimeError(
            f"expected {expected_interval_count} frame intervals for "
            f"{arguments.expected_samples} samples, got "
            f"{frame_interval_count}; raw={raw_frame_range}"
        )

    root_bones = [
        bone for bone in armature.data.bones if bone.parent is None
    ]
    if len(root_bones) != 1:
        raise RuntimeError(
            f"expected exactly one hierarchy root, got {len(root_bones)}"
        )
    hierarchy_root = root_bones[0]
    if (
        arguments.expected_root_name is not None
        and hierarchy_root.name != arguments.expected_root_name
    ):
        raise RuntimeError(
            f"expected hierarchy root {arguments.expected_root_name!r}, "
            f"got {hierarchy_root.name!r}"
        )

    root_positions: list[Vector] = []
    for frame_index in range(frame_start, frame_end + 1):
        bpy.context.scene.frame_set(frame_index)
        bpy.context.view_layer.update()
        root_positions.append(
            (armature.matrix_world @ armature.pose.bones[hierarchy_root.name].head)
            .copy()
        )
    root_motion_extent = coordinate_extent(root_positions)

    print(f"FBX_ARMATURE={armature.name}")
    print(f"FBX_BONE_COUNT={len(armature.data.bones)}")
    print(f"FBX_DEBUG_MESH_VERTICES={len(debug_mesh.data.vertices)}")
    print(f"FBX_DEBUG_MESH_FACES={len(debug_mesh.data.polygons)}")
    print(f"FBX_RAW_FRAME_RANGE={raw_frame_range}")
    print(f"FBX_FRAME_RANGE={frame_start}-{frame_end}")
    print(f"FBX_FRAME_SAMPLE_COUNT={frame_interval_count + 1}")
    print(f"FBX_OBJECT_SCALE={tuple(round(value, 4) for value in armature.scale)}")
    print(f"FBX_REST_EXTENT={rest_extent}")
    print(f"FBX_POSE_EXTENT={pose_extent}")
    print(f"FBX_JOINT_HEAD_EXTENT={joint_head_extent}")
    print(f"FBX_DIMENSIONS={dimensions}")
    print(f"FBX_HIERARCHY_ROOT={hierarchy_root.name}")
    print(f"FBX_ROOT_MOTION_EXTENT={root_motion_extent}")
    print("FBX_VALIDATION=PASS")


if __name__ == "__main__":
    main()
