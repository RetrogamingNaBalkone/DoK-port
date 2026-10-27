.syntax unified
.cpu arm7tdmi
.arm
.section .text.sector_plane,"ax",%progbits
.global dok_gba_sector_plane
.type dok_gba_sector_plane,%function
/* dst,pixels,LIGHT,visible masks; wx,wz,dx,dz,coverage,rowmask,count,w-1,h-1,black.
 * Wrapped world-space UV and one shared visplane. Both mask arrays have
 * 16-byte column stride. Frame is the established128-wide tiled scene. */
dok_gba_sector_plane:
    push {r4-r11,lr}
    sub sp,sp,#12
    mov lr,r0
    ldr r4,[lr,#16]
    ldr r5,[lr,#20]
    ldr r6,[lr,#24]
    ldr r7,[lr,#28]
    ldr r12,[lr,#32]
    str r12,[sp]
    ldr r8,[lr,#36]
    ldr r9,[lr,#40]
    ldr r10,[lr,#44]
    ldr r11,[lr,#48]
    ldr r12,[lr,#52]
    str r12,[sp,#4]
    mov r12,#0
    str r12,[sp,#8]
    ldmia lr,{r0-r3}
1:
    ldr r12,[r3]
    tst r12,r8
    beq 2f
    ldr lr,[sp]
    ldr r12,[lr]
    orr r12,r12,r8
    str r12,[lr]
    mov r12,r4,lsl #16
    mov r12,r12,lsr #16
    mul r12,r10,r12
    mov r12,r12,lsr #16
    mov lr,r5,lsl #16
    mov lr,lr,lsr #16
    mul lr,r11,lr
    mov lr,lr,lsr #16
    mla lr,r10,lr,lr
    add lr,lr,r12
    ldrb lr,[r1,lr]
    ldrb lr,[r2,lr]
    cmp lr,#0
    ldreq lr,[sp,#4]
    strb lr,[r0]
    ldr r12,[sp,#8]
    add r12,r12,#1
    str r12,[sp,#8]
2:
    add r4,r4,r6
    add r5,r5,r7
    add r3,r3,#16
    ldr r12,[sp]
    add r12,r12,#16
    str r12,[sp]
    add r0,r0,#1
#ifndef DOK_SECTOR_BITMAP
    and r12,r0,#7
    cmp r12,#4
    addeq r0,r0,#56
#endif
    subs r9,r9,#1
    bne 1b
    ldr r0,[sp,#8]
    add sp,sp,#12
    pop {r4-r11,lr}
    bx lr
.size dok_gba_sector_plane,.-dok_gba_sector_plane
