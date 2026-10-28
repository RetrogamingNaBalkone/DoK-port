.syntax unified
.cpu arm7tdmi
.arm
.section .text.masked_columns,"ax",%progbits
.global dok_gba_masked_column
.type dok_gba_masked_column,%function
/* dst,source,Q16v,Q16step,count,coverage-word,row-bit. Only nonzero,
 * previously uncovered samples publish coverage. AAPCS; no IRQ masking. */
dok_gba_masked_column:
    push {r4-r10,lr}
    mov lr,r0
    ldmia lr,{r0-r6}
    mov r7,#0
    mov r8,#0
1:
    ldr r9,[r5]
    tst r9,r6
    bne 2f
    add r8,r8,#1
    ldrb r10,[r1,r2,lsr #16]
    cmp r10,#0
    beq 2f
    orr r10,r10,r10,lsl #8
    strh r10,[r0]
    orr r9,r9,r6
    str r9,[r5]
    add r7,r7,#1
2:
    add r0,r0,#240
    add r2,r2,r3
    movs r6,r6,lsl #1
    moveq r6,#1
    addeq r5,r5,#4
    subs r4,r4,#1
    bne 1b
    sub r8,r8,r7
    orr r0,r7,r8,lsl #16
    pop {r4-r10,lr}
    bx lr
.size dok_gba_masked_column,.-dok_gba_masked_column
