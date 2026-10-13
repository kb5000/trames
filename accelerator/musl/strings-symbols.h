/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#undef weak_alias
#define TRAMES_ALIAS(a,b) extern __typeof(a) b __attribute__((__weak__,__alias__(#a)))
#define weak_alias(a,b) TRAMES_ALIAS(a,b)
#define PRIVATE __attribute__((visibility("hidden")))
PRIVATE char *trames_orig_strchr(const char *,int);
PRIVATE char *trames_orig_strrchr(const char *,int);
PRIVATE char *trames_orig_strchrnul(const char *,int);
PRIVATE char *trames_orig___strchrnul(const char *,int);
PRIVATE char *trames_orig_strcpy(char *,const char *);
PRIVATE char *trames_orig_stpcpy(char *,const char *);
PRIVATE char *trames_orig___stpcpy(char *,const char *);
PRIVATE char *trames_orig_strncpy(char *,const char *,size_t);
PRIVATE char *trames_orig_stpncpy(char *,const char *,size_t);
PRIVATE char *trames_orig___stpncpy(char *,const char *,size_t);
#define strchr trames_orig_strchr
#define strrchr trames_orig_strrchr
#define strchrnul trames_orig_strchrnul
#define __strchrnul trames_orig___strchrnul
#define strcpy trames_orig_strcpy
#define stpcpy trames_orig_stpcpy
#define __stpcpy trames_orig___stpcpy
#define strncpy trames_orig_strncpy
#define stpncpy trames_orig_stpncpy
#define __stpncpy trames_orig___stpncpy

PRIVATE char *trames_orig_strcat(char *,const char *);
PRIVATE char *trames_orig_strncat(char *,const char *,size_t);
#define strcat trames_orig_strcat
#define strncat trames_orig_strncat
