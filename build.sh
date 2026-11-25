#!/bin/sh
# Builds q2-wordle for the Shanling Q2 (Ingenic X2000, MIPS32r2 little-endian, glibc 2.28, o32 with
# FR=1 and NaN2008, as Q2 Pod's own q2video and the other card games are built) and lays out the
# card folder.
#
#   ./build.sh            -> build/q2-wordle and dist/.wordle/ (copy .wordle to the card's root)
#
# Needs clang, ld.lld and python3 with Pillow, no MIPS sysroot: sysroot/include declares what the
# game uses, and the stub libraries made by sysroot/stubs/gen.py carry the real sonames and symbol
# versions, so on the Q2 the rootfs's own libc, libpthread and libdl are what run (and its libcurl,
# opened at run time). rt/ is glibc's crt1 and libgcc's 64-bit division. _IO_stdin_used is
# exported because glibc looks it up in the program: without it, glibc takes the program for a
# libc5-era one.
#
# The fonts are baked from Clear Sans and Bitter (tools/font.py: CLEAR_SANS_DIR, BITTER_TTF) and
# the word lists from data/ (tools/words.py). cacert.pem is Mozilla's roots (CACERT, by default
# Python's certifi bundle), for checking the New York Times' certificate.
set -e
cd "$(dirname "$0")"

TARGET="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008"
# -G0: no $gp-relative small data. glibc calls main with its own $gp (libc is PIC), so a
# gp-relative global in our non-PIC code would land in libc's memory.
COMMON="$TARGET -mabicalls -fno-pic -G0 -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections \
        -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-math-errno"
STRICT="-Wall -Wextra -Wno-unused-parameter -Werror"

rm -rf build/obj
mkdir -p build/obj
python3 sysroot/stubs/gen.py build/stubs
python3 tools/font.py build/font_data.c
python3 tools/words.py data build/words_data.c
for src in src/*.c build/font_data.c build/words_data.c; do
    clang $COMMON -nostdlibinc -isystem sysroot/include -Isrc $STRICT -c "$src" -o build/obj/$(basename "$src" .c).o
done
for src in rt/*.c; do
    clang $COMMON -nostdlibinc -isystem sysroot/include -w -c "$src" -o build/obj/rt_$(basename "$src" .c).o
done

ld.lld -m elf32ltsmip -e __start --dynamic-linker /lib/ld-linux-mipsn8.so.1 \
    --image-base=0x400000 -z noexecstack --gc-sections --hash-style=sysv --build-id=none -s \
    --export-dynamic-symbol=_IO_stdin_used \
    --as-needed build/obj/*.o build/stubs/libpthread.so.0 build/stubs/libdl.so.2 build/stubs/libc.so.6 \
    -o build/q2-wordle

CACERT=${CACERT:-$(python3 -c 'import certifi; print(certifi.where())')}
rm -rf dist
mkdir -p dist/.wordle
cp build/q2-wordle dist/.wordle/q2-wordle
cp card/q2app card/icon.png card/icon-40.png dist/.wordle/
cp card/README.txt dist/.wordle/README.txt
cp "$CACERT" dist/.wordle/cacert.pem
echo "built: build/q2-wordle ($(wc -c < build/q2-wordle) bytes), card folder: dist/.wordle"
