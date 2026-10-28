.syntax unified
.cpu arm7tdmi
.arm
.section .text.sector_bitmap,"ax",%progbits
.global dok_gba_bitmap_column
.type dok_gba_bitmap_column,%function
dok_gba_bitmap_column:
    push {r4-r6,lr}
    ldr r4,[sp,#16]
    ldr r5,[sp,#24]
    mov r6,r4
1:  mov r12,r2,lsr #16
    ldrb lr,[r1,r12]
    cmp lr,#0
    moveq lr,r5
    strb lr,[r0],#128
    add r2,r2,r3
    subs r4,r4,#1
    bne 1b
    mov r0,r6
    pop {r4-r6,lr}
    bx lr
.size dok_gba_bitmap_column,.-dok_gba_bitmap_column
.global dok_gba_bitmap_masked_column
.type dok_gba_bitmap_masked_column,%function
dok_gba_bitmap_masked_column:
    push {r4-r9,lr}
    mov lr,r0
    ldmia lr,{r0-r6}
    mov r7,#0
    mov r8,#0
1:  mov r12,r2,lsr #16
    ldrb r9,[r1,r12]
    cmp r9,#0
    addeq r8,r8,#1
    beq 2f
    strb r9,[r0]
    ldr r12,[r5]
    orr r12,r12,r6
    str r12,[r5]
    add r7,r7,#1
2:  add r0,r0,#128
    add r2,r2,r3
    movs r6,r6,lsl #1
    moveq r6,#1
    addeq r5,r5,#4
    subs r4,r4,#1
    bne 1b
    orr r0,r7,r8,lsl #16
    pop {r4-r9,lr}
    bx lr
.size dok_gba_bitmap_masked_column,.-dok_gba_bitmap_masked_column
.global dok_gba_bitmap_background
.type dok_gba_bitmap_background,%function
/* Same incremental world UV as roof span. Atlas uses native8x8 tile order;
 * map/materials are ROM, background never marks foreground coverage. */
dok_gba_bitmap_background:
    push {r4-r11,lr}
    sub sp,sp,#24
    mov lr,r0
    ldr r4,[lr,#16]
    ldr r5,[lr,#20]
    ldr r6,[lr,#24]
    ldr r7,[lr,#28]
    ldr r8,[lr,#32]
    ldr r9,[lr,#36]
    ldr r10,[lr,#40]
    ldr r12,[lr,#44]
    str r12,[sp]
    ldr r12,[lr,#48]
    str r12,[sp,#4]
    ldr r12,[lr,#52]
    str r12,[sp,#8]
    ldr r12,[lr,#56]
    str r12,[sp,#12]
    ldmia lr,{r0-r3}
1:  ldr r12,[r3]
    tst r12,r8
    bne 4f
    mov r11,r10
    mov r12,r4,asr #16
    ldr lr,[sp]
    cmp r12,lr
    blt 3f
    ldr lr,[sp,#8]
    cmp r12,lr
    bgt 3f
    mov lr,r5,asr #16
    str lr,[sp,#16]
    ldr lr,[sp,#4]
    ldr r12,[sp,#16]
    cmp r12,lr
    blt 3f
    ldr lr,[sp,#12]
    cmp r12,lr
    bgt 3f
    mov lr,r4,asr #16
    add r12,lr,r12,lsl #6
    ldrb r12,[r2,r12]
    cmp r12,#255
    beq 3f
    ldr r12,[r1,r12,lsl #2]
    mov lr,r4,lsr #11
    and lr,lr,#31
    and r11,lr,#24
    mov r11,r11,lsl #3
    and lr,lr,#7
    add r11,r11,lr
    mov lr,r5,lsr #11
    and lr,lr,#31
    str lr,[sp,#20]
    and lr,lr,#24
    add r11,r11,lr,lsl #5
    ldr lr,[sp,#20]
    and lr,lr,#7
    add r11,r11,lr,lsl #3
    ldrb r11,[r12,r11]
    cmp r11,#0
    moveq r11,r10
3:  strb r11,[r0]
4:  add r0,r0,#1
    add r3,r3,#16
    add r4,r4,r6
    add r5,r5,r7
    subs r9,r9,#1
    bne 1b
    add sp,sp,#24
    pop {r4-r11,lr}
    bx lr
.size dok_gba_bitmap_background,.-dok_gba_bitmap_background
