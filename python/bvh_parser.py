from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


_TOKEN_PATTERN = re.compile(r"[{}]|[^\s{}]+")


def tokenize_bvh(source: str) -> list[str]:
    """将 BVH 文本切分为关键字、名称、数字和独立的大括号。"""

    return _TOKEN_PATTERN.findall(source)


class BvhParseError(ValueError):
    """BVH 文本不符合预期格式。"""


@dataclass
class BvhJoint:
    name: str
    parent_index: int
    offset: tuple[float, float, float]
    channels: list[str]
    is_end_site: bool = False


@dataclass
class BvhMotion:
    frame_time: float
    frames: list[list[float]]

    @property
    def frame_count(self) -> int:
        return len(self.frames)

    @property
    def fps(self) -> float:
        return 1.0 / self.frame_time


@dataclass
class BvhData:
    joints: list[BvhJoint]
    motion: BvhMotion

    @property
    def channel_count(self) -> int:
        return sum(len(joint.channels) for joint in self.joints)


class TokenStream:
    def __init__(self, tokens: list[str]) -> None:
        self._tokens = tokens
        self._position = 0

    def at_end(self) -> bool:
        return self._position >= len(self._tokens)

    def peek(self) -> str:
        if self.at_end():
            raise BvhParseError("Unexpected end of BVH data.")

        return self._tokens[self._position]

    def take(self) -> str:
        token = self.peek()
        self._position += 1
        return token

    def expect(self, expected: str) -> None:
        actual = self.take()
        if actual != expected:
            raise BvhParseError(
                f"Expected {expected!r}, got {actual!r} "
                f"at token {self._position - 1}."
            )

    def take_int(self) -> int:
        token = self.take()
        try:
            return int(token)
        except ValueError as error:
            raise BvhParseError(
                f"Expected an integer, got {token!r} "
                f"at token {self._position - 1}."
            ) from error

    def take_float(self) -> float:
        token = self.take()
        try:
            return float(token)
        except ValueError as error:
            raise BvhParseError(
                f"Expected a number, got {token!r} "
                f"at token {self._position - 1}."
            ) from error


def _parse_joint(
    stream: TokenStream,
    joints: list[BvhJoint],
    parent_index: int,
    declaration: str,
) -> None:
    is_end_site = declaration == "End"

    if is_end_site:
        stream.expect("Site")
        if parent_index < 0:
            raise BvhParseError("End Site must have a parent joint.")
        name = f"{joints[parent_index].name}_EndSite"
    else:
        name = stream.take()

    joint_index = len(joints)
    joint = BvhJoint(
        name=name,
        parent_index=parent_index,
        offset=(0.0, 0.0, 0.0),
        channels=[],
        is_end_site=is_end_site,
    )
    joints.append(joint)

    stream.expect("{")
    stream.expect("OFFSET")
    joint.offset = (
        stream.take_float(),
        stream.take_float(),
        stream.take_float(),
    )

    if is_end_site:
        stream.expect("}")
        return

    stream.expect("CHANNELS")
    channel_count = stream.take_int()
    if channel_count < 0:
        raise BvhParseError("CHANNELS count cannot be negative.")

    joint.channels = [stream.take() for _ in range(channel_count)]

    while stream.peek() != "}":
        child_declaration = stream.take()
        if child_declaration not in {"JOINT", "End"}:
            raise BvhParseError(
                f"Expected 'JOINT', 'End', or '}}', "
                f"got {child_declaration!r}."
            )

        _parse_joint(
            stream,
            joints,
            parent_index=joint_index,
            declaration=child_declaration,
        )

    stream.expect("}")


def _parse_hierarchy(stream: TokenStream) -> list[BvhJoint]:
    stream.expect("HIERARCHY")
    stream.expect("ROOT")

    joints: list[BvhJoint] = []
    _parse_joint(
        stream,
        joints,
        parent_index=-1,
        declaration="ROOT",
    )
    return joints


def _parse_motion(
    stream: TokenStream,
    channel_count: int,
) -> BvhMotion:
    stream.expect("MOTION")
    stream.expect("Frames:")
    frame_count = stream.take_int()
    if frame_count < 0:
        raise BvhParseError("Frame count cannot be negative.")

    stream.expect("Frame")
    stream.expect("Time:")
    frame_time = stream.take_float()
    if frame_time <= 0.0:
        raise BvhParseError("Frame Time must be positive.")

    frames = [
        [stream.take_float() for _ in range(channel_count)]
        for _ in range(frame_count)
    ]
    return BvhMotion(frame_time=frame_time, frames=frames)


def parse_bvh(source: str) -> BvhData:
    stream = TokenStream(tokenize_bvh(source))
    joints = _parse_hierarchy(stream)
    channel_count = sum(len(joint.channels) for joint in joints)
    motion = _parse_motion(stream, channel_count)

    if not stream.at_end():
        raise BvhParseError(
            f"Unexpected trailing token {stream.peek()!r}."
        )

    return BvhData(joints=joints, motion=motion)


def print_bvh_summary(data: BvhData) -> None:
    named_joint_count = sum(
        not joint.is_end_site for joint in data.joints
    )
    end_site_count = len(data.joints) - named_joint_count

    print(f"Named joints: {named_joint_count}")
    print(f"End Sites: {end_site_count}")
    print(f"Total hierarchy nodes: {len(data.joints)}")
    print(f"Channels per frame: {data.channel_count}")
    print(f"Frames: {data.motion.frame_count}")
    print(f"Frame Time: {data.motion.frame_time:.7f} s")
    print(f"FPS: {data.motion.fps:.3f}")
    print("\nHierarchy:")

    for index, joint in enumerate(data.joints):
        print(
            f"[{index:02d}] {joint.name} "
            f"parent={joint.parent_index:2d} "
            f"offset={joint.offset} "
            f"channels={joint.channels}"
        )


def main() -> None:
    argument_parser = argparse.ArgumentParser(
        description="Parse a BVH file with the MotionBridge parser."
    )
    argument_parser.add_argument("path", type=Path)
    arguments = argument_parser.parse_args()

    source = arguments.path.read_text(encoding="utf-8")
    data = parse_bvh(source)
    print_bvh_summary(data)


if __name__ == "__main__":
    main()
