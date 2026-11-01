/* G25 material-only horizontal pairs at depth >=960. AAPCS surface_span.
 * Every destination tests/updates its own coverage, including odd boundaries.
 * Cache resets at global logical even columns and each span. No alpha shortcut. */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.ceiling_lod,"ax",%progbits
.global dok_gba_tiled_surface_lod
.type dok_gba_tiled_surface_lod,%function
dok_gba_tiled_surface_lod:
    ldr r1,[r0,#20]
    ldr r2,[r0,#24]
    cmp r1,r2
    movhi r0,#0
    bxhi lr
    push {r4-r11,lr}
    sub sp,sp,#12
    mov r12,#0
    str r12,[sp]
    mvn r12,#0
    str r12,[sp,#8]
    mov lr,r0
    ldr r12,[lr,#56]
    str r12,[sp,#4]
    ldmia lr,{r1,r2,r12}
#ifdef DOK_HW_SURFACE_PRELIT
    mov r1,#0 @ source is prelit: use the spare register for the pixel count
#endif
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
    tst r0,#1
    mvneq r12,#0
    streq r12,[sp,#8]
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
    ldr lr,[sp,#8]
    cmn lr,#1
    bne .Llod_store
    mov r12,r3,asr #16
    cmp r12,r7
    movgt r12,r7
    bic r12,r12,r12,asr #31
    mov lr,r4,asr #16
    cmp lr,r8
    movgt lr,r8
    bic lr,lr,lr,asr #31
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
    str lr,[sp,#8]
.Llod_store:
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
#ifdef DOK_HW_SURFACE_PRELIT
    add r1,r1,#1
#else
    ldr r12,[sp]
    add r12,r12,#1
    str r12,[sp]
#endif
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
#ifdef DOK_HW_SURFACE_PRELIT
    mov r0,r1
#else
    ldr r0,[sp]
#endif
    add sp,sp,#12
    pop {r4-r11,lr}
    bx lr
.size dok_gba_tiled_surface_lod,.-dok_gba_tiled_surface_lod

