#!/usr/bin/env python3
"""Build the BBK 9288 SDK-vs-direct-framebuffer benchmark."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import build_9288  # noqa: E402


BUILD = ROOT / "build" / "frame-path-benchmark"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build" / "9288-FRAME-PATH-BENCH-V2.exe",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    include = BUILD / "sdk-include"
    build_9288.prepare_sdk_headers(args.sdk.resolve(), include)
    clang = build_9288.find_tool(args.toolchain.resolve(), "clang")
    objcopy = build_9288.find_tool(args.toolchain.resolve(), "llvm-objcopy")
    flags = [
        "--target=s1c33-none-elf",
        "-O2",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-jump-tables",
        "-fomit-frame-pointer",
        "-fdata-sections",
        "-ffunction-sections",
        "-fno-strict-aliasing",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
        "-DDL_DOWN",
        "-D_RLS_",
        "-I",
        str(ROOT / "src" / "9288_compat"),
        "-I",
        str(include),
        "-I",
        str(ROOT / "src"),
    ]
    sources = [
        ROOT / "src" / "gam4980_9288_start.c",
        ROOT / "src" / "gam4980_9288_runtime.c",
        ROOT / "tests" / "9288_frame_path_benchmark.c",
    ]
    objects: list[Path] = []
    for source in sources:
        output = BUILD / f"{source.stem}.o"
        run([clang, *flags, "-c", str(source), "-o", str(output)])
        objects.append(output)

    elf = BUILD / "FRAME_PATH_BENCH.elf"
    map_path = BUILD / "FRAME_PATH_BENCH.map"
    run(
        [
            clang,
            "--target=s1c33-none-elf",
            "-fuse-ld=lld",
            "-nostdlib",
            "-Wl,--gc-sections",
            f"-Wl,-T,{ROOT / 'src' / 'gam4980_9288.ld'}",
            f"-Wl,-Map,{map_path}",
            *(str(item) for item in objects),
            "-o",
            str(elf),
        ]
    )
    raw = BUILD / "FRAME_PATH_BENCH.bin"
    run([objcopy, "-O", "binary", "--gap-fill", "255", str(elf), str(raw)])
    payload = raw.read_bytes()
    payload_end = build_9288.read_map_symbol(map_path, "__payload_end")
    expected = payload_end - build_9288.APP_LOAD_ADDRESS
    if len(payload) != expected:
        raise SystemExit(f"payload size {len(payload)} != expected {expected}")

    app = bytearray(build_9288.pack_kf2(payload))
    app[12:28] = b"FRAME_PATH_BENCH"
    args.output.write_bytes(app)
    print(f"built: {args.output.resolve()}")
    print(f"size: {len(app)}")
    print(f"sha256: {hashlib.sha256(app).hexdigest()}")


if __name__ == "__main__":
    main()
