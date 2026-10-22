// SPDX-License-Identifier: GPL-3.0-or-later
.text
.global gpu_call
.type gpu_call,%function
gpu_call:
    movz x16, #6
    movk x16, #1, lsl #16
    movz x17, #1
    hlt #0x5452
    ret
.size gpu_call,.-gpu_call
.section .note.GNU-stack,"",@progbits
