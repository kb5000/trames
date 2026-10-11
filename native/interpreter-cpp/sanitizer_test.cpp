// SPDX-License-Identifier: GPL-3.0-or-later
#include "trames_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static trames_u64 random_word(trames_u64 &seed) {
    seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed;
}
int main() {
    trames_u64 seed=0x463563916742ull, checksum=0;
    for(unsigned round=0;round<100000;round++) {
        TramesCpuState state={};
        for(auto &x:state.x) x=random_word(seed);
        state.pc=0x1000; state.sp=random_word(seed); state.nzcv=random_word(seed)&15;
        trames_u32 words[64];
        for(auto &word:words) word=random_word(seed);
        const trames_u32 known[]={0xd503201f,0x91000400,0xf100041f,0xca000021,0x17fffffc,0xd4000001};
        for(unsigned k=0;k<round%16;k++) words[k]=known[random_word(seed)%6];
        auto before=state;
        auto result=trames_cpp_run(&state,words,round%64+1,round%4097);
        assert(result.instructions<=round%4097 && result.reason<=2 && state.nzcv<=15);
        if(result.instructions==0) assert(std::memcmp(&before,&state,sizeof(state))==0);
        checksum ^= state.pc + result.instructions + result.reason + state.sp;
        for(auto x:state.x) checksum=(checksum<<1 | checksum>>63)^x;
    }
    TramesCpuState state={}; trames_u32 word=0xd503201f;
    assert(trames_cpp_run(nullptr,&word,1,1).reason==2);
    assert(trames_cpp_run(&state,nullptr,1,1).reason==2);
    assert(trames_cpp_run(&state,&word,0,1).reason==2);
    assert(trames_cpp_run(&state,&word,65,1).reason==2);
    assert(trames_cpp_run(&state,&word,1,4097).reason==2);
    std::printf("100000 bounded native calls: %016llx\n",static_cast<unsigned long long>(checksum));
}
