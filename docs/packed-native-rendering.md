# Packed native rendering implementation (in progress)

This is the earlier row-kernel stage. Public atomic replacement has since been
implemented in [public-atomic-graphics.md](public-atomic-graphics.md); the open
items below describe this intermediate artifact, not the newer implementation.

This change is NOT yet a replacement of every public graphics API. It removes
repeated generic memory and per-byte state work inside the existing verified
graphics entry points. Public API interception and whole-call continuation
are still outstanding; do not describe this as complete offline-runtime parity.

## Implemented

- `$5C5D` bitmap rows: prove contiguous source and writable destination spans,
  compose packed bytes with edge masks, and materialize final row pointers,
  temporaries, flags and cycle totals once per row. Overlapping source/destination,
  special aliases, control RAM and incomplete spans keep the original path.
- `$6988` LCD rows: batch the first and interior bytes; retain the verified
  terminal-byte/row continuation. No pixel-by-pixel interpreter work in this
  accepted row's composition. Guest event budgets are unchanged.
- `$650F/$608A` glyphs: verify the complete remaining glyph's normal LCD/control
  mappings and readable source pages before entering map-specialized kernels.
  Constant control RAM accesses become direct accesses; generic mapping and
  callback checks are removed from accepted inner operations. No new SDK calls
  or substituted fonts are introduced. Other layouts retain the old kernels.
- Full framebuffer rewriting and the boundary-submission policy are retained.
  No IRAM execution-loop changes or guest frequency changes.

`native_packed_blit_version=1` identifies the implementation.
`native_packed_graphics_rows` counts accepted bitmap AND glyph fast rows;
it is a count, not a host-time measurement or public-API coverage percentage.

## Verification

Host graphics tests: 11000 CPU/RAM/cycle/LCD comparisons, including 308 fast rows.
Host text tests: 1248 ROM comparisons, including 132 map-specialized glyph rows.
Boundary policy tests and 1612 independent framebuffer expansion cases pass.
Target PIC compilation is checked by the graphics and text test suites.
These checks establish equivalence for tested cases, not real-device speedup.

Isolated target emulator: Fumo intro and story reached; packed fast paths
recorded 10553 rows (bitmap and glyph combined). All 76800 physical pixels
matched the last committed snapshot; Esc returned to the desktop. Built
artifacts are in `build/packed-blit/`, EXE SHA256
`43b31d50a5c5e732a19e33eeec141a61c5e3ee58e6b18b9494b29c2d6be6c11f`,
NAT SHA256 `a08cb72b9b13b5661279313a842821b15247419fdd2c5dbef2f3c484a2db1d69`.
These are intermediate development artifacts, not a completed public-API
replacement release. No push or release was performed.

## Still outstanding

- Replace verified public SysPicture/SysPartPicture/SysAscii/SysChinese calls
  with functional renderers, rather than entering internal ROM continuations.
- Define the interrupt-visible continuation and final public-call state contracts.
- Measure private register-ABI NAT host time, rather than infer its cost from
  the existing C-bridge-only function profiler.
- Determine whole-game speedup from a comparable real-device run; do not infer
  it from QEMU wall time or from fast-row hit counts.
