from __future__ import annotations

import unreal


IS_100STYLE = "-MotionBridge100Style" in unreal.SystemLibrary.get_command_line()
IS_SFU_WALK = "-MotionBridgeSFUWalk" in unreal.SystemLibrary.get_command_line()
DISABLE_AUTO_ALIGN = (
    "-MotionBridgeNoAutoAlign" in unreal.SystemLibrary.get_command_line()
)
LEG_ONE_TO_ONE = (
    "-MotionBridgeLegOneToOne" in unreal.SystemLibrary.get_command_line()
)
if IS_SFU_WALK:
    RIG_DIRECTORY = "/Game/MotionBridge/SFUWalkRigs"
    SOURCE_DIRECTORY = "/Game/MotionBridge/SFUWalkSource"
    SOURCE_MESH_PATH = f"{SOURCE_DIRECTORY}/SK_MotionBridgeSFUWalkSource"
    SOURCE_ANIMATION_PATH = f"{SOURCE_DIRECTORY}/A_MotionBridgeSFUWalkSource_Anim"
elif IS_100STYLE:
    RIG_DIRECTORY = "/Game/MotionBridge/100StyleRigs"
    SOURCE_DIRECTORY = "/Game/MotionBridge/100StyleSource"
    SOURCE_MESH_PATH = f"{SOURCE_DIRECTORY}/SK_MotionBridge100StyleSource"
    SOURCE_ANIMATION_PATH = f"{SOURCE_DIRECTORY}/A_MotionBridge100StyleSource_Anim"
else:
    RIG_DIRECTORY = "/Game/MotionBridge/RootMotionRigs"
    SOURCE_DIRECTORY = "/Game/MotionBridge/RootMotionSource"
    SOURCE_MESH_PATH = f"{SOURCE_DIRECTORY}/SK_MotionBridgeRootMotionSource"
    SOURCE_ANIMATION_PATH = f"{SOURCE_DIRECTORY}/A_MotionBridgeRootMotionSource_Anim"
TARGET_MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

if IS_SFU_WALK:
    SOURCE_RIG_PATH = f"{RIG_DIRECTORY}/IK_MotionBridgeSFUWalkSource"
    TARGET_RIG_PATH = f"{RIG_DIRECTORY}/IK_UE5MannySFUWalk"
    RETARGETER_PATH = f"{RIG_DIRECTORY}/RTG_MotionBridgeSFUWalkToManny"
    OUTPUT_ANIMATION_PATH = (
        "/Game/MotionBridge/SFUWalkRetargeted/A_MotionBridgeSFUWalk_Manny"
    )
elif IS_100STYLE:
    SOURCE_RIG_PATH = f"{RIG_DIRECTORY}/IK_MotionBridge100StyleSource"
    TARGET_RIG_PATH = f"{RIG_DIRECTORY}/IK_UE5Manny100Style"
    RETARGETER_PATH = f"{RIG_DIRECTORY}/RTG_MotionBridge100StyleToManny"
    OUTPUT_ANIMATION_PATH = (
        "/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny"
    )
else:
    SOURCE_RIG_PATH = f"{RIG_DIRECTORY}/IK_MotionBridgeRootMotionSource"
    TARGET_RIG_PATH = f"{RIG_DIRECTORY}/IK_UE5MannyRootMotion"
    RETARGETER_PATH = f"{RIG_DIRECTORY}/RTG_MotionBridgeRootMotionToManny"
    OUTPUT_ANIMATION_PATH = (
        "/Game/MotionBridge/RootMotionRetargeted/"
        "A_MotionBridgeRootMotion_Manny"
    )

# Hips/pelvis 是身体重定向根，由 Root 阶段专门处理，不能再建成
# 单骨骼 FK 链；否则 FK 阶段会覆盖 Root 阶段刚算好的 pelvis 变换。
# MotionRoot/root 则交给 UE 5.5 的 Root Motion Generator 后处理节点。
LEGACY_SOURCE_CHAINS = (
    ("Spine", "Spine", "Spine2"),
    ("Neck", "Neck", "head"),
    ("LeftArm", "LeftShoulder", "LeftHand"),
    ("RightArm", "RightShoulder", "RightHand"),
    ("LeftLeg", "LeftUpLeg", "LeftToeBase"),
    ("RightLeg", "RightUpLeg", "RightToeBase"),
)

