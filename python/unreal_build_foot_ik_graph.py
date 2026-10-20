from __future__ import annotations

import unreal


BLUEPRINT_PATH = "/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK"


def main() -> None:
    """Verify the AnimBlueprint rebuilt by -MotionBridgeBuildFootIKGraph."""
    blueprint = unreal.load_asset(BLUEPRINT_PATH)
    if blueprint is None:
        raise RuntimeError(f"missing Foot IK AnimBlueprint: {BLUEPRINT_PATH}")

    generated_class = blueprint.generated_class()
    if generated_class is None:
        raise RuntimeError("Foot IK AnimBlueprint has no generated class")

    unreal.log(f"MOTIONBRIDGE_FOOT_IK_BLUEPRINT={BLUEPRINT_PATH}")
    unreal.log("MOTIONBRIDGE_FOOT_IK_BLUEPRINT=PASS")


main()
