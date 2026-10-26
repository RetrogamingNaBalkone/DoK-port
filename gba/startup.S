.syntax unified
.cpu arm7tdmi
.arm

/* Cartridge entry. The build script fills the standard 192-byte header. */
.section .header,"ax",%progbits
.global _rom_entry
_rom_entry:
    b _start
    .space 188

.section .text.startup,"ax",%progbits
.global _start
.type _start,%function
_start:
    ldr r0, =0x04000208
    mov r1, #0
    strh r1, [r0]
#ifdef DOK_RENDERER_PROBE
    /* Standard cartridge timing: WS0 3/1, SRAM 8, WS2 8/8, prefetch.
     * GBATEK WAITCNT; no undocumented EWRAM overclock. */
    ldr r0, =0x04000204
    ldr r1, =0x4317
    strh r1, [r0]
#endif
    msr cpsr_c, #0xd2
    ldr sp, =0x03007fa0
    msr cpsr_c, #0xdf
    ldr sp, =0x03007f00

#ifdef DOK_RENDERER_PROBE
    /* Watermark only the main System stack reservation, before any C calls.
     * IRQ/BIOS space above7F00 is excluded from the high-water measurement. */
    ldr r0, =0x03006000
    ldr r1, =0x03007f00
    ldr r2, =0x0d15ea5e
4:  cmp r0, r1
    strlo r2, [r0], #4
    blo 4b
    /* Separate IRQ stack watermark. BIOS-owned 7FA0..7FFF is untouched. */
    ldr r0, =0x03007f00
    ldr r1, =0x03007fa0
7:  cmp r0, r1
    strlo r2, [r0], #4
    blo 7b
#endif

    ldr r0, =__iwram_load
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
5:  cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 5b

    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:  cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 1b

    ldr r1, =__bss_start
    ldr r2, =__bss_end
    mov r3, #0
2:  cmp r1, r2
    strlo r3, [r1], #4
    blo 2b

    /* System mode, IRQ enabled, FIQ disabled. IME is configured by C. */
    msr cpsr_c, #0x5f
    ldr r0, =gba_main
    mov lr, pc
    bx r0
3:  b 3b
.size _start, .-_start
.ltorg

/* BIOS saves r0-r3,r12,lr and restores SPSR. Return with BX LR, not SUBS. */
.align 2
.global gba_irq
.type gba_irq,%function
gba_irq:
    mov r0, #0x04000000
    add r0, r0, #0x200
    ldrh r1, [r0]
    ldrh r2, [r0, #2]
    and r1, r1, r2
    strh r1, [r0, #2]
    ldr r0, =0x03007ff8
    ldrh r2, [r0]
    orr r2, r2, r1
    strh r2, [r0]
    tst r1, #1
    ldrne r0, =g_vblanks
    ldrne r2, [r0]
    addne r2, r2, #1
    strne r2, [r0]
#ifdef DOK_RENDERER_PROBE
    tst r1, #0x210
    beq 6f
    push {r4,lr}
    mov r0, r1
    ldr r3, =gba_audio_irq
    mov lr, pc
    bx r3
    pop {r4,lr}
6:
#endif
    bx lr
.size gba_irq, .-gba_irq
.ltorg
