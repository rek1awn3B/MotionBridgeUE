from __future__ import annotations

import unreal


SOURCE_DIRECTORY = "/Game/MotionBridge/Source"
SOURCE_MESH_PATH = f"{SOURCE_DIRECTORY}/SK_MotionBridgeSource"
SOURCE_ANIMATION_PATH = f"{SOURCE_DIRECTORY}/A_MotionBridgeSource_Anim"
TARGET_MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
SOURCE_RIG_PATH = "/Game/MotionBridge/Rigs/IK_MotionBridgeSource"
TARGET_RIG_PATH = "/Game/MotionBridge/Rigs/IK_UE5Manny"
RETARGETER_PATH = "/Game/MotionBridge/Rigs/RTG_MotionBridgeToUE5Manny"
RETARGETED_ANIMATION_PATH = (
    "/Game/MotionBridge/Retargeted/A_MotionBridgeSource_Manny"
)


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def main() -> None:
    source_mesh = require_asset(SOURCE_MESH_PATH)
    source_animation = require_asset(SOURCE_ANIMATION_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)

    assets = unreal.EditorAssetLibrary.list_assets(
        SOURCE_DIRECTORY,
        recursive=True,
        include_folder=False,
    )
    for asset_path in sorted(assets):
        asset = unreal.load_asset(asset_path)
        class_name = asset.get_class().get_name() if asset else "MISSING"
        unreal.log(f"MOTIONBRIDGE_VALIDATED_ASSET={asset_path}|{class_name}")

    duration_seconds = source_animation.get_play_length()
    if duration_seconds <= 0.0:
        raise RuntimeError("imported animation has no duration")

    source_skeleton = source_mesh.get_editor_property("skeleton")
    target_skeleton = target_mesh.get_editor_property("skeleton")
    unreal.log(f"MOTIONBRIDGE_SOURCE_SKELETON_RAW={source_skeleton}")
    unreal.log(f"MOTIONBRIDGE_TARGET_SKELETON_RAW={target_skeleton}")
    if source_skeleton is None or target_skeleton is None:
        raise RuntimeError("source or target mesh has no skeleton")

    unreal.log(f"MOTIONBRIDGE_ANIMATION_SECONDS={duration_seconds:.6f}")
    unreal.log(f"MOTIONBRIDGE_SOURCE_SKELETON={source_skeleton.get_path_name()}")
    unreal.log(f"MOTIONBRIDGE_TARGET_MESH={target_mesh.get_path_name()}")
    unreal.log(f"MOTIONBRIDGE_TARGET_SKELETON={target_skeleton.get_path_name()}")
    unreal.log("MOTIONBRIDGE_IMPORT_VALIDATION=PASS")

    source_rig = require_asset(SOURCE_RIG_PATH)
    target_rig = require_asset(TARGET_RIG_PATH)
    retargeter = require_asset(RETARGETER_PATH)
    retargeted_animation = require_asset(RETARGETED_ANIMATION_PATH)
    retargeted_skeleton = retargeted_animation.get_editor_property("skeleton")
    if retargeted_skeleton != target_skeleton:
        raise RuntimeError("retargeted animation is not bound to Manny's skeleton")

    source_chain_names = {
        str(chain.chain_name)
        for chain in unreal.IKRigController.get_controller(
            source_rig
        ).get_retarget_chains()
    }
    target_chain_names = {
        str(chain.chain_name)
        for chain in unreal.IKRigController.get_controller(
            target_rig
        ).get_retarget_chains()
    }
    if source_chain_names != target_chain_names:
        raise RuntimeError(
            "source and target IK Rigs do not define matching chain names"
        )

    retarget_controller = unreal.IKRetargeterController.get_controller(retargeter)
    for chain_name in sorted(target_chain_names):
        mapped_source = str(retarget_controller.get_source_chain(chain_name))
        if mapped_source != chain_name:
            raise RuntimeError(
                f"target chain {chain_name} maps to {mapped_source}"
            )
        unreal.log(f"MOTIONBRIDGE_CHAIN_MAPPING={chain_name}->{mapped_source}")

    unreal.log(
        "MOTIONBRIDGE_RETARGETED_SECONDS="
        f"{retargeted_animation.get_play_length():.6f}"
    )
    unreal.log(
        f"MOTIONBRIDGE_RETARGETED_SKELETON={retargeted_skeleton.get_path_name()}"
    )
    unreal.log("MOTIONBRIDGE_RETARGET_VALIDATION=PASS")


main()
