// SPDX-License-Identifier: GPL-3.0-or-later
.arch armv8.1-a
.text
.global smp_lse_increment
.type smp_lse_increment,%function
smp_lse_increment:
    mov x1,#1
    ldaddal x1,x2,[x0]
    ret
.global smp_exclusive_increment
.type smp_exclusive_increment,%function
smp_exclusive_increment:
1:  ldaxr x1,[x0]
    add x1,x1,#1
    stlxr w2,x1,[x0]
    cbnz w2,1b
    ret
.global smp_casp_increment
.type smp_casp_increment,%function
smp_casp_increment:
    ldp x2,x3,[x0]
1:  mov x6,x2
    mov x7,x3
    add x4,x2,#1
    mvn x5,x4
    caspal x2,x3,x4,x5,[x0]
    cmp x2,x6
    ccmp x3,x7,#0,eq
    b.ne 1b
    ret
.global smp_pair_increment
.type smp_pair_increment,%function
smp_pair_increment:
1:  ldaxp x2,x3,[x0]
    add x2,x2,#1
    mvn x3,x2
    stlxp w4,x2,x3,[x0]
    cbnz w4,1b
    ret
.section .note.GNU-stack,"",%progbits
