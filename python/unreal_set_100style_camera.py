from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo"
FRONT_VIEW = "-MotionBridgeFrontFootView" in unreal.SystemLibrary.get_command_line()


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_subsystem.load_level(LEVEL_PATH):
    raise RuntimeError(f"could not load {LEVEL_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cameras = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_actor_label() == "MotionBridge_RootMotionCamera"
]
if len(cameras) != 1:
    raise RuntimeError(f"expected one demo camera, got {len(cameras)}")

camera = cameras[0]
if FRONT_VIEW:
    camera.set_actor_location(unreal.Vector(500.0, 0.0, 125.0), False, False)
    camera.set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=-5.0, yaw=180.0), False
    )
else:
    camera.set_actor_location(unreal.Vector(130.0, 350.0, 125.0), False, False)
    camera.set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=-5.0, yaw=-90.0), False
    )

if not level_subsystem.save_current_level():
    raise RuntimeError(f"could not save {LEVEL_PATH}")

unreal.log(
    "MOTIONBRIDGE_100STYLE_CAMERA=" + ("FRONT" if FRONT_VIEW else "SIDE")
)
