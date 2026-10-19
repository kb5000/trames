// SPDX-License-Identifier: GPL-3.0-or-later
.text
.global native_fill
.type native_fill,%function
native_fill:
    movz x16, #1
    movk x16, #1, lsl #16
    movz x17, #3
    hlt #0x5452
    ret
.size native_fill,.-native_fill
.section .note.GNU-stack,"",@progbits
