ENTRY(DL_AppMain)

SECTIONS
{
    __dp = 0x00000000;
    . = 0x02700000;

    .startup :
    {
        KEEP(*(.text.startup))
    }

    /* Match the app_env_9288 SDK's native IRAM overlay contract exactly:
       the executable image lives at 0x02700100, while its run address is
       0x00000800.  Normal code starts at 0x02701800, which leaves at most
       0x1700 bytes for the overlay. */
    .iram 0x00000800 : AT(0x02700100)
    {
        __iram_start = .;
        . = ALIGN(4);
        __iram_exec_engine_start = .;
        KEEP(*(.iram.hot.exec_engine))
        KEEP(*(.iram.hot.exec_helper))
        /* The real core may fetch an aligned 32-bit instruction word around
           the final 16-bit RET.  Include alignment bytes in the installed
           range instead of leaving the upper halfword owned by firmware. */
        . = ALIGN(4);
        __iram_exec_engine_end = .;

        __iram_picture_head_start = .;
        KEEP(*(.iram.hot.picture_head))
        __iram_picture_head_end = .;

        . = ALIGN(4);
        __iram_picture_tail_start = .;
        KEEP(*(.iram.hot.picture_tail))
        __iram_picture_tail_end = .;

        . = ALIGN(4);
        __iram_shift_region_start = .;
        KEEP(*(.iram.hot.shift_region))
        __iram_shift_region_end = .;

        . = ALIGN(4);
        __iram_bitmap_region_start = .;
        KEEP(*(.iram.hot.bitmap_region))
        __iram_bitmap_region_end = .;

        KEEP(*(.iram.hot .iram.hot.*))
        . = ALIGN(4);
        __iram_end = .;
    }
    __iram_load_start = LOADADDR(.iram);
    __iram_load_end = LOADADDR(.iram) + SIZEOF(.iram);

    ASSERT(SIZEOF(.startup) <= 0x100,
           "startup overlaps the SDK IRAM load image")
    ASSERT(SIZEOF(.iram) <= 0x16c8,
           "GAM4980 IRAM hot core exceeds the true-device experiment bound")

    .text 0x02701800 : AT(0x02701800)
    {
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(4);
    __bss_end = .;

    .scratch (NOLOAD) :
    {
        . = ALIGN(4);
        __scratch_start = .;
        *(.scratch .scratch.*)
        __scratch_end = .;
    }

    /* KF2 only carries a flat payload length.  A real byte after the NOLOAD
       scratch range makes llvm-objcopy materialize the entire intervening
       range in the payload, so the 9288 loader reserves it. */
    . = ALIGN(4);
    .payload_tail :
    {
        BYTE(0xff)
    }
    __payload_end = .;

    /* The 9288 maps 8 MiB of SDRAM at 0x02000000.  Applications start at
       0x02700000, so a flat KF2 payload must stay below 0x02800000.  Crossing
       this boundary can still link and load far enough to corrupt the core's
       scratch buffers, which otherwise looks like a guest boot failure. */
    ASSERT(__payload_end <= 0x02800000,
           "GAM4980 payload exceeds the 9288 SDRAM window")
}
