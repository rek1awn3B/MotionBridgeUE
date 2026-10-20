# MotionBridge UE

MotionBridge UE is a portfolio project for motion-capture animation, AI virtual-human systems, and gameplay/client engineering roles. It connects offline BVH processing and Unreal retargeting with runtime UDP/WebSocket streaming, root motion, foot IK, monitoring, reconnection, and Morph Target facial control.

![BVH motion retargeted to UE5 Manny](artifacts/ue_manny_retarget_contact.png)

The primary portfolio path is full-body Manny animation. The simplified face is
only a protocol/Morph Target validation fixture, not a finished digital-human
asset.

## Highlights

- Custom BVH tokenizer, parser, channel decoder, and forward kinematics implementation.
- Reproducible Blender/FBX/Unreal pipeline and UE IK Retargeter assets for UE5 Manny.
- Root-motion extraction with frame-by-frame world-pose reconstruction tests.
- Runtime root motion applied through `CharacterMovement`, avoiding mesh/capsule divergence and double consumption.
- C++ foot IK with heel/toe traces, pelvis compensation, Two Bone IK, ground-normal alignment, and frame-rate-independent smoothing.
- Shared versioned JSON ingestion for UDP and WebSocket transports, including validation, bounded buffering, interpolation, timeout fallback, and recovery.
- Network callbacks enqueue raw messages; Unreal objects are changed only on the game thread.
- Realtime scalar channels drive four Morph Targets on an original test character.
- Native C++ UMG diagnostics for connection state, input rate, FPS, end-to-end latency, packet quality, buffer delay, and reconnect count.

## Architecture

```text
BVH -> custom parser/decoder/FK -> validation and visualization
  \-> Blender/FBX -> UE source skeleton -> IK Retargeter -> Manny
                                                  \-> root motion
                                                  \-> foot IK

Python UDP/WebSocket source
  -> callback/network thread
  -> MPSC raw-message queue
  -> game-thread validation and time-sorted buffer
  -> interpolation
     ├─ local bone pose -> Pose Snapshot -> AnimGraph
     ├─ root motion -> CharacterMovement
     └─ scalar channels -> Morph Target driver
  -> C++ UMG diagnostics and automated acceptance logs
```

## Verified results

- 50/50 Python tests pass.
- 16/16 standalone C++ labs build and exit successfully.
- Unreal Engine 5.6 Game and Editor targets build successfully.
- UDP playback reproduces 69 frames over 89 Manny bones with `0.000` final local rotation/translation error.
- WebSocket loopback runs at approximately 27–31 Hz with zero rejected frames; measured average local end-to-end latency was about 36.76 ms in the steady validation run.
- Forced server disconnects were recovered automatically twice while streaming continued.
- Four Morph Targets were mapped and driven with zero missing channels.
- On a 10-degree slope, both feet matched `10.000°`; minimum sole clearances were `+0.194 / +0.200 cm`.

See [docs/final_acceptance.md](docs/final_acceptance.md) for commands and evidence.

## Quick start

Requirements: Unreal Engine 5.6, Visual Studio 2022 with C++ game development tools, and Python 3.12.

```powershell
py -3.12 -m pip install -r requirements.txt
.\scripts\setup_unreal_demo.ps1 -EngineRoot "C:\Program Files\Epic Games\UE_5.6"
.\scripts\run_websocket_demo.ps1 -FacialDemo
```

Run build and unit acceptance from a clean CMake configuration:

```powershell
.\scripts\run_acceptance.ps1
```

Include live WebSocket, reconnect, and Morph Target integration tests with:

```powershell
.\scripts\run_acceptance.ps1 -RunRealtime
```

## Scope and attribution

The custom work covers BVH/FK, root-motion processing, runtime ingestion and buffering, thread handoff, foot IK, diagnostics, and Morph-channel driving. Cross-skeleton retargeting uses Unreal's IK Retargeter and is not presented as a custom retargeting solver. The face asset is a minimal original validation asset, not a production facial solver. Public motion assets use the CC BY 4.0 100STYLE dataset; research-only SFU experiments are excluded from the public repository. AI-assisted tools were used during implementation and review; the repository keeps reproducible tests, validation records, and explicit technical boundaries, while the author owns the architecture, data flow, and reported conclusions.

Project code is released under the [MIT License](LICENSE). Third-party data and
Epic Games assets are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
