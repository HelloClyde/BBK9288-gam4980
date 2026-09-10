#ifndef FIRMWARE_NATIVE_GRAPHICS_ABI_H
#define FIRMWARE_NATIVE_GRAPHICS_ABI_H

#include "gam4980_types.h"

/* ABI 5 appends one service pointer after the unchanged ABI 4 CPU context.
 * All services are guest-memory / display ownership facilities. No drawing
 * algorithm or firmware HLE is called through this structure. */
#define FW_GRAPHICS_SERVICE_VERSION 5u
typedef struct firmware_native_graphics_services {
    uint32_t version;
    uint32_t framebuffer; /* zero unless the bare session owns the LCD */
    uint32_t expand_lut; /* existing little-endian uint32_t [2][256] */
    uint32_t page3;
    uint32_t banks; /* uint16_t[16], live guest physical-bank map */
    uint32_t write8_resolved; /* uint32_t(uint16_t,uint8_t): write, return RAM offset or ~0u */
    uint32_t clock; /* uint32_t(void), 256 Hz; diagnostics only */
    uint32_t poll; /* void(void), bounded input checkpoint, no guest execution */
    uint32_t metrics; /* uint32_t[4]: LCD bytes, mirrored writes, text rows, packed fast-path rows */
    uint32_t batch; /* private stack-owned pending row; valid only inside NAT */
    uint32_t synced_frame; /* packed physical-display shadow, 1920 bytes */
    uint32_t synced_rows; /* three validity words; SDK ownership invalidates */
    uint32_t picture_state; /* uint32_t: bit0 picture active, bit1 full-screen */
    uint32_t picture_complete; /* void(uint32_t ram): snapshot before next draw */
    uint32_t read_physical; /* uint8_t(uint32_t): resource byte, no guest execution */
    uint32_t public_metrics; /* uint32_t[8]: calls,picture,part,ascii,chinese,ticks,max,fallback */
    uint32_t bank_map4; /* uint32_t(uint32_t bank): windows 5..8, zero means no mutation */
    uint32_t bank_descriptor_safe; /* uint32_t(uint32_t address): stable three-byte descriptor */
    uint32_t bank_metrics; /* optional uint32_t[12], diagnostic counts only */
} firmware_native_graphics_services_t;

#endif
