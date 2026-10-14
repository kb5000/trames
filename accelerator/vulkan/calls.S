/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.p2align 2
.hidden trames_vulkan_call
.global trames_vulkan_call
.type trames_vulkan_call,%function
trames_vulkan_call:
    movz x16, #6
    movk x16, #1, lsl #16
    movz x17, #1
    hlt #0x5452
    ret
.size trames_vulkan_call,.-trames_vulkan_call
.section .note.GNU-stack,"",%progbits
