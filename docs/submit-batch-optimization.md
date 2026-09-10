# Submission and atomic-picture optimization

Baseline: real-device log `ad171d53-9d49-49ed-99b8-4beb8f917ca1`.
97 s RTC, 24685 hardware ticks, 23872 core ticks, 495 submission ticks,
173 submissions; 187 public SysPicture calls, 1056 public drawing ticks.
Submission average was ~11.18 ms, public picture average ~22.06 ms.
These are quantized 256 Hz measurements. Public drawing is part of core time.
The 18.020 guest FPS is NOT cycle-equivalent to the older renderer.

## Changes

1. Dedicated full-screen expansion path: four packed source bytes per loop,
   last-byte padding mask outside the loop, no per-row dirty-mask or synced-row
   lookup/update. Still exactly 19200 hardware framebuffer bytes per submit,
   including white margins. No DMA and no partial physical-screen writes.
2. Invalidate the optional old row synchronization masks once per full physical
   submission. This prevents stale-cache skips if a partial path is used later;
   it replaces 96 per-row shadow copies, not the authoritative committed frame.
3. Public SysPicture source copy resolves a page once per contiguous chunk,
   instead of looking up the page for each byte. LCD dirty marking is deferred
   to the successful atomic call's end, rather than storing it per changed byte.
   Source guards, edge masks, folded LCD addresses, resource fonts and returns
   are unchanged. No guest IRQ callback is inserted into drawing.
4. Poll hardware input after each complete guest frame. If a catch-up batch has
   already consumed eight host ticks, return to presentation/input processing
   before another frame. This is a between-frame yield, NOT a 31.25 ms maximum
   on a frame or function. It does not solve every long in-frame stall. Excess
   catch-up work is dropped, as with the existing bounded catch-up policy; no
   synthetic guest frames are counted for unexecuted iterations.
5. Submission accounting splits into `bare_scanout_ticks` (expansion + hardware
   writes + wrapper counters) and `bare_present_metadata_ticks` (snapshot and
   pending-state maintenance). Their sum is `bare_present_ticks`; the maximum
   still covers a whole submission. `bare_batch_host_yields` counts early batch
   boundaries. `framebuffer_submit_optimized_version=1` identifies this build.

The new scanout measurement is not bare memory-bus time. The 256 Hz resolution
has not improved, and split intervals share a sampled endpoint to avoid
double-counting. No real-device speedup is claimed before a comparable run.

## Validation

- 1612 production expansion cases against an independent pixel renderer.
- Original-ROM public graphics comparisons and relocation-free target entries.
- Catch-up boundary tests: below/at threshold, final frame, single frame, wrap.
- Existing performance-log contracts.
- Final target build and isolated emulator validation recorded with artifacts.

Install EXE and `gam4980/GAM4980.NAT` together. No GAM or ROM resources included.

## Final build verification

Final isolated emulator reached Fumo intro and story, then returned to the
desktop with Esc. The physical screen matched the last committed snapshot at
all 76800 pixels. The new public picture implementation accepted 110 calls
without fallback in that run; system restoration reported success.
New timing fields were present and additive. The emulator recorded zero ticks
for the short submission spans and no host batch yields, so it supplies no
real-device timing claim or integration coverage of the yield-taken branch;
the threshold and wrap behavior are covered by the production-helper unit test.

- EXE: 693281 bytes, SHA256
  `90378582f96543219b0a391e6b8a3e23355962abf880b7ec499bb733f3a48915`.
- NAT: 208504 bytes, SHA256
  `ac647eea59ff316f2a0ead2ac70a38ef669fe946c1e8a463862cce55c170f223`.
- IRAM: unchanged at 5332 bytes.
- Artifacts: `build/submit-batch/` and `build/GAM4980-SUBMIT-BATCH.zip`.
