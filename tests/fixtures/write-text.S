// SPDX-License-Identifier: GPL-3.0-or-later
.global _start
.section .text
_start:
    adr x1, _start
    str x0, [x1]
    mov x8, #93
    svc #0
