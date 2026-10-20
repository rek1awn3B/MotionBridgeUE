from __future__ import annotations

import unreal


IS_100STYLE = "-MotionBridge100Style" in unreal.SystemLibrary.get_command_line()
IS_SFU_WALK = "-MotionBridgeSFUWalk" in unreal.SystemLibrary.get_command_line()
IS_GAIT_DEMO = IS_100STYLE or IS_SFU_WALK
FRONT_FOOT_VIEW = (
    "-MotionBridgeFrontFootView" in unreal.SystemLibrary.get_command_line()
)
LEVEL_PATH = (
    "/Game/MotionBridge/Maps/L_MotionBridgeSFUWalkDemo"
    if IS_SFU_WALK
    else "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo"
    if IS_100STYLE
    else "/Game/MotionBridge/Maps/L_MotionBridgeRootMotionDemo"
)
CHARACTER_CLASS_PATH = "/Script/TP_ThirdPerson.MotionBridgeRootMotionCharacter"
ANIMATION_PATH = (
    "/Game/MotionBridge/SFUWalkRetargeted/A_MotionBridgeSFUWalk_Manny"
    if IS_SFU_WALK
    else "/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny"
)
ANIM_BLUEPRINT_CLASS_PATH = (
    "/Game/MotionBridge/SFUWalk/"
    "ABP_MotionBridgeSFUWalkFootIK.ABP_MotionBridgeSFUWalkFootIK_C"
    if IS_SFU_WALK
    else "/Game/MotionBridge/100Style/"
    "ABP_MotionBridge100StyleFootIK.ABP_MotionBridge100StyleFootIK_C"
)


def require_asset(path: str) -> unreal.Object:
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"required asset not found: {path}")
    return asset


def spawn(
    actor_class: type,
    location: unreal.Vector,
    rotation: unreal.Rotator | None = None,
) -> unreal.Actor:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(
        actor_class, location, rotation or unreal.Rotator()
    )
    if actor is None:
        raise RuntimeError(f"could not spawn {actor_class}")
    return actor


