#!/usr/bin/env python3
"""Build the true-S1C33 shadow-super equivalence probe."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import build_9288  # noqa: E402


BUILD = ROOT / "build" / "iram-super-equivalence"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def capture(command: list[str]) -> str:
    print("+", " ".join(command))
    return subprocess.check_output(command, cwd=ROOT, text=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument('--compiled-registers', action='store_true')
    parser.add_argument('--atomic-contracts', action='store_true')
    parser.add_argument('--profile-private', action='store_true')
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--engine-source", type=Path,
                        default=ROOT / "src" / "s6502_iram_asm.S")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build" / "9288-IRAM-SUPER-EQ.exe",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    include = BUILD / "sdk-include"
    build_9288.prepare_sdk_headers(args.sdk.resolve(), include)
    toolchain = args.toolchain.resolve()
    clang = build_9288.find_tool(toolchain, "clang")
    objcopy = build_9288.find_tool(toolchain, "llvm-objcopy")
    objdump = build_9288.find_tool(toolchain, "llvm-objdump")
    flags = [
        "--target=s1c33-none-elf",
        "-O2",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-jump-tables",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-fomit-frame-pointer",
        "-fdata-sections",
        "-ffunction-sections",
        "-fno-strict-aliasing",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-unused-function",
        "-Wno-nonportable-include-path",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
        "-DDL_DOWN",
        "-D_RLS_",
        "-DGAM4980_ENABLE_IRAM_HOT_CORE",
        "-DGAM4980_IRAM_V2",
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
        args.engine_source.resolve(),
        ROOT / "tests" / "9288_iram_super_equivalence.c",
    ]
    sys.path.insert(0, str(ROOT / 'tools'))
    import native_register_abi
    from build_firmware_native import specialize_source
    register_source = BUILD / 'register_leaves.c'
    if not args.compiled_registers:
        register_source.write_text('\n'.join(native_register_abi.source('eq_' + hex(pc)[2:], pc, atomic=args.atomic_contracts)
            for pc in sorted(native_register_abi.ENTRIES)), encoding='utf-8')
        sources += [register_source]
    original = (ROOT / 'src' / 'firmware_native_runtime.c').read_text(encoding='utf-8')
    for pc in sorted(native_register_abi.ENTRIES):
        from native_register_codegen import bridge_source
        adapter = BUILD / ('bridge_' + hex(pc)[2:] + '.S')
        adapter.write_text(bridge_source('bridge_'+hex(pc)[2:], None, 0,
            external_target='eq_'+hex(pc)[2:]+'_register',
            record_metrics=not args.compiled_registers), encoding='utf-8')
        sources.append(adapter)
        reference, symbol = specialize_source(original, 'runtime', pc)
        if args.atomic_contracts:
            import native_atomic_contracts
            reference = native_atomic_contracts.lower(reference, 'runtime', pc)
        reference_path = BUILD / (symbol + '.c')
        reference_path.write_text(reference, encoding='utf-8')
        sources.append(reference_path)
        if args.compiled_registers:
            from native_register_codegen import lower
            generated = lower(reference, symbol).replace(symbol + '_register', 'eq_' + hex(pc)[2:] + '_register')
            generated_path = BUILD / ('compiled_' + hex(pc)[2:] + '.c')
            generated_path.write_text(generated, encoding='utf-8')
            sources.append(generated_path)
    flags.append('-DGAM4980_DYNAMIC_NATIVE_ALL')
    if args.profile_private:
        flags.append('-DGAM4980_NATIVE_PROFILE_TEST')
    if args.atomic_contracts:
        # Old instruction-cost expressions are intentionally dead after lowering.
        flags.append('-Wno-unused-but-set-variable')
    objects: list[Path] = []
    for source in sources:
        output = BUILD / f"{source.stem}.o"
        run([clang, *flags, "-c", str(source), "-o", str(output)])
        objects.append(output)

    elf = BUILD / "IRAM_SUPER_EQ.elf"
    map_path = BUILD / "IRAM_SUPER_EQ.map"
    run([
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
    ])
    iram_start = build_9288.read_map_symbol(map_path, "__iram_start")
    iram_end = build_9288.read_map_symbol(map_path, "__iram_end")
    engine_start = build_9288.read_map_symbol(
        map_path, "__iram_exec_engine_start"
    )
    engine_end = build_9288.read_map_symbol(
        map_path, "__iram_exec_engine_end"
    )
    if iram_start != 0x800 or engine_start != 0x800:
        raise SystemExit(
            f"engine is not linked at true IRAM base: {engine_start:#x}"
        )
    if iram_end - iram_start > 0x16C8 or engine_end <= engine_start:
        raise SystemExit(
            f"invalid IRAM engine extent: {engine_start:#x}..{engine_end:#x}"
        )
    disassembly = capture([objdump, "-d", str(elf)])
    (BUILD / "IRAM_SUPER_EQ.dis.txt").write_text(
        disassembly, encoding="utf-8"
    )
    if "<s6502_iram_exec_burst_asm>:" not in disassembly:
        raise SystemExit("linked image does not contain the assembly engine")

    raw = BUILD / "IRAM_SUPER_EQ.bin"
    run([objcopy, "-O", "binary", "--gap-fill", "255", str(elf), str(raw)])
    payload = raw.read_bytes()
    payload_end = build_9288.read_map_symbol(map_path, "__payload_end")
    expected = payload_end - build_9288.APP_LOAD_ADDRESS
    if len(payload) != expected:
        raise SystemExit(f"payload size {len(payload)} != expected {expected}")

    app = bytearray(build_9288.pack_kf2(payload))
    app[12:28] = b"IRAM_SUPER_EQ".ljust(16, b"\0")
    args.output.write_bytes(app)
    print(f"engine: {engine_end - engine_start} bytes at 0x{engine_start:08x}")
    print(f"map: {map_path.resolve()}")
    print(f"built: {args.output.resolve()}")
    print(f"size: {len(app)}")
    print(f"sha256: {hashlib.sha256(app).hexdigest()}")


if __name__ == "__main__":
    main()
