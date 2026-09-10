"""Compare an isolated emulator LCD with the player's last packed guest frame.

No game timing/performance assertion: stop only long enough to read both
surfaces, and resume if the emulator was running on entry.
"""
import argparse
import json
import struct
from pathlib import Path

from PIL import Image

from emulator_qmp_smoke import QmpClient
from emulator_qmp_iram_super_equivalence import map_symbol, read_words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, type=int)
    parser.add_argument('--map', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--live-ram', action='store_true',
                        help='compare immediate NAT output against live folded guest LCD RAM')
    parser.add_argument('--presented', action='store_true',
                        help='compare boundary mode with its last committed snapshot, not pending guest writes')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    qmp = QmpClient('127.0.0.1', args.port)
    running = qmp.command('query-status')['running']
    try:
        if running:
            qmp.command('stop')
        assert read_words(qmp, map_symbol(args.map, 'g_active'), 1)[0] & 255, \
            'bare gameplay is not active; cannot compare selector or Loading UI'
        assert not (args.live_ram and args.presented), 'choose one reference surface'
        words = read_words(qmp, map_symbol(args.map,
                           'g_presented_packed' if args.presented else 'lcd_frame'), 480)
        packed = struct.pack('<480I', *words)
        if args.live_ram:
            ram_pointer = read_words(qmp, map_symbol(args.map, 's6502_stack_ram'), 1)[0]
            ram = struct.pack('<1025I', *read_words(qmp, ram_pointer, 1025))
            output = bytearray(1920)
            for y in range(96):
                for column in range(20):
                    if column == 0:
                        address = 0xff3 if y == 65 else 0x413 + (64-y if y < 65 else y-1)*32
                    else:
                        address = 0x400 + (65-y if y <= 65 else y)*32 + column-1
                        if address == 0x400:
                            address = 0x1000
                    output[y*20+column] = ram[address]
            packed = bytes(output)
        capture = args.output.with_suffix('.ppm')
        qmp.capture(capture)
        actual = Image.open(capture).convert('RGB')
        expected = Image.new('RGB', (320, 240), 'white')
        pixels = expected.load()
        for y in range(96):
            for x in range(159):
                colour = (0, 0, 0) if packed[y*20 + x//8] & (128 >> (x%8)) else (255, 255, 255)
                for dy in range(2):
                    for dx in range(2):
                        pixels[1 + 2*x + dx, 24 + 2*y + dy] = colour
        assert actual.size == expected.size
        actual_bytes = actual.tobytes()
        expected_bytes = expected.tobytes()
        mismatches = sum(actual_bytes[i:i+3] != expected_bytes[i:i+3]
                         for i in range(0, len(actual_bytes), 3))
        actual.save(args.output.with_suffix('.png'))
        result = {'mismatching_pixels': mismatches, 'pixels_checked': 320*240,
                  'guest_size': [159, 96], 'view_origin': [1, 24],
                  'reference': 'live-ram' if args.live_ram else
                               'last-committed-snapshot' if args.presented else 'last-packed-frame'}
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(result))
        assert mismatches == 0, 'LCD differs from last captured guest frame (possibly stopped mid-present)'
    finally:
        if running:
            qmp.command('cont')
        qmp.close()


if __name__ == '__main__':
    main()
