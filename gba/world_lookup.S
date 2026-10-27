.syntax unified
.cpu arm7tdmi
.arm
.section .text.world_lookup,"ax",%progbits
.macro LOOKUP_KERNEL name, ceiling
.global \name
.type \name,%function
/* Row descriptor: dst,bits,FCMap,info,wx,wz,dx,dz,mask,count,plane,height,
 * LIGHT,wall-classes. Coordinates are Q16 cells clipped to the view envelope.
 * Cache the current cell's material; unlike the C walker there is no span
 * division, descriptor construction or function call at every cell crossing.
 * AAPCS. Zero texture indices remain opaque for horizontal planes. */
\name:
    push {r4-r11,lr}
    sub sp,sp,#68
    mov lr,r0
    ldr r4,[lr,#16]
    ldr r5,[lr,#20]
    ldr r6,[lr,#24]
    ldr r7,[lr,#28]
    ldr r8,[lr,#32]
    ldr r9,[lr,#36]
    ldr r12,[lr,#44]
    str r12,[sp,#12]
    ldr r12,[lr,#48]
    str r12,[sp,#4]
    ldr r12,[lr,#52]
    str r12,[sp,#8]
    ldmia lr,{r0-r3}
    str r3,[sp]
    mov r3,r2
    mov r12,#0
    str r12,[sp,#36]
    mov r11,#0
    mvn r10,#0
1:
    ldr r12,[r1]
    tst r12,r8
    bne 8f
    mov lr,r5,asr #16
    mov r12,r4,asr #16
    add r12,r12,lr,lsl #6
    cmp r12,r10
    beq 4f
    mov r10,r12
    ldr lr,[sp,#8]
    ldrb lr,[lr,r12]
    cmp lr,#2
    beq 3f
    ldrb lr,[r3,r12]
    add lr,lr,lr,lsl #2
    ldr r12,[sp]
    add lr,r12,lr,lsl #2
    ldrb r12,[lr,#19]
    tst r12,#(1<<\ceiling)
    beq 3f
    .if \ceiling
    ldrsh r12,[lr,#8]
    ldr r11,[sp,#12]
    cmp r12,r11
    bne 3f
    .endif
    ldr r11,[lr,#(4*\ceiling)]
    ldrb r2,[lr,#(14+\ceiling)]
    ldrb r12,[lr,#(16+\ceiling)]
    str r12,[sp,#20]
    .if !\ceiling
    ldrsh r12,[lr,#10]
    str r12,[sp,#24]
    ldrsh lr,[lr,#12]
    str lr,[sp,#28]
    orr r12,r12,lr
    str r12,[sp,#40]
    /* Info base is cheap to reconstruct only on a cell transition. */
    ldrb lr,[r3,r10]
    add lr,lr,lr,lsl #2
    ldr r12,[sp]
    add lr,r12,lr,lsl #2
    ldrb lr,[lr,#18]
    str lr,[sp,#32]
    .endif
    b 4f
3:
    mov r11,#0
4:
    cmp r11,#0
    beq 8f
    mov r12,r4,lsl #16
    mov r12,r12,lsr #16
    mul r12,r2,r12
    mov r12,r12,lsr #16
    str r12,[sp,#48]
    mov lr,r5,lsl #16
    .if !\ceiling
    mvn lr,lr
    .endif
    mov lr,lr,lsr #16
    ldr r12,[sp,#20]
    mul lr,r12,lr
    mov lr,lr,lsr #16
    .if !\ceiling
    ldr r12,[sp,#40]
    cmp r12,#0
    beq 6f
    ldr r12,[sp,#28]
    add lr,lr,r12
    ldr r12,[sp,#48]
    str lr,[sp,#60]
    ldr lr,[sp,#24]
    add r12,r12,lr
    ldr lr,[sp,#32]
    cmp lr,#0
    beq 5f
    cmp r12,#0
    addlt r12,r12,r2
    addlt r12,r12,#1
    cmp r12,r2
    subgt r12,r12,r2
    subgt r12,r12,#1
    str r12,[sp,#48]
    ldr lr,[sp,#60]
    ldr r12,[sp,#20]
    cmp lr,#0
    addlt lr,lr,r12
    addlt lr,lr,#1
    cmp lr,r12
    subgt lr,lr,r12
    subgt lr,lr,#1
    b 6f
5:
    cmp r12,r2
    bhi 7f
    str r12,[sp,#48]
    ldr lr,[sp,#60]
    ldr r12,[sp,#20]
    cmp lr,r12
    bhi 7f
    .endif
6:
    mla lr,r2,lr,lr
    ldr r12,[sp,#48]
    add lr,lr,r12
    ldrb lr,[r11,lr]
    .if !\ceiling
    b 9f
7:
    mov lr,#0
9:
    .endif
    ldr r12,[sp,#4]
    ldrb lr,[r12,lr]
    orr lr,lr,lr,lsl #8
    strh lr,[r0]
    ldr r12,[r1]
    orr r12,r12,r8
    str r12,[r1]
    ldr r12,[sp,#36]
    add r12,r12,#1
    str r12,[sp,#36]
8:
    add r0,r0,#2
    add r1,r1,#20
    add r4,r4,r6
    add r5,r5,r7
    subs r9,r9,#1
    bne 1b
    ldr r0,[sp,#36]
    add sp,sp,#68
    pop {r4-r11,lr}
    bx lr
.size \name,.-\name
.endm
LOOKUP_KERNEL dok_gba_world_lookup_floor,0
LOOKUP_KERNEL dok_gba_world_lookup_ceiling,1
