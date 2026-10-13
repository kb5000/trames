/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.p2align 2
.hidden trames_numeric_step
.global trames_numeric_step
.type trames_numeric_step,%function
trames_numeric_step:
    movz x16, #3
    movk x16, #1, lsl #16
    movz x17, #1
    hlt #0x5452
    ret
.size trames_numeric_step,.-trames_numeric_step
.section .note.GNU-stack,"",%progbits
