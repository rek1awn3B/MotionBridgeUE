"""把 MotionBridge JSON 动作流实时推送给 Unreal WebSocket 客户端。"""

from __future__ import annotations

import argparse
import asyncio
import copy
import json
import math
import time
from pathlib import Path
from typing import Any, AsyncIterator

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


async def iter_frame_packets(
    stream: dict[str, Any],
    loop_stream: bool,
    facial_demo: bool,
) -> AsyncIterator[tuple[dict[str, Any], float]]:
    frames = stream["frames"]
    fps = float(stream["fps"])
    frame_interval = 1.0 / fps
    output_index = 0
    while True:
        for source_frame in frames:
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
        if not loop_stream:
            break


async def stream_to_client(
    websocket: Any,
    stream: dict[str, Any],
    loop_stream: bool,
    facial_demo: bool,
    disconnect_after: int,
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
    async for packet, frame_interval in iter_frame_packets(
        stream,
        loop_stream,
        facial_demo,
    ):
        packet["sent_at_unix_seconds"] = time.time()
        await websocket.send(json.dumps(packet, separators=(",", ":")))
        sent_frames += 1
        if disconnect_after > 0 and sent_frames >= disconnect_after:
            await websocket.close(1012, "intentional reconnect test")
            break

        next_send_time += frame_interval
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
        / "Neutral_FW_frames_530_694.bvh"
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
        help="发送一遍后停止当前客户端会话；服务仍继续等待新连接",
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
    return parser.parse_args()


def main() -> None:
    asyncio.run(run_server(parse_args()))


if __name__ == "__main__":
    main()
