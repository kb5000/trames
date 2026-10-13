/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.p2align 2
.hidden trames_integer_step
.global trames_integer_step
.type trames_integer_step,%function
trames_integer_step:
    movz x16, #4
    movk x16, #1, lsl #16
    movz x17, #1
    hlt #0x5452
    ret
.size trames_integer_step,.-trames_integer_step
.section .note.GNU-stack,"",%progbits
