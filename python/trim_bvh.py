from __future__ import annotations

import argparse
import re
from pathlib import Path

from bvh_parser import parse_bvh


_MOTION_MARKER = re.compile(r"(?m)^MOTION\s*$")


def trim_bvh_text(
    source: str,
    start_frame: int,
    stop_frame: int,
) -> str:
    """裁出闭区间 `[start_frame, stop_frame]`，保留原骨架声明。"""

    data = parse_bvh(source)
    if start_frame < 0:
        raise ValueError("start frame must be non-negative")
    if stop_frame < start_frame:
        raise ValueError("stop frame must not be smaller than start frame")
    if stop_frame >= data.motion.frame_count:
        raise ValueError(
            f"stop frame {stop_frame} exceeds the last source frame "
            f"{data.motion.frame_count - 1}"
        )

    marker = _MOTION_MARKER.search(source)
    if marker is None:
        raise ValueError("BVH source is missing the MOTION marker")

    hierarchy_text = source[: marker.start()].rstrip()
    selected_frames = data.motion.frames[start_frame : stop_frame + 1]
    frame_lines = [
        " ".join(f"{value:.9f}".rstrip("0").rstrip(".") or "0" for value in frame)
        for frame in selected_frames
    ]
    lines = [
        hierarchy_text,
        "MOTION",
        f"Frames: {len(selected_frames)}",
        f"Frame Time: {data.motion.frame_time:.9f}".rstrip("0").rstrip("."),
        *frame_lines,
    ]
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Trim an inclusive frame range from a BVH file."
    )
    parser.add_argument("source_bvh", type=Path)
    parser.add_argument("output_bvh", type=Path)
    parser.add_argument("--start", type=int, required=True)
    parser.add_argument("--stop", type=int, required=True)
    arguments = parser.parse_args()

    source = arguments.source_bvh.read_text(encoding="utf-8")
    output = trim_bvh_text(source, arguments.start, arguments.stop)
    arguments.output_bvh.parent.mkdir(parents=True, exist_ok=True)
    arguments.output_bvh.write_text(output, encoding="utf-8")

    parsed_output = parse_bvh(output)
    print(f"SOURCE_FRAMES={parse_bvh(source).motion.frame_count}")
    print(f"START_FRAME={arguments.start}")
    print(f"STOP_FRAME={arguments.stop}")
    print(f"OUTPUT_FRAMES={parsed_output.motion.frame_count}")
    print(f"OUTPUT={arguments.output_bvh.resolve()}")


if __name__ == "__main__":
    main()
