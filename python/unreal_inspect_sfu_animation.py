from __future__ import annotations

import unreal


PATHS = (
    "/Game/MotionBridge/SFUWalkSource/A_MotionBridgeSFUWalkSource_Anim",
    "/Game/MotionBridge/SFUWalkRetargeted/A_MotionBridgeSFUWalk_Manny",
)


def main() -> None:
    for path in PATHS:
        animation = unreal.load_asset(path)
        if animation is None:
            raise RuntimeError(f"missing animation: {path}")
        model = animation.data_model_interface
        frame_rate = model.get_frame_rate()
        unreal.log(
            "MOTIONBRIDGE_SFU_ANIMATION|"
            f"path={path}|length={animation.get_play_length():.9f}|"
            f"frames={model.get_number_of_frames()}|"
            f"keys={model.get_number_of_keys()}|"
            f"rate={frame_rate.numerator}/{frame_rate.denominator}"
        )
    unreal.log("MOTIONBRIDGE_SFU_ANIMATION_INSPECTION=PASS")


main()
