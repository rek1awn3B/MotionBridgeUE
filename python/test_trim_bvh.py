import unittest

from bvh_parser import parse_bvh
from test_bvh_parser import MINIMAL_BVH
from trim_bvh import trim_bvh_text


class TrimBvhTests(unittest.TestCase):
    def test_keeps_inclusive_frame_range_and_hierarchy(self) -> None:
        source = parse_bvh(MINIMAL_BVH)
        trimmed = parse_bvh(trim_bvh_text(MINIMAL_BVH, 1, 1))

        self.assertEqual(trimmed.motion.frame_count, 1)
        self.assertEqual(trimmed.motion.frames[0], source.motion.frames[1])
        self.assertEqual(
            [joint.name for joint in trimmed.joints],
            ["Hips", "Chest", "Chest_EndSite"],
        )

    def test_rejects_invalid_ranges(self) -> None:
        with self.assertRaisesRegex(ValueError, "non-negative"):
            trim_bvh_text(MINIMAL_BVH, -1, 0)
        with self.assertRaisesRegex(ValueError, "smaller"):
            trim_bvh_text(MINIMAL_BVH, 1, 0)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            trim_bvh_text(MINIMAL_BVH, 0, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
