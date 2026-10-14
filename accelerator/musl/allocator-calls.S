/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.p2align 2
.hidden trames_malloc_step
.global trames_malloc_step
.type trames_malloc_step,%function
trames_malloc_step:
    movz x16, #5
    movk x16, #1, lsl #16
    movz x17, #1
    hlt #0x5452
    ret
.size trames_malloc_step,.-trames_malloc_step
.p2align 2
.hidden trames_free_step
.global trames_free_step
.type trames_free_step,%function
trames_free_step:
    movz x16, #5
    movk x16, #1, lsl #16
    movz x17, #2
    hlt #0x5452
    ret
.size trames_free_step,.-trames_free_step
.p2align 2
.hidden trames_enframe_step
.global trames_enframe_step
.type trames_enframe_step,%function
trames_enframe_step:
    movz x16, #5
    movk x16, #1, lsl #16
    movz x17, #3
    hlt #0x5452
    ret
.size trames_enframe_step,.-trames_enframe_step
.section .note.GNU-stack,"",%progbits
