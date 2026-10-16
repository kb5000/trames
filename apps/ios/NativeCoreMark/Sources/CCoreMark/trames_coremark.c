// SPDX-License-Identifier: GPL-3.0-or-later
#include "include/trames_coremark.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

int trames_coremark_main(int argc, char *argv[]);
static pthread_mutex_t run_lock = PTHREAD_MUTEX_INITIALIZER;
// Upstream's port timer and algorithm state aren't reentrant. Only benchmark
// runs share this lock; no guest execution or accelerator path uses it.
static char *destination;
static size_t destination_size, written;
static int truncated;

int ee_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    size_t remaining = destination_size - written;
    int count = vsnprintf(destination + written, remaining, format, args);
    va_end(args);
    if (count < 0) { truncated = 1; return count; }
    if ((size_t)count >= remaining) {
        written = destination_size - 1;
        truncated = 1;
    } else {
        written += (size_t)count;
    }
    return count;
}

int trames_coremark_run(uint32_t iterations, int validation, char *output, size_t capacity)
{
    // get_seed_args/parseval use signed 32-bit arithmetic.
    if (!output || capacity < 2 || iterations > 100000000) return -1;
    if (pthread_mutex_trylock(&run_lock) != 0) return -2;
    destination = output;
    destination_size = capacity;
    written = 0;
    truncated = 0;
    output[0] = '\0';
    char iteration_text[16];
    snprintf(iteration_text, sizeof(iteration_text), "%u", iterations);
    char name[] = "CoreMark", zero[] = "0", seed[] = "0x3415", third[] = "0x66";
    char *argv[] = {name, validation ? seed : zero, validation ? seed : zero,
                    third, iteration_text, zero, NULL};
    int status = trames_coremark_main(6, argv);
    if (truncated) status = -3;
    destination = NULL;
    destination_size = written = 0;
    pthread_mutex_unlock(&run_lock);
    return status;
}
