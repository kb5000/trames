// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    mov x0, #1
    adr x1, binary
    mov x2, #4
    mov x8, #64
    svc #0
    cmp x0, #4
    b.ne fail
    mov x0, #2
    adr x1, diagnostic
    mov x2, #diagnostic_end-diagnostic
    svc #0
    mov x0, #37
    b exit
fail:
    mov x0, #99
exit:
    mov x8, #94
    svc #0
.section .rodata
binary: .byte 0x41, 0, 0xff, 0x0a
diagnostic: .ascii "stderr: Trames\n"
diagnostic_end:
