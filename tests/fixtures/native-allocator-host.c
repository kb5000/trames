// SPDX-License-Identifier: GPL-3.0-or-later
/* Sanitizer harness for generated C, including malformed guest addresses. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct callbacks {
    void *opaque;
    int (*load)(void *,uint64_t,unsigned,uint64_t *);
    int (*store)(void *,uint64_t,unsigned,uint64_t);
    int (*cas)(void *,uint64_t,uint32_t,uint32_t,uint32_t *);
};
struct result { uint64_t value,meta,index,counter;int status; };
extern void trames_native_allocator(const struct callbacks *,unsigned,uint64_t,uint64_t,int,uint64_t,uint64_t,struct result *);
static unsigned char bytes[32768];
static int conflict;
static int read_value(void *opaque,uint64_t address,unsigned width,uint64_t *out) {
    (void)opaque;if(width>8 || address>sizeof(bytes)-width)return 1;
    *out=0;memcpy(out,bytes+address,width);return 0;
}
static int write_value(void *opaque,uint64_t address,unsigned width,uint64_t value) {
    (void)opaque;if(width>8 || address>sizeof(bytes)-width)return 1;
    memcpy(bytes+address,&value,width);return 0;
}
static int exchange(void *opaque,uint64_t address,uint32_t old,uint32_t new_value,uint32_t *out) {
    if(conflict){conflict=0;write_value(opaque,address-4,4,0);write_value(opaque,address,4,0);}
    uint64_t value;if(read_value(opaque,address,4,&value))return 1;*out=value;
    return value==old?write_value(opaque,address,4,new_value):0;
}
static void reset(void) {
    memset(bytes,0,sizeof(bytes));
    write_value(0,0x1000,8,0x55aa);write_value(0,0x100c,4,1);
    write_value(0,0x1000+80+8*10,8,0x3018);
    write_value(0,0x3000,8,0x55aa);write_value(0,0x3018+16,8,0x4000);
    write_value(0,0x3018+24,4,12);write_value(0,0x3018+28,4,1);
    write_value(0,0x3018+32,8,3|(1<<5)|(10<<6)|(1<<12));write_value(0,0x4000,8,0x3018);
    write_value(0,0x4008,1,3);
}
int main(void) {
    struct callbacks cb={0,read_value,write_value,exchange};struct result out;
    reset();trames_native_allocator(&cb,1,157,0x1000,1,0,0,&out);assert(!out.status);
    trames_native_allocator(&cb,3,157,out.meta,1,out.index,out.counter,&out);assert(!out.status && out.value==0x41a0);
    trames_native_allocator(&cb,2,out.value,0x1000,1,0,0,&out);assert(!out.status && !out.value);
    reset();trames_native_allocator(&cb,1,157,0x1000,1,0,0,&out);
    trames_native_allocator(&cb,3,157,out.meta,1,out.index,out.counter,&out);conflict=1;
    trames_native_allocator(&cb,2,out.value,0x1000,1,0,0,&out);assert(!out.status && out.value==2 && out.meta==0x3018 && out.index==2);
    reset();write_value(0,0x3018+32,8,3|(63<<6));
    trames_native_allocator(&cb,1,157,0x1000,1,0,0,&out);
    trames_native_allocator(&cb,3,157,out.meta,1,out.index,out.counter,&out);assert(out.status);
    for(uint64_t p=0;p<65536;p+=17){reset();trames_native_allocator(&cb,2,p,0x1000,1,0,0,&out);assert(out.status);}
    reset();trames_native_allocator(&cb,2,UINT64_MAX,0x1000,1,0,0,&out);assert(out.status);
    reset();trames_native_allocator(&cb,1,UINT64_MAX,0x1000,1,0,0,&out);assert(out.status);
    for(uint64_t control=0;control<65536;control+=19) {
        reset();write_value(0,0x3018+32,8,control);
        trames_native_allocator(&cb,3,157,0x3018,1,2,0,&out);
    }
    puts("native-allocator-sanitizers-ok");return 0;
}
