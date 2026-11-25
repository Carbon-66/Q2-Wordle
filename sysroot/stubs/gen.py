#!/usr/bin/env python3
"""Stub shared libraries to link q2-wordle against: each has the real library's soname and the
symbols the game imports, so lld writes the right NEEDED entries and dynamic relocations. On the
Q2, the rootfs's own glibc 2.28, libpthread and libdl resolve them. libcurl is not linked: net.c
opens it with dlopen, so a firmware without it loses only the Daily. Usage: gen.py OUTDIR

Versions matter (as in q2-doom and q2a-miami). An unversioned reference binds to a symbol's
oldest version, and on the Q2's MIPS glibc fopen, fclose and pthread_create have a GLIBC_2.0
compatibility version (libio's old FILE layout, LinuxThreads' pthread_create) beside the current
GLIBC_2.2 one. Bound to the old ones, stdio quietly breaks. So those carry GLIBC_2.2, as stock demo
imports them. Every other symbol here exists only at one version, which an unversioned reference
finds."""
import pathlib, subprocess, sys

LIBS = {
    'libc.so.6': {
        'data': ['stdout', 'stderr', 'stdin'],
        'versioned': {'GLIBC_2.2': ['fopen', 'fclose']},
        'text': '''__libc_start_main __errno_location access atoi bind chdir clock_gettime close
            exit fflush fgets fputc fputs fscanf getenv ioctl localtime_r memcpy memmove memset
            mmap open printf puts recv rename setvbuf signal snprintf socket sscanf strchr strcmp strlen
            strncmp strrchr strstr strcpy time unlink usleep vfprintf freopen fprintf bcmp memchr calloc free fwrite'''.split(),
    },
    'libpthread.so.0': {'data': [], 'versioned': {'GLIBC_2.2': ['pthread_create']},
                        'text': 'pthread_timedjoin_np'.split()},
    'libdl.so.2': {'data': [], 'text': 'dlopen dlsym dlerror'.split()},
}

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for soname, syms in LIBS.items():
    c = out / (soname + '.c')
    script = out / (soname + '.map')
    versioned = syms.get('versioned', {})
    script.write_text(''.join(f'{v} {{ global: {"; ".join(names)}; }};\n' for v, names in versioned.items()))
    c.write_text(''.join(f'void *{d} = 0;\n' for d in syms['data']) +
                 ''.join(f'void {t}(void) {{}}\n' for t in syms['text'] + sum(versioned.values(), [])))
    subprocess.run(['clang', '--target=mipsel-linux-gnu', '-march=mips32r2', '-mabi=32', '-mfp64',
                    '-mnan=2008', '-fPIC', '-nostdlib', '-shared', '-fuse-ld=lld', '-fno-builtin', '-w',
                    f'-Wl,-soname,{soname}', *([f'-Wl,--version-script={script}'] if versioned else []), '-o', out / soname, c],
                   check=True)
