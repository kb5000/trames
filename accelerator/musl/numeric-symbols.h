/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
__attribute__((visibility("hidden"))) double trames_orig_strtod(const char *, char **);
__attribute__((visibility("hidden"))) float trames_orig_strtof(const char *, char **);
#define strtod trames_orig_strtod
#define strtof trames_orig_strtof
/* strtold retains the original binary128 guest implementation and public name. */
