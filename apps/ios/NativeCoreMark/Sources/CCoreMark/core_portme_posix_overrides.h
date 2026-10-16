// SPDX-License-Identifier: GPL-3.0-or-later
// App port settings; all five upstream algorithm/framework C files are intact.
#ifndef CORE_PORTME_POSIX_OVERRIDES_H
#define CORE_PORTME_POSIX_OVERRIDES_H
#define HAS_PRINTF 0
#define MULTITHREAD 1
#define ITERATIONS 0
#define COMPILER_FLAGS "-O2 -fno-lto; MULTITHREAD=1; TOTAL_DATA_SIZE=2000"
#define MEM_LOCATION "Heap"
// Only CoreMark's own output goes to this bounded buffer. Never redirect the
// process stdout/stderr: the Linux terminal and native networking still log.
int ee_printf(const char *format, ...);
#endif
