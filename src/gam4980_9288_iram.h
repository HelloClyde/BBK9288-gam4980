#ifndef GAM4980_9288_IRAM_H
#define GAM4980_9288_IRAM_H

enum {
    GAM4980_IRAM_STATUS_DISABLED = 0,
    GAM4980_IRAM_STATUS_ACTIVE = 1,
    GAM4980_IRAM_STATUS_RESTORED = 2,
    GAM4980_IRAM_STATUS_AMR_BUSY = -1,
    GAM4980_IRAM_STATUS_INVALID_SIZE = -2,
    GAM4980_IRAM_STATUS_COPY_FAILED = -3,
    GAM4980_IRAM_STATUS_RESTORE_FAILED = -4
};

int gam4980_9288_iram_status(void);
unsigned long gam4980_9288_iram_size(void);
unsigned long gam4980_9288_iram_calls(void);
unsigned long gam4980_9288_iram_bytes_installed(void);
int gam4980_9288_iram_enter(void);
unsigned long gam4980_9288_iram_begin_range(
    unsigned long address, unsigned long size
);
void gam4980_9288_iram_end_range(void);
void gam4980_9288_iram_leave(void);
int gam4980_9288_iram_session_install(void);
int gam4980_9288_iram_session_validate(void);
void gam4980_9288_iram_session_restore(void);
unsigned long gam4980_9288_iram_range_depth(void);
unsigned long gam4980_9288_iram_session_repairs(void);

#endif
