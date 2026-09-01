#!/usr/bin/env python3
"""Static safety contracts for true-device-validated bare filesystem reads."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    build = (ROOT / "build_9288.py").read_text(encoding="utf-8")
    frontend = (ROOT / "src" / "gam4980_9288.c").read_text(encoding="utf-8")
    bare = (ROOT / "src" / "gam4980_9288_bare.c").read_text(encoding="utf-8")
    iram = (ROOT / "src" / "gam4980_9288_start.c").read_text(encoding="utf-8")

    assert '"-DGAM4980_BARE_DIRECT_FS"' in build
    read_bank = frontend.split("static int read_rom_bank(", 1)[1].split(
        "static int verify_rom_files", 1
    )[0]
    assert "gam4980_9288_bare_direct_sdk_begin()" in read_bank
    assert "gam4980_9288_bare_direct_sdk_end()" in read_bank
    assert "fs_fseek" in read_bank and "read_exact" in read_bank

    end_guard = bare.split(
        "int gam4980_9288_bare_direct_sdk_end(void)", 1
    )[1].split("int gam4980_9288_bare_suspend_for_sdk", 1)[0]
    for token in (
        "PSR_IE_MASK",
        "TTBR_REGISTER_ADDRESS",
        "irq_sources_masked()",
        "system_is_quiescent()",
        "mask_irq_sources();",
        "prepare_key_scan();",
        "gam4980_9288_iram_session_validate()",
    ):
        assert token in end_guard, token

    validator = iram.split(
        "int gam4980_9288_iram_session_validate(void)", 1
    )[1].split("void gam4980_9288_iram_session_restore", 1)[0]
    assert "g_iram_session_range_count" in validator
    assert "bytes_equal(iram, load, size)" in validator
    assert "copy_bytes(iram, load, size)" in validator
    print("bare direct filesystem safety contract passed")


if __name__ == "__main__":
    main()
