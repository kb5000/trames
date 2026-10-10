// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    mov w0, #4
loop:
    subs w0, w0, #1
    b.ne loop
    cbnz w0, fail
    bl function
    cmp x0, #9
    b.ne fail
    adr x4, continuation
    br x4
continuation:
    ldr x1, constant
    cmp x1, #42
    b.ne fail
    mov x0, #0
    b exit
function:
    mov x0, #9
    ret
fail:
    mov x0, #99
exit:
    mov x8, #93
    svc #0
.balign 8
constant: .quad 42
