#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply two checked startup hooks; original fallback bytes stay unchanged."""
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
for source,target in [('accelerator/guest/pure.c','pure.c'),('accelerator/guest/calls.S','aarch64/calls.S'),('accelerator/musl/init.c','init.c'),('accelerator/musl/symbols.h','symbols.h')]:
    shutil.copyfile(ROOT/source,dest/target)
make=tree/'Makefile'
with make.open('a') as f:
    f.write('\n# Trames pure profile: only these original objects are privately renamed.\n')
    for name in ['aarch64/memcpy','aarch64/memset','memmove','memcmp','strlen','memchr','memrchr','strnlen','strcmp','strncmp']:
        f.write(f'obj/src/string/{name}.o obj/src/string/{name}.lo: CPPFLAGS += -include $(srcdir)/src/trames/symbols.h\n')
    f.write('obj/src/trames/pure.o obj/src/trames/pure.lo: CPPFLAGS += -DTRAMES_FULL_MUSL\n')
    f.write('obj/src/trames/pure.o obj/src/trames/pure.lo obj/src/trames/init.o obj/src/trames/init.lo: CFLAGS += -fno-stack-protector -fno-builtin\n')
(tree/'trames-patch.json').write_text(json.dumps({'upstream':'musl-1.2.5','original_startup_sha256':before,'inputs_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['accelerator/guest/pure.c','accelerator/guest/calls.S','accelerator/musl/init.c','accelerator/musl/symbols.h','scripts/prepare-musl-hybrid.py']}},indent=2)+'\n')