def main() -> None:
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=START")
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        unreal.log("MOTIONBRIDGE_DEMO_STAGE=BEFORE_LOAD_LEVEL")
        if not level_subsystem.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not load {LEVEL_PATH}")
        unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_LOAD_LEVEL")
    elif not level_subsystem.new_level(LEVEL_PATH):
        if not level_subsystem.load_level(LEVEL_PATH):
            raise RuntimeError(f"could not create or load {LEVEL_PATH}")

    # 脚本可重复执行：先移除上一次生成的演示 Actor，避免关卡里叠出
    # 多个自动激活相机，导致 PIE 随机切到旧相机。
    actor_subsystem = unreal.get_editor_subsystem(
        unreal.EditorActorSubsystem
    )
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=BEFORE_LIST_ACTORS")
    generated_labels = {
        "MotionBridge_RootMotionCharacter",
        "MotionBridge_100StyleCharacter",
        "MotionBridge_SFUWalkCharacter",
        "MotionBridge_RootMotionFloor",
        "MotionBridge_RootMotionKeyLight",
        "MotionBridge_RootMotionSkyLight",
        "MotionBridge_RootMotionCamera",
    }
    generated_actors = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() in generated_labels
    ]
    unreal.log(
        f"MOTIONBRIDGE_DEMO_STAGE=AFTER_LIST_ACTORS|count={len(generated_actors)}"
    )
    if generated_actors:
        actor_subsystem.destroy_actors(generated_actors)
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_DESTROY_ACTORS")

    character_class = unreal.load_class(None, CHARACTER_CLASS_PATH)
    if character_class is None:
        raise RuntimeError(f"missing C++ class: {CHARACTER_CLASS_PATH}")

    character = spawn(
        character_class,
        unreal.Vector(0.0, 0.0, 91.0 if IS_SFU_WALK else 100.0),
    )
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_SPAWN_CHARACTER")
    character.set_actor_label(
        "MotionBridge_SFUWalkCharacter"
        if IS_SFU_WALK
        else "MotionBridge_100StyleCharacter"
        if IS_100STYLE
        else "MotionBridge_RootMotionCharacter"
    )
    if IS_GAIT_DEMO:
        if IS_SFU_WALK:
            # SFU 的虚拟 MotionRoot 位于地面，不像旧测试片段那样含有
            # 一个胶囊半高的垂直偏移；因此恢复 Manny 的标准网格下移。
            character.get_editor_property("mesh").set_relative_location(
                unreal.Vector(0.0, 0.0, -96.0)
            )
        character.set_editor_property(
            "use_constant_speed_root_motion",
            False,
        )
        character.set_editor_property(
            "loop_root_motion_playback",
            True,
        )
        character.set_editor_property(
            "constant_speed_root_motion_blend",
            0.0,
        )
        character.set_editor_property(
            "constant_direction_root_motion_blend",
            0.0,
        )
        character.set_editor_property(
            "root_motion_animation",
            require_asset(ANIMATION_PATH),
        )
        anim_class = unreal.load_class(None, ANIM_BLUEPRINT_CLASS_PATH)
        if anim_class is None:
            raise RuntimeError(
                f"missing AnimBlueprint class: {ANIM_BLUEPRINT_CLASS_PATH}"
            )
        character.get_editor_property("mesh").set_anim_instance_class(
            anim_class
        )
        foot_ik_class = unreal.load_class(
            None,
            "/Script/TP_ThirdPerson.MotionBridgeFootIKComponent",
        )
        if foot_ik_class is None:
            raise RuntimeError("missing MotionBridgeFootIKComponent class")
        foot_ik_components = character.get_components_by_class(foot_ik_class)
        if len(foot_ik_components) != 1:
            raise RuntimeError(
                "expected one MotionBridgeFootIKComponent on demo character"
            )
        # 自动验收仍会统计射线、锁脚与穿透指标；演示关卡只隐藏彩色
        # 调试线，让最终视频能直接观察走姿和脚掌接触。该演示是纯平地，
        # 不需要台阶/斜坡场景使用的骨盆下压，避免与动捕自身的髋部起伏叠加。
        foot_ik_components[0].set_editor_property("draw_debug", False)
        foot_ik_components[0].set_editor_property("pelvis_offset_scale", 0.0)
        foot_ik_components[0].set_editor_property("pelvis_offset_bias", -2.0)
        foot_ik_components[0].set_editor_property(
            "horizontal_foot_lock_strength",
            1.0,
        )
        foot_ik_components[0].set_editor_property(
            "weight_response_speed",
            16.0,
        )
        # 解锁若过快，锁定脚会在一帧内跳回原动画摆动位置，表现成
        # 小腿突然向后抽。适度延长释放时间，让支撑到摆动连续过渡。
        foot_ik_components[0].set_editor_property(
            "foot_lock_release_response_speed",
            8.0,
        )
        foot_ik_components[0].set_editor_property(
            "max_horizontal_foot_lock_correction",
            6.0,
        )
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_CONFIGURE_CHARACTER")

    floor = spawn(
        unreal.StaticMeshActor,
        # 运行时根运动沿世界 +X 前进约 258 cm；把地面中心放到路径
        # 中点，保证角色跨过循环接缝后仍位于地板范围内。
        unreal.Vector(
            65.0 if IS_SFU_WALK else 130.0 if IS_100STYLE else 45.0,
            0.0,
            -10.0,
        ),
    )
    floor.set_actor_label("MotionBridge_RootMotionFloor")
    floor.set_actor_scale3d(
        unreal.Vector(8.0 if IS_GAIT_DEMO else 4.0, 3.0, 0.1)
    )
    floor.get_editor_property("static_mesh_component").set_static_mesh(
        require_asset("/Engine/BasicShapes/Cube")
    )
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_SPAWN_FLOOR")

    light = spawn(
        unreal.DirectionalLight,
        unreal.Vector(0.0, 0.0, 300.0),
        unreal.Rotator(roll=0.0, pitch=-45.0, yaw=-35.0),
    )
    light.set_actor_label("MotionBridge_RootMotionKeyLight")
    light.get_editor_property("directional_light_component").set_editor_property(
        "intensity", 4.0
    )
    sky = spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 200.0))
    sky.set_actor_label("MotionBridge_RootMotionSkyLight")
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_SPAWN_LIGHTS")

    camera = spawn(
        unreal.CameraActor,
        (
            unreal.Vector(500.0, 0.0, 125.0)
            if IS_GAIT_DEMO and FRONT_FOOT_VIEW
            else
            # 对准一轮根运动的路径中点，让循环接缝仍完整留在画面中。
            unreal.Vector(65.0 if IS_SFU_WALK else 130.0, 350.0, 125.0)
            if IS_GAIT_DEMO
            else unreal.Vector(320.0, 320.0, 165.0)
        ),
        (
            unreal.Rotator(roll=0.0, pitch=-5.0, yaw=180.0)
            if IS_GAIT_DEMO and FRONT_FOOT_VIEW
            else
            unreal.Rotator(roll=0.0, pitch=-5.0, yaw=-90.0)
            if IS_GAIT_DEMO
            else unreal.Rotator(roll=0.0, pitch=-12.0, yaw=-135.0)
        ),
    )
    camera.set_actor_label("MotionBridge_RootMotionCamera")
    camera.set_editor_property(
        "auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0
    )
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_SPAWN_CAMERA")

    unreal.log("MOTIONBRIDGE_DEMO_STAGE=BEFORE_SAVE")
    if not level_subsystem.save_current_level():
        raise RuntimeError(f"could not save {LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_DEMO_STAGE=AFTER_SAVE")
    unreal.log(f"MOTIONBRIDGE_ROOT_MOTION_DEMO={LEVEL_PATH}")
    unreal.log("MOTIONBRIDGE_ROOT_MOTION_DEMO=PASS")


main()
