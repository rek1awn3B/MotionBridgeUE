from __future__ import annotations

import unreal


EXPECTED_ANIMATION_PATH = (
    "/Game/MotionBridge/Retargeted/"
    "A_MotionBridgeSource_Manny.A_MotionBridgeSource_Manny"
)


def actor_by_label(actors: list[unreal.Actor], label: str) -> unreal.Actor:
    matches = [actor for actor in actors if actor.get_actor_label() == label]
    if len(matches) != 1:
        raise RuntimeError(f"expected one actor named {label}, found {len(matches)}")
    return matches[0]


def main() -> None:
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    character = actor_by_label(actors, "MotionBridge_Manny_Retargeted")
    camera = actor_by_label(actors, "MotionBridge_OverviewCamera")

    component = character.get_editor_property("skeletal_mesh_component")
    animation_data = component.get_editor_property("animation_data")
    animation_path = animation_data.anim_to_play.get_path_name()
    if animation_path != EXPECTED_ANIMATION_PATH:
        raise RuntimeError(f"unexpected saved animation: {animation_path}")
    if not animation_data.saved_looping or not animation_data.saved_playing:
        raise RuntimeError("saved animation is not configured to loop and play")
    if abs(animation_data.saved_play_rate - 1.0) > 1.0e-6:
        raise RuntimeError(
            f"unexpected saved play rate: {animation_data.saved_play_rate}"
        )

    auto_player = camera.get_editor_property("auto_activate_for_player")
    if auto_player != unreal.AutoReceiveInput.PLAYER0:
        raise RuntimeError(f"camera does not target player 0: {auto_player}")

    unreal.log(f"MOTIONBRIDGE_SAVED_ANIMATION={animation_path}")
    unreal.log(
        "MOTIONBRIDGE_SAVED_PLAYBACK="
        f"playing={animation_data.saved_playing},"
        f"looping={animation_data.saved_looping},"
        f"rate={animation_data.saved_play_rate}"
    )
    unreal.log(f"MOTIONBRIDGE_CAMERA_AUTO_PLAYER={auto_player}")
    unreal.log("MOTIONBRIDGE_DEMO_SERIALIZATION=PASS")


main()
