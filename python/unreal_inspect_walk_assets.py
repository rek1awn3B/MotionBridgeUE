from __future__ import annotations

import unreal


ASSET_PATHS = (
    "/Game/Characters/Mannequins/Animations/Manny/MM_Walk_InPlace",
    "/Game/Characters/Mannequins/Animations/Manny/MM_Walk_Fwd",
)


def main() -> None:
    for path in ASSET_PATHS:
        animation = unreal.load_asset(path)
        if animation is None:
            raise RuntimeError(f"missing animation: {path}")

        unreal.log(
            "MOTIONBRIDGE_WALK_ASSET|"
            f"path={path}|"
            f"class={animation.get_class().get_name()}|"
            f"length={animation.get_play_length():.6f}|"
            f"rate_scale={animation.get_editor_property('rate_scale'):.6f}|"
            f"enable_root_motion={animation.get_editor_property('enable_root_motion')}|"
            f"root_motion_root_lock={animation.get_editor_property('root_motion_root_lock')}"
        )

        methods = [name for name in dir(animation) if "curve" in name.lower()]
        unreal.log(
            "MOTIONBRIDGE_WALK_CURVE_METHODS|"
            f"path={path}|methods={','.join(methods)}"
        )

        data_model = animation.get_data_model()
        model_methods = [name for name in dir(data_model) if "curve" in name.lower()]
        unreal.log(
            "MOTIONBRIDGE_WALK_MODEL_CURVE_METHODS|"
            f"path={path}|methods={','.join(model_methods)}"
        )

        if hasattr(data_model, "get_float_curve_names"):
            names = data_model.get_float_curve_names()
            unreal.log(
                "MOTIONBRIDGE_WALK_CURVES|"
                f"path={path}|names={','.join(str(name) for name in names)}"
            )

        root_motion = animation.extract_root_motion(0.0, animation.get_play_length(), False)
        translation = root_motion.translation
        rotation = root_motion.rotation
        unreal.log(
            "MOTIONBRIDGE_WALK_ROOT_MOTION|"
            f"path={path}|"
            f"translation=({translation.x:.6f},{translation.y:.6f},{translation.z:.6f})|"
            f"rotation=({rotation.x:.6f},{rotation.y:.6f},{rotation.z:.6f},{rotation.w:.6f})"
        )

    unreal.log("MOTIONBRIDGE_WALK_ASSET_INSPECTION=PASS")


main()
