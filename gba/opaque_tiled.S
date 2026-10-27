.syntax unified
.cpu arm7tdmi
.arm
.section .text.opaque_tiled,"ax",%progbits
.macro DIV16
    ldr r2,=dok_reciprocal
    ldr r3,[r2,r1,lsl #2]
    umull r2,r12,r0,r3
    mul r2,r12,r1
    sub r2,r0,r2
    cmp r2,r1
    addhs r12,r12,#1
    mov r0,r12
.endm
.macro EDGE offset,base,slot
    add r12,r11,#\offset
    ldmia r12,{r0-r3}
    ldr lr,[r12,#20]
    mul lr,r0,lr
    str lr,[sp,#\slot]
    add r0,r0,r2
    add r1,r1,r3
    ldr r2,[r11,#44]
    cmp r1,r2
    subhs r1,r1,r2
    addhs r0,r0,#1
    stmia r12,{r0,r1}
    ldr r0,[r11,#\base]
    ldr lr,[sp,#\slot]
    add lr,lr,r0
    str lr,[sp,#\slot]
.endm
.global dok_gba_opaque_tiled
.type dok_gba_opaque_tiled,%function
/* Prepared face/edge capsule; exact Q7 U and signed-trunc0 edge sampling.
 * Opaque resource only, depth endpoints0..32767, positive span<=12288.
 * Tile frame,120 coverage columns of5 words; zero colors become black alias.
 * Capsule lives on the caller's IWRAM stack. No nested calls or IRQ masking. */
dok_gba_opaque_tiled:
    push {r4-r11,lr}
    sub sp,sp,#28
    mov r11,r0
    mov r4,#0
    ldr r10,[r11,#144]
.Lcolumn:
    EDGE 72,60,0
    EDGE 96,64,4
    EDGE 120,68,8
    ldr r5,[sp]
    ldr r9,[sp,#4]
    cmp r5,#144
    bge .Lnext
    cmp r9,#0
    blt .Lnext
    cmp r9,r5
    blt .Lnext
    cmp r5,#0
    movlt r5,#0
    cmp r9,#143
    movgt r9,#143
    str r5,[sp,#12]
    str r9,[sp,#16]
    mov r0,r5,lsr #5
    mov r1,r9,lsr #5
    ldr r6,[r11,#12]
    add r6,r6,r0,lsl #2
    mvn r2,#0
    and r3,r5,#31
    mov r2,r2,lsl r3
    mvn r3,#0
    and r12,r9,#31
    rsb r12,r12,#31
    mov r3,r3,lsr r12
    cmp r0,r1
    bne .Lmany_words
    and r2,r2,r3
    ldr r12,[r6]
    and r12,r12,r2
    cmp r12,r2
    beq .Lnext
    b .Ldraw
.Lmany_words:
    ldr r12,[r6]
    and r12,r12,r2
    cmp r12,r2
    bne .Ldraw
    ldr lr,[r11,#12]
    ldr r12,[lr,r1,lsl #2]
    and r12,r12,r3
    cmp r12,r3
    bne .Ldraw
    add r0,r0,#1
    mvn r2,#0
.Lmiddle_words:
    cmp r0,r1
    bhs .Lnext
    ldr r12,[lr,r0,lsl #2]
    cmp r12,r2
    bne .Ldraw
    add r0,r0,#1
    b .Lmiddle_words
.Ldraw:
    ldr r0,[r11,#52]
    sub r0,r0,#1
    ldr r2,[r11,#48]
    sub r0,r0,r2
    mov r0,r0,lsl #16
    ldr r1,[sp,#4]
    ldr r2,[sp]
    subs r1,r1,r2
    moveq r0,#0
    beq .Lvstep
    DIV16
.Lvstep:
    str r0,[sp,#20]
    ldr r2,[r11,#48]
    mov r2,r2,lsl #16
    ldr r1,[sp]
    sub r1,r5,r1
    mla r2,r1,r0,r2
    str r2,[sp,#24]
    ldr r0,[r11,#32]
    ldr r1,[r11,#28]
    cmp r1,#65536
    bhs .Lu_large
    DIV16
    b .Lu_ready
.Lu_large:
    mov r2,#0
    .irp shift,7,6,5,4,3,2,1,0
    cmp r0,r1,lsl #\shift
    subhs r0,r0,r1,lsl #\shift
    orrhs r2,r2,#(1<<\shift)
    .endr
    mov r0,r2
.Lu_ready:
    ldr r2,[r11,#56]
    sub r2,r2,#1
    cmp r0,r2
    movhi r0,r2
    mov r7,r0
    ldr r0,[sp,#8]
    subs r0,r0,#1920
    movlt r0,#0
    blt .Llight
    ldr r3,=0x88888889
    umull r1,r2,r0,r3
    mov r0,r2,lsr #6
    cmp r0,#15
    movhi r0,#15
.Llight:
    ldr r1,[r11,#8]
    ldr r1,[r1,r0,lsl #2]
    ldr r0,[r11,#4]
    add r1,r1,r0
    ldr r0,[r11,#52]
    mla r1,r7,r0,r1
    mov r0,r5,lsr #3
    mov r0,r0,lsl #10
    ldr r12,[r11,#20]
    mov r2,r12,lsr #4
    add r0,r0,r2,lsl #6
    and r2,r5,#7
    add r0,r0,r2,lsl #3
    mov r12,r12,lsr #1
    and r12,r12,#7
    add r0,r0,r12
    ldr r12,[r11]
    add r0,r0,r12
    ldr r2,[sp,#24]
    ldr r3,[sp,#20]
    mov r8,#1
    and r12,r5,#31
    mov r8,r8,lsl r12
    ldr r9,[sp,#16]
    sub r9,r9,r5
    add r9,r9,#1
    and r7,r5,#7
    rsb r7,r7,#8
    ldr r12,[r11,#16]
    ldrb lr,[r12]
    orr lr,lr,#128
    strb lr,[r12]
#ifdef DOK_OPAQUE_GAPS
    /* Batch uncovered runs instead of a coverage test for every texel. */
    ldr r12,[sp,#12]
    and r12,r12,#31
    sub sp,sp,#24
    str r9,[sp]
    ldr r5,[sp,#36]
    and r5,r5,#7
    str r5,[sp,#4]
    str r10,[sp,#8]
    str r6,[sp,#12]
    str r12,[sp,#16]
    bl dok_gba_tiled_opaque_gaps
    add sp,sp,#24
    add r4,r4,r0
    b .Lnext
#endif
.Lpixel:
    ldr lr,[r6]
    tst lr,r8
    bne .Lpixel_next
    ldrb r12,[r1,r2,lsr #16]
    cmp r12,#0
    moveq r12,r10
    strb r12,[r0]
    orr lr,lr,r8
    str lr,[r6]
    add r4,r4,#1
.Lpixel_next:
    add r0,r0,#8
    add r2,r2,r3
    movs r8,r8,lsl #1
    moveq r8,#1
    addeq r6,r6,#4
    subs r7,r7,#1
    addeq r0,r0,#960
    moveq r7,#8
    subs r9,r9,#1
    bne .Lpixel
.Lnext:
    ldr r0,[r11,#12]
    add r0,r0,#20
    str r0,[r11,#12]
    ldr r0,[r11,#16]
    add r0,r0,#1
    str r0,[r11,#16]
    ldr r0,[r11,#28]
    ldr r1,[r11,#36]
    add r0,r0,r1
    str r0,[r11,#28]
    ldr r0,[r11,#32]
    ldr r1,[r11,#40]
    add r0,r0,r1
    str r0,[r11,#32]
    ldr r0,[r11,#20]
    ldr r1,[r11,#24]
    add r0,r0,#2
    str r0,[r11,#20]
    cmp r0,r1
    ble .Lcolumn
    mov r0,r4
    add sp,sp,#28
    pop {r4-r11,lr}
    bx lr
.size dok_gba_opaque_tiled,.-dok_gba_opaque_tiled
.ltorg

#ifdef DOK_OPAQUE_GAPS
.section .text.opaque_gaps,"ax",%progbits
.global dok_gba_tiled_opaque_gaps
.type dok_gba_tiled_opaque_gaps,%function
/* dst,source,v,step; stack:count,y%8,black,bits,y%32. Positive count.
 * Covered pixels are untouched. Each zero run is published in one bit word
 * and sampled by the established8-row unrolled tiled column routine. */
dok_gba_tiled_opaque_gaps:
    push {r4-r11,lr}
    sub sp,sp,#20
    mov r7,r0
    mov r8,r1
    mov r9,r2
    mov r10,r3
    ldr r6,[sp,#56]
    ldr r0,[sp,#60]
    str r0,[sp,#12]
    ldr r11,[sp,#64]
    ldr r4,[sp,#68]
    ldr r5,[sp,#72]
    mov r0,#0
    str r0,[sp]
.Lgap:
    ldr r0,[r4]
    mov r0,r0,lsr r5
    and r1,r0,#1
    str r1,[sp,#16]
    cmp r1,#0
    mvnne r0,r0
    mov r1,#0
    cmp r0,#0
    moveq r1,#32
    beq .Lrun_found
    movs r12,r0,lsl #16
    addeq r1,r1,#16
    moveq r0,r0,lsr #16
    tst r0,#255
    addeq r1,r1,#8
    moveq r0,r0,lsr #8
    tst r0,#15
    addeq r1,r1,#4
    moveq r0,r0,lsr #4
    tst r0,#3
    addeq r1,r1,#2
    moveq r0,r0,lsr #2
    tst r0,#1
    addeq r1,r1,#1
.Lrun_found:
    rsb r2,r5,#32
    cmp r1,r2
    movhi r1,r2
    cmp r1,r6
    movhi r1,r6
    str r1,[sp,#4]
    ldr r0,[sp,#16]
    cmp r0,#0
    bne .Lgap_advance
    rsb r2,r1,#32
    mvn r3,#0
    mov r3,r3,lsr r2
    mov r3,r3,lsl r5
    ldr r0,[r4]
    orr r0,r0,r3
    str r0,[r4]
    mov r0,r7
    mov r1,r8
    mov r2,r9
    mov r3,r10
    sub sp,sp,#16
    ldr r12,[sp,#20]
    str r12,[sp]
    ldr r12,[sp,#28]
    str r12,[sp,#4]
    str r11,[sp,#8]
    bl dok_gba_tiled_column
    add sp,sp,#16
    ldr r1,[sp]
    add r1,r1,r0
    str r1,[sp]
.Lgap_advance:
    ldr r1,[sp,#4]
    mla r9,r1,r10,r9
    add r7,r7,r1,lsl #3
    ldr r0,[sp,#12]
    add r0,r0,r1
    mov r2,r0,lsr #3
    add r7,r7,r2,lsl #10
    sub r7,r7,r2,lsl #6
    and r0,r0,#7
    str r0,[sp,#12]
    add r5,r5,r1
    cmp r5,#32
    subhs r5,r5,#32
    addhs r4,r4,#4
    subs r6,r6,r1
    bne .Lgap
    ldr r0,[sp]
    add sp,sp,#20
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_opaque_gaps,.-dok_gba_tiled_opaque_gaps
#endif
