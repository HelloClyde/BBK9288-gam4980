from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parent
SOURCE_ROOT = PROJECT_ROOT / "src"
ICON_ROOT = PROJECT_ROOT / "assets" / "9288"
BUILD_ROOT = PROJECT_ROOT / "build" / "9288"
DEFAULT_SDK = PROJECT_ROOT / "sdk"

MAGIC0 = 0x0032464B
MAGIC1 = 0x19760212
HEADER_SIZE = 0x30
MACHINE_9288 = 1
# KF2 module 8 is the home menu's Entertainment category.
APP_MODULE = 8
APP_NAME = b"GAM4980"
APP_LOAD_ADDRESS = 0x02700000
APP_RAM_END = 0x02800000
APP_MAX_PAYLOAD_SIZE = APP_RAM_END - APP_LOAD_ADDRESS


def run(command: list[str], step: str) -> None:
    print("+", " ".join(command))
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(f"{step}: tool not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"{step} failed with exit code {exc.returncode}") from exc


def compile_fingerprint(
    compiler: str,
    flags: list[str],
    source: Path,
    dependencies: list[Path],
) -> str:
    """Hash everything that can affect one cached object file."""
    digest = hashlib.sha256()
    compiler_path = Path(compiler)
    digest.update(str(compiler_path).encode("utf-8"))
    try:
        compiler_stat = compiler_path.stat()
        digest.update(
            f"\0{compiler_stat.st_size}\0{compiler_stat.st_mtime_ns}".encode(
                "ascii"
            )
        )
    except OSError:
        # A compiler found through PATH may not be represented by this spelling.
        # Its name still participates in the key.
        pass
    for flag in flags:
        digest.update(b"\0flag\0")
        digest.update(flag.encode("utf-8"))
    unique_paths = {source.resolve(), *(path.resolve() for path in dependencies)}
    canonical_path = lambda item: str(item).replace("\\", "/").casefold()
    for path in sorted(unique_paths, key=canonical_path):
        digest.update(b"\0file\0")
        # The SDK normalization directory lives on a Windows volume.  Its
        # case-insensitive filesystem may retain either Dsys.h or dsys.h when
        # both compatibility spellings target the same file.  Header identity
        # must therefore be case-folded or an unchanged SDK misses the cache
        # nondeterministically on each Python process.
        identity = canonical_path(path)
        digest.update(identity.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def compile_cached(
    compiler: str,
    flags: list[str],
    source: Path,
    output: Path,
    dependencies: list[Path],
    rebuild: bool,
) -> None:
    fingerprint = compile_fingerprint(
        compiler, flags, source, dependencies
    )
    stamp = output.with_suffix(output.suffix + ".sha256")
    if (
        not rebuild
        and output.is_file()
        and stamp.is_file()
        and stamp.read_text(encoding="ascii").strip() == fingerprint
    ):
        print(f"+ reuse {output.name} (inputs unchanged)")
        return
    if not rebuild and output.is_file() and stamp.is_file():
        previous = stamp.read_text(encoding="ascii").strip()
        print(
            f"+ cache miss {output.name}: "
            f"{previous[:12]} -> {fingerprint[:12]}"
        )
    run(
        [compiler, *flags, "-c", str(source), "-o", str(output)],
        f"compile {source.name}",
    )
    stamp.write_text(fingerprint + "\n", encoding="ascii")


def write_sdk_header(path: Path, data: bytes) -> None:
    lines = []
    for line in data.splitlines(keepends=True):
        if line.lstrip().startswith(b"#include"):
            line = line.replace(b"\\", b"/")
        lines.append(line)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(lines))


def prepare_sdk_headers(sdk: Path, destination: Path) -> None:
    source = sdk / "Down_Include"
    if not source.is_dir():
        raise SystemExit(f"9288 SDK headers not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    for header in source.rglob("*.h"):
        relative = header.relative_to(source)
        data = header.read_bytes()
        variants = {
            relative,
            Path(*(part.lower() for part in relative.parts)),
            Path(*(part.lower() for part in relative.parts[:-1]), relative.name),
        }
        for variant in variants:
            write_sdk_header(destination / variant, data)


def find_tool(toolchain: Path, name: str) -> str:
    candidates = (
        toolchain / name,
        toolchain / f"{name}.exe",
        toolchain / "bin" / name,
        toolchain / "bin" / f"{name}.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"missing S1C33 tool: {name}; searched under {toolchain}")


def read_map_symbol(map_path: Path, name: str) -> int:
    suffix = f" {name} = ."
    for line in map_path.read_text(encoding="utf-8").splitlines():
        if line.endswith(suffix):
            try:
                return int(line.split()[0], 16)
            except (IndexError, ValueError) as exc:
                raise SystemExit(f"invalid {name} entry in {map_path}") from exc
    raise SystemExit(f"missing {name} in linker map: {map_path}")


def compile_app(
    sdk: Path,
    toolchain: Path,
    switch_dispatch: bool,
    enable_aot: bool,
    game_load_aot: bool,
    aot_diagnostics: bool,
    lightweight_performance: bool,
    load_diagnostics: bool,
    memory_diagnostics: bool,
    optimization: str,
    firmware_hle_mask: int,
    aggressive_region_hle: bool,
    iram_hot_core: bool,
    iram_exec_engine: bool,
    iram_exec_asm: bool,
    external_exec: bool,
    bare_session: bool,
    dynamic_native_all: bool,
    firmware_native_rom: Path | None,
    native_game: Path | None,
    static_native_game: Path | None,
    static_native_budget: int,
    rebuild: bool,
) -> bytes:
    clang = find_tool(toolchain, "clang")
    objcopy = find_tool(toolchain, "llvm-objcopy")
    objdump = find_tool(toolchain, "llvm-objdump")
    readelf = find_tool(toolchain, "llvm-readelf")
    generated_include = BUILD_ROOT / "sdk-include"
    generated_native = BUILD_ROOT / "static-native"
    prepare_sdk_headers(sdk, generated_include)

    objects: list[Path] = []
    analysis_id = hashlib.sha256()
    for source in sorted(SOURCE_ROOT.glob('s6502*.h')):
        analysis_id.update(source.read_bytes())
    analysis_id.update((SOURCE_ROOT / 'gam4980_core.c').read_bytes())
    common_flags = [
        f"-DGAM4980_ANALYSIS_BUILD_ID=0x{analysis_id.hexdigest()[:8]}u",
        "--target=s1c33-none-elf",
        f"-O{optimization}",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-jump-tables",
        "-fomit-frame-pointer",
        "-fdata-sections",
        "-ffunction-sections",
        "-fno-strict-aliasing",
        "-Wall",
        "-Wextra",
        "-Wno-unused-function",
        "-Wno-unused-parameter",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
        "-DDL_DOWN",
        "-D_RLS_",
        "-DGAM4980_TARGET_9288",
        "-I",
        str(SOURCE_ROOT / "9288_compat"),
        "-I",
        str(generated_include),
        "-I",
        str(SOURCE_ROOT),
    ]
    static_native_objects: list[Path] = []
    if switch_dispatch:
        common_flags.append("-DS6502_NO_COMPUTED_GOTO")
    if enable_aot:
        common_flags.extend(
            [
                "-DGAM4980_ENABLE_AOT",
                "-DGAM4980_ENABLE_FIRMWARE_HLE",
                f"-DGAM4980_FIRMWARE_HLE_MASK={firmware_hle_mask}",
            ]
        )
    if aggressive_region_hle:
        if not enable_aot:
            raise SystemExit("--aggressive-region-hle requires AOT/HLE")
        common_flags.append("-DGAM4980_ENABLE_AGGRESSIVE_REGION_HLE")
    if iram_hot_core:
        if not aggressive_region_hle:
            raise SystemExit(
                "--iram-hot-core requires --aggressive-region-hle"
            )
        common_flags.append("-DGAM4980_ENABLE_IRAM_HOT_CORE")
    if iram_exec_engine:
        if not iram_hot_core or not bare_session:
            raise SystemExit(
                "--iram-exec-engine requires --iram-hot-core and "
                "--bare-session"
            )
        common_flags.append("-DGAM4980_ENABLE_IRAM_EXEC_ENGINE")
    if iram_exec_asm:
        if not iram_exec_engine:
            raise SystemExit(
                "--iram-exec-asm requires --iram-exec-engine"
            )
        common_flags.append("-DGAM4980_IRAM_EXEC_ASM")
        # The true-device default spends the scarce resident overlay on the
        # generic interpreter.  The legacy shadow-superinstruction probe is
        # still built separately without this define, while pageable native
        # modules retain their own explicitly requested bridge.
        if not dynamic_native_all or firmware_native_rom is not None:
            common_flags.append("-DGAM4980_IRAM_V2")
    if dynamic_native_all:
        if not iram_exec_asm:
            raise SystemExit(
                "--dynamic-native-all requires the S1C33 IRAM ASM engine"
            )
        common_flags.append("-DGAM4980_DYNAMIC_NATIVE_ALL")
    if firmware_native_rom is not None:
        common_flags.append("-DGAM4980_AUTHORED_FIRMWARE")
        common_flags.append("-DGAM4980_NATIVE_GRAPHICS_ONLY")
    if static_native_game is not None:
        if not iram_exec_asm:
            raise SystemExit(
                "--static-native-game requires the S1C33 IRAM ASM engine"
            )
        common_flags.extend(
            [
                "-DGAM4980_STATIC_NATIVE_GAME",
                "-DGAM4980_IRAM_STATIC_COMPACT",
                "-I",
                str(generated_native),
            ]
        )
    if external_exec:
        if not iram_exec_engine or not bare_session:
            raise SystemExit(
                "--external-exec requires --iram-exec-engine and "
                "--bare-session"
            )
        common_flags.append("-DGAM4980_FORCE_EXTERNAL_EXEC")
    if bare_session:
        if not iram_hot_core:
            raise SystemExit("--bare-session requires --iram-hot-core")
        if not lightweight_performance:
            raise SystemExit(
                "--bare-session requires --lightweight-performance"
            )
        common_flags.extend(
            [
                "-DGAM4980_ENABLE_BARE_SESSION",
                "-DGAM4980_BARE_DIRECT_FS",
                "-DGAM4980_ENABLE_NATIVE_TRACE_AOT",
            ]
        )
    if game_load_aot:
        if not enable_aot:
            raise SystemExit("--game-load-aot requires the normal AOT dispatcher")
        common_flags.extend(
            [
                "-DGAM4980_ENABLE_GAME_LOAD_AOT",
                "-DGAM4980_RUNTIME_PERFORMANCE_LOG",
            ]
        )
        if lightweight_performance:
            common_flags.append("-DGAM4980_LIGHTWEIGHT_PERFORMANCE_LOG")
    if aot_diagnostics:
        if not enable_aot:
            raise SystemExit("--aot-diagnostics requires the normal AOT dispatcher")
        common_flags.append("-DGAM4980_AOT_DIAGNOSTICS")
    if lightweight_performance and aot_diagnostics:
        raise SystemExit(
            "--lightweight-performance cannot be combined with "
            "--aot-diagnostics"
        )
    if load_diagnostics:
        common_flags.append("-DGAM4980_LOAD_DIAGNOSTICS")
    if memory_diagnostics:
        common_flags.append("-DGAM4980_MEMORY_DIAGNOSTICS")
    if static_native_game is not None:
        run(
            [
                sys.executable,
                str(PROJECT_ROOT / "tools" / "compile_static_native_game.py"),
                "--clang",
                clang,
                "--objcopy",
                objcopy,
                "--include-dir",
                str(SOURCE_ROOT),
                "--game",
                str(static_native_game),
                "--output-dir",
                str(generated_native),
                "--optimization",
                "z",
                "--max-native-bytes",
                hex(static_native_budget),
            ],
            "compile game-specific static native modules",
        )
        static_native_objects = sorted(
            generated_native.glob("static_native_[0-9][0-9][0-9].o")
        )
        if not static_native_objects:
            raise SystemExit("static native compiler generated no objects")

    # Header contents, rather than mtimes, are part of the object cache key.
    # This deliberately includes generated AOT headers: changing either BIN's
    # offline translation invalidates the core object, while changing only the
    # 9288 frontend source leaves that expensive object reusable.
    dependencies = sorted(SOURCE_ROOT.rglob("*.h"))
    dependencies.extend(sorted(SOURCE_ROOT.rglob("*.inc")))
    dependencies.extend(sorted(generated_include.rglob("*.h")))
    if static_native_game is not None:
        dependencies.extend(sorted(generated_native.glob("*.h")))
    compile_units = [
        (SOURCE_ROOT / "gam4980_9288_start.c", []),
        (SOURCE_ROOT / "gam4980_9288_runtime.c", []),
        (SOURCE_ROOT / "gam4980_9288_bare.c", []),
        (
            SOURCE_ROOT / "gam4980_9288.c",
            ["-DGAM4980_SEPARATE_CORE_OBJECT"],
        ),
        (SOURCE_ROOT / "gam4980_core.c", []),
    ]
    if iram_exec_asm:
        compile_units.append((SOURCE_ROOT / "s6502_iram_asm.S", []))
    if static_native_game is not None:
        compile_units.append(
            (generated_native / "gam4980_static_native_registry.c", [])
        )
    for source, unit_flags in compile_units:
        if not source.is_file():
            raise SystemExit(f"missing source: {source}")
        output = BUILD_ROOT / f"{source.stem}.o"
        flags = [*common_flags, *unit_flags]
        compile_cached(
            clang,
            flags,
            source,
            output,
            dependencies,
            rebuild,
        )
        objects.append(output)
    objects.extend(static_native_objects)

    if firmware_native_rom is not None:
        run([
            sys.executable, str(PROJECT_ROOT / "tools" / "build_firmware_native.py"),
            "--toolchain", str(toolchain), "--rom", str(firmware_native_rom),
            "--output", str(BUILD_ROOT / "GAM4980.NAT"),
            *(["--compiled-registers"] if iram_exec_engine else []),
        ], "build authored firmware modules")
    elif dynamic_native_all:
        run(
            [
                sys.executable,
                str(PROJECT_ROOT / "tools" / "pack_native_module.py"),
                "--clang",
                clang,
                "--objcopy",
                objcopy,
                "--readelf",
                readelf,
                "--include-dir",
                str(SOURCE_ROOT),
                "--aot-header",
                str(SOURCE_ROOT / "s6502_aot_ebin_generated.h"),
                "--optimization",
                optimization,
                "--output",
                str(BUILD_ROOT / "GAM4980.NAT"),
            ],
            "pack pageable native module",
        )
        if native_game is not None:
            run(
                [
                    sys.executable,
                    str(PROJECT_ROOT / "tools" / "pack_native_module.py"),
                    "--clang",
                    clang,
                    "--objcopy",
                    objcopy,
                    "--readelf",
                    readelf,
                    "--include-dir",
                    str(SOURCE_ROOT),
                    "--aot-header",
                    str(SOURCE_ROOT / "s6502_aot_ebin_generated.h"),
                    "--optimization",
                    optimization,
                    "--game",
                    str(native_game),
                    "--output",
                    str(BUILD_ROOT / f"{native_game.stem}.GNA"),
                ],
                "pack full-GAM native sidecar",
            )

    elf = BUILD_ROOT / "GAM4980.elf"
    map_path = BUILD_ROOT / "GAM4980.map"
    linker_script = SOURCE_ROOT / "gam4980_9288.ld"
    run(
        [
            clang,
            "--target=s1c33-none-elf",
            "-fuse-ld=lld",
            "-nostdlib",
            "-Wl,--gc-sections",
            f"-Wl,-T,{linker_script}",
            f"-Wl,-Map,{map_path}",
            *(str(item) for item in objects),
            "-o",
            str(elf),
        ],
        "link 9288 ELF",
    )
    bss_start = read_map_symbol(map_path, "__bss_start")
    scratch_end = read_map_symbol(map_path, "__scratch_end")
    payload_end = read_map_symbol(map_path, "__payload_end")
    if payload_end > APP_RAM_END:
        raise SystemExit(
            "9288 payload exceeds the 8 MiB SDRAM window: "
            f"end=0x{payload_end:08x}, limit=0x{APP_RAM_END:08x}"
        )
    iram_start = read_map_symbol(map_path, "__iram_start")
    iram_end = read_map_symbol(map_path, "__iram_end")
    iram_size = iram_end - iram_start
    if iram_hot_core and not (0 < iram_size <= 0x16C8):
        raise SystemExit(
            f"invalid IRAM hot-core size: {iram_size} bytes"
        )
    if not iram_hot_core and iram_size != 0:
        raise SystemExit(
            f"unexpected IRAM contents without --iram-hot-core: {iram_size}"
        )
    if iram_exec_engine:
        iram_exec_start = read_map_symbol(
            map_path, "__iram_exec_engine_start"
        )
        iram_exec_end = read_map_symbol(
            map_path, "__iram_exec_engine_end"
        )
        if (iram_exec_start | iram_exec_end) & 3:
            raise SystemExit(
                "IRAM execution engine range is not 32-bit aligned: "
                f"0x{iram_exec_start:x}..0x{iram_exec_end:x}"
            )
    if iram_hot_core:
        print("+", objdump, "-d --section=.iram", elf)
        iram_disassembly = subprocess.check_output(
            [objdump, "-d", "--section=.iram", str(elf)], text=True
        )
        (BUILD_ROOT / "GAM4980.iram.dis.txt").write_text(
            iram_disassembly, encoding="utf-8"
        )
        # S1C33 relative calls are safe only when both caller and callee live
        # in the copied low-IRAM overlay.  Calls through registers are valid;
        # reject symbol-resolved direct calls whose linked target is outside
        # the complete IRAM range while allowing resident helper calls.
        iram_symbols: dict[str, int] = {}
        for line in iram_disassembly.splitlines():
            match = re.match(
                r"^\s*([0-9a-fA-F]+)\s+<([^>]+)>:$", line
            )
            if match is not None:
                name = match.group(2).split("+", 1)[0]
                iram_symbols[name] = int(match.group(1), 16)
        direct_external_transfers: list[str] = []
        for line in iram_disassembly.splitlines():
            match = re.search(
                r"\b(?:call|jp(?:\.d)?)\b.*<([^>]+)>", line
            )
            if match is None:
                continue
            name = match.group(1).split("+", 1)[0]
            target = iram_symbols.get(name)
            if target is None or not (iram_start <= target < iram_end):
                direct_external_transfers.append(line.strip())
        if direct_external_transfers:
            raise SystemExit(
                "IRAM hot core contains unsafe direct control transfers:\n" +
                "\n".join(direct_external_transfers)
            )
        audit_command = [
            sys.executable,
            str(PROJECT_ROOT / "tests" / "audit_9288_iram_exec.py"),
            "--map",
            str(map_path),
            "--disassembly",
            str(BUILD_ROOT / "GAM4980.iram.dis.txt"),
            "--max-size",
            "0x16c8",
        ]
        if iram_exec_asm:
            audit_command.extend(
                [
                    "--expect-asm",
                    "--required-symbol",
                    "s6502_iram_exec_burst_asm",
                    "--dispatch-table-symbol",
                    "s6502_iram_dispatch_table",
                    "--allow-indirect-call-register",
                    "r13",
                    "--expected-indirect-calls",
                    "2" if (dynamic_native_all or static_native_game is not None) else "0",
                ]
            )
        run(audit_command, "audit 9288 IRAM execution engine")
    raw = BUILD_ROOT / "GAM4980.bin"
    # The flat KF2 payload is addressed from 0x02700000.  llvm-objcopy's
    # binary backend keys its span from .iram's low VMA (0x800), despite the
    # section having the correct 0x02700100 LMA.  Extract the external image
    # without .iram, then place the raw overlay at the SDK's load-image
    # offset.  Relocations remain resolved for the 0x800 run address.
    run(
        [
            objcopy, "-O", "binary", "--gap-fill", "255",
            "--remove-section=.iram", str(elf), str(raw),
        ],
        "extract external 9288 payload",
    )
    payload_bytes = bytearray(raw.read_bytes())
    if iram_size:
        iram_raw = BUILD_ROOT / "GAM4980.iram.bin"
        run(
            [
                objcopy, "-O", "binary", "--only-section=.iram",
                str(elf), str(iram_raw),
            ],
            "extract IRAM load image",
        )
        iram_image = iram_raw.read_bytes()
        if len(iram_image) != iram_size:
            raise SystemExit(
                "IRAM image size mismatch: "
                f"{len(iram_image)} bytes, expected {iram_size}"
            )
        iram_load_offset = 0x100
        iram_load_end = iram_load_offset + iram_size
        if iram_load_end > len(payload_bytes):
            raise SystemExit("IRAM load image falls outside the KF2 payload")
        payload_bytes[iram_load_offset:iram_load_end] = iram_image
    payload = bytes(payload_bytes)
    raw.write_bytes(payload)
    run([readelf, "-h", "-S", str(elf)], "inspect 9288 ELF")
    expected_size = payload_end - APP_LOAD_ADDRESS
    if expected_size <= 0 or len(payload) != expected_size:
        raise SystemExit(
            "KF2 payload does not reserve the complete runtime image: "
            f"{len(payload)} bytes, expected {expected_size}"
        )
    reserve_start = bss_start - APP_LOAD_ADDRESS
    reserve_end = scratch_end - APP_LOAD_ADDRESS
    if not (0 <= reserve_start <= reserve_end < len(payload)) or any(
        byte != 0xff for byte in payload[reserve_start:reserve_end]
    ):
        raise SystemExit("KF2 BSS/scratch reservation is not file-backed")
    return payload


def read_icon(path: Path, width: int, height: int) -> bytes:
    if not path.is_file():
        raise SystemExit(
            f"missing GAM4980 icon: {path}; run tools/convert_9288_icon.py"
        )
    data = path.read_bytes()
    expected_size = 12 + width * height * 2 // 8
    if len(data) != expected_size:
        raise SystemExit(
            f"invalid 9288 icon size: {path} has {len(data)} bytes, "
            f"expected {expected_size}"
        )
    icon_width, icon_height, bpp, reserved0, reserved1, reserved2 = (
        struct.unpack_from("<HHHHHH", data)
    )
    if (icon_width, icon_height, bpp, reserved0, reserved1, reserved2) != (
        width,
        height,
        2,
        0,
        0,
        0,
    ):
        raise SystemExit(f"invalid 9288 icon header: {path}")
    return data


def pack_kf2(payload: bytes, *, app_name: bytes = APP_NAME,
             icon_root: Path | None = None) -> bytes:
    if len(payload) > APP_MAX_PAYLOAD_SIZE:
        raise SystemExit(
            "9288 KF2 payload is too large for the application RAM window: "
            f"{len(payload)} bytes, maximum {APP_MAX_PAYLOAD_SIZE}"
        )
    if not app_name or len(app_name) > 15 or b"\0" in app_name:
        raise ValueError("KF2 name must contain 1-15 bytes and no embedded NUL")
    icon_root = ICON_ROOT if icon_root is None else icon_root
    icon1 = read_icon(icon_root / "ico1.bin", 40, 40)
    icon2 = read_icon(icon_root / "ico2.bin", 16, 16)
    code_offset = HEADER_SIZE + len(icon1) + len(icon2)
    total_size = code_offset + len(payload)
    name = app_name.ljust(16, b"\0")
    header = struct.pack(
        "<IIHH16sIIIII",
        MAGIC0,
        MAGIC1,
        MACHINE_9288,
        APP_MODULE,
        name,
        code_offset,
        HEADER_SIZE,
        len(icon1),
        len(icon2),
        total_size,
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError(f"KF2 header is {len(header)} bytes")
    return header + icon1 + icon2 + payload


def read_existing_payload(path: Path) -> bytes:
    app = path.read_bytes()
    if len(app) < HEADER_SIZE:
        raise SystemExit(f"invalid existing KF2 file: {path}")
    (
        magic0,
        magic1,
        machine,
        _module,
        _name,
        code_offset,
        header_size,
        icon1_size,
        icon2_size,
        total_size,
    ) = struct.unpack_from("<IIHH16sIIIII", app)
    if (magic0, magic1, machine, header_size) != (
        MAGIC0,
        MAGIC1,
        MACHINE_9288,
        HEADER_SIZE,
    ):
        raise SystemExit(f"existing file is not a BBK 9288 KF2 executable: {path}")
    if code_offset != HEADER_SIZE + icon1_size + icon2_size:
        raise SystemExit(f"invalid KF2 code offset in existing file: {path}")
    if total_size != len(app):
        raise SystemExit(f"invalid KF2 total size in existing file: {path}")
    return app[code_offset:]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the standalone BBK 9288 gam4980 port"
    )
    parser.add_argument("--build-dir", type=Path,
                        help="intermediate output directory (may be on another drive)")
    parser.add_argument(
        "--sdk",
        type=Path,
        default=DEFAULT_SDK,
        help="9288 SDK root (WSL path when run in WSL)",
    )
    parser.add_argument(
        "--toolchain",
        type=Path,
        help="directory containing the S1C33 LLVM tools",
    )
    parser.add_argument(
        "--reuse-payload-from",
        type=Path,
        help="reuse an existing 9288 KF2 payload when only repacking resources",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=BUILD_ROOT / "GAM4980.exe",
    )
    parser.add_argument(
        "--switch-dispatch",
        action="store_true",
        help="use the slower portable 6502 switch dispatcher",
    )
    parser.add_argument(
        "--no-aot",
        dest="aot",
        action="store_false",
        default=True,
        help="disable all AOT paths and use only the interpreter",
    )
    parser.add_argument(
        "--game-load-aot",
        dest="game_load_aot",
        action="store_true",
        default=True,
        help=(
            "enable runtime settings, performance logging, and game-code "
            "AOT templates (default)"
        ),
    )
    parser.add_argument(
        "--no-game-load-aot",
        dest="game_load_aot",
        action="store_false",
        help="omit game-code AOT templates and runtime performance sampling",
    )
    parser.add_argument(
        "--load-diagnostics",
        action="store_true",
        help="persist true-device load stages to A:\\gam4980\\DIAG.TXT",
    )
    parser.add_argument(
        "--aot-diagnostics",
        action="store_true",
        help="record per-AOT-block and per-HLE-path hotspot counters",
    )
    parser.add_argument(
        "--lightweight-performance",
        action="store_true",
        help=(
            "keep only low-overhead RTC/frame throughput logging and compile "
            "hot AOT/HLE diagnostics out"
        ),
    )
    parser.add_argument(
        "--memory-diagnostics",
        action="store_true",
        help="expose volatile load/ROM/frame counters for emulator inspection",
    )
    parser.add_argument(
        "--optimization",
        choices=("2", "3", "s", "z"),
        default="2",
        help=(
            "compiler optimization level "
            "(default: 2, preferred for the physical S1C33 CPU)"
        ),
    )
    parser.add_argument(
        "--firmware-hle-mask",
        type=lambda value: int(value, 0),
        choices=range(1024),
        default=1023,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--aggressive-region-hle",
        action="store_true",
        help=(
            "HLE complete rows of the E.BIN bitmap rectangle routine while "
            "preserving CPU scheduling-slice boundaries"
        ),
    )
    parser.add_argument(
        "--iram-hot-core",
        action="store_true",
        help=(
            "place the hottest verified picture HLE routines in the 9288 "
            "SDK IRAM overlay"
        ),
    )
    parser.add_argument(
        "--bare-session",
        action="store_true",
        help=(
            "run gameplay with system IRQs masked, direct 256 Hz clock, "
            "matrix keyboard, LCD framebuffer, and session-resident IRAM"
        ),
    )
    parser.add_argument(
        "--iram-exec-engine",
        action="store_true",
        help=(
            "use the SDK IRAM overlay for a session-resident 65C02 "
            "execution loop instead of picture-specific HLE"
        ),
    )
    iram_exec_implementation = parser.add_mutually_exclusive_group()
    iram_exec_implementation.add_argument(
        "--iram-exec-asm",
        dest="iram_exec_asm",
        action="store_true",
        default=None,
        help=(
            "compile the register-resident S1C33 assembly execution engine "
            "(the default when --iram-exec-engine is enabled)"
        ),
    )
    iram_exec_implementation.add_argument(
        "--iram-exec-c",
        dest="iram_exec_asm",
        action="store_false",
        help=(
            "retain the previous C IRAM execution engine for A/B and "
            "equivalence testing"
        ),
    )
    parser.add_argument(
        "--external-exec",
        action="store_true",
        help=(
            "keep the bare session but force the normal execution engine "
            "to remain in external RAM; used as a strict IRAM A/B control"
        ),
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="ignore the content-addressed object cache and rebuild every unit",
    )
    parser.add_argument(
        "--firmware-native-rom", type=Path,
        help="development only: build authored firmware NAT from this E.BIN; incomplete coverage",
    )
    parser.add_argument(
        "--dynamic-native-all",
        action="store_true",
        help=(
            "experimentally page all firmware/game native code from .NAT/.GNA; "
            "disabled by default because physical 9288 external-code fetches "
            "are much slower than the emulator models"
        ),
    )
    parser.add_argument(
        "--native-game",
        type=Path,
        help=(
            "offline-compile all statically recoverable code in this GAM "
            "to a same-name .GNA sidecar"
        ),
    )
    parser.add_argument(
        "--static-native-game",
        type=Path,
        help=(
            "PC-recompile the initial working set of this GAM to S1C33 ELF "
            "objects and link them directly into a game-specific KF2"
        ),
    )
    parser.add_argument(
        "--static-native-budget",
        type=lambda value: int(value, 0),
        default=0x50000,
        help=(
            "maximum generated S1C33 code bytes linked into a game-specific "
            "KF2 (default: 0x50000)"
        ),
    )
    return parser.parse_args()


def main() -> None:
    global BUILD_ROOT
    args = parse_args()
    if args.build_dir is not None:
        BUILD_ROOT = args.build_dir.resolve()
    if args.firmware_native_rom is not None:
        if args.native_game is not None or args.static_native_game is not None:
            raise SystemExit("authored firmware package cannot be mixed with game AOT")
        args.dynamic_native_all = True
    if args.iram_exec_asm is not None and not args.iram_exec_engine:
        raise SystemExit(
            "--iram-exec-asm/--iram-exec-c require --iram-exec-engine"
        )
    if args.native_game is not None and not args.dynamic_native_all:
        raise SystemExit("--native-game requires --dynamic-native-all")
    if args.static_native_game is not None and args.reuse_payload_from is not None:
        raise SystemExit("--static-native-game cannot reuse an existing payload")
    if args.static_native_budget <= 0:
        raise SystemExit("--static-native-budget must be positive")
    sdk = args.sdk.resolve()
    output = args.output.resolve()
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.reuse_payload_from is not None:
        payload = read_existing_payload(args.reuse_payload_from.resolve())
    else:
        if args.toolchain is None:
            raise SystemExit("--toolchain is required unless --reuse-payload-from is used")
        payload = compile_app(
            sdk,
            args.toolchain.resolve(),
            switch_dispatch=args.switch_dispatch,
            enable_aot=args.aot,
            game_load_aot=args.game_load_aot and args.aot,
            aot_diagnostics=args.aot_diagnostics,
            lightweight_performance=args.lightweight_performance,
            load_diagnostics=args.load_diagnostics,
            memory_diagnostics=args.memory_diagnostics,
            optimization=args.optimization,
            firmware_hle_mask=args.firmware_hle_mask,
            aggressive_region_hle=args.aggressive_region_hle,
            iram_hot_core=args.iram_hot_core,
            iram_exec_engine=args.iram_exec_engine,
            iram_exec_asm=(
                args.iram_exec_engine
                if args.iram_exec_asm is None
                else args.iram_exec_asm
            ),
            external_exec=args.external_exec,
            bare_session=args.bare_session,
            dynamic_native_all=args.dynamic_native_all,
            firmware_native_rom=args.firmware_native_rom,
            native_game=(
                args.native_game.resolve()
                if args.native_game is not None else None
            ),
            static_native_game=(
                args.static_native_game.resolve()
                if args.static_native_game is not None else None
            ),
            static_native_budget=args.static_native_budget,
            rebuild=args.rebuild,
        )
    app = pack_kf2(payload)
    output.write_bytes(app)
    native_package = BUILD_ROOT / "GAM4980.NAT"
    if native_package.is_file() and args.dynamic_native_all and (
        args.iram_exec_asm is None or args.iram_exec_asm
    ):
        native_output = output.parent / "GAM4980.NAT"
        if native_package.resolve() != native_output.resolve():
            shutil.copyfile(native_package, native_output)
        print(f"native module: {native_output}")
    if args.native_game is not None:
        game_native_package = BUILD_ROOT / f"{args.native_game.stem}.GNA"
        if game_native_package.is_file():
            game_native_output = output.parent / game_native_package.name
            if game_native_package.resolve() != game_native_output.resolve():
                shutil.copyfile(game_native_package, game_native_output)
            print(f"game native module: {game_native_output}")
    digest = hashlib.sha256(app).hexdigest()
    print(f"built: {output}")
    print(f"payload: {len(payload)} bytes")
    print(f"KF2: {len(app)} bytes, sha256={digest}")


if __name__ == "__main__":
    main()
