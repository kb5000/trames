/* SPDX-License-Identifier: GPL-3.0-or-later */
/* NativeCall v1 pure module: x0..x2 input; aggregate result x0/x1.
 * x16 = (ABI << 16) | module; x17 = function. Each exact entry is checked.
 * A disabled ordinary Linux run never executes these virtual HLT instructions.
 */
.text
.macro ENTRY id
.p2align 2
.hidden trames_call_\id
.global trames_call_\id
.type trames_call_\id,%function
trames_call_\id:
    movz x16, #1
    movk x16, #1, lsl #16
    movz x17, #\id
    hlt #0x5452
    ret
.size trames_call_\id,.-trames_call_\id
.endm
ENTRY 1
ENTRY 2
ENTRY 3
ENTRY 4
ENTRY 5
ENTRY 6
ENTRY 7
ENTRY 8
ENTRY 9
ENTRY 10
ENTRY 11
ENTRY 12
ENTRY 13
ENTRY 14
ENTRY 15
ENTRY 16
ENTRY 17
.section .note.GNU-stack,"",%progbits
