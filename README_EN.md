# MotionBridge UE

MotionBridge UE is a realtime animation bridge from raw BVH motion to UE5 Manny. Python parses BVH, computes source local/global poses, maps them to Manny, and streams frames at approximately 60 Hz. Unreal C++ validates and buffers messages, interpolates poses, applies Pose Snapshots and character root motion, runs foot IK, reconnects after transport failures, and exposes runtime diagnostics.

## Actual runtime path

```text
raw BVH
  -> custom parser / channel decoder / forward kinematics
  -> source-coordinate calibration
  -> per-frame source global rotations
  -> calibrated skeleton-pair map: R_target_global = A * R_source_global * B
  -> Manny local rotations
  -> 89-bone packet + separate actor root motion
  -> WebSocket
  -> Unreal queue / validation / interpolation
  -> Pose Snapshot + CharacterMovement + foot IK
```

The runtime server reads the raw BVH and two calibration JSON files. It does **not** read pre-exported Manny pose frames. `source_reference_stream.json` and `manny_root_motion_stream.json` are offline calibration/reproducibility inputs only.

The fixed 100STYLE-to-Manny calibration dynamically maps 26 major joints. Manny-only twist, finger, and IK helper bones remain at target reference pose, while the packet preserves the full 89-bone target order. On the aligned validation clip, the maximum fitted rotation error is `7.157°` at `foot_l`. This is a calibrated mapping for one source/target skeleton pair, not an arbitrary-skeleton automatic retargeter.

See [docs/runtime_bvh_to_manny.md](docs/runtime_bvh_to_manny.md) for the math, calibration inputs, and limitations.

## Implemented systems

- Custom BVH tokenizer, parser, channel decoder, and forward kinematics.
- Runtime source-to-Manny pose conversion from the current BVH frame.
- Versioned UDP/WebSocket JSON ingestion with validation, a bounded time-sorted buffer, interpolation, timeout fallback, and reconnection.
- MPSC handoff from network callbacks; Unreal objects are updated only on the game thread.
- Actor/capsule root motion separated from skeletal pelvis motion.
- C++ foot IK with heel/toe traces, pelvis compensation, Two Bone IK, surface-normal alignment, and frame-rate-independent smoothing.
- Native C++ UMG diagnostics and four Morph Target validation channels.
- A separate offline Blender/FBX/UE IK Retargeter workflow retained for comparison and asset production; it is not required by the realtime server.

## Reproduce

Requirements: Unreal Engine 5.6, Visual Studio 2022 with C++ game development tools, and Python 3.12.

```powershell
py -3.12 -m pip install -r requirements.txt
.\scripts\setup_unreal_demo.ps1 -EngineRoot "D:\Program Files\Epic Games\UE_5.6"
.\scripts\run_websocket_demo.ps1
.\scripts\run_realtime_acceptance.ps1
py -3.12 -m unittest discover -s python -p "test_bvh_stream.py" -v
```

The final realtime acceptance verifies an 89-bone direct Manny snapshot, zero rejected frames, three forced reconnects, runtime foot IK with positive sole clearance, and all four Morph Target channels. Full markers and commands are recorded in [docs/final_acceptance.md](docs/final_acceptance.md).

The complete Python suite passes 57/57 tests, including raw-BVH runtime conversion, looped-root-motion, and source-provenance coverage. The default portfolio demo plays the unmodified 470–694 frame excerpt (about 3.75 seconds), returns the actor to its start after each pass, and repeats on a 12 m × 12 m floor.

Public motion data comes from the CC BY 4.0 100STYLE dataset. Project code is released under the [MIT License](LICENSE); third-party and Epic Games assets retain their own terms as documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
