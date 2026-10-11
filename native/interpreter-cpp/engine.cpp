// SPDX-License-Identifier: GPL-3.0-or-later
#include "trames_cpu.h"
static_assert(sizeof(TramesCpuState)==272, "CPU POD layout");
static_assert(sizeof(TramesCpuResult)==16, "result POD layout");
using u64=trames_u64; using u32=trames_u32; using i64=trames_i64;
static u64 mask(bool wide) { return wide ? ~u64(0) : u64(0xffffffff); }
static u64 reg(TramesCpuState *s,u32 n,bool sp=false) { return n==31 ? (sp?s->sp:0) : s->x[n]; }
static void put(TramesCpuState *s,u32 n,u64 v,bool wide,bool sp=false) {
    v &= mask(wide); if(n!=31) s->x[n]=v; else if(sp) s->sp=v;
}
static u64 sext(u64 v,u32 bits) { const u64 sign=u64(1)<<(bits-1); return (v^sign)-sign; }
static u64 shift(u64 v,u32 kind,u32 amount,bool wide) {
    v &=mask(wide); if(!amount) return v;
    switch(kind) {
    case 0: return (v<<amount)&mask(wide);
    case 1: return v>>amount;
    case 2: return wide ? u64(i64(v)>>amount) : u64(trames_u32(trames_i32(v)>>amount));
    default: return ((v>>amount)|(v<<((wide?64:32)-amount)))&mask(wide);
    }
}
static u64 arithmetic(TramesCpuState *s,u64 a,u64 b,bool sub,bool flags,bool wide) {
    const u64 m=mask(wide), sign=u64(1)<<(wide?63:31); a&=m; b&=m;
    const u64 r=(sub?a-b:a+b)&m;
    if(flags) {
        const bool c=sub ? a>=b : (wide ? r<a : a+b>m);
        const bool v=((sub?(a^b):~(a^b))&(a^r)&sign)!=0;
        s->nzcv=((r&sign)?8:0)|(r==0?4:0)|(c?2:0)|(v?1:0);
    }
    return r;
}
static bool condition(u32 f,u32 c) {
    bool n=f&8,z=f&4,carry=f&2,v=f&1, result=false;
    switch(c>>1) {case 0:result=z;break;case 1:result=carry;break;case 2:result=n;break;case 3:result=v;break;case 4:result=carry&&!z;break;case 5:result=n==v;break;case 6:result=!z&&(n==v);break;default:result=true;}
    return (c&1)&&c!=15 ? !result : result;
}
static trames_u8 decode(u32 i) {
    if(i==0xd503201f) return 1;
    if(i==0xd4000001) return 2;
    if((i&0x1f800000)==0x12800000) {
        if(((i>>29)&3)==1 || (!(i>>31) && ((i>>21)&3)>=2)) return 0;
        return 3;
    }
    if((i&0x1f800000)==0x11000000) return 4;
    if((i&0x1f200000)==0x0b000000) {
        if(((i>>22)&3)==3 || (!(i>>31) && ((i>>10)&63)>=32)) return 0;
        return 5;
    }
    if((i&0x1f000000)==0x0a000000) {
        if(!(i>>31) && ((i>>10)&63)>=32) return 0;
        return 6;
    }
    if((i&0x7c000000)==0x14000000) return 7;
    if((i&0x7e000000)==0x34000000) return 8;
    if((i&0xff000010)==0x54000000) return 9;
    if((i&0x1f000000)==0x10000000) return 10;
    if((i&0xfffffc1f)==0xd61f0000 || (i&0xfffffc1f)==0xd63f0000 || (i&0xfffffc1f)==0xd65f0000) return 11;
    return 0;
}
extern "C" u32 trames_cpp_abi_version() { return 1; }
extern "C" TramesCpuResult trames_cpp_run(TramesCpuState *s,const u32 *words,u32 count,u32 budget) {
    if(!s || !words || count==0 || count>64 || budget>4096) return {0,2,0};
    trames_u8 decoded[64]; for(u32 k=0;k<count;k++) decoded[k]=decode(words[k]);
    const u64 base=s->pc; u64 done=0; u32 index=0,i=0,rd=0,rn=0,rm=0; bool wide=false;
    #if (defined(__GNUC__) || defined(__clang__)) && !defined(TRAMES_PORTABLE_DISPATCH)
    static const void *labels[]={&&fallback,&&nop,&&svc,&&mov,&&imm,&&add,&&logical,&&branch,&&cb,&&cond,&&adr,&&br};
    #define DISPATCH() goto *labels[decoded[index]]
    #else
    #define DISPATCH() switch(decoded[index]) {case 1:goto nop;case 2:goto svc;case 3:goto mov;case 4:goto imm;case 5:goto add;case 6:goto logical;case 7:goto branch;case 8:goto cb;case 9:goto cond;case 10:goto adr;case 11:goto br;default:goto fallback;}
    #endif
    #define NEXT() do {s->pc+=4;done++;goto dispatch;} while(false)
dispatch:
    if(done>=budget || s->pc<base || (s->pc-base)%4 || (s->pc-base)/4>=count) return {done,0,0};
    index=u32((s->pc-base)/4); i=words[index]; rd=i&31; rn=(i>>5)&31; rm=(i>>16)&31; wide=i>>31;
    if(decoded[index]>11) goto fallback;
    DISPATCH();
fallback: return {done,2,0};
nop: NEXT();
svc: s->pc+=4; return {done+1,1,0};
mov: {
    const u32 op=(i>>29)&3, sh=((i>>21)&3)*16; const u64 v=u64((i>>5)&0xffff)<<sh;
    put(s,rd,op==0?~v:op==2?v:(reg(s,rd)&~(u64(0xffff)<<sh))|v,wide); NEXT();
}
imm: {
    const bool sub=i&(1u<<30), f=i&(1u<<29); const u64 v=u64((i>>10)&4095)<<((i&(1u<<22))?12:0);
    put(s,rd,arithmetic(s,reg(s,rn,true),v,sub,f,wide),wide,!f); NEXT();
}
add: {
    const u64 v=shift(reg(s,rm),(i>>22)&3,(i>>10)&63,wide);
    put(s,rd,arithmetic(s,reg(s,rn),v,i&(1u<<30),i&(1u<<29),wide),wide); NEXT();
}
logical: {
    u64 rhs=shift(reg(s,rm),(i>>22)&3,(i>>10)&63,wide); if(i&(1u<<21)) rhs=~rhs;
    const u32 op=(i>>29)&3; const u64 a=reg(s,rn), v=(op==0||op==3?a&rhs:op==1?a|rhs:a^rhs)&mask(wide);
    if(op==3) s->nzcv=((v>>(wide?63:31))?8:0)|(v==0?4:0);
    put(s,rd,v,wide); NEXT();
}
branch: if(wide) put(s,30,s->pc+4,true); s->pc+=sext(u64(i&0x3ffffff)<<2,28); done++; goto dispatch;
cb: if(bool(reg(s,rd)&mask(wide))==bool(i&(1u<<24))) {s->pc+=sext(u64((i>>5)&0x7ffff)<<2,21);done++;goto dispatch;} NEXT();
cond: if(condition(s->nzcv,i&15)) {s->pc+=sext(u64((i>>5)&0x7ffff)<<2,21);done++;goto dispatch;} NEXT();
adr: {const u64 v=sext((u64((i>>5)&0x7ffff)<<2)|((i>>29)&3),21); put(s,rd,wide?(s->pc&~u64(4095))+(v<<12):s->pc+v,true);NEXT();}
br: {const u64 target=reg(s,rn); if((i&0xfffffc1f)==0xd63f0000) put(s,30,s->pc+4,true);s->pc=target;done++;goto dispatch;}
    #undef DISPATCH
    #undef NEXT
}
