.syntax unified
.cpu arm7tdmi
.arm
.section .text.tiled_scene,"ax",%progbits
.global dok_gba_tiled_column
.type dok_gba_tiled_column,%function
/* dst tiled,source,Q16 v,step; stack:count,first y modulo8,black alias.
 * An8-row run stays in one tile, then advances to the next tile row. */
dok_gba_tiled_column:
    push {r4-r8,lr}
    ldr r4,[sp,#24]
    ldr r5,[sp,#28]
    ldr r6,[sp,#32]
    mov r7,r4
    rsb r5,r5,#8
    cmp r5,r4
    movhi r5,r4
    sub r4,r4,r5
#ifdef DOK_HW_NONZERO_COLUMN
    cmp r6,#0
    beq 6f
#endif
1:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    cmp r12,#0
    moveq r12,r6
    strb r12,[r0],#8
    subs r5,r5,#1
    bne 1b
    add r0,r0,#960
#ifdef DOK_HW_COMPACT_ZERO
    cmp r4,#0
    beq 5f
    mov r5,#8
    cmp r5,r4
    movhi r5,r4
    sub r4,r4,r5
    b 1b
#else
    cmp r4,#8
    blo 3f
2:
    .rept 8
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    cmp r12,#0
    moveq r12,r6
    strb r12,[r0],#8
    .endr
    add r0,r0,#960
    sub r4,r4,#8
    cmp r4,#8
    bhs 2b
3:
    cmp r4,#0
    beq 5f
4:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    cmp r12,#0
    moveq r12,r6
    strb r12,[r0],#8
    subs r4,r4,#1
    bne 4b
#endif
5:
    mov r0,r7
    pop {r4-r8,lr}
    bx lr
#ifdef DOK_HW_NONZERO_COLUMN
/* black=0 is accepted only for a source column proven entirely nonzero. */
6:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    strb r12,[r0],#8
    subs r5,r5,#1
    bne 6b
    add r0,r0,#960
    cmp r4,#8
    blo 8f
7:
    .rept 8
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    strb r12,[r0],#8
    .endr
    add r0,r0,#960
    sub r4,r4,#8
    cmp r4,#8
    bhs 7b
8:
    cmp r4,#0
    beq 5b
9:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    strb r12,[r0],#8
    subs r4,r4,#1
    bne 9b
    b 5b
#endif
.size dok_gba_tiled_column,.-dok_gba_tiled_column

#ifdef DOK_HW_OPAQUE_BATCH
.section .text.tiled_opaque_batch,"ax",%progbits
.global dok_gba_tiled_opaque_batch
.type dok_gba_tiled_opaque_batch,%function
/* Descriptor:dst,source,v,step,count,bits,row mask,y%8,black alias.
 * Sample whole open8-row groups directly. Mixed groups preserve covered
 * pixels; full groups advance without touching the source. Positive count. */
dok_gba_tiled_opaque_batch:
    push {r4-r11,lr}
    mov lr,r0
    ldmia lr,{r0-r6}
    ldr r11,[lr,#28]
    ldr r8,[lr,#32]
    rsb r11,r11,#8
    mov r7,#0
.Lopaque_batch:
    ldr r9,[r5]
    mov r10,r11
    cmp r10,r4
    movhi r10,r4
    sub r4,r4,r10
    sub r11,r11,r10
    rsb r12,r10,#32
    mvn lr,#0
    mov lr,lr,lsr r12
    mul lr,r6,lr
    and r12,r9,lr
    cmp r12,lr
    beq .Lopaque_skip
    cmp r12,#0
    bne .Lopaque_mixed
    orr r9,r9,lr
    str r9,[r5]
    mov r6,r6,lsl r10
    add r7,r7,r10
    cmp r8,#0
    bne .Lopaque_alias
    cmp r10,#8
    bne .Lopaque_open
.Lopaque_eight:
    .rept 4
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    strb r12,[r0],#8
    .endr
    subs r10,r10,#4
    bne .Lopaque_eight
    b .Lopaque_group_done
.Lopaque_open:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    strb r12,[r0],#8
    subs r10,r10,#1
    bne .Lopaque_open
    b .Lopaque_group_done
.Lopaque_alias:
    ldrb r12,[r1,r2,lsr #16]
    add r2,r2,r3
    cmp r12,#0
    moveq r12,r8
    strb r12,[r0],#8
    subs r10,r10,#1
    bne .Lopaque_alias
    b .Lopaque_group_done
.Lopaque_mixed:
    tst r9,r6
    bne .Lopaque_next
    ldrb r12,[r1,r2,lsr #16]
    cmp r12,#0
    moveq r12,r8
    strb r12,[r0]
    orr r9,r9,r6
    add r7,r7,#1
.Lopaque_next:
    add r0,r0,#8
    add r2,r2,r3
    mov r6,r6,lsl #1
    subs r10,r10,#1
    bne .Lopaque_mixed
    str r9,[r5]
    b .Lopaque_group_done
.Lopaque_skip:
    add r0,r0,r10,lsl #3
    mla r2,r10,r3,r2
    mov r6,r6,lsl r10
.Lopaque_group_done:
    cmp r6,#0
    moveq r6,#1
    addeq r5,r5,#4
    cmp r11,#0
    addeq r0,r0,#960
    moveq r11,#8
    cmp r4,#0
    bne .Lopaque_batch
    mov r0,r7
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_opaque_batch,.-dok_gba_tiled_opaque_batch
.section .text.tiled_scene,"ax",%progbits
#endif

#ifdef DOK_HW_BG_LOWERED
.global dok_gba_coverage_span
.type dok_gba_coverage_span,%function
/* First column word, row mask, positive number of120-wide logical samples. */
dok_gba_coverage_span:
    mov r12,#0
1:
    ldr r3,[r0]
    tst r3,r1
    addeq r12,r12,#1
    orr r3,r3,r1
    str r3,[r0],#16
    subs r2,r2,#1
    bne 1b
    mov r0,r12
    bx lr
.size dok_gba_coverage_span,.-dok_gba_coverage_span
#endif

#ifdef DOK_HW_TILED_MASKED
.global dok_gba_tiled_masked_column
.type dok_gba_tiled_masked_column,%function
/* dst,source,v,step,count,bits,row mask,y%8. Shared coverage, source0 alpha. */
dok_gba_tiled_masked_column:
    push {r4-r11,lr}
    mov lr,r0
    ldmia lr,{r0-r6}
    ldr r11,[lr,#28]
    rsb r11,r11,#8
    mov r7,#0
    mov r8,#0
#ifdef DOK_HW_MASKED_BATCH
/* Coverage words and tiled rows both contain complete eight-row groups.
 * Keep one word in a register until the current group has finished. */
.Lmasked_batch:
    ldr r9,[r5]
    mov r10,r11
    cmp r10,r4
    movhi r10,r4
    sub r4,r4,r10
    sub r11,r11,r10
    rsb r12,r10,#32
    mvn lr,#0
    mov lr,lr,lsr r12
    mul lr,r6,lr
    and r12,r9,lr
    cmp r12,lr
    beq .Lmasked_skip
.Lmasked_sample:
    tst r9,r6
    bne .Lmasked_next
    add r8,r8,#1
    ldrb r12,[r1,r2,lsr #16]
    cmp r12,#0
    strbne r12,[r0]
    orrne r9,r9,r6
    addne r7,r7,#1
.Lmasked_next:
    add r0,r0,#8
    add r2,r2,r3
    mov r6,r6,lsl #1
    subs r10,r10,#1
    bne .Lmasked_sample
    str r9,[r5]
    b .Lmasked_group_done
.Lmasked_skip:
    add r0,r0,r10,lsl #3
    mla r2,r10,r3,r2
    mov r6,r6,lsl r10
.Lmasked_group_done:
    cmp r6,#0
    moveq r6,#1
    addeq r5,r5,#4
    cmp r11,#0
    addeq r0,r0,#960
    moveq r11,#8
    cmp r4,#0
    bne .Lmasked_batch
#else
1:
    ldr r9,[r5]
    tst r9,r6
    bne 2f
    add r8,r8,#1
    ldrb r10,[r1,r2,lsr #16]
    cmp r10,#0
    beq 2f
    strb r10,[r0]
    orr r9,r9,r6
    str r9,[r5]
    add r7,r7,#1
2:
    add r0,r0,#8
    add r2,r2,r3
    movs r6,r6,lsl #1
    moveq r6,#1
    addeq r5,r5,#4
    subs r11,r11,#1
    addeq r0,r0,#960
    moveq r11,#8
    subs r4,r4,#1
    bne 1b
#endif
    sub r8,r8,r7
    orr r0,r7,r8,lsl #16
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_masked_column,.-dok_gba_tiled_masked_column
#endif

.global dok_gba_tiled_surface
#ifdef DOK_HW_SURFACE_PAIR
.global dok_gba_tiled_surface_pair
dok_gba_tiled_surface_pair:
#endif
.type dok_gba_tiled_surface,%function
/* Existing surface_span ABI plus black alias at56. Frame base is4 mod8,
 * enforced by the aligned8 workspace in the isolated hardware main. */
dok_gba_tiled_surface:
    ldr r1,[r0,#20]
    ldr r2,[r0,#24]
    cmp r1,r2
    movhi r0,#0
    bxhi lr
    push {r4-r11,lr}
    sub sp,sp,#12
    mov r12,#0
    str r12,[sp]
    mov lr,r0
    ldr r12,[lr,#56]
    str r12,[sp,#4]
    ldmia lr,{r1,r2,r12}
    ldr r3,[lr,#32]
    ldr r4,[lr,#36]
    ldr r5,[lr,#40]
    ldr r6,[lr,#44]
    lsl r5,r5,#1
    lsl r6,r6,#1
    ldr r7,[lr,#12]
    ldr r8,[lr,#16]
    sub r7,r7,#1
    sub r8,r8,#1
    ldr r9,[lr,#48]
    ldr r10,[lr,#52]
    ldr r11,[lr,#24]
    ldr r0,[lr,#20]
    sub r11,r11,r0
    mov r11,r11,lsr #1
    add r11,r11,#1
    mov r0,r12
1:
    ldr r12,[r9]
#ifdef DOK_HW_SURFACE_PAIR
    and lr,r12,r10
    cmp lr,r10
    beq 2f
    bic lr,r10,r12
    str lr,[sp,#8]
#else
    tst r12,r10
    bne 2f
#endif
    orr r12,r12,r10
    str r12,[r9]
    mov r12,r3,asr #16
    cmp r12,#0
    movlt r12,#0
    cmp r12,r7
    movgt r12,r7
    mov lr,r4,asr #16
    cmp lr,#0
    movlt lr,#0
    cmp lr,r8
    movgt lr,r8
    mla lr,r7,lr,lr
    add lr,lr,r12
#ifdef DOK_HW_SURFACE_PRELIT
    ldrb lr,[r2,lr]
#else
    ldrb lr,[r1,lr]
    ldrb lr,[r2,lr]
#endif
    cmp lr,#0
    ldreq lr,[sp,#4]
#ifdef DOK_HW_SURFACE_PAIR
    and r12,r10,r10,lsr #1
    cmp r12,#0
    beq 6f
    ldr r12,[sp,#8]
    tst r12,r10,lsr #1
    strbne lr,[r0]
    tst r12,r10,lsl #1
    strbne lr,[r0,#8]
    b 7f
6:
#endif
    strb lr,[r0]
#ifdef DOK_HW_SURFACE_PAIR
7:
#endif
    ldr r12,[sp]
    add r12,r12,#1
    str r12,[sp]
2:
    add r0,r0,#1
    and r12,r0,#7
    cmp r12,#4
    addeq r0,r0,#56
#ifdef DOK_HW_COVER4
    add r9,r9,#16
#else
    add r9,r9,#20
#endif
    add r3,r3,r5
    add r4,r4,r6
    subs r11,r11,#1
    bne 1b
    ldr r0,[sp]
    add sp,sp,#12
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_surface,.-dok_gba_tiled_surface

.global dok_gba_tiled_ui_panel
.type dok_gba_tiled_ui_panel,%function
/* dst,ROM compact tiles,column count,destination tile-row pitch. Two rows. */
dok_gba_tiled_ui_panel:
    push {r4-r8,lr}
    mov r4,r2,lsl #6
    mov r12,#2
1:
    mov r2,r4,lsr #4
2:
    ldmia r1!,{r5-r8}
    stmia r0!,{r5-r8}
    subs r2,r2,#1
    bne 2b
    add r0,r0,r3
    sub r0,r0,r4
    subs r12,r12,#1
    bne 1b
    pop {r4-r8,lr}
    bx lr
.size dok_gba_tiled_ui_panel,.-dok_gba_tiled_ui_panel

.global dok_gba_tiled_clear
.type dok_gba_tiled_clear,%function
/* dst,positive byte count divisible32. No IRQ masking. */
dok_gba_tiled_clear:
    push {r4-r9,lr}
    mov r2,#0
    mov r3,#0
    mov r4,#0
    mov r5,#0
    mov r6,#0
    mov r7,#0
    mov r8,#0
    mov r9,#0
1:
    stmia r0!,{r2-r9}
    subs r1,r1,#32
    bne 1b
    pop {r4-r9,lr}
    bx lr
.size dok_gba_tiled_clear,.-dok_gba_tiled_clear

.global dok_gba_hw_map_cells
.type dok_gba_hw_map_cells,%function
/* Precomputed28-byte commands: torus offset, six packed first tile rows.
 * Four rows per cell, with a4096-byte stride between plane/light maps. */
dok_gba_hw_map_cells:
    push {r4-r7,lr}
    ldr r7,=0x04040404
1:
    ldr r2,[r0],#4
    add r2,r2,#0x06000000
#ifdef DOK_SECTOR_GPU
    add r2,r2,#0x6000
#else
    add r2,r2,#0x8000
#endif
    mov r3,#6
2:
    ldr r4,[r0],#4
#ifdef DOK_SECTOR_GPU
    mov r5,#0
3:
    mov r6,r4
    cmp r3,#4
    cmpne r3,#1
    bne 4f
    ldr r12,=0x20202020
    add r12,r4,r12
    ldr lr,=0x80808080
    and r12,r12,lr
    add r6,r4,r12,lsr #5
4:
    str r6,[r2,r5]
    cmp r4,#0
    addne r4,r4,r7
    add r5,r5,#64
    cmp r5,#256
    bne 3b
#else
    str r4,[r2]
    cmp r4,#0
    addne r4,r4,r7
    str r4,[r2,#64]
    addne r4,r4,r7
    str r4,[r2,#128]
    addne r4,r4,r7
    str r4,[r2,#192]
#endif
    add r2,r2,#4096
    subs r3,r3,#1
    bne 2b
    subs r1,r1,#1
    bne 1b
    pop {r4-r7,lr}
    bx lr
.size dok_gba_hw_map_cells,.-dok_gba_hw_map_cells
