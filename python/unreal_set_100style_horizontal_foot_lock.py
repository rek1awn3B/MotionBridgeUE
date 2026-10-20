from __future__ import annotations

import unreal


LEVEL_PATH = "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo"
DISABLE = (
    "-MotionBridgeDisableHorizontalFootLock"
    in unreal.SystemLibrary.get_command_line()
)


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_subsystem.load_level(LEVEL_PATH):
    raise RuntimeError(f"could not load {LEVEL_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
characters = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_actor_label() == "MotionBridge_100StyleCharacter"
]
if len(characters) != 1:
    raise RuntimeError(f"expected one demo character, got {len(characters)}")

foot_ik_class = unreal.load_class(
    None,
    "/Script/TP_ThirdPerson.MotionBridgeFootIKComponent",
)
if foot_ik_class is None:
    raise RuntimeError("missing MotionBridgeFootIKComponent class")

components = characters[0].get_components_by_class(foot_ik_class)
if len(components) != 1:
    raise RuntimeError(f"expected one Foot IK component, got {len(components)}")

strength = 0.0 if DISABLE else 1.0
components[0].set_editor_property("horizontal_foot_lock_strength", strength)

if not level_subsystem.save_current_level():
    raise RuntimeError(f"could not save {LEVEL_PATH}")

unreal.log(f"MOTIONBRIDGE_HORIZONTAL_FOOT_LOCK={strength:.1f}")
