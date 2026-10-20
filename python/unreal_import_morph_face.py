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
        / "motionbridge_face.fbx"
    )
    if not source_fbx.is_file():
        raise RuntimeError(f"source FBX not found: {source_fbx}")

    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_animations = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.create_physics_asset = False
    options.skeletal_mesh_import_data.import_mesh_lo_ds = False
    options.skeletal_mesh_import_data.set_editor_property(
        "import_morph_targets",
        True,
    )

    task = unreal.AssetImportTask()
    task.filename = str(source_fbx)
    task.destination_path = "/Game/MotionBridge/Facial"
    task.destination_name = "SK_MotionBridgeFace"
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = True
    task.options = options

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not task.imported_object_paths:
        raise RuntimeError("UE did not import the Morph Target face FBX")

    unreal.EditorAssetLibrary.save_directory(
        "/Game/MotionBridge/Facial",
        only_if_is_dirty=False,
        recursive=True,
    )
    unreal.log("MOTIONBRIDGE_MORPH_IMPORT=PASS")
    for path in task.imported_object_paths:
        unreal.log(f"MOTIONBRIDGE_MORPH_ASSET={path}")


main()
