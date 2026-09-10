"""Inspect the bank/NAT work counters of an already running isolated emulator.

This is a correctness/work-count check, not a physical-device FPS benchmark.
The caller selects its own test QMP port; no emulator is launched or terminated.
"""
import argparse
import json
import struct
from pathlib import Path

from emulator_qmp_smoke import QmpClient
from emulator_qmp_iram_super_equivalence import map_symbol, read_words


SYMBOLS = (
    'native_module_status_value', 'native_module_function_package',
    'native_function_union_active', 'native_module_full_rebuild_count',
    'native_module_bank_refresh_count', 'native_module_bank_nochange_count',
    'native_module_function_bank_fastpath_count',
    'native_module_rebuild_module_visit_count',
    'native_module_manifest_size', 'native_module_resident_code_bytes',
    'native_module_preloaded_count', 'native_module_load_count',
    'native_module_eviction_count',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--rom-index', action='store_true',
                        help='also validate the physical-ROM reverse index')
    args = parser.parse_args()
    addresses = {name: map_symbol(args.map, name) for name in SYMBOLS}
    if args.rom_index:
        addresses.update({name: map_symbol(args.map, name) for name in (
            'rom_cache_index_lookup_count', 'rom_cache_index_hit_count',
            'rom_cache_index_miss_count', 'rom_cache_runtime_miss_count',
        )})
    addresses['s6502_native_shared_metrics'] = map_symbol(
        args.map, 's6502_native_shared_metrics')
    qmp = QmpClient('127.0.0.1', args.port)
    was_running = qmp.command('query-status')['running']
    try:
        if was_running:
            qmp.command('stop')
        data = {name: read_words(qmp, address, 1)[0]
                for name, address in addresses.items()}
        data['native_calls'] = data.pop('s6502_native_shared_metrics')
        header = read_words(qmp, map_symbol(args.map, 'native_module_header'), 1)[0]
        data['package_module_count'] = read_words(qmp, header + 20, 1)[0]
        if args.rom_index:
            def read_bytes(name, size):
                words = read_words(qmp, map_symbol(args.map, name), (size+3)//4)
                return struct.pack('<' + 'I'*len(words), *words)[:size]
            # This target keeps 64 lines; other cache sizes are covered by
            # the host differential test matrix, not inferred from pointers.
            index = read_bytes('rom_page_line_index', 1024)
            valid = read_bytes('rom_bank_valid', 64)
            regions = read_bytes('rom_bank_region', 64)
            pages = read_words(qmp, map_symbol(args.map, 'rom_bank_page'), 64)
            for line in range(64):
                if valid[line]:
                    assert regions[line] < 2 and pages[line] < 0x200000
                    assert pages[line] & 0xfff == 0
                    assert index[regions[line]*512 + (pages[line] >> 12)] == line
            for key, line in enumerate(index):
                if line != 255:
                    assert line < 64 and valid[line]
                    assert key == regions[line]*512 + (pages[line] >> 12)
            assert data['rom_cache_index_lookup_count'] > 0
            assert data['rom_cache_index_lookup_count'] == (
                data['rom_cache_index_hit_count'] + data['rom_cache_index_miss_count'])
            data['rom_index_consistent'] = True
        if args.capture:
            qmp.capture(args.capture)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(data, indent=2))
        assert data['native_module_status_value'] == 1
        assert data['native_module_function_package'] == 1
        assert data['native_function_union_active'] == 1
        assert data['native_module_full_rebuild_count'] == 1
        assert data['native_module_rebuild_module_visit_count'] == data['package_module_count']
        assert data['native_calls'] > 0
        assert data['native_module_function_bank_fastpath_count'] > 0
        assert data['native_module_bank_refresh_count'] == (
            data['native_module_bank_nochange_count'] +
            data['native_module_function_bank_fastpath_count'])
        print('NAT bank dispatch: one immutable index build, runtime remaps PASS')
    finally:
        if was_running:
            qmp.command('cont')
        qmp.close()


if __name__ == '__main__':
    main()
