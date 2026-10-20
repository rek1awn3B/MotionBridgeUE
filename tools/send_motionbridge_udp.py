"""向本机 UE MotionBridge 接收组件发送一小段协议示例数据。"""

from __future__ import annotations

import argparse
import json
import math
import socket
import time


def axis_angle(axis: tuple[float, float, float], degrees: float) -> list[float]:
    half_angle = math.radians(degrees) * 0.5
    scale = math.sin(half_angle)
    return [
        axis[0] * scale,
        axis[1] * scale,
        axis[2] * scale,
        math.cos(half_angle),
    ]


def send_packet(sock: socket.socket, address: tuple[str, int], packet: dict) -> None:
    payload = json.dumps(packet, separators=(",", ":")).encode("utf-8")
    sock.sendto(payload, address)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7001)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument(
        "--reconnect-gap",
        type=float,
        default=0.0,
        help="首段后暂停多少秒，再从时间戳 0 重发同一条流",
    )
    parser.add_argument(
        "--first-burst-frames",
        type=int,
        default=15,
        help="重连测试首段发送的帧数",
    )
    parser.add_argument(
        "--stream-file",
        help="回放 unreal_export_manny_pose_stream.py 导出的 JSON",
    )
    args = parser.parse_args()

    address = (args.host, args.port)
    if args.stream_file:
        with open(args.stream_file, "r", encoding="utf-8") as stream_file:
            stream = json.load(stream_file)
        bones = stream["bones"]
        frames = stream["frames"]
        stream_id = stream["stream_id"]
    else:
        bones = [
            "root",
            "pelvis",
            "spine_01",
            "spine_02",
            "spine_03",
            "spine_04",
            "spine_05",
            "neck_01",
            "neck_02",
            "head",
            "clavicle_l",
            "upperarm_l",
            "lowerarm_l",
            "hand_l",
            "clavicle_r",
            "upperarm_r",
            "lowerarm_r",
            "hand_r",
            "thigh_l",
            "calf_l",
            "foot_l",
            "ball_l",
            "thigh_r",
            "calf_r",
            "foot_r",
            "ball_r",
        ]
        rotations_by_bone = {
            "spine_01": axis_angle((1.0, 0.0, 0.0), 22.0),
            "neck_01": axis_angle((0.0, 0.0, 1.0), 17.0),
            "head": axis_angle((0.0, 1.0, 0.0), -13.0),
        }
        rotations = [
            rotations_by_bone.get(bone, [0.0, 0.0, 0.0, 1.0])
            for bone in bones
        ]
        frame_interval = 1.0 / max(args.fps, 1.0)
        frames = [
            {
                "frame_index": frame_index,
                "timestamp": frame_index * frame_interval,
                "root_position": [0.0, 0.0, 0.0],
                "root_rotation": [0.0, 0.0, 0.0, 1.0],
                "bone_rotations": rotations,
            }
            for frame_index in range(args.frames)
        ]
        stream_id = "python-local-test"

    def send_stream_definition(sock: socket.socket) -> None:
        send_packet(
            sock,
            address,
            {
                "type": "stream",
                "protocol_version": 1,
                "stream_id": stream_id,
                "bones": bones,
            },
        )

    def send_frames(sock: socket.socket, frames_to_send: list[dict]) -> None:
        time.sleep(0.05)
        previous_timestamp = frames_to_send[0]["timestamp"]
        for frame in frames_to_send:
            timestamp = frame["timestamp"]
            time.sleep(max(timestamp - previous_timestamp, 0.0))
            send_packet(
                sock,
                address,
                {"type": "frame", **frame, "stream_id": stream_id},
            )
            previous_timestamp = timestamp

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        if args.reconnect_gap > 0.0:
            first_burst_count = max(1, min(args.first_burst_frames, len(frames)))
            send_stream_definition(sock)
            send_frames(sock, frames[:first_burst_count])
            time.sleep(args.reconnect_gap)

        send_stream_definition(sock)
        send_frames(sock, frames)

    if args.reconnect_gap > 0.0:
        print(
            "sent reconnect test: "
            f"{first_burst_count} frames, {args.reconnect_gap:.3f}s gap, "
            f"then {len(frames)} frames to {args.host}:{args.port}"
        )
    else:
        print(
            f"sent stream definition and {len(frames)} frames "
            f"to {args.host}:{args.port}"
        )


if __name__ == "__main__":
    main()
