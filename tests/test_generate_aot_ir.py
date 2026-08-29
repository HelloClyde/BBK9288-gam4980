import unittest

from tools.generate_aot_ebin import (
    CpuFlag,
    InstructionIR,
    analyze_flag_liveness,
    emit_block_ir,
    flag_effects,
)


def instruction(pc: int, opcode: int) -> InstructionIR:
    reads, writes = flag_effects(opcode)
    return InstructionIR(pc, bytes((opcode,)), reads, writes)


class AotIrFlagLivenessTest(unittest.TestCase):
    def analyze(self, *opcodes: int) -> list[InstructionIR]:
        instructions = [
            instruction(0x6000 + index, opcode)
            for index, opcode in enumerate(opcodes)
        ]
        analyze_flag_liveness(instructions)
        return instructions

    def test_overwritten_nz_results_are_dead(self) -> None:
        lda_first, tax, lda_last = self.analyze(0xA9, 0xAA, 0xA9)
        self.assertEqual(lda_first.live_writes, CpuFlag(0))
        self.assertEqual(tax.live_writes, CpuFlag(0))
        self.assertEqual(lda_last.live_writes, CpuFlag.NZ)

    def test_adc_carry_dependency_survives(self) -> None:
        adc_first, adc_last = self.analyze(0x69, 0x69)
        self.assertEqual(adc_first.live_writes, CpuFlag.C)
        self.assertEqual(adc_last.live_writes, CpuFlag.NZCV)

    def test_php_observes_previous_status(self) -> None:
        lda, php, lda_last = self.analyze(0xA9, 0x08, 0xA9)
        self.assertEqual(lda.live_writes, CpuFlag.NZ)
        self.assertEqual(php.reads, CpuFlag.ALL)
        self.assertEqual(lda_last.live_writes, CpuFlag.NZ)

    def test_rotate_reads_old_carry(self) -> None:
        sec, rol, lda = self.analyze(0x38, 0x2A, 0xA9)
        self.assertEqual(sec.live_writes, CpuFlag.C)
        self.assertEqual(rol.live_writes, CpuFlag.C)
        self.assertEqual(lda.live_writes, CpuFlag.NZ)

    def test_final_writer_remains_fully_observable(self) -> None:
        compare, branch = self.analyze(0xC9, 0xD0)
        self.assertEqual(compare.live_writes, CpuFlag.NZC)
        self.assertEqual(branch.reads, CpuFlag.Z)

    def test_add16_phrase_fuses_direct_ram_pair(self) -> None:
        data = (
            b"\x18", b"\xad\x3a\x00", b"\x69\x20", b"\x8d\x3a\x00",
            b"\xad\x3b\x00", b"\x69\x00", b"\x8d\x3b\x00",
        )
        instructions = []
        for index, encoded in enumerate(data):
            reads, writes = flag_effects(encoded[0])
            instructions.append(
                InstructionIR(0x6600 + index, encoded, reads, writes)
            )
        analyze_flag_liveness(instructions)

        emitted, requires_binary, fusion_count = emit_block_ir(instructions)

        self.assertEqual(fusion_count, 1)
        self.assertTrue(requires_binary)
        self.assertEqual(len(emitted), 1)
        self.assertIn("S6502_AOT_ADD16_IMM(0x003au, 0x0020u", emitted[0])

    def test_add16_phrase_rejects_special_page_zero_register(self) -> None:
        data = (
            b"\x18", b"\xad\x0d\x00", b"\x69\x01", b"\x8d\x0d\x00",
            b"\xad\x0e\x00", b"\x69\x00", b"\x8d\x0e\x00",
        )
        instructions = []
        for index, encoded in enumerate(data):
            reads, writes = flag_effects(encoded[0])
            instructions.append(
                InstructionIR(0x6700 + index, encoded, reads, writes)
            )
        analyze_flag_liveness(instructions)

        emitted, requires_binary, fusion_count = emit_block_ir(instructions)

        self.assertEqual(fusion_count, 0)
        self.assertFalse(requires_binary)
        self.assertEqual(len(emitted), len(instructions))


if __name__ == "__main__":
    unittest.main()
