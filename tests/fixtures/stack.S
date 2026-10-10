// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    ldr x3, [sp]
    cmp x3, #2
    b.ne fail
    ldr x1, [sp, #16]
    mov x0, #1
    mov x2, #3
    mov x8, #64
    svc #0
    mov x0, x3
    b exit
fail:
    mov x0, #99
exit:
    mov x8, #93
    svc #0
