# Public atomic native drawing

User-approved contract: public drawing calls are atomic to the GAM caller.
Finish the drawing before returning; do not emulate drawing-internal guest
interrupt checkpoints or reconstruct every intermediate 6502 scratch store.
This is functional replacement, not instruction/cycle equivalence.

## Implementation

`src/firmware_native_public_graphics.c` supplies four separately loadable NAT
functions, with both C and shared-register entry adapters:

| API | Physical entry | Virtual entry |
| --- | --- | --- |
| SysPicture | EB582D | 682D |
| SysPartPicture | EB8000 | 5000 |
| SysAscii | EB53D7 | 63D7 |
| SysChinese | EB4C57 | 5C57 |

The source/page checks happen before drawing. Packed rows are composed with
byte shifts and edge masks directly in canonical guest LCD RAM. There is no
6502 instruction execution, row-by-row guest state reconstruction, SDK drawing
call or guest IRQ callback in these accepted functions. The normal bank-call
wrapper still restores the caller's bank. The native return pops the caller's
6502 return address, leaves the software argument stack unchanged, and treats
A/X/Y/condition flags as caller-clobbered for these void APIs.

The hardware framebuffer is still rewritten in full at submission boundaries.
This preserves the fix for persistent white lines. SysPicture completion can
publish a complete packed snapshot. One picture/glyph call is atomic; this does
not invent a transaction around multiple independent tile calls made by a game.

SysPicture follows the offline renderer's packed composition, including its
single-byte padding behavior and aligned flag=1 copy. SysPartPicture follows
the actual firmware's two-byte image header/crop contract, independently tested
against that ROM; it is not claimed to be copied from an offline implementation.
Fonts use the original 8.BIN resources and E.BIN character conversion tables,
not SDK font rendering inside bare mode. The rightmost glyph preserves the
invisible padding bit beyond the 159-pixel screen.

Special mappings, source/LCD aliasing, missing readable pages, decimal mode,
or insufficient entry budget return without guest mutation and retain the
compatible implementation. Internal firmware entry points are still supported.
This does not make E.BIN resource-only for every possible game call.

## Accounting

An accepted call charges six synthetic guest cycles (atomic service/return),
not the original routine's thousands of instruction cycles. Accordingly,
`wall_guest_rate_cycle_equivalent=0` when these replacements have executed.
The old guest frame/speed counters remain for continuity, but cannot establish
speedup relative to the old cycle-accounted renderer or standalone game ports.

`native_public_*` reports all accepted public calls, split by API, plus total
host ticks, maximum call ticks and compatibility fallback attempts. This is
recorded inside the callable body, including private-register callers that the
old C-bridge profiler missed. Host ticks use the supplied hardware clock only
when debug profiling is enabled. Resolution remains 1/256 second; zeros for
short calls are quantization, not evidence of zero cost. Total per-call rounded
ticks should not be confused with a high-resolution or exclusive session timer.

Compare comparable real-device routes and wall time, input responsiveness and
actual submissions. Public-call timing is nested inside core time, not additive.

## Verification

- 587 public picture/crop/glyph cases against executed original ROM pixels,
  including sub-byte shifts, screen fold rows, narrow pictures, full screen,
  font class boundaries and all 227 nonzero conversion-table entries.
- Stack/return and argument preservation, plus three no-mutation source guards.
- All four specialized S1C33 entry objects are relocation-free.
- Existing 11000 graphics and 1248 glyph internal-entry equivalence cases pass.
- Submission policy and 1612 framebuffer expansion cases pass.
- Package entry ownership and dispatch guards: 17 tests pass, 145 functions,
  no duplicate public/internal owner for 682D.

Target-emulator results and final artifact hashes are recorded below after
final build validation. No real-device performance claim is made by these tests.

### Final artifact check

Final isolated emulator: Fumo title/main menu and Mota title/story display;
both returned to the desktop with Esc. Both committed-screen checks compared
76800 physical pixels with zero differences. Fumo's story was also checked
during integration before adding the three missing static dispatcher entries.
Observed public calls in these game routes were SysPicture calls (Mota 147
at the story snapshot); no claim of in-game public text/crop coverage is made.
Those other APIs have original-ROM functional tests and target PIC checks.
The final IRAM engine remains 5332 bytes: these drawing bodies are external
NAT functions, not additional IRAM payload.

- EXE: 692121 bytes, SHA256
  `be8d89ff78f5afba0eaa105eae6469dd3513a230f969f5c6f3073e41611a8a0d`.
- NAT: 208482 bytes, SHA256
  `e4d37c4b9bf20e76fa162edad955916f96e5fbd6225c4343d6f0820c28d2e6ca`.
- Final build directory: `build/public-atomic/`.

## Installation

Replace EXE and `gam4980/GAM4980.NAT` together. Graphics service ABI is version 4;
do not mix this EXE with older drawing modules. Keep existing 8.BIN, E.BIN and
GAM files. The deliverable contains no games, ROM resources or SDK.
