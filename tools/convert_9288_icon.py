#!/usr/bin/env python3
"""Convert the original GAM4980 PNG into BBK 9288 KF2 icon resources."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct

from PIL import Image


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (
    PROJECT_ROOT / "assets" / "9288" / "manual-grid-v4" / "dictionary-30x30.png"
)
DEFAULT_OUTPUT = PROJECT_ROOT / "assets" / "9288"
DEFAULT_FRAME_ROOT = PROJECT_ROOT / "sdk" / "apmk"
BIG_ICON_SPEC = ("ico1", 40, 40, (5, 5, 35, 35))
SMALL_ICON_SPEC = ("ico2", 16, 16)


def quantize_grayscale(image: Image.Image, width: int, height: int) -> list[int]:
    rgba = image.convert("RGBA")
    opaque = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    opaque.alpha_composite(rgba)
    grayscale = opaque.convert("L").resize(
        (width, height), Image.Resampling.LANCZOS
    )

    # V4 has deliberate thick contours. Use the nearest hardware gray level
    # rather than the V3 darkening curve, which crushed the keyboard gaps.
    pixels = []
    for value in grayscale.tobytes():
        if value < 43:
            pixels.append(0)
        elif value < 128:
            pixels.append(1)
        elif value < 213:
            pixels.append(2)
        else:
            pixels.append(3)
    if len(set(pixels)) != 4:
        raise ValueError("expected all four fixed grayscale levels")
    return pixels


def decode_icon(path: Path, width: int, height: int) -> list[int]:
    data = path.read_bytes()
    expected_size = 12 + width * height * 2 // 8
    if len(data) != expected_size:
        raise ValueError(f"invalid 9288 frame resource size: {path}")
    if struct.unpack_from("<HHHHHH", data) != (width, height, 2, 0, 0, 0):
        raise ValueError(f"invalid 9288 frame resource header: {path}")

    pixels: list[int] = []
    for value in data[12:]:
        pixels.extend(
            ((value >> 6) & 3, (value >> 4) & 3, (value >> 2) & 3, value & 3)
        )
    return pixels


def crop_icon_art(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    bounding_box = rgba.getchannel("A").getbbox()
    if bounding_box is None:
        raise ValueError("source icon is fully transparent")
    if bounding_box == (0, 0, rgba.width, rgba.height):
        # Imagegen already supplied a centered square with safe whitespace.
        # Preserve that composition instead of adding a second padding layer.
        return rgba
    cropped = rgba.crop(bounding_box)
    side = max(cropped.size)
    padding = max(1, side // 24)
    square = Image.new(
        "RGBA", (side + padding * 2, side + padding * 2), (255, 255, 255, 0)
    )
    square.alpha_composite(
        cropped,
        ((square.width - cropped.width) // 2, (square.height - cropped.height) // 2),
    )
    return square


def compose_with_frame(
    frame: list[int],
    width: int,
    height: int,
    inner_box: tuple[int, int, int, int],
    artwork: Image.Image,
) -> list[int]:
    left, top, right, bottom = inner_box
    inner_width = right - left
    inner_height = bottom - top
    content = quantize_grayscale(artwork, inner_width, inner_height)
    output = frame.copy()
    for y in range(inner_height):
        source_offset = y * inner_width
        target_offset = (top + y) * width + left
        output[target_offset : target_offset + inner_width] = content[
            source_offset : source_offset + inner_width
        ]
    return output


def encode_icon(width: int, height: int, pixels: list[int]) -> bytes:
    if width % 4:
        raise ValueError("9288 2-bpp icon width must be divisible by four")
    if len(pixels) != width * height:
        raise ValueError("pixel count does not match icon dimensions")

    payload = bytearray()
    for offset in range(0, len(pixels), 4):
        p0, p1, p2, p3 = pixels[offset : offset + 4]
        payload.append((p0 << 6) | (p1 << 4) | (p2 << 2) | p3)
    header = struct.pack("<HHHHHH", width, height, 2, 0, 0, 0)
    return header + payload


def write_preview(path: Path, width: int, height: int, pixels: list[int]) -> None:
    preview = Image.new("L", (width, height))
    preview.putdata([level * 85 for level in pixels])
    preview.save(path)
    # Nearest-neighbor enlargement shows the actual hardware pixels, not
    # a smoothed high-resolution illustration.
    preview.resize((width * 6, height * 6), Image.Resampling.NEAREST).save(
        path.with_name(path.stem + "-6x.png")
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert the original GAM4980 icon to 9288 2-bpp resources"
    )
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--small-source", type=Path,
                        default=PROJECT_ROOT / "assets/9288/gam4980-icon-selected-12.png",
                        help="separately simplified artwork for the 16x16 icon")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--frame-root",
        type=Path,
        default=DEFAULT_FRAME_ROOT,
        help="directory containing the SDK ico1.bin frame template",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    frame_root = args.frame_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    artwork = crop_icon_art(Image.open(source))

    stem, width, height, inner_box = BIG_ICON_SPEC
    frame = decode_icon(frame_root / f"{stem}.bin", width, height)
    pixels = compose_with_frame(frame, width, height, inner_box, artwork)
    resource = encode_icon(width, height, pixels)
    resource_path = output / f"{stem}.bin"
    preview_path = output / f"{stem}.png"
    resource_path.write_bytes(resource)
    write_preview(preview_path, width, height, pixels)
    print(f"built: {resource_path} ({len(resource)} bytes)")
    print(f"preview: {preview_path}")

    # Use a dedicated micro-icon rather than shrinking the detailed main icon.
    # Retain the SDK's exact outer frame; only its interior is replaced.
    stem, width, height = SMALL_ICON_SPEC
    small_art = crop_icon_art(Image.open(args.small_source))
    small_frame = decode_icon(frame_root / f"{stem}.bin", width, height)
    pixels = compose_with_frame(small_frame, width, height, (2, 2, 14, 14), small_art)
    resource = encode_icon(width, height, pixels)
    resource_path = output / f"{stem}.bin"
    preview_path = output / f"{stem}.png"
    resource_path.write_bytes(resource)
    write_preview(preview_path, width, height, pixels)
    print(f"built: {resource_path} ({len(resource)} bytes)")
    print(f"preview: {preview_path}")


if __name__ == "__main__":
    main()
