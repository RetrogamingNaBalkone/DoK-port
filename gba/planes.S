.syntax unified
.cpu arm7tdmi
.arm
.section .text.planes,"ax",%progbits
.global dok_gba_plane_divide
.type dok_gba_plane_divide,%function
/* r0=signed numerator; r1=positive divisor1..16384. AAPCS, no stack.
 * Reciprocal quotient is at most one below floor(abs(n)/d); one remainder
 * correction makes it exact. Entry1 uses UINT32_MAX and the same correction.
 * Restoring the sign gives native truncation toward zero, including INT_MIN. */
dok_gba_plane_divide:
    mov r3,r0,asr #31
    eor r0,r0,r3
    sub r0,r0,r3
    ldr r2,=dok_exact_plane_reciprocal
    ldr r2,[r2,r1,lsl #2]
    umull r2,r12,r0,r2
    mul r2,r12,r1
    sub r2,r0,r2
    cmp r2,r1
    addhs r12,r12,#1
    eor r0,r12,r3
    sub r0,r0,r3
    bx lr
.size dok_gba_plane_divide,.-dok_gba_plane_divide
.ltorg
#ifdef DOK_RENDER_RASTER_DIV_PROTOTYPE
.global dok_gba_texture_quotient
.type dok_gba_texture_quotient,%function
/* Unsigned n/d with quotient0..255 and d<=0x01ffffff, checked by caller.
 * Eight restoring bits; shifts cannot overflow. No table, stack or IRQ mask. */
dok_gba_texture_quotient:
    mov r2,#0
    .irp shift,7,6,5,4,3,2,1,0
    cmp r0,r1,lsl #\shift
    subhs r0,r0,r1,lsl #\shift
    orrhs r2,r2,#(1<<\shift)
    .endr
    mov r0,r2
    bx lr
.size dok_gba_texture_quotient,.-dok_gba_texture_quotient
#endif

#ifdef DOK_GBA_U_WIDE
.global dok_gba_divide_positive32
.type dok_gba_divide_positive32,%function
/* n=0..INT_MAX,d=1..INT_MAX. Normalize d to16 bits; reciprocal estimate
 * differs from floor(n/d) by at most1. The trial product fits uint32. */
dok_gba_divide_positive32:
    push {r4,lr}
    mov r2,r1
    mov r4,#0
    cmp r2,#0x800000
    movhs r2,r2,lsr #8
    addhs r4,r4,#8
    cmp r2,#0x80000
    movhs r2,r2,lsr #4
    addhs r4,r4,#4
    cmp r2,#0x20000
    movhs r2,r2,lsr #2
    addhs r4,r4,#2
    cmp r2,#0x10000
    movhs r2,r2,lsr #1
    addhs r4,r4,#1
    ldr r3,=dok_reciprocal
    ldr r3,[r3,r2,lsl #2]
    umull r2,r12,r0,r3
    mov r12,r12,lsr r4
    mul r2,r12,r1
    cmp r2,r0
    subhi r0,r12,#1
    bhi 1f
    sub r2,r0,r2
    cmp r2,r1
    addhs r12,r12,#1
    mov r0,r12
1:
    pop {r4,lr}
    bx lr
.size dok_gba_divide_positive32,.-dok_gba_divide_positive32
.ltorg
#endif

#ifdef DOK_GBA_U_RECIP
.global dok_gba_divide_u16
.type dok_gba_divide_u16,%function
/* Unsigned numerator, positive divisor1..65535. The existing Q32 reciprocal
 * is floor(2^32/d), with UINT_MAX for1. One remainder correction is exact. */
dok_gba_divide_u16:
    ldr r2,=dok_reciprocal
    ldr r2,[r2,r1,lsl #2]
    umull r3,r12,r0,r2
    mul r2,r12,r1
    sub r2,r0,r2
    cmp r2,r1
    addhs r12,r12,#1
    mov r0,r12
    bx lr
.size dok_gba_divide_u16,.-dok_gba_divide_u16
.ltorg
#endif
