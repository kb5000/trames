/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.p2align 2
.hidden trames_callback_step
.global trames_callback_step
.type trames_callback_step,%function
trames_callback_step:
    movz x16,#2
    movk x16,#1,lsl#16
    movz x17,#1
    hlt #0x5452
    ret
.size trames_callback_step,.-trames_callback_step
.section .note.GNU-stack,"",%progbits
