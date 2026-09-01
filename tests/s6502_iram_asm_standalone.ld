ENTRY(s6502_iram_exec_burst_asm)

SECTIONS
{
    .iram 0x00000800 :
    {
        __iram_start = .;
        __iram_exec_engine_start = .;
        KEEP(*(.iram.hot.exec_engine))
        . = ALIGN(4);
        __iram_exec_engine_end = .;
        __iram_end = .;
    }

    /DISCARD/ :
    {
        *(.note.GNU-stack)
        *(.comment)
    }
}
