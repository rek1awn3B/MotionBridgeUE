from __future__ import annotations

import unreal


RIG_DIRECTORY = "/Game/MotionBridge/Rigs"
SOURCE_DIRECTORY = "/Game/MotionBridge/Source"
SOURCE_MESH_PATH = f"{SOURCE_DIRECTORY}/SK_MotionBridgeSource"
SOURCE_ANIMATION_PATH = f"{SOURCE_DIRECTORY}/A_MotionBridgeSource_Anim"
TARGET_MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

SOURCE_RIG_PATH = f"{RIG_DIRECTORY}/IK_MotionBridgeSource"
TARGET_RIG_PATH = f"{RIG_DIRECTORY}/IK_UE5Manny"
RETARGETER_PATH = f"{RIG_DIRECTORY}/RTG_MotionBridgeToUE5Manny"
RETARGETED_ANIMATION_PATH = (
    "/Game/MotionBridge/Retargeted/A_MotionBridgeSource_Manny"
)
LEGACY_RETARGETED_ANIMATION_PATH = "/Game/A_MotionBridgeSource_Anim_Mannequin"

# 这份 BVH 的参考层级将 Hips 放在原点，但第一帧根通道的竖直位置是
# 98.2948 cm。UE 的 IK Retargeter 使用不带动画的参考姿势做比例校准，
# 因此需要一个独立的源重定向姿势，把 Hips 恢复到动作数据约定的离地高度。
# 这只校准参考姿势，不会删除或烘焙掉动画中的 Root Motion。
SOURCE_GROUNDED_POSE_NAME = "BVH_Grounded"
SOURCE_ROOT_HEIGHT_CM = 98.2948

SOURCE_CHAINS = (
    ("Root", "Hips", "Hips"),
    ("Spine", "Spine", "Spine2"),
    ("Neck", "Neck", "head"),
    ("LeftArm", "LeftShoulder", "LeftHand"),
    ("RightArm", "RightShoulder", "RightHand"),
    ("LeftLeg", "LeftUpLeg", "LeftToeBase"),
    ("RightLeg", "RightUpLeg", "RightToeBase"),
)

TARGET_CHAINS = (
    ("Root", "pelvis", "pelvis"),
    ("Spine", "spine_01", "spine_05"),
    ("Neck", "neck_01", "head"),
    ("LeftArm", "clavicle_l", "hand_l"),
    ("RightArm", "clavicle_r", "hand_r"),
    ("LeftLeg", "thigh_l", "ball_l"),
    ("RightLeg", "thigh_r", "ball_r"),
)


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def create_or_load_asset(
    asset_path: str,
    asset_class: type,
    factory: unreal.Factory,
) -> unreal.Object:
    existing = unreal.load_asset(asset_path)
    if existing is not None:
        return existing

    package_path, asset_name = asset_path.rsplit("/", 1)
    created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        asset_class,
        factory,
    )
    if created is None:
        raise RuntimeError(f"could not create asset: {asset_path}")
    return created


def configure_ik_rig(
    asset_path: str,
    skeletal_mesh: unreal.SkeletalMesh,
    retarget_root: str,
    chains: tuple[tuple[str, str, str], ...],
) -> unreal.IKRigDefinition:
    rig = create_or_load_asset(
        asset_path,
        unreal.IKRigDefinition,
        unreal.IKRigDefinitionFactory(),
    )
    controller = unreal.IKRigController.get_controller(rig)
    if not controller.set_skeletal_mesh(skeletal_mesh):
        raise RuntimeError(f"could not assign mesh to IK Rig: {asset_path}")
    if not controller.set_retarget_root(retarget_root):
        raise RuntimeError(
            f"could not set retarget root {retarget_root}: {asset_path}"
        )

    # 脚本首次运行时创建链；重复运行时保留已有链，便于手工微调后不被覆盖。
    existing_names = {
        str(chain.chain_name)
        for chain in controller.get_retarget_chains()
    }
    for chain_name, start_bone, end_bone in chains:
        if chain_name in existing_names:
            start_set = controller.set_retarget_chain_start_bone(
                chain_name,
                start_bone,
            )
            end_set = controller.set_retarget_chain_end_bone(
                chain_name,
                end_bone,
            )
            if not start_set or not end_set:
                raise RuntimeError(
                    f"could not update chain {chain_name} "
                    f"({start_bone} -> {end_bone})"
                )
        else:
            created_name = controller.add_retarget_chain(
                chain_name,
                start_bone,
                end_bone,
                "",
            )
            if str(created_name) != chain_name:
                raise RuntimeError(
                    f"could not create chain {chain_name} "
                    f"({start_bone} -> {end_bone})"
                )
        unreal.log(
            f"MOTIONBRIDGE_CHAIN={asset_path}|{chain_name}|"
            f"{start_bone}|{end_bone}"
        )
    return rig


