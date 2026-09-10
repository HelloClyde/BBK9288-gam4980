#!/usr/bin/env python3
"""Contracts for the private C6502 shadow-decode superinstructions."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IramSuperShadowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.core = (ROOT / "src" / "gam4980_core.c").read_text(
            encoding="utf-8"
        )
        cls.asm = (ROOT / "src" / "s6502_iram_asm.S").read_text(
            encoding="utf-8"
        )
        cls.abi = (ROOT / "src" / "s6502_iram_exec_abi.h").read_text(
            encoding="utf-8"
        )

    def test_shadow_has_separate_fetch_mapping(self) -> None:
        self.assertIn("uint8_t *s6502_iram_code_pages[0x100u]", self.core)
        self.assertIn("CTX_CODE_PAGES,         64", self.asm)
        self.assertIn("CTX_SUPER_HITS,         68", self.asm)
        self.assertIn("S6502_IRAM_ASM_CONTEXT_SIZE                108u", self.abi)
        self.assertIn("S6502_IRAM_ASM_CONTEXT_SAVED_FETCH_OFFSET  100u", self.abi)
        self.assertIn("uint32_t code_pages;", self.abi)
        self.assertIn("uint32_t super_hits;", self.abi)
        self.assertIn("uint32_t native_shared_entry;", self.abi)
        self.assertIn("uint32_t native_shared_metrics;", self.abi)

    def test_templates_cannot_cross_a_shadow_page(self) -> None:
        builder = self.core.split(
            "static void s6502_iram_refresh_shadow_bank", 1
        )[1].split("#endif\n\n#ifdef GAM4980_ENABLE_GAME_LOAD_AOT", 1)[0]
        self.assertIn("s6502_iram_super_bank_head", builder)
        self.assertIn("gam4980_memcpy(shadow, source_base, 0x1000u)", builder)
        self.assertIn("shadow[offset] = S6502_IRAM_SHADOW_OPCODE", builder)
        self.assertNotIn("s6502_game_aot_cfg_test", builder)
        self.assertNotIn("sys.mem_r[page_index] = shadow", builder)

    def test_same_physical_and_source_bank_is_a_zero_rebuild(self) -> None:
        builder = self.core.split(
            "static void s6502_iram_refresh_shadow_bank", 1
        )[1].split("#endif\n\n#ifdef GAM4980_ENABLE_GAME_LOAD_AOT", 1)[0]
        guard = (
            "s6502_iram_shadow_physical_bank[virtual_bank] == physical_bank &&\n"
            "            s6502_iram_shadow_source_base[virtual_bank] == source_base"
        )
        self.assertIn(guard, builder)
        self.assertLess(builder.index(guard), builder.index("gam4980_memcpy"))

    def test_resident_supers_are_not_preempted_by_c_aot(self) -> None:
        keep = self.core.split("static int s6502_iram_keep_game_aot", 1)[1]
        keep = keep.split("#ifdef GAM4980_ENABLE_FIRMWARE_HLE", 1)[0]
        for semantic in (
            "C6502_TEMPLATE_LOAD_OPER1_IMM16",
            "C6502_TEMPLATE_LOAD_OPER2_IMM16",
            "C6502_TEMPLATE_STACK_ADD16",
            "C6502_TEMPLATE_STACK_SUB16",
            "C6502_TEMPLATE_ADD16_OPER1_OPER2",
        ):
            with self.subTest(semantic=semantic):
                self.assertIn(f"entry->semantic == {semantic}", keep)
        self.assertIn("return 0;", keep)

    def test_guest_opcode_count_excludes_synthetic_02(self) -> None:
        table = self.asm.split("s6502_iram_dispatch_table:", 1)[1]
        table = table.split("s6502_iram_dispatch_table_end:", 1)[0]
        self.assertIn(
            "DISPATCH16 .Lslow_opcode, .Lslow_opcode, .Lsuper, .Lslow_opcode",
            table,
        )

    def test_debug_off_uses_nullable_hit_counter(self) -> None:
        macro = self.asm.split(".macro SUPER_HIT", 1)[1].split(
            ".endm", 1
        )[0]
        self.assertIn("CTX_SUPER_HITS", macro)
        self.assertIn("cmp   %r13, 0", macro)
        self.assertIn("context.super_hits = s6502_iram_diagnostics_enabled", self.core)
        self.assertIn("g_gam4980_iram_super_hits : 0u", self.core)

    def test_runtime_trusts_only_builder_validated_shadow_markers(self) -> None:
        self.assertNotIn(".macro SUPER_EXPECT", self.asm)
        handler = self.asm.split(".Lsuper:", 1)[1].split(
            "/* CLC / SEC */", 1
        )[0]
        for discriminator in ("0xa9", "0x08", "0x18"):
            self.assertIn(discriminator, handler)
        builder = self.core.split(
            "static void s6502_iram_refresh_shadow_bank", 1
        )[1].split("#endif\n\n#ifdef GAM4980_ENABLE_GAME_LOAD_AOT", 1)[0]
        self.assertLess(
            builder.index("s6502_iram_super_entry_matches"),
            builder.index("shadow[offset] = S6502_IRAM_SHADOW_OPCODE"),
        )

    def test_immediate_word_store_is_a_compiler_wide_phrase(self) -> None:
        generated = (ROOT / "src" / "s6502_c6502_spec_generated.h").read_text(
            encoding="utf-8"
        )
        matcher = self.core.split(
            "static uint8_t s6502_game_aot_template_at", 1
        )[1].split("static uint8_t s6502_game_aot_pattern_at_priority", 1)[0]
        shadow = self.core.split(
            "static uint8_t s6502_iram_shadow_template_at", 1
        )[1].split("static uint8_t s6502_iram_super_entry_kind", 1)[0]
        handler = self.asm.split(".Lsuper_imm16:", 1)[1].split(
            ".Lsuper_stack16:", 1
        )[0]

        self.assertIn(
            "{2u, 8u, {0xa9u, 0x00u, 0x85u, 0x00u, "
            "0xa9u, 0x00u, 0x85u, 0x00u",
            generated,
        )
        self.assertIn("code[7] != (uint8_t)(code[3] + 1u)", matcher)
        self.assertIn("code[7] == (uint8_t)(code[3] + 1u)", shadow)
        self.assertNotIn(".Lsuper_imm16_dest_ok", handler)
        self.assertNotIn("jrne  .Lexit_slow1", handler)

    def test_debug_off_nops_instruction_counting_without_hot_branch(self) -> None:
        self.assertIn("s6502_iram_count_patch_offsets:", self.asm)
        self.assertEqual(self.asm.count(".long s6502_iram_count_patch_"), 7)
        patcher = self.core.split(
            "static void s6502_iram_patch_release_counters", 1
        )[1].split("static int s6502_iram_install_exec_range", 1)[0]
        self.assertIn("s6502_iram_diagnostics_enabled", patcher)
        self.assertIn("instruction[0] = 0u", patcher)
        self.assertIn("instruction[1] = 0u", patcher)
        self.assertNotIn("s6502_iram_diagnostics_enabled", self.asm)

    def test_bank_churn_has_a_bounded_adaptive_fallback(self) -> None:
        self.assertIn("S6502_IRAM_SHADOW_CHECK_REBUILDS 16u", self.core)
        self.assertIn("S6502_IRAM_SHADOW_NO_METRICS_LIMIT 32u", self.core)
        self.assertIn("s6502_iram_shadow_adaptive_check();", self.core)
        disable = self.core.split(
            "static void s6502_iram_disable_shadow", 1
        )[1].split("static void s6502_iram_shadow_adaptive_check", 1)[0]
        self.assertIn("s6502_iram_shadow_enabled = 0", disable)
        self.assertIn("s6502_iram_code_pages[page] = sys.mem_r[page]", disable)
        refresh = self.core.split(
            "static void s6502_iram_refresh_shadow_bank", 1
        )[1].split("#endif\n\n#ifdef GAM4980_ENABLE_GAME_LOAD_AOT", 1)[0]
        self.assertIn(
            "virtual_bank >= 16u || !s6502_iram_shadow_enabled", refresh
        )
        self.assertIn(
            "s6502_iram_shadow_enabled ? s6502_iram_code_pages : sys.mem_r",
            self.core,
        )

    def test_deep_exit_diagnostics_are_sampled_but_bursts_are_exact(self) -> None:
        self.assertIn("S6502_IRAM_EXIT_SAMPLE_RATE 16u", self.core)
        record_exit = self.core.split(
            "static void s6502_iram_record_exit(", 1
        )[1].split("static uint32_t s6502_iram_exec_burst_call", 1)[0]
        self.assertIn("++s6502_iram_exit_sample_cursor", record_exit)
        self.assertIn("++s6502_iram_exit_samples", record_exit)
        record_burst = self.core.split(
            "static void s6502_iram_record_burst", 1
        )[1].split("static void s6502_iram_record_exit_hotspot", 1)[0]
        self.assertNotIn("s6502_iram_exit_sample_cursor", record_burst)


if __name__ == "__main__":
    unittest.main()
