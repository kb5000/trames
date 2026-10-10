// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.text
_start:
    mov x0, #-100
    adr x1, directory
    mov x2, #0x1c0
    mov x8, #34
    svc #0
    cbnz x0, fail
    adr x0, directory
    mov x8, #49
    svc #0
    cbnz x0, fail
    mov x0, #-100
    adr x1, filename
    mov x2, #0x42
    mov x3, #0x180
    mov x8, #56
    svc #0
    cmp x0, #0
    b.lt fail
    mov x19, x0
    adr x1, content
    mov x2, #3
    mov x8, #64
    svc #0
    cmp x0, #3
    b.ne fail
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x8, #62
    svc #0
    mov x0, x19
    adrp x1, buffer
    add x1, x1, :lo12:buffer
    mov x2, #3
    mov x8, #63
    svc #0
    cmp x0, #3
    b.ne fail
    mov x0, #1
    mov x8, #64
    svc #0
    mov x0, x19
    adrp x1, statbuf
    add x1, x1, :lo12:statbuf
    mov x8, #80
    svc #0
    cbnz x0, fail
    ldr x4, [x1, #48]
    cmp x4, #3
    b.ne fail
    mov x0, x19
    mov x8, #57
    svc #0
    cbnz x0, fail
    mov x0, #0
    b exit
fail: mov x0, #99
exit:
    mov x8, #93
    svc #0
.section .rodata
filename: .asciz "created.txt"
directory: .asciz "workdir"
content: .ascii "aZ\n"
.bss
.balign 8
buffer: .skip 8
statbuf: .skip 128
