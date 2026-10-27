.syntax unified
.cpu arm7tdmi
.arm
.section .text.ui_panels,"ax",%progbits
.global dok_gba_copy_panel_rows
.type dok_gba_copy_panel_rows,%function
/* Exact 32-bit ROM-to-EWRAM panel copy, r2=words/row, r3=rows, stride240.
 * Caller validates aligned, bounded HUD rectangles; no IRQ masking. */
dok_gba_copy_panel_rows:
    push {r4-r7,lr}
    rsb lr,r2,#60
    lsl lr,lr,#2
1:
    mov r12,r2
    cmp r12,#4
    blo 3f
2:
    ldmia r1!,{r4-r7}
    stmia r0!,{r4-r7}
    sub r12,r12,#4
    cmp r12,#4
    bhs 2b
3:
    cmp r12,#0
    beq 5f
4:
    ldr r4,[r1],#4
    str r4,[r0],#4
    subs r12,r12,#1
    bne 4b
5:
    add r0,r0,lr
    subs r3,r3,#1
    bne 1b
    pop {r4-r7,lr}
    bx lr
.size dok_gba_copy_panel_rows,.-dok_gba_copy_panel_rows
