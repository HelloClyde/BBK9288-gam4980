# C6502 semantics in the live emulator

The normal `GAM4980_IRAM_V2` executable does not execute the legacy shadow
opcode templates. Offline `.GNA` peephole coverage is not evidence of coverage
in this executable, and neither code size nor static operation count measures
a dynamic S1C33/6502 instruction ratio.

The first live integration adds four load-time templates:

| Semantic ID | Pattern | Guest instructions | Guest cycles |
|---|---|---:|---:|
| 14 | 16-bit immediate addition | 7 | 18 |
| 15 | 16-bit immediate subtraction | 7 | 18 |
| 16 | Addition with PHP/SEI/PLP | 10 | 27 |
| 17 | Subtraction with PHP/SEI/PLP | 10 | 27 |

The matcher recognizes adjacent zero-page words, excluding I/O/bank registers,
wraparound and low-byte stores that alias a pending high-byte read. Decimal
mode falls back before any state or hit counter is changed. The preserving
forms retain the observable stack write. The non-preserving forms keep the
final high-byte A, N/Z and arithmetic carry/overflow.

The load-time collector prioritizes these forms over legacy software-stack
templates. `s6502_iram_keep_game_aot` explicitly publishes their entries in the
live bank-aware dispatch table. They execute in external native C code, through
the existing AOT boundary, not in additional IRAM or a PC-generated sidecar.
That boundary still has a cost: dynamic hits establish activity, not speedup.

PERF.LOG reports add16/sub16 immediate and preserving generic hits separately.
`tests/compare_c6502_active_semantics.py` compares masks 8191 and 131071 using
CPU/RAM/timing state hashes, and requires actual state output to avoid a vacuous
pass. Its host executable must be built with `GAM4980_STATE_DIAGNOSTICS` and
`GAM4980_AOT_DIAGNOSTICS`.

The initial four-template integration did not include register-register
arithmetic, generic copy, indirect memory or software-stack access; these are
now connected as described below. Caller-side DAAA/DACA lifting is a separate
experimental call transformation and remains disabled.

Host regression (2026-09-07), with the same input schedule and 100, 1000 and
5000 frames, agrees on full state with the four new semantics disabled.
At 5000 frames, observed ADD16_IMM / ADD16_PRESERVE / SUB16_PRESERVE hits are:
Fumo 747 / 2688 / 638; Sanguo 130 / 1075 / 281; Mota 2316 / 2138 / 1156.
SUB16_IMM has no observed hit in those scenes. These are host activity counts,
not real-device speed measurements.

Target emulator smoke (2026-09-07) used `GAM4980-C6502-ACTIVE.exe`,
920741 bytes, SHA256
`4be271573b8caff058812577ad90eef3355811858468aa77834210da938e8bef`.
IRAM audit passed at 5756 bytes. The file selector, startup animation and
opening story rendered normally. QMP memory inspection of the live target
counter array at `0x0278f6b8` showed semantic IDs 14..17 = 72 / 0 / 851 / 64
at the story capture; the metrics-enable word at `0x0278f6b4` was 1.
This establishes target execution of three forms, not real-device speedup or
exhaustive target equivalence. Screenshots are under
`build/emulator-c6502-active/`. The three-game full-state comparisons above
were host regressions; the target run was a Fumo smoke test.

## Legacy instruction AOT disabled by default

`GAM4980_ENABLE_LEGACY_INSTRUCTION_AOT` now defaults to 0. The firmware
dispatch retains only HLE hooks; instruction-expanded firmware bodies are
omitted, their IRAM dispatch entries are not published, and game linear
trace entries are no longer collected. Semantic templates and HLE remain
enabled independently. Unknown code continues through the interpreter.
The switch may be set to 1 only for comparative builds; do not use the
global `--no-aot` option as a substitute, since it disables semantic paths.

PERF.LOG includes `legacy_instruction_aot_enabled=0`. The resulting
`GAM4980-NO-LEGACY-AOT.exe` is 638537 bytes (previous ACTIVE: 920741 bytes),
with an unchanged 5756-byte IRAM overlay. Three-game full-state host
comparisons at 100/1000/5000 frames against the previous ACTIVE host binary
passed, as did the 128-test unit suite. This is not a measured device speedup.
Use `tests/compare_legacy_aot_disabled.py` to repeat the binary comparison.
The new target EXE also reached the Fumo opening story in the emulator.
Live semantic counters 14..17 were 69 / 0 / 851 / 63, confirming semantic
fusion still executes with legacy AOT disabled. Screenshots are in
`build/emulator-no-legacy/`; this smoke test is not an exhaustive target
state comparison.

## Remaining generic peepholes connected

The runtime now has counterparts for all 12 forms recognized by
`tools/pack_native_module.py:semantic_peephole`, not all conceivable C6502
compiler transformations. IDs 18..25 add:

| ID | Form | Instructions | Base cycles |
|---|---|---:|---:|
|18/19|ADD16_REGS / SUB16_REGS|7|20|
|20|STORE16_IMM|4|10|
|21|COPY16|4|12|
|22|LOAD16_INDIRECT|6|20 + read page crossings|
|23|STORE16_INDIRECT|6|22|
|24|LOAD_STACK8|2|7 + read page crossing|
|25|STORE_STACK8|2|8|

Direct words exclude special zero-page addresses and unsafe arithmetic
overlap. COPY16 keeps interleaved reads/writes for overlapping pairs.
Indirect operations retain generic memory side effects and re-read pointers
after writes; these are not unchecked host word loads/stores. Arithmetic
falls back in decimal mode. Short stack phrases resume the current straight-
line slice without an additional dispatcher boundary: redispatching at that
new boundary caused a Sanguo long-run state mismatch despite isolated
instruction equivalence. The corrected version matches the previous
NO-LEGACY host binary on all three games at 100/1000/5000 frames.

Long arithmetic/indirect phrases are prioritized ahead of tiny stack access
phrases in the bounded load table. IRAM publishes the new semantic entries;
implementations remain external native C. All twelve generic hit counters
are present in the lightweight performance log. This is coverage evidence,
not a speedup claim. PC pretranslation is not required, and legacy
instruction AOT remains disabled.

`tests/semantic_generic_equivalence.c` checks the eight new forms against
ordinary execution over 2048 cases: flags/decimal fallback, index wrap,
page crossing, overlapping copies and pointer-aliasing writes. Build with
`GAM4980_ENABLE_AOT`, `GAM4980_ENABLE_FIRMWARE_HLE`, and
`GAM4980_ENABLE_GAME_LOAD_AOT` enabled.

Final target build: `GAM4980-SEMANTIC-ALL.exe`, 649257 bytes, SHA256
`d5478a711c91a8736b32d795817b77a6b2cd5fa04476c0abe6f06b17bf476908`.
IRAM audit passes at 5756 bytes. The emulator reached the Fumo opening story;
live counter IDs 20..25 at that capture were 439 / 6 / 14 / 2 / 169 / 3.
Evidence images are under `build/emulator-semantic-all/`. IDs 18/19 were not
hit in that target scene and are covered by the isolated host tests instead.
Device performance has not yet been measured.
