"""Summarize the last complete PERF session, preserving repeated NAT groups."""
import argparse
import json
import re
from pathlib import Path

HLE_NAMES = ('bitmap_copy', 'wide_glyph', 'glyph_row', 'shift_blit',
             'byte_fill', 'multiply16', 'compare16', 'game_counter',
             'game_bitmap', 'bitmap_region', 'shift_region', 'game_scan',
             'game_record_scan', 'game_table_chain', 'game_object_flow',
             'game_record_reverse', 'bank_switch', 'and_long',
             'load_oper1_temp', 'compare_long', 'indirect_call',
             'picture_tail', 'picture_head', 'picture_resume',
             'graphics_address', 'hline_middle', 'part_picture_row',
             'pixel_tail', 'game_callback_scan')

def analyze(text):
    sessions = re.findall(r'\[GAM4980 PERF[^\]]*\](.*?)\[END\]', text, re.S)
    if not sessions:
        raise ValueError('No complete GAM4980 PERF session')
    values, functions, current = {}, [], None
    hle_functions, hle_current = [], None
    for line in sessions[-1].replace('\\_', '_').splitlines():
        key, sep, value = line.strip().partition('=')
        if not sep:
            continue
        if key == 'hle_sample_id':
            hle_current = {'id': int(value, 10)}
            hle_functions.append(hle_current)
        elif key.startswith('hle_sample_') and hle_current is not None:
            hle_current[key.removeprefix('hle_sample_')] = int(value, 10)
        elif key == 'nat_sample_pc':
            current = {'physical_pc': int(value, 10)}
            functions.append(current)
        elif key.startswith('nat_sample_') and current is not None:
            current[key.removeprefix('nat_sample_')] = int(value, 10)
        else:
            values[key] = value
    phases = {k[len('host_exclusive_'):-len('_ticks')]: int(v)
              for k, v in values.items()
              if k.startswith('host_exclusive_') and k.endswith('_ticks')}
    total = sum(phases.values())
    for entry in functions:
        ticks = entry.get('exclusive_ticks', 0)
        entry['physical_pc_hex'] = f"{entry['physical_pc']:06X}"
        entry['sampled_core_percent'] = round(100 * ticks / total, 2) if total else None
    functions.sort(key=lambda x: (-x.get('exclusive_ticks', 0), -x.get('attempts', 0)))
    for entry in hle_functions:
        entry['name'] = HLE_NAMES[entry['id']] if 0 <= entry['id'] < len(HLE_NAMES) else 'lookup/unclassified'
        entry['sampled_core_percent'] = round(100 * entry.get('exclusive_ticks', 0) / total, 2) if total else None
        entry['kind'] = 'lookup/unclassified' if entry['id'] == 31 else 'HLE path (may combine multiple matched entries)'
    hle_functions.sort(key=lambda x: -x.get('exclusive_ticks', 0))
    return {'game': values.get('game'), 'sampled_phase_ticks': phases,
            'sampled_total_ticks': total, 'functions': functions, 'hle_functions': hle_functions,
            'hle_attribution_delta_ticks': (phases.get('hle', 0) - sum(x.get('exclusive_ticks', 0) for x in hle_functions)) if hle_functions else None,
            'guest_fps': int(values.get('wall_guest_millifps', '0')) / 1000,
            'warning': 'Sampled exclusive time only, not full-session time. Zero ticks do not mean free. ' +
                ('Includes sampled private register-ABI entries; instrumentation overhead is not subtracted.'
                 if values.get('nat_function_profile_c_bridge_only') == '0' else
                 'Register-ABI entries are included in IRAM time, not the NAT function table.')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    data = args.log.read_bytes()
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        text = data.decode('gb18030')
    print(json.dumps(analyze(text), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
