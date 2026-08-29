#!/usr/bin/env python3
"""Build the minimal BBK 9288 RTC-versus-GUI-timer probe."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import build_9288  # noqa: E402


BUILD = ROOT / "build" / "rtc-timer-probe"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument(
        "--output", type=Path, default=BUILD / "RTC_TEST.exe"
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
        "-fomit-frame-pointer",
        "-fdata-sections",
        "-ffunction-sections",
        "-Wall",
        "-Wextra",
        "-Werror",
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
        ROOT / "tests" / "9288_rtc_timer_probe.c",
    ]
    objects: list[Path] = []
    for source in sources:
        output = BUILD / f"{source.stem}.o"
        run([clang, *flags, "-c", str(source), "-o", str(output)])
        objects.append(output)

    elf = BUILD / "RTC_TEST.elf"
    map_path = BUILD / "RTC_TEST.map"
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
    raw = BUILD / "RTC_TEST.bin"
    run([objcopy, "-O", "binary", "--gap-fill", "255", str(elf), str(raw)])
    payload = raw.read_bytes()
    payload_end = build_9288.read_map_symbol(map_path, "__payload_end")
    expected = payload_end - build_9288.APP_LOAD_ADDRESS
    if len(payload) != expected:
        raise SystemExit(f"payload size {len(payload)} != expected {expected}")

    app = bytearray(build_9288.pack_kf2(payload))
    app[12:28] = b"RTC_TEST".ljust(16, b"\0")
    args.output.write_bytes(app)
    print(f"built: {args.output.resolve()}")
    print(f"size: {len(app)}")
    print(f"sha256: {hashlib.sha256(app).hexdigest()}")


if __name__ == "__main__":
    main()
