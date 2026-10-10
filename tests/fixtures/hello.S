// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    mov x0, #1
    adr x1, message
    mov x2, #message_end-message
    mov x8, #64
    svc #0
    mov x0, #0
    mov x8, #93
    svc #0
.section .rodata
message: .ascii "Hello from Trames!\n"
message_end:
