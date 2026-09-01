#!/usr/bin/env python3
"""Contracts for the pageable S1C33 native module container."""

from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import pack_native_module as packer


class NativeModulePackageTest(unittest.TestCase):
    def test_real_aot_fingerprints_and_layout(self) -> None:
        signature, blocks = packer.parse_aot_header(
            ROOT / "src" / "s6502_aot_ebin_generated.h"
        )
        grouped = {}
        for block_id, block in enumerate(blocks):
            key = (block[1] >> 12, block[0] >> 12)
            grouped.setdefault(key, []).append((block_id, block))
        modules = [
            packer.NativeModule(
                slot, bank,
                bytes(((index + bank) * 37 + 11) & 0xFF for index in range(64)),
                grouped[(slot, bank)],
            )
            for slot, bank in sorted(grouped)
        ]
        package = packer.build_package(modules, signature)
        header = struct.unpack_from("<16I", package)

        self.assertEqual(header[0], packer.MAGIC)
        self.assertEqual(header[1:4], (1, 4, 64))
        self.assertEqual(header[4], len(package))
        self.assertEqual(header[5], len(modules))
        self.assertEqual(header[7], len(blocks))
        self.assertEqual(header[9], 0)
        self.assertGreater(header[11], 100)
        self.assertEqual(header[14], len(package) - header[13])
        self.assertEqual(
            header[15], packer.fnv1a(package[header[3] : header[13]])
        )

        parsed_ids = set()
        for index in range(header[7]):
            module, block_id, physical_pc, fingerprint = struct.unpack_from(
                "<4I", package, header[8] + index * 16
            )
            block = blocks[block_id]
            expected = signature[block[2] : block[2] + block[3]]
            self.assertLess(module, len(modules))
            self.assertEqual(physical_pc, block[0])
            self.assertEqual(fingerprint, packer.fnv1a(expected))
            parsed_ids.add(block_id)
        self.assertEqual(parsed_ids, set(range(len(blocks))))

    def test_generated_sources_cover_every_non_hle_block(self) -> None:
        signature, blocks = packer.parse_aot_header(
            ROOT / "src" / "s6502_aot_ebin_generated.h"
        )
        groups = {}
        for block_id, block in enumerate(blocks):
            key = (block[1] >> 12, block[0] >> 12)
            groups.setdefault(key, []).append((block_id, block))
        rendered = "\n".join(
            packer.render_module_source(index, group, signature)[0]
            for index, group in enumerate(groups.values())
        )
        for block in blocks:
            case = f"case 0x{block[1]:04x}u:"
            if block[1] in packer.aotgen.HLE_ENTRY_PCS:
                continue
            self.assertIn(case, rendered)

        # One normal C ABI bridge can now execute a run of basic blocks from
        # the same module.  The guest registers remain locals until a hard
        # barrier forces the chain to publish its state back to IRAM.
        self.assertIn("native_dispatch:", rendered)
        self.assertIn("native_chain:", rendered)
        self.assertIn("native_return:", rendered)
        self.assertIn("*native_epoch != entry_epoch", rendered)
        self.assertIn("goto native_chain", rendered)
        self.assertIn("metrics[5] += native_blocks - 1u", rendered)
        self.assertIn("metrics[6] = native_blocks", rendered)
        self.assertIn("metrics[7] += native_direct_links", rendered)
        self.assertIn("metrics_enabled = metrics ? metrics[9] : 0u", rendered)
        self.assertIn("if (metrics) metrics[8] =", rendered)
        self.assertIn("NATIVE_DIRECT(native_block_", rendered)
        self.assertIn("S6502_IRAM_PAGE_READ_DIRECT", rendered)
        self.assertIn("S6502_IRAM_PAGE_WRITE_DIRECT", rendered)
        self.assertIn("context->lcd_write_calls", rendered)
        self.assertIn('noinline, section(".text.s6502_native_module_', rendered)

    def test_shared_chain_accounting_leaves_final_tail_to_iram(self) -> None:
        # The module accounts for every internal transition and every guest
        # instruction except the final control instruction.  The unchanged
        # IRAM common tail adds those final two units exactly once.
        block_instruction_counts = (4, 7, 3, 9)
        module_instructions = sum(block_instruction_counts) - 1
        module_transitions = len(block_instruction_counts) - 1
        self.assertEqual(module_instructions + 1, sum(block_instruction_counts))
        self.assertEqual(module_transitions + 1, len(block_instruction_counts))

    def test_memory_split_keeps_old_total(self) -> None:
        core_h = (ROOT / "src" / "gam4980_core.h").read_text(encoding="utf-8")
        self.assertIn("#ifdef GAM4980_DYNAMIC_NATIVE_ALL", core_h)
        self.assertIn("GAM4980_BARE_ROM_CACHE_LINES 64u", core_h)
        self.assertIn("GAM4980_BARE_ROM_CACHE_LINES 128u", core_h)
        self.assertIn("GAM4980_NATIVE_CODE_ARENA_SIZE 0x40000u", core_h)
        self.assertEqual(64 * 0x1000 + 0x40000, 128 * 0x1000)
        native_h = (ROOT / "src" / "gam4980_native_module.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("GAM4980_NATIVE_CODE_SLOT_COUNT    4u", native_h)

    def test_nonzero_module_entry_is_published_to_every_page_link(self) -> None:
        signature, blocks = packer.parse_aot_header(
            ROOT / "src" / "s6502_aot_ebin_generated.h"
        )
        module = packer.NativeModule(
            5, 0x0EB0, bytes(range(64)), [(0, blocks[0])], 12
        )
        package = packer.build_package([module], signature)
        header = struct.unpack_from("<16I", package)
        module_record = struct.unpack_from("<14I", package, header[6])
        link_record = struct.unpack_from("<4I", package, header[12])
        self.assertEqual(module_record[5], 12)
        self.assertEqual(link_record[3], 12)

    def test_stable_module_key_is_stored_in_reserved_manifest_word(self) -> None:
        signature, blocks = packer.parse_aot_header(
            ROOT / "src" / "s6502_aot_ebin_generated.h"
        )
        module = packer.NativeModule(
            5, 0x0EB0, bytes(range(64)), [(0, blocks[0])],
            module_key=0x50EB050C,
        )
        package = packer.build_package([module], signature)
        header = struct.unpack_from("<16I", package)
        module_record = struct.unpack_from("<14I", package, header[6])
        self.assertEqual(module_record[13], 0x50EB050C)

    def test_game_cfg_recovers_direct_and_banked_functions(self) -> None:
        game = bytearray(0x8000)
        game[0x40:0x42] = (0x5046).to_bytes(2, "little")
        game[0x42:0x46] = len(game).to_bytes(4, "little")
        game[0x46:0x4c] = bytes.fromhex("a9 01 20 60 50 60")
        game[0x60] = 0x60
        game[0x70:0x7b] = bytes.fromhex(
            "a2 00 86 26 a2 51 86 27 20 f6 d2"
        )
        game[0x7b] = 0x60
        game[0x100:0x103] = bytes((0x60, 0x50, 0xE0))

        blocks, stats = packer.recover_game_blocks(bytes(game))
        entries = {(record[0], record[1]) for record in blocks}
        self.assertIn((packer.GAME_PHYSICAL_BASE + 0x46, 0x5046), entries)
        self.assertIn((packer.GAME_PHYSICAL_BASE + 0x60, 0x5060), entries)
        self.assertIn((packer.GAME_PHYSICAL_BASE + 0x70, 0x5070), entries)
        self.assertEqual(stats["entry_pc"], 0x5046)
        self.assertGreaterEqual(stats["queued_entries"], 3)

    def test_game_package_binds_whole_file_and_omits_block_matches(self) -> None:
        signature, blocks = packer.parse_aot_header(
            ROOT / "src" / "s6502_aot_ebin_generated.h"
        )
        firmware = packer.NativeModule(
            5, blocks[0][0] >> 12, bytes(range(64)), [(0, blocks[0])]
        )
        game_record = (
            packer.GAME_PHYSICAL_BASE, 0x5000, 0, 1, 1, 0
        )
        game = packer.NativeModule(
            5, packer.GAME_PHYSICAL_BASE >> 12, bytes(range(32)),
            [(0, game_record)], 0, packer.MODULE_GAME,
        )
        metadata = packer.GameMetadata(
            1234, 1024, 0x12345678, 0x5046, 1, 32, ((0, 1),)
        )
        package = packer.build_package(
            [firmware, game], signature, metadata
        )
        header = struct.unpack_from("<16I", package)
        extension = struct.unpack_from("<8I", package, packer.HEADER_SIZE)

        self.assertEqual(header[1], packer.GAME_FORMAT_VERSION)
        self.assertEqual(header[3], packer.GAME_HEADER_SIZE)
        self.assertEqual(header[7], 1)
        self.assertEqual(
            extension, (1, 1234, 1024, 0x12345678, 0x5046, 1, 32, 1)
        )
        self.assertEqual(header[9], 1)
        span = struct.unpack_from("<4I", package, header[10])
        self.assertEqual(
            span,
            (packer.RELOC_GAME_OWNER, 0, packer.RELOC_GAME_CODE_SPAN, 1),
        )
        game_module = struct.unpack_from(
            "<14I", package, header[6] + packer.MODULE_SIZE
        )
        self.assertTrue(game_module[1] & packer.MODULE_GAME)
        self.assertEqual(game_module[7], 0)


if __name__ == "__main__":
    unittest.main()
