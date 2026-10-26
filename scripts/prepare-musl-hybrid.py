#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply checked startup/library entry hooks; retain pinned original algorithms."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[1]
tree=Path(sys.argv[1])
before={}
for name,marker in [
    ('src/env/__libc_start_main.c','\tlibc.auxv = auxv = (void *)(envp+i+1);'),
    ('ldso/dynlink.c','\tlibc.auxv = auxv;')]:
    p=tree/name;data=p.read_bytes();before[name]=hashlib.sha256(data).hexdigest()
    text=data.decode();assert text.count(marker)==1,name
    text=text.replace('#include "libc.h"','#include "libc.h"\nhidden void __trames_native_init(const size_t *);',1)
    text=text.replace(marker,marker+'\n\t__trames_native_init(auxv);',1);p.write_text(text)
dest=tree/'src/trames';(dest/'aarch64').mkdir(parents=True,exist_ok=True)
allocator_inputs=[('accelerator/musl/allocator.h','allocator.h'),('accelerator/musl/allocator-calls.S','aarch64/allocator-calls.S')]
inputs=[('accelerator/musl/integer.c','integer.c'),('accelerator/musl/integer-symbols.h','integer-symbols.h'),('accelerator/musl/integer-calls.S','aarch64/integer-calls.S'),('accelerator/musl/strings.c','strings.c'),('accelerator/musl/strings-symbols.h','strings-symbols.h'),('accelerator/guest/pure.c','pure.c'),('accelerator/guest/calls.S','aarch64/calls.S'),('accelerator/musl/init.c','init.c'),('accelerator/musl/symbols.h','symbols.h'),('accelerator/musl/callbacks.c','callbacks.c'),('accelerator/musl/callbacks.h','callbacks.h'),('accelerator/musl/callback-calls.S','aarch64/callback-calls.S'),('accelerator/musl/sort-symbols.h','sort-symbols.h'),('accelerator/musl/numeric.c','numeric.c'),('accelerator/musl/numeric-symbols.h','numeric-symbols.h'),('accelerator/musl/numeric-calls.S','aarch64/numeric-calls.S')]
inputs += allocator_inputs
for source,target in inputs:
    shutil.copyfile(ROOT/source,dest/target)
# Hash-check source and add only the native entry/continuation boundaries.
for name in ['malloc.c','free.c']:
    path=tree/'src/malloc/mallocng'/name
    data=path.read_bytes()
    assert data==(ROOT/'runtime/crates/trames-native/upstream/musl-1.2.5/mallocng'/name).read_bytes(),name
    before['src/malloc/mallocng/'+name]=hashlib.sha256(data).hexdigest()
    text=data.decode().replace('#include "meta.h"','#include "meta.h"\n#include "../../trames/allocator.h"',1)
    if name=='malloc.c':
        marker='\tsc = size_to_class(n);\n\n\trdlock();'
        assert text.count(marker)==1
        text=text.replace(marker,marker+'''
\tif (__trames_allocator_enabled && !__malloc_replaced && trames_allocator_layout_supported()) {
\t\tstruct trames_allocator_result r=trames_malloc_step(n,(uintptr_t)&ctx,TRAMES_ALLOCATOR_COOKIE);
\t\tif (!r.fallback) {
\t\t\tg=(struct meta *)(r.value & UINT64_C(0x0000ffffffffffff));
\t\t\tidx=(r.value>>48)&31; ctr=(r.value>>53)&255;
\t\t\tunlock();
\t\t\tuintptr_t state[5]={TRAMES_ALLOCATOR_COOKIE,n,(uintptr_t)g,idx,ctr};
\t\t\tr=trames_enframe_step((uintptr_t)state,0,0);
\t\t\tif (!r.fallback) return (void *)r.value;
\t\t\treturn enframe((struct meta *)state[2],state[3],n,state[4]);
\t\t}
\t}''',1)
    else:
        marker='\tif (!p) return;\n\n\tstruct meta *g = get_meta(p);\n\tint idx = get_slot_index(p);'
        assert text.count(marker)==1
        text=text.replace(marker,'''
\tif (!p) return;
\tstruct meta *g;
\tint idx;
\tif (__trames_allocator_enabled && !__malloc_replaced && trames_allocator_layout_supported()) {
\t\tuintptr_t state[4]={TRAMES_ALLOCATOR_COOKIE,MT,0,0};
\t\tstruct trames_allocator_result r=trames_free_step((uintptr_t)p,(uintptr_t)&ctx,(uintptr_t)state);
\t\tif (!r.fallback) {
\t\t\tif (!r.value) return;
\t\t\tg=(struct meta *)state[2]; idx=state[3];
\t\t\tgoto trames_native_slow;
\t\t}
\t}
\tg = get_meta(p);
\tidx = get_slot_index(p);''',1)
        start=text.index('void free(void *p)')
        marker='\twrlock();'
        text=text[:start]+text[start:].replace(marker,'trames_native_slow:\n'+marker,1)
    path.write_text(text)
make=tree/'Makefile'
with make.open('a') as f:
    f.write('\n# Trames pure profile: only these original objects are privately renamed.\n')
    for name in ['aarch64/memcpy','aarch64/memset','memmove','memcmp','strlen','memchr','memrchr','strnlen','strcmp','strncmp']:
        f.write(f'obj/src/string/{name}.o obj/src/string/{name}.lo: CPPFLAGS += -include $(srcdir)/src/trames/symbols.h\n')
    f.write('obj/src/trames/pure.o obj/src/trames/pure.lo: CPPFLAGS += -DTRAMES_FULL_MUSL\n')
    for name in ['qsort','bsearch']:
        f.write(f'obj/src/stdlib/{name}.o obj/src/stdlib/{name}.lo: CPPFLAGS += -include $(srcdir)/src/trames/sort-symbols.h\n')
    f.write('obj/src/stdlib/strtod.o obj/src/stdlib/strtod.lo: CPPFLAGS += -include $(srcdir)/src/trames/numeric-symbols.h\n')
    f.write('obj/src/stdlib/strtol.o obj/src/stdlib/strtol.lo: CPPFLAGS += -include $(srcdir)/src/trames/integer-symbols.h\n')
    for name in ['strchr','strrchr','strchrnul','strcpy','stpcpy','strncpy','stpncpy','strcat','strncat']:
        f.write(f'obj/src/string/{name}.o obj/src/string/{name}.lo: CPPFLAGS += -include $(srcdir)/src/trames/strings-symbols.h\n')
    f.write('obj/src/trames/integer.o obj/src/trames/integer.lo obj/src/trames/strings.o obj/src/trames/strings.lo: CFLAGS += -fno-stack-protector -fno-builtin\n')
    f.write('obj/src/trames/numeric.o obj/src/trames/numeric.lo: CFLAGS += -fno-stack-protector -fno-builtin\n')
    f.write('obj/src/trames/callbacks.o obj/src/trames/callbacks.lo: CFLAGS += -fno-stack-protector -fno-builtin\n')
    f.write('obj/src/trames/pure.o obj/src/trames/pure.lo obj/src/trames/init.o obj/src/trames/init.lo: CFLAGS += -fno-stack-protector -fno-builtin\n')
(tree/'trames-patch.json').write_text(json.dumps({'upstream':'musl-1.2.5','original_startup_sha256':before,'inputs_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in [s for s,_ in inputs]+['scripts/prepare-musl-hybrid.py']}},indent=2)+'\n')