HUNDRED_STYLE_SOURCE_CHAINS = (
    ("Spine", "Chest", "Chest4"),
    ("Neck", "Neck", "Head"),
    ("LeftArm", "LeftCollar", "LeftWrist"),
    ("RightArm", "RightCollar", "RightWrist"),
    ("LeftLeg", "LeftHip", "LeftToe"),
    ("RightLeg", "RightHip", "RightToe"),
)

SFU_SOURCE_CHAINS = (
    ("Spine", "Spine", "Spine1"),
    ("Neck", "Neck", "Head"),
    ("LeftArm", "LeftShoulder", "LeftHand"),
    ("RightArm", "RightShoulder", "RightHand"),
    ("LeftLeg", "LeftUpLeg", "LeftToeBase"),
    ("RightLeg", "RightUpLeg", "RightToeBase"),
)

SOURCE_CHAINS = (
    SFU_SOURCE_CHAINS
    if IS_SFU_WALK
    else HUNDRED_STYLE_SOURCE_CHAINS
    if IS_100STYLE
    else LEGACY_SOURCE_CHAINS
)

TARGET_CHAINS = (
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


def recreate_asset(
    asset_path: str,
    asset_class: type,
    factory: unreal.Factory,
) -> unreal.Object:
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        if not unreal.EditorAssetLibrary.delete_asset(asset_path):
            raise RuntimeError(f"could not replace generated asset: {asset_path}")
    package_path, asset_name = asset_path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        asset_class,
        factory,
    )
    if asset is None:
        raise RuntimeError(f"could not create asset: {asset_path}")
    return asset


def create_ik_rig(
    asset_path: str,
    skeletal_mesh: unreal.SkeletalMesh,
    retarget_root: str,
    chains: tuple[tuple[str, str, str], ...],
) -> unreal.IKRigDefinition:
    rig = recreate_asset(
        asset_path,
        unreal.IKRigDefinition,
        unreal.IKRigDefinitionFactory(),
    )
    controller = unreal.IKRigController.get_controller(rig)
    if not controller.set_skeletal_mesh(skeletal_mesh):
        raise RuntimeError(f"could not assign skeletal mesh: {asset_path}")
    if not controller.set_retarget_root(retarget_root):
        raise RuntimeError(
            f"could not set retarget root {retarget_root}: {asset_path}"
        )
    for chain_name, start_bone, end_bone in chains:
        created_name = str(
            controller.add_retarget_chain(
                chain_name,
                start_bone,
                end_bone,
                "",
            )
        )
        if created_name != chain_name:
            raise RuntimeError(
                f"could not create chain {chain_name}: "
                f"{start_bone} -> {end_bone}; got {created_name}"
            )
        unreal.log(
            f"MOTIONBRIDGE_ROOT_CHAIN={asset_path}|{chain_name}|"
            f"{start_bone}|{end_bone}"
        )
    return rig


