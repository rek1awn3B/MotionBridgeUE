from __future__ import annotations

from pathlib import Path

import unreal


IS_100STYLE = "-MotionBridge100Style" in unreal.SystemLibrary.get_command_line()
if IS_100STYLE:
    DESTINATION = "/Game/MotionBridge/100StyleSource"
    MESH_NAME = "SK_MotionBridge100StyleSource"
    ANIMATION_NAME = "A_MotionBridge100StyleSource"
else:
    DESTINATION = "/Game/MotionBridge/RootMotionSource"
    MESH_NAME = "SK_MotionBridgeRootMotionSource"
    ANIMATION_NAME = "A_MotionBridgeRootMotionSource"
# Blender FBX 中的骨架尺寸元数据能被 UE 正确换算，不能再缩小；但 UE 5.5
# 对动画平移通道仍会多放大 100 倍，所以只对动画数据使用 0.01 补偿。
UE_FBX_MESH_IMPORT_SCALE = 1.0
UE_FBX_ANIMATION_IMPORT_SCALE = 0.01


def main() -> None:
    project_directory = Path(unreal.Paths.project_dir()).resolve()
    repository_directory = project_directory.parents[1]
    if IS_100STYLE:
        source_fbx = (
            repository_directory
            / "artifacts"
            / "candidates"
            / "Neutral_FW_frames_530_694_left_foot_corrected.fbx"
        )
    else:
        source_fbx = (
            repository_directory
            / "build"
            / "day9"
            / "test_freebvh_root_motion.fbx"
        )
    if not source_fbx.is_file():
        raise RuntimeError(f"root-motion FBX not found: {source_fbx}")

    # 该目录只存放此脚本的生成物。先删除再导入，避免 UE 原子重导入沿用
    # 第一次导入时缓存的比例设置。
    if unreal.EditorAssetLibrary.does_directory_exist(DESTINATION):
        if not unreal.EditorAssetLibrary.delete_directory(DESTINATION):
            raise RuntimeError(
                f"failed to clear generated asset directory: {DESTINATION}"
            )

    mesh_options = unreal.FbxImportUI()
    mesh_options.import_mesh = True
    mesh_options.import_animations = True
    mesh_options.import_as_skeletal = True
    mesh_options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    mesh_options.create_physics_asset = False
    mesh_options.skeletal_mesh_import_data.set_editor_property(
        "import_uniform_scale",
        UE_FBX_MESH_IMPORT_SCALE,
    )

    mesh_task = unreal.AssetImportTask()
    mesh_task.filename = str(source_fbx)
    mesh_task.destination_path = DESTINATION
    mesh_task.destination_name = MESH_NAME
    mesh_task.automated = True
    mesh_task.replace_existing = True
    mesh_task.replace_existing_settings = True
    mesh_task.save = True
    mesh_task.options = mesh_options

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_tools.import_asset_tasks([mesh_task])
    if not mesh_task.imported_object_paths:
        raise RuntimeError("UE did not import the root-motion skeletal mesh")

    skeleton_path = f"{DESTINATION}/{MESH_NAME}_Skeleton"
    skeleton = unreal.load_asset(skeleton_path)
    if skeleton is None:
        raise RuntimeError(f"imported skeleton not found: {skeleton_path}")

    animation_options = unreal.FbxImportUI()
    animation_options.import_mesh = False
    animation_options.import_animations = True
    animation_options.import_as_skeletal = False
    animation_options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    animation_options.skeleton = skeleton
    animation_options.anim_sequence_import_data.set_editor_property(
        "import_uniform_scale",
        UE_FBX_ANIMATION_IMPORT_SCALE,
    )

    animation_task = unreal.AssetImportTask()
    animation_task.filename = str(source_fbx)
    animation_task.destination_path = DESTINATION
    animation_task.destination_name = ANIMATION_NAME
    animation_task.automated = True
    animation_task.replace_existing = True
    animation_task.replace_existing_settings = True
    animation_task.save = True
    animation_task.options = animation_options
    asset_tools.import_asset_tasks([animation_task])
    if not animation_task.imported_object_paths:
        raise RuntimeError("UE did not import the root-motion animation")

    unreal.EditorAssetLibrary.save_directory(
        DESTINATION,
        only_if_is_dirty=False,
        recursive=True,
    )
    for asset_path in (
        list(mesh_task.imported_object_paths)
        + list(animation_task.imported_object_paths)
    ):
        unreal.log(f"MOTIONBRIDGE_ROOT_MOTION_ASSET={asset_path}")
    unreal.log("MOTIONBRIDGE_ROOT_MOTION_IMPORT=PASS")


main()
