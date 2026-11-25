#!/bin/sh
# Host tests for q2-wordle (x86-64 Linux, clang, python3 with Pillow, the system's libcurl): the
# real game and platform layer on fbshim.so's fake /dev/fb0, driven through Q2 Pod's input socket
# (test/drive.py). The Daily's word comes from JSON files over file:// or a local HTTP server, so
# no network is needed. Each scenario must end on TERM with status 0, the panel back on Q2 Pod's
# page and the socket gone. Frames land in test/out/*.png.
set -e
cd "$(dirname "$0")/.."
mkdir -p test/out
./test/hostbuild.sh > /dev/null
clang -O1 -Wall -Werror -Isrc test/logic_test.c src/logic.c build/words_data.c -o build/host/logic_test
./build/host/logic_test
run() { # scenario [env...]
    sc=$1; shift
    env "$@" python3 -I test/drive.py build/host test/out $sc > test/out/$sc.log 2>&1 || { cat test/out/$sc.log; exit 1; }
    grep -q "^exit status: 0" test/out/$sc.log || { echo "FAIL: $sc: exit status"; cat test/out/$sc.log; exit 1; }
    grep -q "^panel: yoffset 0 " test/out/$sc.log || { echo "FAIL: $sc: panel not handed back"; exit 1; }
    grep -q "^socket left: False" test/out/$sc.log || { echo "FAIL: $sc: socket left behind"; exit 1; }
    echo "$sc: ok"
}
res() { grep "^result: $2:" test/out/$1.log | sed 's/^result: [^:]*: //'; }
fail() { echo "FAIL: $*"; exit 1; }
run home
run random
run lose
run hard
run daily
run offline
run cached
run screens
run touch
run skew DRIVE_HTTP=1 DRIVE_SKEW_DAYS=2
cp test/out/daily.log test/out/daily-file.log
run daily DRIVE_HTTP=1
[ "$(res home home_sel)" = 1 ] && [ "$(res home home_sel_wrap)" = 4 ] || fail "home: the wheel"
[ "$(res random cursor_after_five)" = 19 ] || fail "random: the highlight didn't jump to Enter"
[ "$(res random rows_after_one)" = 1 ] && [ "$(res random toast_bad)" = "Not in word list" ] &&
    [ "$(res random toast_short)" = "Not enough letters" ] || fail "random: guesses"
[ "$(res random done)" = 1 ] && [ "$(res random rows)" = 3 ] && [ "$(res random played)" = 1 ] &&
    [ "$(res random won)" = 1 ] && [ "$(res random stats_over)" = 1 ] || fail "random: the win"
[ "$(res random new_rows)" = 0 ] && [ "$(res random new_done)" = 0 ] && [ "$(res random screen_after_back)" = 0 ] ||
    fail "random: new word, back"
[ "$(res lose done)" = 2 ] && [ "$(res lose toast)" = CRANE ] && [ "$(res lose won)" = 0 ] || fail "lose"
[ "$(res hard hard)" = 1 ] && [ "$(res hard toast_hard)" = "1st letter must be C" ] && [ "$(res hard rows)" = 1 ] ||
    fail "hard mode"
[ "$(res daily number)" = 1621 ] && [ "$(res daily answer)" = plumb ] && [ "$(res daily done)" = 1 ] &&
    [ "$(res daily cache_lines)" = 4 ] && [ "$(res daily screen_again)" = 2 ] || fail "daily over HTTP"
[ "$(res daily-file number)" = 1621 ] && [ "$(res daily-file cache_lines)" = 4 ] || fail "daily over file://"
[ "$(res offline mode)" = 1 ] && [ "$(res offline screen)" = 2 ] || fail "offline: to Random"
[ "$(res cached answer)" = ghost ] && [ "$(res cached number)" = 1700 ] || fail "cached: today's word offline"
[ "$(res skew answer)" = quart ] && [ "$(res skew number)" = 1623 ] || fail "skew: the server's date"
[ "$(res screens help_small)" = 12 ] && [ "$(res screens help_eased)" = 1 ] || fail "help: smooth wheel scroll"
[ "$(res screens dark)" = 1 ] && [ "$(res screens screen)" = 0 ] || fail "screens"
[ "$(res touch ncur)" = 1 ] && [ "$(res touch ncur_board)" = 1 ] && [ "$(res touch screen)" = 0 ] || fail "touch"
echo "all ok"
