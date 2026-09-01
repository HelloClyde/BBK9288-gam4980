#!/usr/bin/env python3
"""Static contracts for the true-device warm profile and latest PERF log."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def test_warm_profile(core: str) -> None:
    body = core.split("int gam4980_warm_bare_rom_cache(void)", 1)[1].split(
        "uint32_t next_line", 1
    )[0]
    ranges = re.findall(
        r"\{\s*(GAM4980_ROM_REGION_[8E]),\s*"
        r"0x([0-9a-fA-F]+)u,\s*0x([0-9a-fA-F]+)u\s*\}",
        body,
    )
    actual = {"GAM4980_ROM_REGION_8": set(), "GAM4980_ROM_REGION_E": set()}
    for region, first_text, last_text in ranges:
        first = int(first_text, 16)
        last = int(last_text, 16)
        if first > last:
            raise AssertionError(f"reversed warm range: {region} {first:03X}-{last:03X}")
        pages = set(range(first, last + 1))
        overlap = actual[region] & pages
        if overlap:
            raise AssertionError(
                f"overlapping warm pages in {region}: "
                + " ".join(f"{page:03X}" for page in sorted(overlap))
            )
        actual[region] |= pages

    expected_8 = (
        set(range(0x000, 0x01E))
        | {0x022}
        | set(range(0x033, 0x037))
        | {0x078}
    )
    expected_e = (
        set(range(0x002, 0x006))
        | set(range(0x00D, 0x029))
        | set(range(0x045, 0x049))
        | set(range(0x051, 0x055))
        | set(range(0x0A0, 0x0AB))
        | set(range(0x0B0, 0x0C8))
        | set(range(0x0D4, 0x0D8))
        | {0x1FF}
    )
    if actual["GAM4980_ROM_REGION_8"] != expected_8:
        raise AssertionError("ROM8 warm profile differs from the measured set")
    if actual["GAM4980_ROM_REGION_E"] != expected_e:
        raise AssertionError("ROME warm profile differs from the measured set")
    total = sum(len(pages) for pages in actual.values())
    if total != 116:
        raise AssertionError(f"warm profile has {total} pages, expected 116")
    if "#define ROM_CACHE_LINES GAM4980_BARE_ROM_CACHE_LINES" not in core:
        raise AssertionError("bare ROM cache is not tied to the configured budget")
    if "if (page_count > ROM_CACHE_LINES - next_line)\n            continue;" not in core:
        raise AssertionError("oversized warm ranges must be skipped atomically")
    print(
        "ROM warm candidate contract passed: "
        f"ROM8={len(expected_8)} ROME={len(expected_e)} candidate={total}, "
        "resident=92"
    )


def test_performance_log(frontend: str) -> None:
    start = frontend.index("static void write_performance_log(void)")
    end = frontend.index("\nstatic ", start + 1)
    writer = frontend[start:end]
    remove = writer.index("fs_remove(k_performance_log_path)")
    opened = writer.index("fs_fopen(k_performance_log_path, FS_O_WRONLY)")
    if remove >= opened:
        raise AssertionError("PERF.LOG must be removed before it is recreated")
    if frontend.count("k_performance_log_path") != 3:
        raise AssertionError(
            "PERF.LOG gained another access path; latest-only semantics need review"
        )
    required_writer_tokens = (
        "[GAM4980 PERF LIGHT 1]",
        '"rom_cache_warm_pages"',
        '"rom_cache_runtime_misses"',
        "performance_log_rom_miss_trace(file)",
        "performance_log_iram_exit_diagnostics(file)",
        '"iram_exec_calls"',
        '"iram_exec_dispatch_exits"',
        '"iram_exec_slow_exits"',
        '"firmware_aot_token_link_hits"',
        '"native_trace_aot_enabled"',
        '"native_trace_7c30_validation"',
        '"native_trace_7c30_calls"',
        '"native_trace_7c30_iterations"',
        '"native_trace_7c30_guest_cycles"',
        '"native_trace_7c30_slice_exits"',
        '"native_trace_7c30_terminal_exits"',
        '"game_aot_linear_link_hits"',
        '"bare_key_scan_gap_over_4"',
        '"bare_host_tick_hz"',
        '"bare_core_ticks"',
        '"bare_render_ticks"',
        '"bare_present_ticks"',
        '"bare_direct_fs_calls"',
        '"bare_direct_fs_failures"',
        '"bare_direct_fs_ticks"',
        '"bare_direct_fs_iram_repairs"',
        '"[END]\\r\\n"',
    )
    for token in required_writer_tokens:
        if token not in writer:
            raise AssertionError(f"PERF writer missing required token: {token}")
    required_diagnostic_tokens = (
        '"iram_burst_0"',
        '"iram_burst_1_4"',
        '"iram_burst_5_16"',
        '"iram_burst_17_64"',
        '"iram_burst_65_256"',
        '"iram_burst_gt_256"',
        '"iram_dispatch_unknown_hits"',
        '"iram_dispatch_firmware_aot_hits"',
        '"iram_dispatch_firmware_hle_hits"',
        '"iram_dispatch_game_hle_hits"',
        '"iram_dispatch_game_aot_hits"',
        '"iram_fastchain_calls"',
        '"iram_fastchain_guest_cycles"',
        '"iram_fastchain_reentries"',
        '"iram_fastchain_zero_returns"',
        '"iram_fastchain_reinstall_failures"',
        '"iram_fastchain_non_dispatch_skips"',
        '"native_shared_validation"',
        '"native_shared_calls"',
        '"native_shared_blocks"',
        '"native_shared_guest_cycles"',
        '"native_shared_7c30_entries"',
        '"native_shared_misses"',
        '"native_shared_chain_links"',
        '"native_shared_max_chain"',
        '"native_shared_direct_links"',
        '"native_module_status"',
        '"native_module_match_count"',
        '"native_module_package_size"',
        '"native_module_count"',
        '"native_module_preloaded"',
        '"native_module_loads"',
        '"native_module_evictions"',
        '"native_module_bytes_loaded"',
        '"native_module_fallbacks"',
        '"native_module_fault_attempts"',
        '"native_module_fault_deferred"',
        '"native_module_cooldown_deferred"',
        '"native_module_thrash_suppressions"',
        '"native_module_batches"',
        '"native_module_transition_count"',
        '"native_module_transition rank="',
        '"native_module_arena_size"',
        '"native_module_slot_size"',
        '"native_module_resident_count"',
        '"native_module_alloc_units_used"',
        '"native_module_alloc_units_total"',
        '"iram_exit_sample_rate"',
        '"iram_exit_samples"',
        '"iram_shadow_super_enabled_end"',
        '"iram_shadow_adaptive_checks"',
        '"iram_shadow_adaptive_disables"',
        '"iram_shadow_disable_reason"',
        '"iram_exit_hot reason="',
        '"iram_slow_opcode rank="',
        '"rom_cache_miss_trace_count"',
        '"rom_cache_miss_trace_dropped"',
    )
    for token in required_diagnostic_tokens:
        if token not in frontend:
            raise AssertionError(f"PERF diagnostics missing required token: {token}")
    print("PERF latest-only and diagnostic field contract passed")


def main() -> None:
    core = (ROOT / "src" / "gam4980_core.c").read_text(encoding="utf-8")
    frontend = (ROOT / "src" / "gam4980_9288.c").read_text(encoding="utf-8")
    test_warm_profile(core)
    test_performance_log(frontend)


if __name__ == "__main__":
    main()
