#!/bin/sh
# host build of the whole game (x86-64 Linux), for the tests: the system's libcurl through dlopen
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
python3 tools/font.py build/font_data.c
python3 tools/words.py data build/words_data.c
clang -O2 -g -Wall -Wextra -Wno-unused-parameter -Werror -Isrc -pthread src/*.c build/font_data.c build/words_data.c -ldl \
    -o build/host/q2-wordle
clang -shared -fPIC -O1 -w test/fbshim.c -o build/host/fbshim.so -ldl
