// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.text
_start:
    adrp x20, snapshot
    add x20, x20, :lo12:snapshot
    mov x0, #0x80ff
    movk x0, #0x1122, lsl #16
    mov x1, #-1
    stp x0, x1, [x20], #16
    ldp x2, x3, [x20, #-16]
    stp x2, x3, [x20], #16
    ldpsw x4, x5, [x20, #-32]
    stp x4, x5, [x20], #16
    ldrsb x6, [x20, #-48]
    ldrsh w7, [x20, #-48]
    stp x6, x7, [x20], #16
    mov x8, #-64
    ldr w9, [x20, x8]
    str x9, [x20], #8
    dup v0.16b, w0
    str q0, [x20], #16
    movi v1.4s, #0x80, lsl #16
    str q1, [x20], #16
    movi v2.8h, #0xaa, lsl #8
    str q2, [x20], #16
    mvni v3.4s, #0x12, msl #8
    str q3, [x20], #16
    movi v4.2d, #0xff00ff00ff00ff00
    str q4, [x20], #16
    eor v5.16b, v0.16b, v1.16b
    ins v5.d[1], x0
    umov x10, v5.d[1]
    str x10, [x20], #8
    stp q4, q5, [x20]
    ldp q6, q7, [x20]
    add x20, x20, #32
    stp q6, q7, [x20]
    adrp x1, snapshot
    add x1, x1, :lo12:snapshot
    mov x0, #1
    mov x2, #256
    mov x8, #64
    svc #0
    mov x0, #0
    mov x8, #93
    svc #0
.bss
.balign 16
snapshot: .skip 256
