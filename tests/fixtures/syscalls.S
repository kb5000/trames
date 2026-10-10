// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    mov x8, #64
    mov x0, #1
    mov x1, #-1
    mov x2, #3
    svc #0
    cmn x0, #14
    b.ne fail
    mov x0, #77
    mov x2, #0
    svc #0
    cmn x0, #9
    b.ne fail
    mov x0, #1
    svc #0
    cmn x0, #14
    b.ne fail
    mov x0, #1
    mov x1, #0
    svc #0
    cbnz x0, fail
    mov x8, #65535
    svc #0
    cmn x0, #38
    b.ne fail
    mov x0, #0
    b exit
fail:
    mov x0, #99
exit:
    mov x8, #93
    svc #0
