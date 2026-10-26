import asyncio
import importlib.util
import math
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "motionbridge_websocket_server.py"
)
SPEC = importlib.util.spec_from_file_location("motionbridge_websocket_server", MODULE_PATH)
SERVER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(SERVER)


def sample_stream():
    return {
        "protocol_version": 1,
        "stream_id": "test",
        "fps": 20.0,
        "bones": ["root"],
        "frames": [
            {
                "frame_index": 99,
                "timestamp": 12.0,
                "root_position": [0.0, 0.0, 0.0],
                "root_rotation": [0.0, 0.0, 0.0, 1.0],
                "bone_rotations": [[0.0, 0.0, 0.0, 1.0]],
            }
        ],
    }


class WebSocketServerTests(unittest.TestCase):
    def test_stream_definition_can_advertise_facial_channels(self):
        definition = SERVER.build_stream_definition(
            sample_stream(), facial_demo=True
        )
        self.assertEqual(definition["type"], "stream")
        self.assertEqual(definition["bones"], ["root"])
        self.assertEqual(definition["channels"], SERVER.FACIAL_CHANNELS)

    def test_stream_definition_preserves_source_provenance(self):
        stream = sample_stream()
        stream["source_format"] = "BVH"
        stream["source_file"] = "walk.bvh"
        stream["retarget_mode"] = "calibrated-map"
        definition = SERVER.build_stream_definition(stream)
        self.assertEqual(definition["source_format"], "BVH")
        self.assertEqual(definition["source_file"], "walk.bvh")
        self.assertEqual(definition["retarget_mode"], "calibrated-map")

    def test_frame_packet_rewrites_clock_and_adds_sender_timestamp(self):
        packet = SERVER.build_frame_packet(
            sample_stream()["frames"][0],
            frame_index=7,
            timestamp=0.35,
            facial_demo=True,
            sent_at_unix_seconds=1234.5,
            stream_id="test",
        )
        self.assertEqual(packet["type"], "frame")
        self.assertEqual(packet["frame_index"], 7)
        self.assertTrue(math.isclose(packet["timestamp"], 0.35))
        self.assertEqual(packet["sent_at_unix_seconds"], 1234.5)
        self.assertEqual(packet["stream_id"], "test")
        self.assertEqual(len(packet["channels"]), 4)

    def test_channel_only_stream_definition_does_not_require_bones(self):
        stream = sample_stream()
        stream.pop("bones")
        stream["channels"] = ["Smile"]
        definition = SERVER.build_stream_definition(stream)
        self.assertNotIn("bones", definition)
        self.assertEqual(definition["channels"], ["Smile"])

    def test_non_looping_packet_iterator_is_monotonic(self):
        stream = sample_stream()
        stream["frames"] = stream["frames"] * 3

        async def collect():
            result = []
            async for packet, interval in SERVER.iter_frame_packets(
                stream,
                loop_stream=False,
                facial_demo=False,
            ):
                result.append((packet, interval))
            return result

        packets = asyncio.run(collect())
        self.assertEqual(
            [packet["frame_index"] for packet, _ in packets],
            [0, 1, 2],
        )
        self.assertEqual(
            [packet["timestamp"] for packet, _ in packets],
            [0.0, 0.05, 0.1],
        )
        self.assertTrue(
            all(math.isclose(interval, 0.05) for _, interval in packets)
        )
        self.assertTrue(
            all(packet["stream_id"] == "test" for packet, _ in packets)
        )
