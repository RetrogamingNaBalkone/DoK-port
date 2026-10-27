.syntax unified
.cpu arm7tdmi
.arm
.section .text.geometry_cache,"ax",%progbits
.global dok_gba_geometry_copy
.type dok_gba_geometry_copy,%function
/* Aligned pointers, byte count divisible4. No overlap. */
dok_gba_geometry_copy:
    push {r4-r10,lr}
    cmp r2,#32
    blo 2f
1:
    ldmia r1!,{r3-r10}
    stmia r0!,{r3-r10}
    sub r2,r2,#32
    cmp r2,#32
    bhs 1b
2:
    cmp r2,#0
    beq 4f
3:
    ldr r3,[r1],#4
    str r3,[r0],#4
    subs r2,r2,#4
    bne 3b
4:
    pop {r4-r10,lr}
    bx lr
.size dok_gba_geometry_copy,.-dok_gba_geometry_copy
.global dok_gba_geometry_equal
.type dok_gba_geometry_equal,%function
/* Exact comparison, aligned pointers, byte count divisible4. */
dok_gba_geometry_equal:
    push {r4-r8,lr}
    cmp r2,#16
    blo 2f
1:
    ldmia r0!,{r3-r6}
    ldmia r1!,{r7,r8,r12,lr}
    cmp r3,r7
    cmpeq r4,r8
    cmpeq r5,r12
    cmpeq r6,lr
    bne 5f
    sub r2,r2,#16
    cmp r2,#16
    bhs 1b
2:
    cmp r2,#0
    beq 4f
3:
    ldr r3,[r0],#4
    ldr r4,[r1],#4
    cmp r3,r4
    bne 5f
    subs r2,r2,#4
    bne 3b
4:
    mov r0,#1
    b 6f
5:
    mov r0,#0
6:
    pop {r4-r8,lr}
    bx lr
.size dok_gba_geometry_equal,.-dok_gba_geometry_equal
