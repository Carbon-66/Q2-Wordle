# Wordle for Q2

Wordle for the Shanling Q2 under [Q2 Pod](https://github.com/DiamondBond/q2-pod), as a full-screen
**card game** in Library → Games. It has two modes:

- **Daily Puzzle** gets today's New York Times word over Wi-Fi.
- **Random Word** picks from the word list built into the game and works offline.

The board, the keyboard's colours and the rules (duplicate letters, hard mode) follow the original
game. The input is built for the Q2: the wheel moves a highlight round the keyboard and Centre
types the highlighted key. You can also slide a finger to a key and lift to type it.

The player's guide (install, controls, settings) is [card/README.txt](card/README.txt), copied into
the card folder as `README.txt`.

## Building

```sh
./build.sh       # build/q2-wordle and dist/.wordle (copy .wordle to the card's root)
test/run.sh      # host tests: the rules, then the real main() on a fake Q2 panel
```

The build needs `clang`, `ld.lld` and `python3` with Pillow. `tools/font.py` bakes the fonts from
Clear Sans (`CLEAR_SANS_DIR`, [intel/clear-sans](https://github.com/intel/clear-sans)) and Bitter
(`BITTER_TTF`). `cacert.pem` is Mozilla's CA bundle (`CACERT`, which defaults to Python's
certifi). The binary is built as q2-doom, Q2A Miami, Challenger Deep and Vortex are, with no MIPS
sysroot. It matches the ABI of Q2 Pod's own helpers: MIPS32r2 little-endian, o32, FR=1, NaN2008,
glibc 2.28, interpreter `/lib/ld-linux-mipsn8.so.1`. `sysroot/stubs/gen.py` makes stub `libc.so.6`,
`libpthread.so.0` and `libdl.so.2` to link against. If you add a libc call, add it to `gen.py`
too.

The tests need the host's libcurl. The Daily's JSON comes from a temporary folder through
`file://` or a local HTTP server, so they need no network.

## How the Daily works

The NYT's own Wordle page loads `https://www.nytimes.com/svc/wordle/v2/YYYY-MM-DD.json`, whose
`solution` field is the day's word and whose `days_since_launch` is the puzzle number. This
endpoint is unofficial and may change. `src/net.c` fetches it on a thread:

- **libcurl** is the rootfs's own copy, the one Q2 Pod's scrobbling, lyrics and radio use. The
  game opens it with `dlopen` instead of linking it, so a firmware without it loses only the
  Daily.
- **Certificates** are checked against `cacert.pem` in the card folder, falling back to the
  firmware's `/etc/scrobble-ca.pem`. Certificate dates aren't checked, the same as Q2 Pod's
  `tls_untimed`, so a Q2 whose clock was never set can still connect.
- **The date** is the local one from the Q2's clock and the `TZ` Q2 Pod passes on. If the server's
  `Date` header is more than an hour off from the Q2's clock, the game asks again for the day it is
  by the server's clock.
- **Days ahead**: after today's word, the next 7 days are fetched and saved to `wordle.cache`, with
  each word shifted so a glance at the file gives nothing away. If Wi-Fi is down or asleep when a
  Daily starts, today's word comes from there.

Card games don't keep Smart Wi-Fi's radio awake (`addon_running` counts card apps, not games), so
the radio may be asleep when the game starts. The fetch fails after its 10 s connect timeout, and
the error page offers Try Again or a Random Word.

## Layout

| Path | What it is |
| --- | --- |
| `src/q2.c` | The platform: the portrait panel, Q2 Pod's input datagrams, the log, `main()` (Vortex's) |
| `src/game.c` | Screens, input, animations, saves, statistics and the loop |
| `src/logic.c` | Scoring a guess, the word lists' lookups, hard mode's checks |
| `src/net.c` | Today's word over libcurl, on a thread |
| `src/gfx.c` | Drawing: antialiased rounded boxes, lines, text plain and scaled |
| `data/` | `answers.txt` (2,309 words a Random game picks from) and `allowed.txt` (10,638 more accepted guesses), from [stuartpb/wordles](https://github.com/stuartpb/wordles) (public domain) |
| `tools/` | `font.py`, `words.py` (both write C into `build/`) and `icon.py` |
| `card/` | `q2app`, icons and the player's README |
| `test/` | Host build, fake framebuffer (`fbshim.c`), scripted play (`drive.py`), rule checks (`logic_test.c`) |

The screen is the 375 × 320 landscape view behind glass with 80 px rounded corners. Everything sits
inside that shape, so the keyboard's bottom row is narrower than the other two.

## Licences

Code: MIT ([LICENSE](LICENSE)). Clear Sans: Apache 2.0. Bitter: SIL Open Font License 1.1. Word
lists: public domain. `cacert.pem`: Mozilla Public License 2.0.

Wordle is a trademark of The New York Times Company. This is an unofficial fan port and is not
affiliated with or endorsed by it.
