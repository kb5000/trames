// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TRAMES_COREMARK_H
#define TRAMES_COREMARK_H
#include <stddef.h>
#include <stdint.h>

// Runs synchronously on the calling thread. iterations=0 uses upstream auto
// calibration. Returns 0 when complete, -1 on invalid arguments, -2 if busy,
// and -3 if output was truncated. Inspect the original output for CRC validity
// and the >=10 second requirement; upstream main returns zero even on failure.
int trames_coremark_run(uint32_t iterations, int validation, char *output, size_t capacity);
#endif
