.syntax unified
.cpu arm7tdmi
.arm
.section .text.perf06_flat,"ax",%progbits
/* dst,bits,mask,count,color. Horizontal tile columns, coverage stride16. */
.global dok_gba_flat_span
.type dok_gba_flat_span,%function
dok_gba_flat_span:
    push {r4-r6,lr}
    ldr r4,[sp,#16]
    mov r5,#0
1:
    ldr r6,[r1]
    tst r6,r2
    orreq r6,r6,r2
    streq r6,[r1]
    strbeq r4,[r0]
    addeq r5,r5,#1
    add r0,r0,#1
    and r6,r0,#7
    cmp r6,#4
    addeq r0,r0,#56
    add r1,r1,#16
    subs r3,r3,#1
    bne 1b
    mov r0,r5
    pop {r4-r6,lr}
    bx lr
.size dok_gba_flat_span,.-dok_gba_flat_span

/* dst,bits,mask,count,color,y%8. Writes only open bits, no source reads. */
.global dok_gba_flat_column
.section .text.perf06_flat_column,"ax",%progbits
.type dok_gba_flat_column,%function
dok_gba_flat_column:
    push {r4-r7,lr}
    ldr r4,[sp,#20]
    ldr r7,[sp,#24]
    rsb r7,r7,#8
    mov r5,#0
1:
    ldr r6,[r1]
    tst r6,r2
    orreq r6,r6,r2
    streq r6,[r1]
    strbeq r4,[r0]
    addeq r5,r5,#1
    add r0,r0,#8
    movs r2,r2,lsl #1
    moveq r2,#1
    addeq r1,r1,#4
    subs r7,r7,#1
    addeq r0,r0,#960
    moveq r7,#8
    subs r3,r3,#1
    bne 1b
    mov r0,r5
    pop {r4-r7,lr}
    bx lr
.size dok_gba_flat_column,.-dok_gba_flat_column

/* frame,bits,record,x,u,top,bottom,begin,end,color. All rails in one call. */
.global dok_gba_far_fence
.section .text.perf06_far_fence,"ax",%progbits
.type dok_gba_far_fence,%function
dok_gba_far_fence:
    push {r4-r11,lr}
    mov r11,r0
    mov r9,#0
    ldr r8,[r11,#8]
    ldr r0,[r11,#16]
    add r0,r8,r0
    ldrb r0,[r0,#4]
    cmp r0,#0
    beq .Lfence_rails
    mov r7,#0
    ldr r0,[r11,#28]
    ldr r1,[r11,#32]
    b .Lfence_span
.Lfence_rails:
    ldrb r7,[r8,#1]
    add r8,r8,#260
.Lfence_next:
    cmp r7,#0
    beq .Lfence_done
    sub r7,r7,#1
    ldrh r0,[r8],#2
    ldrh r1,[r8],#2
    ldr r2,[r11,#20]
    ldr r3,[r11,#24]
    sub r3,r3,r2
    mul r0,r3,r0
    mul r1,r3,r1
    add r0,r2,r0,asr #8
    add r1,r2,r1,asr #8
    ldr r2,[r11,#28]
    cmp r0,r2
    movlt r0,r2
    ldr r2,[r11,#32]
    cmp r1,r2
    movgt r1,r2
.Lfence_span:
    cmp r0,r1
    bgt .Lfence_next
    sub r4,r1,r0
    add r4,r4,#1
    ldr r1,[r11,#4]
    mov r2,r0,lsr #5
    add r1,r1,r2,lsl #2
    and r3,r0,#31
    mov r2,#1
    mov r2,r2,lsl r3
    and r6,r0,#7
    mov r5,r0,lsr #3
    ldr r0,[r11]
    add r0,r0,r5,lsl #10
    add r0,r0,r6,lsl #3
    rsb r6,r6,#8
    ldr r3,[r11,#12]
    mov r5,r3,lsr #4
    add r0,r0,r5,lsl #6
    mov r3,r3,lsr #1
    and r3,r3,#7
    add r0,r0,r3
    ldr r3,[r11,#36]
.Lfence_pixel:
    ldr r5,[r1]
    tst r5,r2
    orreq r5,r5,r2
    streq r5,[r1]
    strbeq r3,[r0]
    addeq r9,r9,#1
    add r0,r0,#8
    movs r2,r2,lsl #1
    moveq r2,#1
    addeq r1,r1,#4
    subs r6,r6,#1
    addeq r0,r0,#960
    moveq r6,#8
    subs r4,r4,#1
    bne .Lfence_pixel
    b .Lfence_next
.Lfence_done:
    mov r0,r9
    pop {r4-r11,lr}
    bx lr
.size dok_gba_far_fence,.-dok_gba_far_fence
