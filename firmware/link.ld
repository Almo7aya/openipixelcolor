/* openipixelcolor - firmware for the 32x32 Bluetooth LED panel (NDS32 V3 MCU).
 * Flash: image runs from 0x10000 (the 64 KiB below belong to the bootloader).
 *        0x108000.. is the bootloader's update staging area, 0x1FF000 the update record.
 * RAM:   0x20000000; at least 0x20000000-0x2001921C is known to work. Real size not yet known,
 *        so stay inside that.
 */
OUTPUT_ARCH(nds32)
ENTRY(_vectors)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00010000, LENGTH = 0x000F8000
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00019400
}

__stack_size = 0x2000;   /* 8 KiB at the bottom of RAM: overflow faults instead of corrupting */

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text.boot)
        /* internal-flash driver: must sit inside 0x10000-0x10FFF, the window sys_memctl_config()
           locks into cache, so it can run while the flash is busy */
        __flashdrv_start = .;
        KEEP(*(.text.flashdrv))
        __flashdrv_end = .;
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > FLASH

    .stack (NOLOAD) :
    {
        . = . + __stack_size;
        __stack_top = .;
    } > RAM

    .data : ALIGN(8)
    {
        __data_start = .;
        *(.data .data.*)
        *(.sdata_d .sdata_d.* .sdata_w .sdata_w.* .sdata_h .sdata_h.* .sdata_b .sdata_b.* .sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.sbss_d .sbss_d.* .sbss_w .sbss_w.* .sbss_h .sbss_h.* .sbss_b .sbss_b.* .sbss .sbss.* .scommon*)
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* boot/crash bookkeeping that must survive a reset: never cleared by crt_init */
    .noinit (NOLOAD) : ALIGN(4)
    {
        *(.noinit .noinit.*)
    } > RAM

    /* gp-relative accesses reach +-256 KiB: centre gp in RAM so all of it is reachable */
    _SDA_BASE_ = ORIGIN(RAM) + 0x10000;

    __image_end = __data_load + SIZEOF(.data);
    __ram_end   = ORIGIN(RAM) + LENGTH(RAM);

    /DISCARD/ : { *(.comment) *(.note*) }
}

ASSERT(__flashdrv_end <= 0x00011000, "flash driver must stay inside the cache-locked window 0x10000-0x10FFF")
