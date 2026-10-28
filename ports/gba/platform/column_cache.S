.syntax unified
.cpu arm7tdmi
.arm
.section .text.column_cache,"ax",%progbits
.global dok_gba_tiled_column_cached
.type dok_gba_tiled_column_cached,%function
/* Same ABI as tiled_column. Long magnified ROM spans are copied in16-byte
 * sequential bursts to a bounded IWRAM stack cache, then sampled unchanged.
 * No persistent cache state, IRQ mask or texel quantization. */
dok_gba_tiled_column_cached:
    cmp r3,#65536
    bhi dok_gba_tiled_column
    ldr r12,[sp]
    cmp r12,#32
    blo dok_gba_tiled_column
    mov r12,r1,lsr #25
    cmp r12,#4
    bne dok_gba_tiled_column
    push {r4-r9,lr}
    mov r4,r0
    mov r7,r3
    add r5,r1,r2,lsr #16
    bic r5,r5,#15
    sub r6,r5,r1
    sub r6,r2,r6,lsl #16
    ldr r12,[sp,#28]
    sub r8,r12,#1
    mla r8,r3,r8,r2
    add r8,r1,r8,lsr #16
    add r8,r8,#16
    bic r8,r8,#15
    sub r8,r8,r5
    cmp r8,#256
    bhi 2f
    ldr r0,[sp,#32]
    ldr r1,[sp,#36]
    sub sp,sp,#276
    str r12,[sp]
    str r0,[sp,#4]
    str r1,[sp,#8]
    add r9,sp,#16
1:
    ldmia r5!,{r0-r3}
    stmia r9!,{r0-r3}
    subs r8,r8,#16
    bne 1b
    mov r0,r4
    add r1,sp,#16
    mov r2,r6
    mov r3,r7
    bl dok_gba_tiled_column
    add sp,sp,#276
    pop {r4-r9,lr}
    bx lr
2:
    pop {r4-r9,lr}
    b dok_gba_tiled_column
.size dok_gba_tiled_column_cached,.-dok_gba_tiled_column_cached
