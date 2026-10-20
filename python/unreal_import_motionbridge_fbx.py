from __future__ import annotations

from pathlib import Path

import unreal


def main() -> None:
    project_directory = Path(unreal.Paths.project_dir()).resolve()
    repository_directory = project_directory.parents[1]
    source_fbx = (
        repository_directory
        / "artifacts"
        / "blender"
        / "test_freebvh.fbx"
    )
    if not source_fbx.is_file():
        raise RuntimeError(f"source FBX not found: {source_fbx}")

    import_options = unreal.FbxImportUI()
    import_options.import_mesh = True
    import_options.import_animations = True
    import_options.import_as_skeletal = True
    import_options.mesh_type_to_import = (
        unreal.FBXImportType.FBXIT_SKELETAL_MESH
    )
    import_options.create_physics_asset = False
    import_options.skeletal_mesh_import_data.import_mesh_lo_ds = False
    import_task = unreal.AssetImportTask()
    import_task.filename = str(source_fbx)
    import_task.destination_path = "/Game/MotionBridge/Source"
    import_task.destination_name = "SK_MotionBridgeSource"
    import_task.automated = True
    import_task.replace_existing = True
    import_task.replace_existing_settings = True
    import_task.save = True
    import_task.options = import_options

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_tools.import_asset_tasks([import_task])

    mesh_paths = list(import_task.imported_object_paths)
    if not mesh_paths:
        raise RuntimeError("UE did not produce any assets from the FBX")

    skeleton_path = (
        "/Game/MotionBridge/Source/"
        "SK_MotionBridgeSource_Skeleton.SK_MotionBridgeSource_Skeleton"
    )
    skeleton = unreal.load_asset(skeleton_path)
    if skeleton is None:
        raise RuntimeError(f"imported skeleton not found: {skeleton_path}")

    animation_options = unreal.FbxImportUI()
    animation_options.import_mesh = False
    animation_options.import_animations = True
    animation_options.import_as_skeletal = False
    animation_options.mesh_type_to_import = (
        unreal.FBXImportType.FBXIT_ANIMATION
    )
    animation_options.skeleton = skeleton

    animation_task = unreal.AssetImportTask()
    animation_task.filename = str(source_fbx)
    animation_task.destination_path = "/Game/MotionBridge/Source"
    animation_task.destination_name = "A_MotionBridgeSource"
    animation_task.automated = True
    animation_task.replace_existing = True
    animation_task.replace_existing_settings = True
    animation_task.save = True
    animation_task.options = animation_options
    asset_tools.import_asset_tasks([animation_task])

    animation_paths = list(animation_task.imported_object_paths)
    if not animation_paths:
        raise RuntimeError("UE did not produce an animation from the FBX")

    imported_paths = mesh_paths + animation_paths

    unreal.EditorAssetLibrary.save_directory(
        "/Game/MotionBridge/Source",
        only_if_is_dirty=False,
        recursive=True,
    )

    unreal.log("MOTIONBRIDGE_IMPORT=PASS")
    for imported_path in imported_paths:
        unreal.log(f"MOTIONBRIDGE_ASSET={imported_path}")


main()
