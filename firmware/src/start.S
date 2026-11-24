/*
 * start.S - reset, vector table, image header and interrupt entry.
 *
 * Target: Andes NDS32 V3 (instructions big-endian, data little-endian), image linked at 0x10000.
 * Image layout expected by the bootloader (see docs/HARDWARE.md):
 *   +0x00  41 x `j` vectors (reset, 8 exceptions, 32 HW interrupts)
 *   +0xB0  image header in the bootloader's format (layout fields + "FHD" magic)
 *   +0x100 code
 *
 * System registers are accessed with raw encodings so we do not depend on the assembler's
 * system-register names:  mfsr = 0x64000002 | rt<<20 | sr<<10,  mtsr = ... | 3.
 */

    .macro MFSR_RAW rt, sr
    .byte 0x64 | (((\rt) >> 4) & 1), ((((\rt) & 0xF) << 4) | (((\sr) >> 6) & 0xF)), (((\sr) & 0x3F) << 2), 0x02
    .endm
    .macro MTSR_RAW rt, sr
    .byte 0x64 | (((\rt) >> 4) & 1), ((((\rt) & 0xF) << 4) | (((\sr) >> 6) & 0xF)), (((\sr) & 0x3F) << 2), 0x03
    .endm

    /* system register indices (major<<7 | minor<<3 | ext) */
    .set SR_MSC_CFG,   0x020
    .set SR_PSW,       0x080
    .set SR_IPSW,      0x081
    .set SR_IVB,       0x089
    .set SR_INT_CTRL,  0x08A
    .set SR_ITYPE,     0x0A8
    .set SR_IPC,       0x0A9
    .set SR_EVA,       0x0A0
    .set SR_INT_MASK,  0x0C0
    .set SR_INT_MASK2, 0x0C1
    .set SR_INT_PRI,   0x0D8
    .set SR_INT_PRI2,  0x0D9
    .set SR_MMU_CTL,   0x100
    .set SR_MISC_179,  0x179

/* ------------------------------------------------------------------------------------------ */
    .section .vectors, "ax"
    .global _vectors
_vectors:
    j _reset
    .irp n, 1,2,3,4,5,6,7,8
    j _exc_\n
    .endr
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    j _irq_\n
    .endr

    .org 0xB0, 0xFF
    .global _image_header
_image_header:                       /* format fields the bootloader expects, +0xB0..+0xD7 */
    .byte 0x00,0x00,0x10,0x00, 0x00,0x00,0x1D,0x00, 0x14,0x00,0x01,0x03, 0xFF,0xFF,0xFF,0xFF
    .byte 0xC9,0xBD,0xBE,0xB0, 0x06,0x07,0x00,0x00
    .org 0xD4, 0xFF
    .byte 0x46,0x48,0x44,0x03        /* "FHD\x03" */
    .org 0x100, 0xFF

/* ------------------------------------------------------------------------------------------ */
    .section .text.boot, "ax"
    .global _reset
_reset:
    la      $gp, _SDA_BASE_

    /* if the core has an EX9 instruction table, disable it (ITB = 0) */
    MFSR_RAW 0, SR_MSC_CFG
    sethi   $r1, 0x1000              /* 1 << 24 */
    and     $r2, $r0, $r1
    beqz    $r2, 1f
    movi    $r0, 0
    .byte   0x42, 0x0e, 0x00, 0x21   /* mtusr $r0, $ITB */
1:
    la      $sp, __stack_top

    jal     sys_power_trim_init      /* 0x4002E8xx writes (C) */
    jal     cpu_sysreg_init          /* below */
    jal     sys_memctl_reset         /* 0x40009000 / 0x40010128 (C) */
    jal     crt_init                 /* .data / .bss (C) */
    jal     main
2:  j       2b

/* Same early init sequence as the vendor firmware */
    .global cpu_sysreg_init