def configure_source_grounded_pose(
    controller: unreal.IKRetargeterController,
) -> None:
    """创建可重复执行的源根高度校准姿势，并保留 Default Pose。"""

    source = unreal.RetargetSourceOrTarget.SOURCE
    existing_pose_names = {
        str(pose_name)
        for pose_name in controller.get_retarget_poses(source).keys()
    }
    if SOURCE_GROUNDED_POSE_NAME not in existing_pose_names:
        created_name = str(
            controller.create_retarget_pose(
                SOURCE_GROUNDED_POSE_NAME,
                source,
            )
        )
        if created_name != SOURCE_GROUNDED_POSE_NAME:
            raise RuntimeError(
                "could not create source retarget pose: "
                f"{SOURCE_GROUNDED_POSE_NAME}; got {created_name}"
            )

    if not controller.set_current_retarget_pose(
        SOURCE_GROUNDED_POSE_NAME,
        source,
    ):
        raise RuntimeError(
            "could not select source retarget pose: "
            f"{SOURCE_GROUNDED_POSE_NAME}"
        )

    # 先清空再设置，避免脚本重复运行时累计根偏移。
    controller.reset_retarget_pose(
        SOURCE_GROUNDED_POSE_NAME,
        [],
        source,
    )
    controller.set_root_offset_in_retarget_pose(
        unreal.Vector(0.0, 0.0, SOURCE_ROOT_HEIGHT_CM),
        source,
    )
    unreal.log(
        "MOTIONBRIDGE_SOURCE_RETARGET_POSE="
        f"{SOURCE_GROUNDED_POSE_NAME}|ROOT_Z={SOURCE_ROOT_HEIGHT_CM}"
    )


def main() -> None:
    source_mesh = require_asset(SOURCE_MESH_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)
    source_animation = require_asset(SOURCE_ANIMATION_PATH)

    source_rig = configure_ik_rig(
        SOURCE_RIG_PATH,
        source_mesh,
        "Hips",
        SOURCE_CHAINS,
    )
    target_rig = configure_ik_rig(
        TARGET_RIG_PATH,
        target_mesh,
        "pelvis",
        TARGET_CHAINS,
    )

    retargeter = create_or_load_asset(
        RETARGETER_PATH,
        unreal.IKRetargeter,
        unreal.IKRetargetFactory(),
    )
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    controller.set_preview_mesh(
        unreal.RetargetSourceOrTarget.SOURCE,
        source_mesh,
    )
    controller.set_preview_mesh(
        unreal.RetargetSourceOrTarget.TARGET,
        target_mesh,
    )
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    configure_source_grounded_pose(controller)
    controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)

    unreal.EditorAssetLibrary.save_directory(
        RIG_DIRECTORY,
        only_if_is_dirty=False,
        recursive=True,
    )

    if unreal.EditorAssetLibrary.does_asset_exist(RETARGETED_ANIMATION_PATH):
        if not unreal.EditorAssetLibrary.delete_asset(
            RETARGETED_ANIMATION_PATH
        ):
            raise RuntimeError("could not replace the retargeted animation")

    if unreal.EditorAssetLibrary.does_asset_exist(
        LEGACY_RETARGETED_ANIMATION_PATH
    ):
        if not unreal.EditorAssetLibrary.rename_asset(
            LEGACY_RETARGETED_ANIMATION_PATH,
            RETARGETED_ANIMATION_PATH,
        ):
            raise RuntimeError("could not move the retargeted animation")
        retargeted_path = RETARGETED_ANIMATION_PATH
    else:
        animation_asset_data = unreal.EditorAssetLibrary.find_asset_data(
            SOURCE_ANIMATION_PATH
        )
        retargeted_assets = (
            unreal.IKRetargetBatchOperation.duplicate_and_retarget(
                [animation_asset_data],
                source_mesh,
                target_mesh,
                retargeter,
                "",
                "",
                "",
                "_Manny",
                False,
            )
        )
        if not retargeted_assets:
            raise RuntimeError("batch retarget produced no animation asset")
        generated_path = str(retargeted_assets[0].package_name)
        if not unreal.EditorAssetLibrary.rename_asset(
            generated_path,
            RETARGETED_ANIMATION_PATH,
        ):
            raise RuntimeError(
                f"could not move retargeted animation: {generated_path}"
            )
        retargeted_path = RETARGETED_ANIMATION_PATH

    unreal.EditorAssetLibrary.save_directory(
        "/Game/MotionBridge",
        only_if_is_dirty=False,
        recursive=True,
    )

    # UE 5.5 的 ACL 压缩器会在这份全 Manny 骨骼轨道上触发除零；
    # 原始姿态数据是有限且完整的，因此改用引擎默认压缩器。
    retargeted_animation = require_asset(retargeted_path)
    default_bone_compression = require_asset(
        "/Engine/Animation/DefaultAnimBoneCompressionSettings"
    )
    default_curve_compression = require_asset(
        "/Engine/Animation/DefaultAnimCurveCompressionSettings"
    )
    retargeted_animation.set_editor_property(
        "bone_compression_settings",
        default_bone_compression,
    )
    retargeted_animation.set_editor_property(
        "curve_compression_settings",
        default_curve_compression,
    )
    unreal.EditorAssetLibrary.save_asset(
        retargeted_path,
        only_if_is_dirty=False,
    )

    unreal.log(f"MOTIONBRIDGE_RETARGETED={retargeted_path}")
    unreal.log("MOTIONBRIDGE_RETARGET_SETUP=PASS")


main()
