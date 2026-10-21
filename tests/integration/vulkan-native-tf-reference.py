#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare TF assembly with the original native Vulkan driver, not a CPU model.

Derive a focused topology variant from the existing ordinary Vulkan fixture.
The original fixture stays unchanged; its default guest acceptance is reused.
Optional shader reuse verifies exact passed compiler inputs before rebinding
only new Metal engine inputs. No previous compilation is presented as a new one.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ENGINE = 'crates/trames-vulkan/native/transform-feedback/'
REBIND = {ENGINE+'assemble.metal', ENGINE+'metal.hpp', ENGINE+'metal.mm',
          'tests/fixtures/vulkan-native-tf-metal.mm'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'target/vulkan-native-tf')
    parser.add_argument('--reuse-shaders', type=Path)
    parser.add_argument('--reuse-run', type=int)
    parser.add_argument('--reuse-sha')
    parser.add_argument('--include', type=Path)
    parser.add_argument('--vulkan-library', default='-lvulkan')
    args = parser.parse_args();out = args.output.resolve();out.mkdir(parents=True, exist_ok=True)
    if args.reuse_shaders:
        assert args.reuse_run and args.reuse_sha
        previous = json.loads((args.reuse_shaders/'build.json').read_text())
        original_digest = digest(args.reuse_shaders/'build.json')
        assert previous['upstream_sha256'] == json.loads((ROOT/'scripts/vulkan-upstream.json').read_text())['archives']['moltenvk-dependency-SPIRV-Cross.tar.gz']['sha256']
        for name, expected in previous['inputs'].items():
            if name not in REBIND:
                assert digest(ROOT/name) == expected, ('reused compiler/source input changed', name)
        for name, expected in previous['shaders'].items():
            assert digest(args.reuse_shaders/name) == expected, ('shader input hash', name)
            shutil.copyfile(args.reuse_shaders/name, out/name)
        record = {**previous, 'inputs': dict(previous['inputs']), 'shaders': dict(previous['shaders'])}
        record['upstream_compilation_reused'] = dict(run=args.reuse_run, sha=args.reuse_sha,
            original_record_sha256=original_digest, original_inputs=previous['inputs'])
        for name in REBIND:
            record['inputs'][name] = digest(ROOT/name)
    else:
        record = json.loads((out/'build.json').read_text())
    fixture = ROOT/'tests/fixtures/vulkan-feedback.c'
    text = fixture.read_text()
    substitutions = {
        'assert(argc==2);': 'assert(argc==3);unsigned topology=(unsigned)strtoul(argv[2],NULL,10);',
        '.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST': '.topology=topology',
        'sizes[]={144,48}': 'sizes[]={960,320}',
        'vkCmdDraw(c,3,2,2,4)': 'vkCmdDraw(c,5,2,2,4)',
        'assert(captured[0]==6 && captured[1]==6 && captured[2]);':
            'assert(captured[0]==captured[1] && captured[2]);printf("tf-reference %u %llu\\n",topology,(unsigned long long)captured[0]);',
    }
    for old, new in substitutions.items():
        assert text.count(old) == 1, ('reference fixture drift', old)
        text = text.replace(old, new)
    begin = text.index('        for(unsigned i=0;i<6;i++){')
    end = text.index('        puts("guest-feedback-ok:', begin)
    text = text[:begin]+'''        unsigned vertices=topology==0?1:topology<=2?2:3;
        for(unsigned i=0;i<captured[0]*vertices;i++){
            float got[4];memcpy(got,allocations[1].data+16+i*24+4,16);
            assert(got[0]>=2 && got[0]<=6 && got[1]>=4 && got[1]<=5);
            printf(" %u",(unsigned)(got[1]-4)*5+(unsigned)(got[0]-2));
        }
        puts("");
'''+text[end:]
    source = out/'native-reference.c';source.write_text(text)
    compiler = [os.environ.get('CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror']
    if args.include:
        compiler += ['-I'+str(args.include.resolve())]
    command = [*compiler, str(source), args.vulkan_library, '-o', str(out/'native-reference')]
    subprocess.run(command, check=True)
    cases = []
    for topology in range(6):
        run = subprocess.run([str(out/'native-reference'), str(out/'vulkan-feedback.spv'), str(topology)],
                             check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        lines = run.stdout.splitlines();index = next(i for i, line in enumerate(lines) if line.startswith('tf-reference '))
        got_topology, primitives = map(int, lines[index].split()[1:]);assert got_topology == topology
        mapping = list(map(int, lines[index+1].split()))
        primitive_size = 1 if topology == 0 else 2 if topology <= 2 else 3
        assert len(mapping) == primitives*primitive_size
        cases.append(dict(topology=topology, primitives=primitives, vertices=len(mapping), map=mapping))
        print(f'native-vulkan-tf-reference-ok: topology={topology} primitives={primitives} map={mapping}', flush=True)
    result = out/'native-vulkan-reference.json'
    result.write_text(json.dumps(dict(kind='real native Vulkan driver; no CPU TF model', cases=cases,
        source_fixture_sha256=digest(fixture), generated_source_sha256=digest(source),
        binary_sha256=digest(out/'native-reference'), shader_sha256=digest(out/'vulkan-feedback.spv')), indent=2)+'\n')
    record['shaders'][result.name] = digest(result)
    record['inputs'][str(fixture.relative_to(ROOT))] = digest(fixture)
    record['inputs'][str(Path(__file__).relative_to(ROOT))] = digest(Path(__file__))
    (out/'build.json').write_text(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    main()
