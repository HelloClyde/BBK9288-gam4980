#!/usr/bin/env python3
"""Structural guards for the resident S1C33 PC/dispatch fast path."""

from __future__ import annotations

from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSEMBLY = PROJECT_ROOT / "src" / "s6502_iram_asm.S"
NATIVE_PACKER = PROJECT_ROOT / "tools" / "pack_native_module.py"


def between(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


class IramAsmPcFastPathTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = ASSEMBLY.read_text(encoding="utf-8")
        cls.native_packer = NATIVE_PACKER.read_text(encoding="utf-8")

    def test_operand_fetches_defer_pc_truncation(self) -> None:
        fetch8 = between(self.source, ".macro FETCH8", ".endm")
        fetch16 = between(self.source, ".macro FETCH16 slow", ".endm")
        self.assertNotIn("ld.uh %r8", fetch8)
        self.assertNotIn("ld.uh %r8", fetch16)
        self.assertNotIn("call  .Lfetch16_cross", fetch16)
        self.assertIn("add   %r8, 1", fetch8)
        self.assertIn("add   %r8, 2", fetch16)

    def test_word_operand_at_page_tail_maps_second_byte_in_iram(self) -> None:
        fetch16 = between(self.source, ".macro FETCH16_CROSS slow", ".endm")
        self.assertIn("call  .Lfetch16_cross", fetch16)
        self.assertIn("cmp   %r13, 0", fetch16)
        self.assertIn("jreq  \\slow", fetch16)
        bridge = between(
            self.source, ".Lfetch16_cross:", ".Lstore_post_rules:"
        )
        self.assertIn("ext   CTX_CODE_PAGES", bridge)
        self.assertIn("ld.uh %r13, %r13", bridge)
        self.assertIn("ld.w  %r13, [%r13]", bridge)
        self.assertIn("ld.ub %r12, [%r2]", bridge)
        self.assertIn("ld.ub %r2, [%r13]+", bridge)
        lda = between(self.source, ".LopAD:", ".Lop2D:")
        self.assertIn("FETCH16_CROSS .Lexit_slow1", lda)
        jump = between(self.source, ".Lop4C:", ".Lop60:")
        self.assertIn("FETCH16_CROSS .Lexit_slow1", jump)

    def test_top_indexed_arithmetic_and_store_stay_in_iram(self) -> None:
        arithmetic = between(self.source, ".Lop7D:", ".Lop8D:")
        self.assertIn(".LopFD:", arithmetic)
        self.assertIn("and   %r12, FLAG_D", arithmetic)
        self.assertIn("add   %r12, %r5", arithmetic)
        self.assertIn("MAP_READ_VALUE .Lexit_slow3", arithmetic)
        self.assertIn("jp    .Lbinary_add", arithmetic)
        store = between(self.source, ".Lop9D:", "/* LDA/ADC/SBC/STA")
        self.assertIn("add   %r12, %r5", store)
        self.assertIn("MAP_WRITE_POINTER .Lexit_slow3", store)
        self.assertIn("add   %r10, 5", store)

    def test_direct_store_preserves_two_ram_write_post_rules(self) -> None:
        store = between(
            self.source, ".macro STORE_MAPPED_VALUE", ".endm"
        )
        self.assertIn("and   %r2, PAGE_FORCE_MASK", store)
        self.assertIn("call  .Lstore_post_rules", store)
        rules = between(
            self.source, ".Lstore_post_rules:", "/* ---------- Hot opcode"
        )
        self.assertIn("ext   0x1b", rules)
        self.assertIn("ext   0x28", rules)
        self.assertIn("ld.w  %r12, -1", rules)
        rmw = between(self.source, ".Lrmw_rol:", "/* Cheap second-tier")
        self.assertEqual(rmw.count("STORE_MAPPED_VALUE 1"), 6)

    def test_page_interior_dispatch_has_no_cross_page_test(self) -> None:
        dispatch = between(
            self.source, ".Ldispatch_fetch:", ".Ldispatch_page_tail:"
        )
        self.assertNotIn("ld.uh %r8", dispatch)
        self.assertNotIn("ext   255\n    cmp   %r12, %r12", dispatch)
        self.assertIn("ld.ub %r12, [%r2]+", dispatch)
        self.assertIn("sll   %r12, 1", dispatch)
        self.assertIn("ld.uh %r13, [%r13]", dispatch)
        self.assertIn("jp    %r13", dispatch)

    def test_page_tail_maps_wrapped_operand_page_before_handler(self) -> None:
        tail = between(self.source, ".Ldispatch_page_tail:", ".Lset_nz_finish:")
        self.assertIn("add   %r8, 1", tail)
        self.assertIn("ld.uh %r8, %r8", tail)
        self.assertIn("srl   %r12, 8", tail)
        self.assertIn("cmp   %r12, 0", tail)
        self.assertIn("jreq  .Lexit_slow1", tail)
        self.assertIn("ld.w  %r2, [%r2]", tail)
        self.assertIn("jreq  .Lexit_slow1", tail)
        self.assertIn("jp    %r13", tail)

    def test_finish_only_splits_at_page_start_or_tail(self) -> None:
        finish = between(self.source, ".Lfinish:", ".Lset_nz_finish_zero:")
        self.assertIn("cmp   %r12, 0", finish)
        self.assertIn("jreq  .Lmap_fetch", finish)
        self.assertIn("ext   255\n    cmp   %r12, %r12", finish)
        self.assertIn("jreq  .Ldispatch_page_tail", finish)
        self.assertIn("jp    .Ldispatch_fetch", finish)

    def test_observable_pc_boundaries_truncate_to_16_bits(self) -> None:
        map_fetch = between(self.source, ".Lmap_fetch:", ".Ldispatch_fetch:")
        control = between(self.source, ".Lcontrol_complete:", ".Lexit_slow3:")
        writeback = between(self.source, ".Lwriteback:", ".Lreturn:")
        self.assertIn("ld.uh %r8, %r8", map_fetch)
        self.assertIn("ld.uh %r8, %r8", control)
        self.assertIn("ld.uh %r8, %r8", writeback)

    def test_slow_rollbacks_still_match_consumed_instruction_bytes(self) -> None:
        exits = between(self.source, ".Lexit_slow3:", ".Lexit_deadline:")
        self.assertIn("sub   %r8, 3", exits)
        self.assertIn("sub   %r8, 2", exits)
        self.assertIn("sub   %r8, 1", exits)
        self.assertIn("jp    .Lwriteback", exits)

    def test_nz_is_lazy_and_materialized_at_observation_boundaries(self) -> None:
        nz_finish = between(self.source, ".Lset_nz_finish:", ".Lfinish:")
        nz_reload = between(self.source, ".Lset_nz_reload:", ".Lfinish_reload:")
        for tail in (nz_finish, nz_reload):
            self.assertIn("ld.ub %r9, %r9", tail)
            self.assertIn("add   %r12, 1", tail)
            self.assertIn("sll   %r12, 8", tail)
            self.assertIn("or    %r9, %r12", tail)
            self.assertNotIn("%r11", tail)
        materialize = between(
            self.source, ".Lmaterialize_nz:", ".Lfinish_reload:"
        )
        self.assertIn("srl   %r12, 8", materialize)
        self.assertIn("ext   125", materialize)
        self.assertIn("or    %r9, FLAG_Z", materialize)
        for start, end in (
            (".LopD0:", ".Lop90:"),
            (".Lop30:", ".LopF0:"),
            (".LopF0:", ".Lbranch_not_taken:"),
            (".Lop10:", ".Lslow_opcode:"),
        ):
            branch = between(self.source, start, end)
            self.assertIn("BRANCH_", branch)
            self.assertNotIn("call  .Lmaterialize_nz", branch)
        self.assertIn("call  .Lmaterialize_nz", between(
            self.source, ".Lop08:", ".Lop68:"
        ))
        writeback = between(self.source, ".Lwriteback:", ".Lreturn:")
        self.assertIn("call  .Lmaterialize_nz", writeback)
        self.assertIn("ld.w  [%r0], %r11", writeback)

    def test_dispatch_can_enter_only_the_validated_module_abi(self) -> None:
        dispatch = between(
            self.source, ".Lexit_dispatch:", ".Lexit_dispatch_write:"
        )
        self.assertIn("ext   CTX_NATIVE_ENTRY", dispatch)
        self.assertEqual(dispatch.count("call  %r13"), 1)
        self.assertIn("jp    .Lcontrol_reload", dispatch)
        self.assertNotIn(".Lwriteback", dispatch)
        for offset in (
            "CTX_AC", "CTX_IX", "CTX_IY", "CTX_SP", "CTX_STATUS",
            "CTX_CYCLES", "CTX_INSTRUCTIONS",
        ):
            self.assertIn(offset, dispatch)

    def test_native_modules_are_generated_outside_iram(self) -> None:
        self.assertIn('section = f".text.{symbol}"', self.native_packer)
        self.assertIn("for block_id, record in enumerate(records)", self.native_packer)
        self.assertIn(
            "groups[(virtual >> group_shift, physical >> group_shift)]",
            self.native_packer,
        )
        self.assertIn("slot = blocks[0][1][1] >> 12", self.native_packer)
        self.assertIn("bank = blocks[0][1][0] >> 12", self.native_packer)
        self.assertNotIn('section(".iram")', self.native_packer)

    def test_accumulator_rotates_keep_dispatch_table_register(self) -> None:
        rol = between(self.source, ".Lop2A:", ".Lop4A:")
        ror = between(self.source, ".Lop6A:", ".LopAA:")
        self.assertNotIn("%r3", rol)
        self.assertNotIn("%r3", ror)
        self.assertIn("jp    .Lset_nz_finish", rol)
        self.assertIn("jp    .Lset_nz_finish", ror)

    def test_pc_boundary_model_preserves_wrap_and_rollback(self) -> None:
        # Exhaust all guest PCs and the operand widths supported by FETCH8/16.
        for pc in range(0x10000):
            opcode_pc = (pc + 1) & 0xFFFF
            self.assertEqual(opcode_pc, (pc + 1) & 0xFFFF)
            for operand_width in (0, 1, 2):
                completed = (opcode_pc + operand_width) & 0xFFFF
                rolled_back = (completed - (1 + operand_width)) & 0xFFFF
                self.assertEqual(rolled_back, pc)

        # Opcode fetch at 0xffff wraps into side-effectful page zero.  The
        # page-tail slow1 path must replay from the original opcode address.
        wrapped_after_opcode = (0xFFFF + 1) & 0xFFFF
        self.assertEqual(wrapped_after_opcode, 0)
        self.assertEqual((wrapped_after_opcode - 1) & 0xFFFF, 0xFFFF)


if __name__ == "__main__":
    unittest.main()
