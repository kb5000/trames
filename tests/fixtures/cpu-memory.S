// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.text
_start:
    adrp x1, buffer
    add x1, x1, :lo12:buffer
    movz x2, #0x4240
    movk x2, #0xf, lsl #16 // 1,000,000 iterations
1:
    ldr x3, [x1]
    add x3, x3, #1
    str x3, [x1]
    ldp x4, x5, [x1]
    eor x4, x4, x5
    stp x4, x5, [x1]
    ldr q0, [x1]
    str q0, [x1]
    ldrb w6, [x1, #16]
    add w6, w6, #1
    strb w6, [x1, #16]
    subs x2, x2, #1
    b.ne 1b
    movz x7, #0x4240
    movk x7, #0xf, lsl #16
    cmp x3, x7
    b.ne fail
    ldrb w6, [x1, #16]
    cmp w6, #64
    b.ne fail
    mov x0, #1
    adr x1, message
    mov x2, #14
    mov x8, #64
    svc #0
    mov x0, #0
    b exit
fail:
    mov x0, #99
exit:
    mov x8, #93
    svc #0
message: .ascii "cpu-memory-ok\n"
.bss
.balign 4096
buffer: .skip 4096