def configure_retarget_ops(
    controller: unreal.IKRetargeterController,
) -> None:
    """配置 UE 5.5 与 5.6 两套重定向操作栈接口。"""
    controller.remove_all_ops()

    if hasattr(unreal, "RootMotionGeneratorOp"):
        # UE 5.5：普通身体重定向仍由旧的固定阶段完成，操作栈中只需要
        # 额外加入复制 MotionRoot 的后处理节点。
        root_op_index = controller.add_retarget_op(
            unreal.RootMotionGeneratorOp
        )
        if root_op_index < 0:
            raise RuntimeError("could not add Root Motion Generator op")
        root_op = controller.get_retarget_op_at_index(root_op_index)
        if root_op is None:
            raise RuntimeError("Root Motion Generator op is missing")
        root_op.set_editor_property("source_root_bone", "MotionRoot")
        root_op.set_editor_property("target_root_bone", "root")
        root_op.set_editor_property("target_pelvis_bone", "pelvis")
        root_op.set_editor_property(
            "root_motion_source",
            unreal.RootMotionSource.COPY_FROM_SOURCE_ROOT,
        )
        root_op.set_editor_property(
            "root_height_source",
            unreal.RootMotionHeightSource.SNAP_TO_GROUND,
        )
        root_op.set_editor_property(
            "propagate_to_non_retargeted_children",
            True,
        )
        unreal.log(
            "MOTIONBRIDGE_ROOT_OP_API=UE5.5_OBJECT|"
            "MotionRoot|root|pelvis|COPY_FROM_SOURCE_ROOT"
        )
        return

    # UE 5.6：骨盆、FK、IK 求解和根运动本身都已迁移到操作栈。
    # 必须加入整套默认操作，不能像 5.5 那样只留下根运动节点。
    controller.add_default_ops()
    root_controller = None
    op_descriptions: list[str] = []
    for op_index in range(controller.get_num_retarget_ops()):
        op_controller = controller.get_op_controller(op_index)
        if op_controller is None:
            continue
        class_name = op_controller.get_class().get_name()
        op_name = str(controller.get_op_name(op_index))
        op_descriptions.append(f"{op_index}:{op_name}:{class_name}")
        if class_name == "IKRetargetRootMotionController":
            root_controller = op_controller

    unreal.log("MOTIONBRIDGE_RETARGET_OPS=" + "|".join(op_descriptions))
    if root_controller is None:
        raise RuntimeError("UE 5.6 Root Motion op controller is missing")

    root_controller.set_source_root_bone("MotionRoot")
    root_controller.set_target_root_bone("root")
    root_controller.set_target_pelvis_bone("pelvis")
    root_settings = root_controller.get_settings()
    root_settings.set_editor_property(
        "root_motion_source",
        unreal.RootMotionSource.COPY_FROM_SOURCE_ROOT,
    )
    root_settings.set_editor_property(
        "root_height_source",
        unreal.RootMotionHeightSource.SNAP_TO_GROUND,
    )
    root_settings.set_editor_property(
        "propagate_to_non_retargeted_children",
        True,
    )
    root_controller.set_settings(root_settings)
    unreal.log(
        "MOTIONBRIDGE_ROOT_OP_API=UE5.6_STRUCT|"
        "MotionRoot|root|pelvis|COPY_FROM_SOURCE_ROOT"
    )


def configure_leg_fk_rotation(controller: unreal.IKRetargeterController) -> None:
    if not LEG_ONE_TO_ONE:
        return
    for op_index in range(controller.get_num_retarget_ops()):
        op_controller = controller.get_op_controller(op_index)
        if op_controller is None:
            continue
        if op_controller.get_class().get_name() != "IKRetargetFKChainsController":
            continue
        settings = op_controller.get_settings()
        chains = list(settings.get_editor_property("chains_to_retarget"))
        changed_chains: list[str] = []
        for chain in chains:
            chain_name = str(chain.get_editor_property("target_chain_name"))
            if chain_name not in {"LeftLeg", "RightLeg"}:
                continue
            chain.set_editor_property(
                "rotation_mode",
                unreal.FKChainRotationMode.ONE_TO_ONE,
            )
            changed_chains.append(chain_name)
        settings.set_editor_property("chains_to_retarget", chains)
        op_controller.set_settings(settings)
        unreal.log(
            "MOTIONBRIDGE_LEG_FK_ROTATION=ONE_TO_ONE|"
            + "|".join(changed_chains)
        )
        return
    raise RuntimeError("UE 5.6 FK Chains op controller is missing")


def main() -> None:
    source_mesh = require_asset(SOURCE_MESH_PATH)
    source_animation = require_asset(SOURCE_ANIMATION_PATH)
    target_mesh = require_asset(TARGET_MESH_PATH)

    # duplicate_and_retarget 会在生成目标动画时立即构建压缩数据。
    # 必须在此之前声明源动画包含 Root Motion；如果等目标资产生成后再开，
    # 编辑器 RAW 轨道仍能看到位移，但运行时压缩轨道可能已把 root 位移裁掉。
    source_animation.set_editor_property("enable_root_motion", True)
    source_animation.set_editor_property("force_root_lock", False)
    source_animation.set_editor_property(
        "use_normalized_root_motion_scale",
        False,
    )
    source_animation.set_editor_property(
        "root_motion_root_lock",
        unreal.RootMotionRootLock.REF_POSE,
    )
    unreal.EditorAssetLibrary.save_asset(
        SOURCE_ANIMATION_PATH,
        only_if_is_dirty=False,
    )

    source_rig = create_ik_rig(
        SOURCE_RIG_PATH,
        source_mesh,
        "Hips",
        SOURCE_CHAINS,
    )
    target_rig = create_ik_rig(
        TARGET_RIG_PATH,
        target_mesh,
        "pelvis",
        TARGET_CHAINS,
    )

    retargeter = recreate_asset(
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
    configure_retarget_ops(controller)
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    if DISABLE_AUTO_ALIGN:
        unreal.log("MOTIONBRIDGE_RETARGET_AUTO_ALIGN=DISABLED")
    else:
        controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
        unreal.log("MOTIONBRIDGE_RETARGET_AUTO_ALIGN=TARGET_ALL_BONES")
    configure_leg_fk_rotation(controller)

    # 新建资产也显式写入四个阶段开关。这样脚本不依赖 UE 对旧版
    # bRetargetRoot_DEPRECATED 字段的迁移结果；若 Root 阶段被关掉，
    # pelvis 会固定在参考姿势，后处理再写 root 就会形成反向抵消。
    global_settings = controller.get_global_settings()
    global_settings.set_editor_property("enable_root", True)
    global_settings.set_editor_property("enable_fk", True)
    global_settings.set_editor_property("enable_ik", True)
    global_settings.set_editor_property("enable_post", True)
    controller.set_global_settings(global_settings)
    unreal.log(
        "MOTIONBRIDGE_RETARGET_PHASES="
        f"root:{global_settings.get_editor_property('enable_root')}|"
        f"fk:{global_settings.get_editor_property('enable_fk')}|"
        f"ik:{global_settings.get_editor_property('enable_ik')}|"
        f"post:{global_settings.get_editor_property('enable_post')}"
    )

    unreal.EditorAssetLibrary.save_directory(
        RIG_DIRECTORY,
        only_if_is_dirty=False,
        recursive=True,
    )

    if unreal.EditorAssetLibrary.does_asset_exist(OUTPUT_ANIMATION_PATH):
        if not unreal.EditorAssetLibrary.delete_asset(OUTPUT_ANIMATION_PATH):
            raise RuntimeError("could not replace generated Manny animation")

    source_data = unreal.EditorAssetLibrary.find_asset_data(
        SOURCE_ANIMATION_PATH
    )
    generated_assets = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        [source_data],
        source_mesh,
        target_mesh,
        retargeter,
        "",
        "",
        "",
        "_Manny",
        False,
    )
    if not generated_assets:
        raise RuntimeError("batch retarget produced no animation")
    generated_path = str(generated_assets[0].package_name)
    if not unreal.EditorAssetLibrary.rename_asset(
        generated_path,
        OUTPUT_ANIMATION_PATH,
    ):
        raise RuntimeError(
            f"could not move generated animation: {generated_path}"
        )

    animation = require_asset(OUTPUT_ANIMATION_PATH)
    animation.set_editor_property("enable_root_motion", True)
    animation.set_editor_property("force_root_lock", False)
    animation.set_editor_property("use_normalized_root_motion_scale", False)
    animation.set_editor_property(
        "root_motion_root_lock",
        unreal.RootMotionRootLock.REF_POSE,
    )
    animation.set_editor_property(
        "bone_compression_settings",
        require_asset("/Engine/Animation/DefaultAnimBoneCompressionSettings"),
    )
    animation.set_editor_property(
        "curve_compression_settings",
        require_asset("/Engine/Animation/DefaultAnimCurveCompressionSettings"),
    )
    unreal.EditorAssetLibrary.save_asset(
        OUTPUT_ANIMATION_PATH,
        only_if_is_dirty=False,
    )

    if IS_100STYLE or IS_SFU_WALK:
        blueprint_directory = (
            "/Game/MotionBridge/SFUWalk"
            if IS_SFU_WALK
            else "/Game/MotionBridge/100Style"
        )
        source_blueprint = (
            "/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK"
        )
        target_blueprint = (
            f"{blueprint_directory}/"
            + (
                "ABP_MotionBridgeSFUWalkFootIK"
                if IS_SFU_WALK
                else "ABP_MotionBridge100StyleFootIK"
            )
        )
        if unreal.EditorAssetLibrary.does_asset_exist(target_blueprint):
            if not unreal.EditorAssetLibrary.delete_asset(target_blueprint):
                raise RuntimeError("could not replace generated AnimBlueprint")
        if not unreal.EditorAssetLibrary.duplicate_asset(
            source_blueprint,
            target_blueprint,
        ):
            raise RuntimeError("could not duplicate generated AnimBlueprint")
        unreal.EditorAssetLibrary.save_asset(
            target_blueprint,
            only_if_is_dirty=False,
        )

    unreal.log(
        "MOTIONBRIDGE_ROOT_MOTION_RETARGETED=" + OUTPUT_ANIMATION_PATH
    )
    unreal.log("MOTIONBRIDGE_ROOT_MOTION_RETARGET=PASS")


main()
