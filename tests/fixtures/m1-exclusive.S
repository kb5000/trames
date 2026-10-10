// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.text
_start:
    adrp x20, memory
    add x20, x20, :lo12:memory
    mov w0, #41
    stlr w0, [x20]
    ldaxr w1, [x20]
    add w1, w1, #1
    stlxr w2, w1, [x20]
    str w2, [x20, #4]
    ldxr w3, [x20]
    clrex
    stxr w4, w3, [x20]
    str w4, [x20, #8]
    ldar w5, [x20]
    str w5, [x20, #12]
    mrs x6, dczid_el0
    str x6, [x20, #16]
    mov x7, #0x9999
    str x7, [x20, #64]
    add x7, x20, #64
    dc zva, x7
    ldr x7, [x20, #64]
    str x7, [x20, #24]
    dmb ish
    dsb ish
    isb
    mov x0, #1
    mov x1, x20
    mov x2, #32
    mov x8, #64
    svc #0
    mov x0, #0
    mov x8, #93
    svc #0
.bss
.balign 64
memory: .skip 128
