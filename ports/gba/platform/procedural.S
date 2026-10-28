.syntax unified
.cpu arm7tdmi
.arm
.section .text.procedural,"ax",%progbits
.global dok_gba_procedural_span
.type dok_gba_procedural_span,%function
/* AAPCS; procedural_span ABI asserted in the C caller. Pixel-pair output,
 * Q16 cell positions, bounds clipped by caller, immutable source-derived tones.
 * Coverage is published only for written pixels. No IRQ masking or heap. */
dok_gba_procedural_span:
    push {r4-r11,lr}
    sub sp,sp,#16
    mov lr,r0
    ldr r4,[lr,#16]
    ldr r5,[lr,#20]
    ldr r6,[lr,#24]
    ldr r7,[lr,#28]
    ldr r8,[lr,#32]
    ldr r11,[lr,#36]
    ldr r12,[lr,#40]
    str r12,[sp,#4]
    ldr r12,[lr,#44]
    str r12,[sp,#8]
    ldr r10,[lr,#48]
    mov r12,#0
    str r12,[sp]
    ldmia lr,{r0-r3}
1:
    ldr r12,[r1]
    tst r12,r8
    bne 3f
    mov lr,r5,asr #16
    mov r12,r4,asr #16
    add r12,r12,lr,lsl #6
    ldrb r12,[r2,r12]
    add lr,r12,r12,lsl #2
    add lr,r3,lr,lsl #2
    ldrb r12,[lr,#19]
    cmp r12,#0
    beq 3f
    ldr r12,[sp,#4]
    cmp r12,#0
    beq 2f
    ldrsh r12,[lr,#8]
    ldr r9,[sp,#8]
    cmp r12,r9
    bne 3f
    ldr lr,[lr,#4]
    b 4f
2:
    ldr lr,[lr]
4:
    mov r12,r4,lsr #14
    eor r12,r12,r5,lsr #14
    and r12,r12,#1
    ldrb r12,[lr,r12]
    tst r4,#0x3e00
    ldrbeq r12,[lr,#2]
    tst r5,#0x3e00
    ldrbeq r12,[lr,#2]
    ldrb r12,[r10,r12]
    orr r12,r12,r12,lsl #8
    strh r12,[r0]
    ldr r12,[r1]
    orr r12,r12,r8
    str r12,[r1]
    ldr r12,[sp]
    add r12,r12,#1
    str r12,[sp]
3:
    add r0,r0,#2
    add r1,r1,#20
    add r4,r4,r6
    add r5,r5,r7
    subs r11,r11,#1
    bne 1b
    ldr r0,[sp]
    add sp,sp,#16
    pop {r4-r11,lr}
    bx lr
.size dok_gba_procedural_span,.-dok_gba_procedural_span
