import unittest
from pathlib import Path

from bvh_parser import (
    BvhData,
    BvhJoint,
    BvhMotion,
    BvhParseError,
    TokenStream,
    parse_bvh,
    tokenize_bvh,
)


MINIMAL_BVH = """\
HIERARCHY
ROOT Hips
{
    OFFSET 0.0 0.0 0.0
    CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
    JOINT Chest
    {
        OFFSET 0.0 10.0 0.0
        CHANNELS 3 Yrotation Zrotation Xrotation
        End Site
        {
            OFFSET 0.0 8.0 0.0
        }
    }
}
MOTION
Frames: 2
Frame Time: 0.0333333
0 0 0 0 0 0 0 0 0
1 2 3 10 20 30 40 50 60
"""


class BvhTokenizerTests(unittest.TestCase):
    def test_separates_braces_from_adjacent_text(self) -> None:
        source = "ROOT Hips{OFFSET 0 1 0}"

        self.assertEqual(
            tokenize_bvh(source),
            ["ROOT", "Hips", "{", "OFFSET", "0", "1", "0", "}"],
        )

    def test_preserves_signed_decimal_and_exponent_numbers(self) -> None:
        source = "OFFSET -1.5 0.0 2e-3"

        self.assertEqual(
            tokenize_bvh(source),
            ["OFFSET", "-1.5", "0.0", "2e-3"],
        )

    def test_keeps_multiword_constructs_as_separate_tokens(self) -> None:
        source = "End Site { }\nFrames: 2\nFrame Time: 0.0333333"

        self.assertEqual(
            tokenize_bvh(source),
            [
                "End",
                "Site",
                "{",
                "}",
                "Frames:",
                "2",
                "Frame",
                "Time:",
                "0.0333333",
            ],
        )


class TokenStreamTests(unittest.TestCase):
    def test_peek_does_not_consume_but_take_does(self) -> None:
        stream = TokenStream(["ROOT", "Hips"])

        self.assertEqual(stream.peek(), "ROOT")
        self.assertEqual(stream.peek(), "ROOT")
        self.assertEqual(stream.take(), "ROOT")
        self.assertEqual(stream.take(), "Hips")
        self.assertTrue(stream.at_end())

    def test_reads_typed_values(self) -> None:
        stream = TokenStream(["3", "-1.5", "2e-3"])

        self.assertEqual(stream.take_int(), 3)
        self.assertEqual(stream.take_float(), -1.5)
        self.assertEqual(stream.take_float(), 0.002)

    def test_expect_reports_unexpected_token(self) -> None:
        stream = TokenStream(["JOINT"])

        with self.assertRaisesRegex(
            BvhParseError,
            "Expected 'ROOT', got 'JOINT'",
        ):
            stream.expect("ROOT")

    def test_reading_past_end_reports_parse_error(self) -> None:
        stream = TokenStream([])

        with self.assertRaisesRegex(BvhParseError, "Unexpected end"):
            stream.take()


class BvhDataTests(unittest.TestCase):
    def test_channel_count_sums_channels_from_all_joints(self) -> None:
        data = BvhData(
            joints=[
                BvhJoint(
                    "Hips",
                    -1,
                    (0.0, 0.0, 0.0),
                    [
                        "Xposition",
                        "Yposition",
                        "Zposition",
                        "Zrotation",
                        "Xrotation",
                        "Yrotation",
                    ],
                ),
                BvhJoint(
                    "Chest",
                    0,
                    (0.0, 10.0, 0.0),
                    ["Yrotation", "Zrotation", "Xrotation"],
                ),
                BvhJoint(
                    "Chest_EndSite",
                    1,
                    (0.0, 8.0, 0.0),
                    [],
                    is_end_site=True,
                ),
            ],
            motion=BvhMotion(frame_time=1.0 / 30.0, frames=[]),
        )

        self.assertEqual(data.channel_count, 9)


class BvhParserTests(unittest.TestCase):
    def test_parses_hierarchy_and_motion_metadata(self) -> None:
        data = parse_bvh(MINIMAL_BVH)

        self.assertEqual(
            [joint.name for joint in data.joints],
            ["Hips", "Chest", "Chest_EndSite"],
        )
        self.assertEqual(
            [joint.parent_index for joint in data.joints],
            [-1, 0, 1],
        )
        self.assertEqual(data.joints[1].offset, (0.0, 10.0, 0.0))
        self.assertTrue(data.joints[2].is_end_site)
        self.assertEqual(len(data.motion.frames), 2)
        self.assertAlmostEqual(data.motion.frame_time, 0.0333333)

    def test_preserves_each_joints_declared_channel_order(self) -> None:
        data = parse_bvh(MINIMAL_BVH)

        self.assertEqual(
            data.joints[0].channels,
            [
                "Xposition",
                "Yposition",
                "Zposition",
                "Zrotation",
                "Xrotation",
                "Yrotation",
            ],
        )
        self.assertEqual(
            data.joints[1].channels,
            ["Yrotation", "Zrotation", "Xrotation"],
        )
        self.assertEqual(data.joints[2].channels, [])

    def test_rejects_extra_motion_values(self) -> None:
        with self.assertRaisesRegex(
            BvhParseError,
            "Unexpected trailing token '99'",
        ):
            parse_bvh(MINIMAL_BVH + "99\n")

    def test_parses_licensed_real_bvh_fixture(self) -> None:
        fixture_path = (
            Path(__file__).resolve().parent.parent
            / "data"
            / "bvh"
            / "test_freebvh.bvh"
        )
        data = parse_bvh(fixture_path.read_text(encoding="utf-8"))

        self.assertEqual(len(data.joints), 70)
        self.assertEqual(
            sum(not joint.is_end_site for joint in data.joints),
            55,
        )
        self.assertEqual(data.channel_count, 168)
        self.assertEqual(data.motion.frame_count, 69)
        self.assertAlmostEqual(data.motion.frame_time, 0.0333333)
        self.assertEqual(
            data.joints[0].channels,
            [
                "Xposition",
                "Yposition",
                "Zposition",
                "Zrotation",
                "Yrotation",
                "Xrotation",
            ],
        )

if __name__ == "__main__":
    unittest.main(verbosity=2)
