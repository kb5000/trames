/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <inttypes.h>
// Expand privately renamed symbols before musl stringifies the alias target.
#undef weak_alias
#define TRAMES_ALIAS(a,b) extern __typeof(a) b __attribute__((__weak__,__alias__(#a)))
#define weak_alias(a,b) TRAMES_ALIAS(a,b)
#define PRIVATE __attribute__((visibility("hidden")))
#define ALIASES(name,type) PRIVATE type trames_orig_##name(const char *,char **,int); PRIVATE type trames_orig___##name##_internal(const char *,char **,int)
ALIASES(strtol,long); ALIASES(strtoul,unsigned long);
ALIASES(strtoll,long long); ALIASES(strtoull,unsigned long long);
ALIASES(strtoimax,intmax_t); ALIASES(strtoumax,uintmax_t);
#define strtol trames_orig_strtol
#define strtoul trames_orig_strtoul
#define strtoll trames_orig_strtoll
#define strtoull trames_orig_strtoull
#define strtoimax trames_orig_strtoimax
#define strtoumax trames_orig_strtoumax
#define __strtol_internal trames_orig___strtol_internal
#define __strtoul_internal trames_orig___strtoul_internal
#define __strtoll_internal trames_orig___strtoll_internal
#define __strtoull_internal trames_orig___strtoull_internal
#define __strtoimax_internal trames_orig___strtoimax_internal
#define __strtoumax_internal trames_orig___strtoumax_internal
