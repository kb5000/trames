// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    adrp x1, buffer
    add x1, x1, :lo12:buffer
    ldr x0, [x1]
    cbnz x0, fail
    mov x3, #0x1122
    movk x3, #0x3344, lsl #16
    movk x3, #0x5566, lsl #32
    movk x3, #0x7788, lsl #48
    add x1, x1, #4092
    str x3, [x1]
    ldr x4, [x1]
    cmp x4, x3
    b.ne fail
    ldrb w5, [x1]
    cmp w5, #0x22
    b.ne fail
    mov x0, #1
    mov x2, #8
    mov x8, #64
    svc #0
    mov x0, #0
    b exit
fail:
    mov x0, #99
exit:
    mov x8, #93
    svc #0
.section .bss
.balign 4096
buffer: .skip 8192
