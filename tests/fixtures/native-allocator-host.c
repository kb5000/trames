// SPDX-License-Identifier: GPL-3.0-or-later
/* Sanitizer harness for generated C, including malformed guest addresses. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct write {uint64_t address,value;uint32_t width,padding;};
struct read_page {uint64_t base;const unsigned char *bytes;};
struct callbacks {
    void *opaque;
    int (*load)(void *,uint64_t,unsigned,uint64_t *);
    int (*cas)(void *,uint64_t,uint32_t,uint32_t,const struct write *,unsigned,uint32_t *);
    int (*read_page)(void *,uint64_t,unsigned,struct read_page *);
};
struct result {
    uint64_t value,meta,index,counter;int status;unsigned write_count;
    struct write writes[16];
    unsigned load_callbacks,page_callbacks,direct_loads,journal_writes;
};
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
static int commit(const struct write *writes,unsigned count) {
    for(unsigned i=0;i<count;i++)
        if(writes[i].width>8 || writes[i].address>sizeof(bytes)-writes[i].width)return 1;
    for(unsigned i=0;i<count;i++)
        if(write_value(0,writes[i].address,writes[i].width,writes[i].value))return 1;
    return 0;
}
static int pin_page(void *opaque,uint64_t address,unsigned width,struct read_page *out) {
    (void)opaque;
    uint64_t untagged=address & 0x00ffffffffffffffULL;
    if(width>8 || untagged>sizeof(bytes)-width)return 1;
    out->base=untagged & -4096ULL;out->bytes=bytes+out->base;return 0;
}
static void run(const struct callbacks *cb,unsigned op,uint64_t value,uint64_t context,int mt,uint64_t index,uint64_t counter,struct result *out) {
    trames_native_allocator(cb,op,value,context,mt,index,counter,out);
    if(!out->status && commit(out->writes,out->write_count))out->status=1;
}
static int exchange(void *opaque,uint64_t address,uint32_t old,uint32_t new_value,const struct write *writes,unsigned count,uint32_t *out) {
    if(commit(writes,count))return 1;
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
  for(int direct=0;direct<2;direct++) {
    struct callbacks cb={0,read_value,exchange,direct?pin_page:0};struct result out;
    reset();run(&cb,1,157,0x1000,1,0,0,&out);assert(!out.status);
    run(&cb,3,157,out.meta,1,out.index,out.counter,&out);assert(!out.status && out.value==0x41a0);
    run(&cb,2,out.value,0x1000,1,0,0,&out);assert(!out.status && !out.value);
    reset();run(&cb,1,157,0x1000,1,0,0,&out);
    run(&cb,3,157,out.meta,1,out.index,out.counter,&out);conflict=1;
    run(&cb,2,out.value,0x1000,1,0,0,&out);assert(!out.status && out.value==2 && out.meta==0x3018 && out.index==2);
    reset();write_value(0,0x3018+32,8,3|(63<<6));
    run(&cb,1,157,0x1000,1,0,0,&out);
    run(&cb,3,157,out.meta,1,out.index,out.counter,&out);assert(out.status);
    for(uint64_t p=0;p<65536;p+=17){reset();run(&cb,2,p,0x1000,1,0,0,&out);assert(out.status);}
    reset();run(&cb,2,UINT64_MAX,0x1000,1,0,0,&out);assert(out.status);
    reset();run(&cb,1,UINT64_MAX,0x1000,1,0,0,&out);assert(out.status);
    for(uint64_t control=0;control<65536;control+=19) {
        reset();write_value(0,0x3018+32,8,control);
        run(&cb,3,157,0x3018,1,2,0,&out);
    }
  }
    puts("native-allocator-sanitizers-ok");return 0;
}
