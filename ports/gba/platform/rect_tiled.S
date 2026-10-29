.syntax unified
.cpu arm7tdmi
.arm
.section .text.rect_tiled,"ax",%progbits
.global dok_gba_tiled_rectangle
.type dok_gba_tiled_rectangle,%function
/* Constant-depth rectangle: same nearest-neighbor U/V, alpha, coverage and
 * statistics as the C path. Q32 reciprocal needs one exact remainder correction.
 * The caller owns the 96-byte capsule; no persistent cache or IRQ masking. */
dok_gba_tiled_rectangle:
    push {r4-r11,lr}
    sub sp,sp,#44
    mov r11,r0
    mov r4,#0
    mov r5,#0
.Lrect_column:
    ldr r6,[r11,#12]
    ldr r7,[r11]
    ldr r8,[r11,#20]
    ldr r0,[r11,#76]
    ldr r1,[r11,#80]
    ldr r2,[r11,#84]
    cmp r0,r1
    ldreq r3,[r11,#88]
    andeq r2,r2,r3
    ldr r3,[r6,r0,lsl #2]
    and r3,r3,r2
    cmp r3,r2
    bne .Lrect_sample
    cmp r0,r1
    beq .Lrect_next
    ldr r2,[r11,#88]
    ldr r3,[r6,r1,lsl #2]
    and r3,r3,r2
    cmp r3,r2
    bne .Lrect_sample
    add r0,r0,#1
    mvn r2,#0
.Lrect_middle:
    cmp r0,r1
    bhs .Lrect_next
    ldr r3,[r6,r0,lsl #2]
    cmp r3,r2
    bne .Lrect_sample
    add r0,r0,#1
    b .Lrect_middle
.Lrect_sample:
    ldr r0,[r11,#36]
    ldr r1,[r11,#44]
    umull r2,r3,r0,r1
    ldr r1,[r11,#48]
    mul r2,r3,r1
    sub r2,r0,r2
    cmp r2,r1
    addhs r3,r3,#1
    ldr r2,[r11,#28]
    sub r2,r2,#1
    cmp r3,r2
    movhi r3,r2
    ldr r0,[r11,#4]
    ldr r1,[r11,#32]
    mla r9,r3,r1,r0
    ldr r0,[r11,#8]
    ldrb r10,[r0,r3]
    ldr r0,[r11,#72]
    cmp r10,#0
    moveq r1,r0
    movne r1,#0
    cmp r1,#0
    bne .Lrect_masked
    ldr r0,[r11,#16]
    ldrb r0,[r0]
    cmp r0,#0
    bne .Lrect_gaps
    /* First opaque owner: publish the whole interval once, then use the
     * eight-row unrolled sampler without per-pixel coverage checks. */
    ldr r0,[r11,#76]
    ldr r1,[r11,#80]
    ldr r2,[r11,#84]
    cmp r0,r1
    ldreq r3,[r11,#88]
    andeq r2,r2,r3
    ldr r3,[r6,r0,lsl #2]
    orr r3,r3,r2
    str r3,[r6,r0,lsl #2]
    cmp r0,r1
    beq .Lrect_uncovered
    ldr r2,[r11,#88]
    ldr r3,[r6,r1,lsl #2]
    orr r3,r3,r2
    str r3,[r6,r1,lsl #2]
    mvn r2,#0
    add r0,r0,#1
.Lrect_mark:
    cmp r0,r1
    strlo r2,[r6,r0,lsl #2]
    addlo r0,r0,#1
    blo .Lrect_mark
.Lrect_uncovered:
    ldr r0,[r11,#60]
    str r0,[sp]
    ldr r0,[r11,#64]
    and r0,r0,#7
    str r0,[sp,#4]
    ldr r0,[r11,#68]
    cmp r10,#0
    movne r0,#0
    str r0,[sp,#8]
    mov r0,r7
    mov r1,r9
    ldr r2,[r11,#52]
    ldr r3,[r11,#56]
    bl dok_gba_tiled_column
    add r4,r4,r0
    b .Lrect_owned
.Lrect_gaps:
    ldr r0,[r11,#60]
    str r0,[sp]
    ldr r0,[r11,#64]
    and r1,r0,#7
    str r1,[sp,#4]
    and r0,r0,#31
    str r0,[sp,#16]
    ldr r0,[r11,#68]
    cmp r10,#0
    movne r0,#0
    str r0,[sp,#8]
    ldr r0,[r11,#76]
    add r0,r6,r0,lsl #2
    str r0,[sp,#12]
    mov r0,r7
    mov r1,r9
    ldr r2,[r11,#52]
    ldr r3,[r11,#56]
    bl dok_gba_tiled_opaque_gaps
    add r4,r4,r0
    b .Lrect_owned
.Lrect_masked:
    stmia sp,{r7,r9} /* dst,source; complete the masked-column capsule. */
    ldr r0,[r11,#52]
    str r0,[sp,#8]
    ldr r0,[r11,#56]
    str r0,[sp,#12]
    ldr r0,[r11,#60]
    str r0,[sp,#16]
    ldr r0,[r11,#76]
    add r0,r6,r0,lsl #2
    str r0,[sp,#20]
    ldr r0,[r11,#64]
    and r1,r0,#31
    mov r2,#1
    mov r2,r2,lsl r1
    str r2,[sp,#24]
    and r0,r0,#7
    str r0,[sp,#28]
    mov r0,sp
    bl dok_gba_tiled_masked_column
    add r5,r5,r0,lsr #16
    mov r0,r0,lsl #16
    add r4,r4,r0,lsr #16
.Lrect_owned:
    ldr r0,[r11,#16]
    ldrb r1,[r0]
    orr r1,r1,#128
    strb r1,[r0]
.Lrect_next:
    ldr r0,[r11,#36]
    ldr r1,[r11,#40]
    add r0,r0,r1
    str r0,[r11,#36]
    ldr r0,[r11,#12]
    ldr r1,[r11,#92]
    add r0,r0,r1
    str r0,[r11,#12]
    ldr r0,[r11,#16]
    add r0,r0,#1
    str r0,[r11,#16]
    add r8,r8,#2
    str r8,[r11,#20]
    add r7,r7,#1
    tst r8,#15
    addeq r7,r7,#56
    str r7,[r11]
    ldr r0,[r11,#24]
    cmp r8,r0
    bls .Lrect_column
    orr r0,r4,r5,lsl #16
    add sp,sp,#44
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_rectangle,.-dok_gba_tiled_rectangle
