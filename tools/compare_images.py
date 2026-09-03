#!/usr/bin/env python3
import argparse
import shutil
from pathlib import Path

from PIL import Image, ImageChops


EXPECTED_SIZE = (320, 240)
EXPECTED_MODE = "RGB"


def _load(path: Path) -> Image.Image:
    image = Image.open(path)
    image.load()
    if image.size != EXPECTED_SIZE:
        raise ValueError(f"{path} must be an exact 320x240 image; got {image.size}")
    if image.mode != EXPECTED_MODE:
        raise ValueError(f"{path} must use RGB mode; got {image.mode}")
    return image


def validate(path: Path) -> None:
    image = _load(path)
    image.close()


def compare(expected_path: Path, actual_path: Path, failure_dir: Path) -> bool:
    expected = _load(expected_path)
    actual = _load(actual_path)
    difference = ImageChops.difference(expected, actual)
    if difference.getbbox() is None:
        return True

    failure_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(expected_path, failure_dir / "expected.png")
    shutil.copyfile(actual_path, failure_dir / "actual.png")
    highlighted = actual.copy()
    expected_pixels = expected.load()
    actual_pixels = actual.load()
    highlighted_pixels = highlighted.load()
    for y in range(EXPECTED_SIZE[1]):
        for x in range(EXPECTED_SIZE[0]):
            if expected_pixels[x, y] != actual_pixels[x, y]:
                highlighted_pixels[x, y] = (255, 0, 255)
    highlighted.save(failure_dir / "diff.png")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Exact CYD screenshot comparator")
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("failure_dir", type=Path)
    args = parser.parse_args()
    if compare(args.expected, args.actual, args.failure_dir):
        return 0
    print(f"pixel mismatch: {args.expected} != {args.actual}")
    print(f"failure images: {args.failure_dir}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
