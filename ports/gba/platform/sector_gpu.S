.syntax unified
.cpu arm7tdmi
.arm
.section .text.sector_gpu_copy,"ax",%progbits
.global dok_gba_sector_gpu_copy
.type dok_gba_sector_gpu_copy,%function
/* dst8-wide patch column, source tiled mip column, count, source row&7,
 * source tile-row jump. Final alpha is evaluated by OBJ, no CPU coverage. */
dok_gba_sector_gpu_copy:
    push {r4-r8,lr}
    ldr r4,[sp,#24]
    ldr r5,[sp,#28]
    ldr r6,[sp,#32]
    ldr r7,[sp,#36]
    mov r8,r3,lsr #4
    and r3,r3,#7
    mov r12,#0
1:  ldrb lr,[r1]
    ldrb lr,[r5,lr]
    cmp lr,#0
    moveq lr,r6
    orr r12,r12,lr
    strb lr,[r0],#8
    add r8,r8,#1
    ands r8,r8,#7
    addeq r0,r0,r7
    add r1,r1,#8
    add r3,r3,#1
    ands r3,r3,#7
    addeq r1,r1,r4
    subs r2,r2,#1
    bne 1b
    mov r0,r12
    pop {r4-r8,lr}
    bx lr
.size dok_gba_sector_gpu_copy,.-dok_gba_sector_gpu_copy
