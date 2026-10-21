#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the native GPU TF compiler against verified, unmodified SPIRV-Cross.

This is an internal GPU engine acceptance, not extension advertisement. Apple
acceptance executes captured vertex data and replay on Metal; Linux only checks
the original compiler and layout. No guest rootfs, Firefox data, or CPU JIT.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT/'crates/trames-vulkan/native/transform-feedback'
SOURCES = ('spirv_cross.cpp', 'spirv_parser.cpp', 'spirv_cross_parsed_ir.cpp',
           'spirv_cfg.cpp', 'spirv_glsl.cpp', 'spirv_msl.cpp')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'target/vulkan-native-tf')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--glslang', default='glslangValidator')
    args = parser.parse_args()
    out = args.output.resolve();out.mkdir(parents=True, exist_ok=True)
    item = json.loads((ROOT/'scripts/vulkan-upstream.json').read_text())['archives']['moltenvk-dependency-SPIRV-Cross.tar.gz']
    archive = args.archive or out/'SPIRV-Cross.tar.gz'
    if not archive.exists():
        partial = archive.with_suffix('.part')
        with urllib.request.urlopen(item['url'], timeout=90) as source, partial.open('wb') as target:
            while chunk := source.read(1 << 20):
                target.write(chunk)
        partial.replace(archive)
    assert digest(archive) == item['sha256'], 'Original SPIRV-Cross digest mismatch'
    source = out/'upstream';source.mkdir(exist_ok=True)
    with tarfile.open(archive) as files:
        for member in files:
            relative = Path(*Path(member.name).parts[1:])
            # The compiler needs only upstream's top-level source and headers.
            if member.isfile() and len(relative.parts) == 1:
                (source/relative).write_bytes(files.extractfile(member).read())
    header = source/'spirv_msl.hpp'
    original = header.read_text()
    updated = original.replace('std::string entry_point_args_classic(bool append_comma);',
                               'virtual std::string entry_point_args_classic(bool append_comma);')
    updated = updated.replace('std::string entry_point_args_argument_buffer(bool append_comma);',
                              'virtual std::string entry_point_args_argument_buffer(bool append_comma);')
    assert updated != original
    header.write_text(updated)
    compiler = out/'compiler'
    command = [os.environ.get('CXX', 'c++'), '-std=c++14', '-O2', '-DNDEBUG',
               '-Wall', '-Wextra', '-Werror', '-I'+str(source), '-I'+str(NATIVE),
               str(ROOT/'tests/fixtures/vulkan-native-tf-compiler.cpp'), str(NATIVE/'compiler.cpp'),
               *map(str, (source/name for name in SOURCES)), '-o', str(compiler)]
    # Compile unchanged original translation units once; later adapter edits do
    # not cause another full upstream rebuild. Bind objects to verified sources,
    # exact flags, host architecture and compiler version.
    version = subprocess.check_output([command[0], '--version'], text=True)
    identity = hashlib.sha256((item['sha256'] + digest(header) + version + platform.machine() + str(command[:9])).encode()).hexdigest()[:16]
    objects = out/('objects-'+identity);objects.mkdir(exist_ok=True)
    linked = []
    for src in [ROOT/'tests/fixtures/vulkan-native-tf-compiler.cpp', NATIVE/'compiler.cpp', *(source/name for name in SOURCES)]:
        key = hashlib.sha256((digest(src) + digest(NATIVE/'compiler.hpp')).encode()).hexdigest()[:16] if src.parent != source else digest(src)[:16]
        obj = objects/(src.stem+'-'+key+'.o')
        if not obj.exists():
            subprocess.run(command[:9] + ['-c', str(src), '-o', str(obj)], check=True)
        linked.append(str(obj))
    subprocess.run([command[0], *linked, '-o', str(compiler)], check=True)
    for name in ('vulkan-feedback', 'vulkan-native-tf'):
        shader = out/(name+'.spv')
        subprocess.run([args.glslang, '-V', '--target-env', 'vulkan1.0',
                        str(ROOT/('tests/fixtures/'+name+'.vert')), '-o', str(shader)], check=True)
        subprocess.run([str(compiler), str(shader), str(out/(name+'.metal')), str(out/(name+'.json'))], check=True)
    for index_type in ('16', '32'):
        name = 'vulkan-native-tf-u'+index_type
        subprocess.run([str(compiler), str(out/'vulkan-native-tf.spv'),
                        str(out/(name+'.metal')), str(out/(name+'.json')), index_type], check=True)
    # Only the adapter is new. Every original dependency remains untouched and
    # its archive/licenses are available alongside the acceptance source.
    inputs = [NATIVE/'compiler.hpp', NATIVE/'compiler.cpp', NATIVE/'assemble.metal', NATIVE/'metal.hpp', NATIVE/'metal.mm',
              ROOT/'tests/fixtures/vulkan-native-tf-metal.mm',
              ROOT/'tests/fixtures/vulkan-native-tf-compiler.cpp', ROOT/'tests/fixtures/vulkan-native-tf.vert',
              ROOT/'tests/fixtures/vulkan-feedback.vert', ROOT/'scripts/vulkan-upstream.json', Path(__file__)]
    record = dict(upstream_sha256=item['sha256'], compiler_sha256=digest(compiler),
                  inputs={str(p.relative_to(ROOT)): digest(p) for p in inputs},
                  shaders={p.name: digest(p) for p in out.iterdir() if p.suffix in ('.spv', '.metal', '.json') and p.name != 'build.json'},
                  command=command)
    (out/'build.json').write_text(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    main()
