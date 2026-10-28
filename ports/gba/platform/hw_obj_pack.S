.syntax unified
.cpu arm7tdmi
.arm
.section .text.hw_obj_pack,"ax",%progbits
.global dok_gba_hw_compact
.type dok_gba_hw_compact,%function
/* In-place240x144 paired pixels ->128x144, including8 zero padding bytes.
 * r0=frame,r1=duplicate-black palette index. Destination always trails reads. */
dok_gba_hw_compact:
    push {r4-r9,lr}
    mov r4,r0
    mov r5,r0
    mov r8,r1
    mov r9,#144
1:
    mov r6,#60
2:
    ldr r2,[r5],#4
    and r3,r2,#255
    mov r2,r2,lsr #8
    and r2,r2,#0xff00
    orr r3,r3,r2
    tst r3,#255
    orreq r3,r3,r8
    tst r3,#0xff00
    orreq r3,r3,r8,lsl #8
    strh r3,[r4],#2
    subs r6,r6,#1
    bne 2b
    mov r2,#0
    str r2,[r4],#4
    str r2,[r4],#4
    subs r9,r9,#1
    bne 1b
    pop {r4-r9,lr}
    bx lr
.size dok_gba_hw_compact,.-dok_gba_hw_compact

.global dok_gba_hw_tiles
.type dok_gba_hw_tiles,%function
/* r0=128x144 compact rows,r1=1024-byte scratch. Convert in-place to
 *16x18 row-major8x8 tiles. Touch neither HUD source nor the frame guards. */
dok_gba_hw_tiles:
    push {r4-r11,lr}
    mov r4,r0
    mov r5,r1
    mov r8,#18
1:
    mov r2,r4
    mov r3,r5
    mov r9,#64
2:
    ldmia r2!,{r6,r7,r10,r11}
    stmia r3!,{r6,r7,r10,r11}
    subs r9,r9,#1
    bne 2b
    mov r10,#0
    mov r9,#16
3:
    add r2,r5,r10
    mov r11,#8
4:
    ldmia r2,{r6,r7}
    stmia r4!,{r6,r7}
    add r2,r2,#128
    subs r11,r11,#1
    bne 4b
    add r10,r10,#8
    subs r9,r9,#1
    bne 3b
    subs r8,r8,#1
    bne 1b
    pop {r4-r11,lr}
    bx lr
.size dok_gba_hw_tiles,.-dok_gba_hw_tiles

.global dok_gba_hw_ui_tile
.type dok_gba_hw_ui_tile,%function
/* r0=64-byte tile,r1=aligned source,r2=row pitch,r3=black index. */
dok_gba_hw_ui_tile:
    push {r4-r6,lr}
    mov r6,#8
1:
    ldmia r1,{r4,r5}
    .irp reg,r4,r5
    tst \reg,#0xff
    orreq \reg,\reg,r3
    tst \reg,#0xff00
    orreq \reg,\reg,r3,lsl #8
    tst \reg,#0xff0000
    orreq \reg,\reg,r3,lsl #16
    tst \reg,#0xff000000
    orreq \reg,\reg,r3,lsl #24
    .endr
    stmia r0!,{r4,r5}
    add r1,r1,r2
    subs r6,r6,#1
    bne 1b
    pop {r4-r6,lr}
    bx lr
.size dok_gba_hw_ui_tile,.-dok_gba_hw_ui_tile
