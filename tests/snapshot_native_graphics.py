"""Read actual ABI-5 NAT execution / framebuffer counters from an isolated QEMU."""
import argparse
import json
from pathlib import Path
from emulator_qmp_smoke import QmpClient
from emulator_qmp_iram_super_equivalence import map_symbol, read_words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-graphics', action='store_true')
    parser.add_argument('--require-text', action='store_true')
    args = parser.parse_args()
    qmp = QmpClient('127.0.0.1', args.port)
    running = qmp.command('query-status')['running']
    try:
        if running:
            qmp.command('stop')
        def words(symbol, count=1):
            return read_words(qmp, map_symbol(args.map, symbol), count)
        result = {name: words(name)[0] for name in (
            'native_module_status_value', 'native_module_preloaded_count',
            'native_module_load_count', 'native_module_resident_code_bytes',
            'native_module_manifest_size', 'native_module_eviction_count')}
        result['services'] = words('native_graphics_services', 9)
        result['io'] = dict(zip(('lcd_bytes', 'mirrored_writes', 'text_rows', 'reserved'),
                                words('native_graphics_io_metrics', 4)))
        raw = words('native_graphics_host_profile', 10)
        keys = ('attempts', 'accepted', 'host_ticks', 'max_host_ticks', 'zero_returns')
        result['graphics'] = dict(zip(keys, raw[:5]))
        result['text'] = dict(zip(keys, raw[5:]))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        qmp.capture(args.output.with_suffix('.ppm'))
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
        assert result['native_module_status_value'] == 1
        if args.require_graphics:
            assert result['graphics']['accepted'] > 0
        if args.require_text:
            assert result['text']['accepted'] > 0
        if args.require_graphics or args.require_text:
            assert result['io']['lcd_bytes'] > 0
            assert result['services'][0:2] == [2, 0x3c0000]
        for name in ('graphics', 'text'):
            p = result[name]
            # Pausing in a NAT call can leave the current attempt unfinished.
            assert 0 <= p['attempts'] - p['accepted'] - p['zero_returns'] <= 1
    finally:
        if running:
            qmp.command('cont')
        qmp.close()


if __name__ == '__main__':
    main()
