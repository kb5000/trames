// SPDX-License-Identifier: GPL-3.0-or-later
// Freestanding dispatch experiment. No guest memory, FFI or runtime backend.
// STYLE: 0 switch loop, 1 computed goto, 2 Clang guaranteed tail calls.
using u64 = unsigned long long;
using u32 = unsigned int;
struct Operation { u32 kind, operand; };
struct State { u64 value, pc, remaining; const Operation *code; };
static u64 add(State *s) { return s->value + s->code[s->pc].operand; }
static u64 mix(State *s) { return (s->value << 7) ^ (s->value >> 3) ^ s->code[s->pc].operand; }

#if STYLE == 2
#if !defined(__clang__) || !__has_attribute(musttail)
#error Guaranteed tail dispatch requires Clang musttail
#endif
using Handler = u64 (*)(State *);
static u64 addition(State *);
static u64 mixing(State *);
static u64 branching(State *);
static const Handler handlers[] = {addition, mixing, branching};
#define NEXT() do { \
    if (--s->remaining == 0) return s->value; \
    [[clang::musttail]] return handlers[s->code[s->pc].kind](s); \
} while (false)
static u64 addition(State *s) { s->value = add(s); ++s->pc; NEXT(); }
static u64 mixing(State *s) { s->value = mix(s); ++s->pc; NEXT(); }
static u64 branching(State *s) { s->pc = s->code[s->pc].operand; NEXT(); }
extern "C" u64 dispatch(State *s) {
    if (s->remaining == 0) return s->value;
    [[clang::musttail]] return handlers[s->code[s->pc].kind](s);
}
#undef NEXT
#else
extern "C" u64 dispatch(State *s) {
    if (s->remaining == 0) return s->value;
#if STYLE == 1
    static const void *handlers[] = {&&addition, &&mixing, &&branching};
#define GO() goto *handlers[s->code[s->pc].kind]
#else
#define GO() switch(s->code[s->pc].kind) {case 0: goto addition; case 1: goto mixing; default: goto branching;}
#endif
#define NEXT() do { if (--s->remaining == 0) return s->value; GO(); } while (false)
    GO();
addition: s->value = add(s); ++s->pc; NEXT();
mixing: s->value = mix(s); ++s->pc; NEXT();
branching: s->pc = s->code[s->pc].operand; NEXT();
#undef NEXT
#undef GO
}
#endif

#ifdef DRIVER
#include <cstdio>
#include <cstdlib>
#include <chrono>
int main(int argc, char **argv) {
    Operation code[256];
    u32 random = 7189;
    for (u32 i = 0; i < 255; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        code[i] = {random & 1, random};
    }
    code[255] = {2, 0};
    State state = {42, 0, argc == 2 ? std::strtoull(argv[1], nullptr, 10) : 10000000, code};
    auto start = std::chrono::steady_clock::now();
    u64 value = dispatch(&state);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"style\":%d,\"checksum\":%llu,\"pc\":%llu,\"remaining\":%llu,\"seconds\":%.9f}\n",
                STYLE, value, state.pc, state.remaining, seconds);
}
#endif