cpu_sysreg_init:
    movi    $r0, 0
    MTSR_RAW 0, SR_MISC_179
    MFSR_RAW 0, SR_MMU_CTL
    sethi   $r1, 0x800               /* 1 << 23 */
    or      $r0, $r0, $r1
    MTSR_RAW 0, SR_MMU_CTL
    movi    $r0, 0
    MTSR_RAW 0, SR_INT_MASK
    la      $r0, _vectors            /* = 0x10000 */
    MTSR_RAW 0, SR_IVB
    MFSR_RAW 0, SR_PSW
    movi    $r1, ~6
    and     $r0, $r0, $r1
    sethi   $r1, 0x70                /* 0x70000: CPL = 7 */
    ori     $r1, $r1, 0x8            /* POM = superuser */
    or      $r0, $r0, $r1
    MTSR_RAW 0, SR_PSW
    dsb
    /* programmable interrupt priority present? (IVB bit 0) */
    MFSR_RAW 0, SR_IVB
    andi    $r1, $r0, 1
    beqz    $r1, 3f
    movi    $r1, 0
    MTSR_RAW 1, SR_INT_CTRL
    andi    $r0, $r0, 0xE
    movi    $r1, 0xA
    bne     $r0, $r1, 4f
    li      $r0, 0xAAAAAEAB          /* IRQ0/5 = 3, others 2 */
    MTSR_RAW 0, SR_INT_PRI
    li      $r0, 0x2AAAAAAA          /* IRQ16..30 = 2, IRQ31 = 0 (highest: display) */
    MTSR_RAW 0, SR_INT_PRI2
    j       3f
4:  movi    $r0, 0xE4
    MTSR_RAW 0, SR_INT_PRI
3:  ret

/* ------------------------------------------------------------------------------------------ */
/* small CPU helpers callable from C                                                          */
    .global cpu_gie_enable
cpu_gie_enable:
    setgie.e
    dsb
    ret

    .global cpu_gie_disable
cpu_gie_disable:
    setgie.d
    dsb
    ret

    .global cpu_int_mask2_get
cpu_int_mask2_get:
    MFSR_RAW 0, SR_INT_MASK2
    ret

    .global cpu_int_mask2_set
cpu_int_mask2_set:
    MTSR_RAW 0, SR_INT_MASK2
    dsb
    ret

    .global cpu_mmu_ctl_or
cpu_mmu_ctl_or:                      /* MMU_CTL |= r0; isb */
    MFSR_RAW 1, SR_MMU_CTL
    or      $r1, $r1, $r0
    MTSR_RAW 1, SR_MMU_CTL
    isb
    ret

    .global cpu_psw_get
cpu_psw_get:
    MFSR_RAW 0, SR_PSW
    ret

/* ------------------------------------------------------------------------------------------ */
/* exceptions: report and hang                                                                */
    .irp n, 1,2,3,4,5,6,7,8
_exc_\n:
    movi    $r0, \n
    j       _exc_common
    .endr

_exc_common:
    MFSR_RAW 1, SR_IPC
    MFSR_RAW 2, SR_ITYPE
    MFSR_RAW 3, SR_EVA
    move    $r4, $sp
    la      $sp, __stack_top         /* fresh stack for the reporter */
    jal     c_exception              /* void c_exception(type, ipc, itype, eva, sp) - no return */
5:  j       5b

/* ------------------------------------------------------------------------------------------ */
/* hardware interrupts: same frame layout as the vendor firmware, minus IFC_LP        */
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
_irq_\n:
    smw.adm $r0, [$sp], $r0, #0
    movi    $r0, \n
    j       _irq_common
    .endr

_irq_common:
    smw.adm $r1, [$sp], $r5, #0
    smw.adm $r15, [$sp], $r27, #0xe  /* r15..r27, fp, gp, lp */
    MFSR_RAW 1, SR_IPC
    MFSR_RAW 2, SR_IPSW
    smw.adm $r1, [$sp], $r2, #0
    MFSR_RAW 1, SR_PSW               /* PSW - 1: INTL-1 and GIE=1 -> higher-priority IRQs may nest */
    addi    $r1, $r1, -1
    MTSR_RAW 1, SR_PSW
    move    $r2, $sp                 /* align sp to 8, remember old sp */
    addi    $sp, $sp, -4
    movi    $r1, -8
    and     $sp, $sp, $r1
    swi     $r2, [$sp + 0]
    la      $r1, isr_table
    slli    $r3, $r0, 2
    add     $r1, $r1, $r3
    lwi     $r1, [$r1 + 0]
    jral    $r1                      /* isr(irq) */
    lwi     $sp, [$sp + 0]
    lmw.bim $r0, [$sp], $r1, #0      /* r0 = IPC, r1 = IPSW */
    setgie.d
    dsb
    MTSR_RAW 0, SR_IPC
    MTSR_RAW 1, SR_IPSW
    lmw.bim $r15, [$sp], $r27, #0xe
    lmw.bim $r1, [$sp], $r5, #0
    lmw.bim $r0, [$sp], $r0, #0
    iret
