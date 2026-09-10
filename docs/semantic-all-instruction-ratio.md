# SEMANTIC-ALL emulator instruction samples (2026-09-07)

EXE: `GAM4980-SEMANTIC-ALL.exe`, SHA256
`d5478a711c91a8736b32d795817b77a6b2cd5fa04476c0abe6f06b17bf476908`.
Game: the user's 851968-byte Fumo GAM, SHA256
`231b0797ec3eef4db79e45f2585ef8b03b4abd4b4c10a7657dd442800c0dce63`.

This is an **IRAM interpreter-only** dynamic instruction ratio. It is not a
whole-game ratio and not a device performance measurement. No emulator or
EXE code was modified for this measurement.

## Method

QEMU HMP `one-insn-per-tb on` plus `log exec,nochain` records each executed
S1C33 translation step. Every trace line was verified to have `CF_COUNT_MASK`
equal to 1. S1C33's translator consumes one 16-bit word per step, so EXT
prefix instructions count separately. Logging is bounded to short windows;
single-instruction mode and logging are turned off after each sample.

Numerator: executed PCs within the current IRAM overlay `[0x800,0x1e7c)`.
Denominator: visits to the four `add %r11, 1` guest-instruction completion
sites at 0x8a4, 0x8b4, 0x8e8 and 0x8fe, identified in the matching ELF's
disassembly and checked against assembly source. This counts completed
ordinary guest instructions, not dispatch attempts. The numerator includes
IRAM entry/exit, fetch, flags, mapping checks and failed attempts. Sample
edges may include a partial instruction/burst.

## Results

| Scene | IRAM S1C33 words | Completed 6502 instructions | Host / guest |
|---|---:|---:|---:|
|Title animation|85,888|1,400|61.35|
|Opening text|87,865|1,382|63.58|
|Later opening text|303,778|4,314|70.42|
|Combined, weighted|477,531|7,096|67.30|

External S1C33 counts were 345239, 586525 and 1000409 respectively. These
include application C paths and potentially other non-IRAM work. Do not
divide them by the IRAM-only guest count: that would omit guest work handled
by C, semantic fusion and HLE. HLE-equivalent guest instruction totals are
not available from these samples. No map movement sample was collected.

Trace files, JSON summaries and screenshots are in `build/emulator-ratio/`.
Reproduction helper: `tests/emulator_instruction_ratio.py`. It expects the
matching ELF disassembly and running QMP endpoint. The sampling overhead
changes execution speed and potentially workload mix; these results are
not frame-rate predictions, whole-session averages, or a before/after
speedup comparison. The earlier unsupported 1:18 estimate is not supported
by this measurement.

## Instruction-cost attribution

Reused the same three traces without changing or rerunning the executable.
The build-specific region classifier is `tests/analyze_iram_trace_cost.py`;
its detailed output is `build/emulator-ratio/cost-breakdown.json`.
The following percentages measure **instruction volume, not elapsed time**.

| Region | S1C33 words | IRAM share | Words per completed guest instruction |
|---|---:|---:|---:|
|Handler bodies, including inline address/flag work|212511|44.50%|29.95|
|Code mapping and reload helpers|85122|17.83%|12.00|
|Completion and control checks|61392|12.86%|8.65|
|Opcode fetch and dispatch|50593|10.59%|7.13|
|Shared lazy N/Z helpers|37562|7.87%|5.29|
|Entry|10048|2.10%|1.42|
|Exit/writeback|10436|2.19%|1.47|
|Special-store postprocessing|9867|2.07%|1.39|

Every executed IRAM PC is assigned exactly once. Handler bodies are not
pure arithmetic: inline operand fetch, addressing, flag handling and guest
cycle bookkeeping remain in that group. Shared N/Z is not all flags cost.

Of 313 fully observed bursts, 77 completed no guest instructions. Those
zero-work bursts consumed 8346 IRAM words, only 1.75% of total IRAM volume.
Their external setup/fallback cost is not included. Thus zero-work entry
avoidance is not the leading *IRAM-only* opportunity in these samples.

There were 3525 visits to the code mapping helper and 2806 visits to the
reload completion path (39.54% of the 7096 completed guest instructions).
The latter rebuilds the dispatch base/host fetch mapping after R2/R3 have
been used as scratch. The leading reload-origin handlers were:

| Opcode | Operation | Reload completions |
|---|---|---:|
|8D|STA absolute|645|
|2E|ROL absolute|586|
|0E|ASL absolute|264|
|B1|LDA (zp),Y|224|
|85|STA zp|214|

Origins are attributed by the actual dispatch jump and the matching binary's
256-entry opcode table, not static instruction frequency. Only 22 visits
used the rare cross-page 16-bit operand helper, so optimizing that rare path
is not the priority.

Recommended first experiment: preserve the cached host PC/table registers
through the highest-frequency data-access handlers so they can use the
ordinary completion path. Mapping, bank changes, dirty writes and operand
crossings must retain their existing checks. Next consider the combined
fetch/completion/control path (23.45% of IRAM volume); do not remove guest
event boundaries. No engine change or speedup claim accompanies this report.
