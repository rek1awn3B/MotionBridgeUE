from __future__ import annotations

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))

from blender_bvh_to_fbx import clear_scene, import_bvh


def world_head(armature: bpy.types.Object, bone_name: str) -> Vector:
    return armature.matrix_world @ armature.pose.bones[bone_name].head


def create_stick_mesh(armature: bpy.types.Object) -> bpy.types.Object:
    bones = [bone for bone in armature.data.bones if bone.name != "MotionRoot"]
    vertices = [tuple(world_head(armature, bone.name)) for bone in bones]
    bone_indices = {bone.name: index for index, bone in enumerate(bones)}
    edges: list[tuple[int, int]] = []
    for bone in bones:
        if bone.parent is not None and bone.parent.name in bone_indices:
            edges.append((bone_indices[bone.parent.name], bone_indices[bone.name]))

    mesh = bpy.data.meshes.new("MotionBridge_SourceStick_Mesh")
    mesh.from_pydata(vertices, edges, [])
    mesh.update()
    stick = bpy.data.objects.new("MotionBridge_SourceStick", mesh)
    bpy.context.collection.objects.link(stick)

    skin_modifier = stick.modifiers.new("MotionBridge_Skin", "SKIN")
    bpy.context.view_layer.objects.active = stick
    stick.select_set(True)
    skin_vertices = stick.data.skin_vertices[0].data
    for vertex in skin_vertices:
        vertex.radius = (0.025, 0.025)

    material = bpy.data.materials.new("MotionBridge_SourceStick_Material")
    material.diffuse_color = (0.03, 0.75, 1.0, 1.0)
    stick.data.materials.append(material)

    def update_stick_mesh(_scene: bpy.types.Scene) -> None:
        for bone in bones:
            stick.data.vertices[bone_indices[bone.name]].co = world_head(
                armature, bone.name
            )
        stick.data.update()

    bpy.app.handlers.frame_change_pre.append(update_stick_mesh)
    return stick


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_bvh", type=Path)
    parser.add_argument("output_directory", type=Path)
    if "--" not in sys.argv:
        raise RuntimeError("pass script arguments after Blender's -- separator")
    arguments = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])

    output_directory = arguments.output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)

    clear_scene()
    armature = import_bvh(arguments.source_bvh.resolve())
    create_stick_mesh(armature)
    scene = bpy.context.scene
    action = armature.animation_data.action
    if action is None:
        raise RuntimeError("imported BVH has no action")
    scene.frame_start = int(action.frame_range[0])
    scene.frame_end = int(action.frame_range[1])
    scene.render.fps = round(scene.render.fps / scene.render.fps_base)
    scene.render.fps_base = 1.0

    scene.frame_set(scene.frame_start)
    start_hips = world_head(armature, "Hips")
    scene.frame_set(scene.frame_end)
    end_hips = world_head(armature, "Hips")
    travel = end_hips - start_hips
    travel.z = 0.0
    travel.normalize()
    scene.frame_set((scene.frame_start + scene.frame_end) // 2)
    middle_joints = [
        world_head(armature, bone.name)
        for bone in armature.data.bones
        if bone.name != "MotionRoot"
    ]
    minimum_z = min(position.z for position in middle_joints)
    maximum_z = max(position.z for position in middle_joints)
    center = (start_hips + end_hips) * 0.5
    center.z = (minimum_z + maximum_z) * 0.5

    camera_data = bpy.data.cameras.new("MotionBridge_SourceCamera")
    camera = bpy.data.objects.new("MotionBridge_SourceCamera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = center + travel * 4.0
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = (maximum_z - minimum_z) * 1.25
    scene.camera = camera

    bpy.ops.mesh.primitive_plane_add(
        size=12.0,
        location=(center.x, center.y, minimum_z - 0.01),
    )
    plane = bpy.context.object
    plane_material = bpy.data.materials.new("MotionBridge_SourceFloor")
    plane_material.diffuse_color = (0.12, 0.12, 0.12, 1.0)
    plane.data.materials.append(plane_material)

    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.background_type = "WORLD"
    scene.render.resolution_x = 720
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(output_directory / "SourceFrame")
    scene.frame_set(scene.frame_start)
    bpy.ops.render.render(animation=True)
    print(f"SOURCE_PREVIEW={output_directory}")


if __name__ == "__main__":
    main()
