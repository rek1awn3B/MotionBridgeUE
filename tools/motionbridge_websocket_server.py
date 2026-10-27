"""把 MotionBridge JSON 动作流实时推送给 Unreal WebSocket 客户端。"""

from __future__ import annotations

import argparse
import asyncio
import copy
import json
import math
import time
from pathlib import Path
from typing import Any, AsyncIterator, Iterator

import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIRECTORY = REPOSITORY_ROOT / "python"
if str(PYTHON_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIRECTORY))

from bvh_stream import build_manny_stream_from_bvh, build_stream_from_bvh


FACIAL_CHANNELS = ["Smile", "JawOpen", "BlinkLeft", "BlinkRight"]


def load_stream(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream_file:
        stream = json.load(stream_file)
    bones = stream.get("bones", [])
    channels = stream.get("channels", [])
    if not isinstance(bones, list):
        raise ValueError("stream.bones must be a list when provided")
    if not isinstance(channels, list):
        raise ValueError("stream.channels must be a list when provided")
    if not bones and not channels:
        raise ValueError("stream must declare bones or channels")
    if not isinstance(stream.get("frames"), list) or not stream["frames"]:
        raise ValueError("stream.frames must be a non-empty list")
    if float(stream.get("fps", 0.0)) <= 0.0:
        raise ValueError("stream.fps must be positive")
    return stream


def build_stream_definition(
    stream: dict[str, Any],
    facial_demo: bool = False,
) -> dict[str, Any]:
    definition: dict[str, Any] = {
        "type": "stream",
        "protocol_version": int(stream.get("protocol_version", 1)),
        "stream_id": str(stream.get("stream_id", "motionbridge-websocket")),
    }
    bones = list(stream.get("bones", []))
    if bones:
        definition["bones"] = bones
    channels = list(stream.get("channels", []))
    if facial_demo:
        channels = FACIAL_CHANNELS.copy()
    if channels:
        definition["channels"] = channels
    for metadata_key in ("source_format", "source_file", "retarget_mode"):
        if stream.get(metadata_key):
            definition[metadata_key] = str(stream[metadata_key])
    return definition


def facial_values(timestamp: float) -> list[float]:
    """生成稳定且肉眼可辨认的表情通道，便于无摄像头演示。"""
    smile = 0.5 + 0.5 * math.sin(timestamp * 1.4)
    jaw_open = 0.18 + 0.16 * (1.0 + math.sin(timestamp * 5.0))
    # 每两秒闭眼约 0.2 秒；足够自然，也不会因渲染帧率较低而漏采。
    blink = 1.0 if timestamp % 2.0 < 0.20 else 0.0
    return [smile, jaw_open, blink, blink]


def build_frame_packet(
    source_frame: dict[str, Any],
    frame_index: int,
    timestamp: float,
    facial_demo: bool = False,
    sent_at_unix_seconds: float | None = None,
    stream_id: str | None = None,
) -> dict[str, Any]:
    frame = copy.deepcopy(source_frame)
    frame["type"] = "frame"
    frame["frame_index"] = frame_index
    frame["timestamp"] = timestamp
    frame["sent_at_unix_seconds"] = (
        time.time() if sent_at_unix_seconds is None else sent_at_unix_seconds
    )
    if stream_id is not None:
        frame["stream_id"] = stream_id
    if facial_demo:
        frame["channels"] = facial_values(timestamp)
    return frame


def quaternion_yaw(rotation: list[float]) -> float:
    """提取 UE 四元数的 yaw；Actor 根旋转按协议只包含水平旋转。"""

    x, y, z, w = (float(component) for component in rotation)
    return math.atan2(
        2.0 * (w * z + x * y),
        1.0 - 2.0 * (y * y + z * z),
    )


def yaw_quaternion(yaw: float) -> list[float]:
    half_yaw = 0.5 * yaw
    return [0.0, 0.0, math.sin(half_yaw), math.cos(half_yaw)]


def transform_root_for_loop(
    frame: dict[str, Any],
    first_position: list[float],
    first_yaw: float,
    cycle_position: list[float],
    cycle_yaw: float,
) -> None:
    """把一轮中的根姿势接到上一轮末尾，避免循环时瞬移回起点。"""

    root_position = frame.get("root_position")
    root_rotation = frame.get("root_rotation")
    if not isinstance(root_position, list) or len(root_position) != 3:
        return
    if not isinstance(root_rotation, list) or len(root_rotation) != 4:
        return

    relative_x = float(root_position[0]) - first_position[0]
    relative_y = float(root_position[1]) - first_position[1]
    relative_z = float(root_position[2]) - first_position[2]
    rotation_from_first = cycle_yaw - first_yaw
    cosine = math.cos(rotation_from_first)
    sine = math.sin(rotation_from_first)
    frame["root_position"] = [
        cycle_position[0] + cosine * relative_x - sine * relative_y,
        cycle_position[1] + sine * relative_x + cosine * relative_y,
        cycle_position[2] + relative_z,
    ]
    frame_yaw = quaternion_yaw(root_rotation)
    frame["root_rotation"] = yaw_quaternion(
        cycle_yaw + frame_yaw - first_yaw
    )


def iter_looped_frames(
    stream: dict[str, Any],
    loop_stream: bool,
    continuous_root: bool = False,
    continuous_root_cycles: int = 0,
) -> Iterator[dict[str, Any]]:
    """按需循环源帧，并可把每一轮根运动连续接到上一轮末尾。"""

    frames = stream["frames"]
    first_position = [
        float(component)
        for component in frames[0].get("root_position", [0.0, 0.0, 0.0])
    ]
    first_yaw = quaternion_yaw(
        frames[0].get("root_rotation", [0.0, 0.0, 0.0, 1.0])
    )
    cycle_position = first_position.copy()
    cycle_yaw = first_yaw
    completed_cycles = 0

    while True:
        for source_frame in frames:
            frame = copy.deepcopy(source_frame)
            if continuous_root:
                transform_root_for_loop(
                    frame,
                    first_position,
                    first_yaw,
                    cycle_position,
                    cycle_yaw,
                )
            yield frame
        if not loop_stream:
            break
        if continuous_root:
            completed_cycles += 1
            if (
                continuous_root_cycles > 0
                and completed_cycles >= continuous_root_cycles
            ):
                cycle_position = first_position.copy()
                cycle_yaw = first_yaw
                completed_cycles = 0
            else:
                last_frame = copy.deepcopy(frames[-1])
                transform_root_for_loop(
                    last_frame,
                    first_position,
                    first_yaw,
                    cycle_position,
                    cycle_yaw,
                )
                cycle_position = [
                    float(component)
                    for component in last_frame["root_position"]
                ]
                cycle_yaw = quaternion_yaw(last_frame["root_rotation"])


async def iter_frame_packets(
    stream: dict[str, Any],
    loop_stream: bool,
    facial_demo: bool,
    continuous_root: bool = False,
    continuous_root_cycles: int = 0,
) -> AsyncIterator[tuple[dict[str, Any], float]]:
    fps = float(stream["fps"])
    frame_interval = 1.0 / fps
    output_index = 0
    for source_frame in iter_looped_frames(
        stream,
        loop_stream,
        continuous_root,
        continuous_root_cycles,
    ):
        timestamp = output_index * frame_interval
        yield (
            build_frame_packet(
                source_frame,
                output_index,
                timestamp,
                facial_demo,
                stream_id=str(
                    stream.get("stream_id", "motionbridge-websocket")
                ),
            ),
            frame_interval,
        )
        output_index += 1


async def stream_to_client(
    websocket: Any,
    stream: dict[str, Any],
    loop_stream: bool,
    facial_demo: bool,
    disconnect_after: int,
    continuous_root: bool = False,
    continuous_root_cycles: int = 0,
) -> None:
    await websocket.send(
        json.dumps(
            build_stream_definition(stream, facial_demo),
            separators=(",", ":"),
        )
    )
    print(f"client connected: {websocket.remote_address}", flush=True)

    sent_frames = 0
    next_send_time = asyncio.get_running_loop().time()
    first_packet: dict[str, Any] | None = None
    last_packet: dict[str, Any] | None = None
    last_frame_interval = 0.0
    async for packet, frame_interval in iter_frame_packets(
        stream,
        loop_stream,
        facial_demo,
        continuous_root,
        continuous_root_cycles,
    ):
        packet["sent_at_unix_seconds"] = time.time()
        await websocket.send(json.dumps(packet, separators=(",", ":")))
        if first_packet is None:
            first_packet = copy.deepcopy(packet)
        last_packet = copy.deepcopy(packet)
        last_frame_interval = frame_interval
        sent_frames += 1
        if disconnect_after > 0 and sent_frames >= disconnect_after:
            await websocket.close(1012, "intentional reconnect test")
            break

        next_send_time += frame_interval
        await asyncio.sleep(
            max(next_send_time - asyncio.get_running_loop().time(), 0.0)
        )

    # 单次演示发送完后，下一帧切回第一帧的根姿势和骨骼姿势，再以递增
    # 时间戳持续发送。角色因此回到演示原点并停住，不会重新播放，也不会
    # 因长时间没有新数据而把正常结束误报成 timeout。
    if (
        not loop_stream
        and disconnect_after <= 0
        and first_packet is not None
        and last_packet is not None
        and last_frame_interval > 0.0
    ):
        hold_frame_index = int(last_packet["frame_index"])
        hold_timestamp = float(last_packet["timestamp"])
        while True:
            hold_frame_index += 1
            hold_timestamp += last_frame_interval
            hold_packet = copy.deepcopy(first_packet)
            hold_packet["frame_index"] = hold_frame_index
            hold_packet["timestamp"] = hold_timestamp
            hold_packet["sent_at_unix_seconds"] = time.time()
            await websocket.send(
                json.dumps(hold_packet, separators=(",", ":"))
            )
            sent_frames += 1
            next_send_time += last_frame_interval
            await asyncio.sleep(
                max(next_send_time - asyncio.get_running_loop().time(), 0.0)
            )

    print(f"client session ended after {sent_frames} frames", flush=True)


async def run_server(args: argparse.Namespace) -> None:
    try:
        from websockets.asyncio.server import serve
    except ImportError as error:
        raise SystemExit(
            "missing dependency: run `python -m pip install -r requirements.txt`"
        ) from error

    stream = (
        load_stream(args.stream_file)
        if args.stream_file is not None
        else build_manny_stream_from_bvh(
            args.bvh_file,
            args.calibration_file,
            args.manny_calibration_file,
        )
    )

    async def handler(websocket: Any) -> None:
        try:
            await stream_to_client(
                websocket,
                stream,
                not args.once,
                args.facial_demo,
                args.disconnect_after,
                args.continuous_root,
                args.continuous_root_cycles,
            )
        except Exception as error:  # 客户端主动退出属于正常实时流生命周期。
            print(f"client disconnected: {error}", flush=True)

    async with serve(handler, args.host, args.port, max_size=4 * 1024 * 1024):
        print(
            f"MotionBridge WebSocket server listening on "
            f"ws://{args.host}:{args.port}/",
            flush=True,
        )
        await asyncio.Future()


def parse_args() -> argparse.Namespace:
    default_bvh = (
        REPOSITORY_ROOT
        / "data"
        / "bvh"
        / "100style"
        / "Neutral_FW_frames_470_694.bvh"
    )
    default_calibration = (
        REPOSITORY_ROOT
        / "data"
        / "calibration"
        / "100style_source_calibration.json"
    )
    default_manny_calibration = (
        REPOSITORY_ROOT
        / "data"
        / "calibration"
        / "100style_to_manny_calibration.json"
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    source = parser.add_mutually_exclusive_group()
    source.add_argument(
        "--bvh-file",
        type=Path,
        default=default_bvh,
        help="实时解析并发送的原始 BVH；这是默认输入",
    )
    source.add_argument(
        "--stream-file",
        type=Path,
        help="兼容旧测试或自定义设备适配器的预生成 JSON 流",
    )
    parser.add_argument(
        "--calibration-file",
        type=Path,
        default=default_calibration,
        help="BVH 关节坐标到 UE 源骨架坐标的标定文件",
    )
    parser.add_argument(
        "--manny-calibration-file",
        type=Path,
        default=default_manny_calibration,
        help="UE 源骨架到 Manny 局部关节坐标的公开标定文件",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="只发送一遍，然后回到第一帧并保持连接",
    )
    parser.add_argument(
        "--facial-demo",
        action="store_true",
        help="附加 Smile/JawOpen/BlinkLeft/BlinkRight 标量通道",
    )
    parser.add_argument(
        "--disconnect-after",
        type=int,
        default=0,
        help="发送指定帧数后主动断开，用于验证 UE 自动重连",
    )
    parser.add_argument(
        "--continuous-root",
        action="store_true",
        help="循环动作时累计根位移与朝向，使角色持续向前而不是返回起点",
    )
    parser.add_argument(
        "--continuous-root-cycles",
        type=int,
        default=0,
        help="连续累计指定轮数后回到原点；0 表示永不自动重置",
    )
    args = parser.parse_args()
    if args.continuous_root_cycles < 0:
        parser.error("--continuous-root-cycles must be non-negative")
    return args


def main() -> None:
    asyncio.run(run_server(parse_args()))


if __name__ == "__main__":
    main()
