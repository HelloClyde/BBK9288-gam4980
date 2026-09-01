#ifndef GAM4980_9288_BARE_H
#define GAM4980_9288_BARE_H

#include "gam4980_types.h"

typedef struct T_GAM4980_9288_BareKeys {
    u8 row[8];
} T_GAM4980_9288_BareKeys;

enum {
    GAM4980_BARE_STATUS_DISABLED = 0,
    GAM4980_BARE_STATUS_READY = 1,
    GAM4980_BARE_STATUS_ACTIVE = 2,
    GAM4980_BARE_STATUS_CLOCK_STOPPED = -1,
    GAM4980_BARE_STATUS_BACKGROUND_BUSY = -2,
    GAM4980_BARE_STATUS_IRAM_FAILED = -3,
    GAM4980_BARE_STATUS_RESTORE_FAILED = -4,
    GAM4980_BARE_STATUS_NESTED_ROM_READ = -5,
    GAM4980_BARE_STATUS_DIRECT_SDK_FAILED = -6
};

int gam4980_9288_bare_prepare(void);
int gam4980_9288_bare_enter(void);
int gam4980_9288_bare_leave(void);
int gam4980_9288_bare_active(void);
u32 gam4980_9288_bare_clock(void);
void gam4980_9288_bare_scan_keys(T_GAM4980_9288_BareKeys *keys);
void gam4980_9288_bare_submit(const u8 *frame, u32 size);
int gam4980_9288_bare_wait_video_idle(u32 max_polls);

/* The true-device BARE FS PROBE verified that read-only FS calls do not need
 * firmware IRQ service.  These guards are called from external RAM around a
 * direct filesystem operation and repair/validate all resident IRAM ranges
 * before control may return to an IRAM caller. */
int gam4980_9288_bare_direct_sdk_begin(void);
int gam4980_9288_bare_direct_sdk_end(void);

/* A ROM cache miss is serviced outside the bare interval.  These calls are
 * valid only from external RAM code; an IRAM HLE wrapper exposes its depth so
 * the filesystem gateway never restores code underneath a live IRAM frame. */
int gam4980_9288_bare_suspend_for_sdk(void);
int gam4980_9288_bare_resume_after_sdk(void);
void gam4980_9288_bare_note_nested_rom_read(void);

int gam4980_9288_bare_status(void);
u32 gam4980_9288_bare_entries(void);
u32 gam4980_9288_bare_restores(void);
u32 gam4980_9288_bare_rom_suspends(void);
u32 gam4980_9288_bare_new_irq_factors(void);
u32 gam4980_9288_bare_direct_sdk_calls(void);
u32 gam4980_9288_bare_direct_sdk_failures(void);

#endif
