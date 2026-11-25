/* Wordle's screens, input, saves and loop.
 *
 *   Home      Daily Puzzle, Random Word, How to Play, Statistics, Settings
 *   Fetch     getting today's word (net.c), or why it couldn't
 *   Play      the board and the keyboard; the stats panel over it once a game ends
 *   Help, Settings, Stats
 *
 * Input: the wheel moves a highlight round the keyboard (or a menu) and Centre presses it; a touch
 * on the keyboard lights the key under the finger, which follows a slide and types on lift; Return
 * deletes a letter, or goes back. Hold Return: Q2 Pod ends the game. Everything is saved as it
 * happens (wordle.sav), so a game left, or closed after Q2 Pod's two idle minutes, comes back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wd.h"

#define SAVE_FILE "wordle.sav"
#define CACHE_FILE "wordle.cache"
#define ROWS 6

/* timing, ms */
#define POP_MS 110
#define FLIP_MS 340
#define STAGGER_MS 260
#define SHAKE_MS 520
#define BOUNCE_MS 520
#define BOUNCE_STEP 100
#define TOAST_MS 1600
#define STATS_AFTER_MS 1500
#define WHEEL_STEP 20 /* raw wheel travel per highlight step: 10 a turn */

enum { MODE_DAILY, MODE_RANDOM };
enum { S_HOME, S_FETCH, S_PLAY, S_STATS, S_HELP, S_SETTINGS };

/* ---------------------------------------------------------------------------------------------
 * Colours */

typedef struct {
    uint32_t bg, text, line, empty, filled, correct, present, absent, key, key_text, key_absent, toast, toast_text,
        dim, focus;
} Theme;
static Theme th;

/* ---------------------------------------------------------------------------------------------
 * State */

typedef struct {
    char answer[6];
    char guess[ROWS][6];
    unsigned char mark[ROWS][5];
    int rows, done, hard; /* done: 0 playing, 1 won, 2 lost; hard: locked in at the first guess */
    char cur[6];
    int ncur;
    char date[11]; /* the Daily's */
    int number;
} Game;

typedef struct {
    int played, won, streak, best, dist[ROWS], last; /* last: the Daily's day index of the last win */
} Stats;

static struct {
    int hard, dark, contrast;
    Game g[2];
    Stats st[2];
    int rnd_a, rnd_b, rnd_i;
} sv;

static struct {
    int screen, mode, home_sel, set_sel, stats_tab, help_y, help_h;
    float help_pos, help_target; /* the help's scroll, eased toward where the wheel has sent it */
    int cursor, touch_key; /* the highlighted key; the key under a finger, -1 none */
    int wheel_acc;
    uint64_t pop_t[5], reveal_t, shake_t, bounce_t, toast_t, end_t;
    int reveal_row, shake_row;
    char toast[64];
    int toast_hold; /* stays until the stats panel */
    int stats_over; /* the stats panel over the board */
    int stats_auto; /* shown once the end's reveal is over */
    char fetch_err[160];
    int fetch_failed, fetch_sel;
    int today_y, today_m, today_d; /* the date the Daily is for, once known */
    int dirty, tick; /* tick: a clock on screen, redrawn each second */
} ui;

static uint64_t ms_now(void) { return now_us() / 1000; }

static void set_theme(void) {
    if (sv.dark)
        th = (Theme){ 0x121213, 0xf8f8f8, 0x3a3a3c, 0x3a3a3c, 0x565758, 0x538d4e, 0xb59f3b, 0x3a3a3c, 0x818384,
                      0xf8f8f8, 0x3a3a3c, 0xf8f8f8, 0x121213, 0x818384, 0xf8f8f8 };
    else
        th = (Theme){ 0xffffff, 0x1a1a1b, 0xd3d6da, 0xd3d6da, 0x878a8c, 0x6aaa64, 0xc9b458, 0x787c7e, 0xd3d6da,
                      0x1a1a1b, 0x787c7e, 0x1a1a1b, 0xffffff, 0x787c7e, 0x1a1a1b };
    if (sv.contrast) th.correct = 0xf5793a, th.present = 0x85c0f9;
}

/* ---------------------------------------------------------------------------------------------
 * Dates */

static int days_from_civil(int y, int m, int d) { /* days since 1970-01-01 */
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int date_index(const char *s) {
    int y, m, d;
    return sscanf(s, "%d-%d-%d", &y, &m, &d) == 3 ? days_from_civil(y, m, d) : 0;
}

/* the local date by the Q2's clock; 0 when the clock was never set */
static int clock_date(int *y, int *m, int *d) {
    time_t t = time(0);
    struct tm tm;
    localtime_r(&t, &tm);
    *y = tm.tm_year + 1900, *m = tm.tm_mon + 1, *d = tm.tm_mday;
    return *y >= 2025;
}

static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };
static const char *const DAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

static void long_date(int y, int m, int d, char *out, int n) {
    int wd = ((days_from_civil(y, m, d) % 7) + 11) % 7; /* 1970-01-01 was a Thursday */
    snprintf(out, (unsigned)n, "%s, %s %d, %d", DAYS[wd], MONTHS[m - 1], d, y);
}

/* ---------------------------------------------------------------------------------------------
 * Saves: key value lines. Words are kept shifted, so a look at the file gives nothing away. */

static void hide(const char *w, char *out) {
    for (int i = 0; i < 5; ++i) out[i] = w[i] >= 'a' && w[i] <= 'z' ? (char)('a' + (w[i] - 'a' + 7 + 3 * i) % 26) : 'a';
    out[5] = 0;
}

static void unhide(const char *w, char *out) {
    for (int i = 0; i < 5; ++i) out[i] = w[i] >= 'a' && w[i] <= 'z' ? (char)('a' + (w[i] - 'a' + 26 * 2 - 7 - 3 * i) % 26) : 'a';
    out[5] = 0;
}

static int valid_word(const char *w) {
    if (strlen(w) != 5) return 0;
    for (int i = 0; i < 5; ++i)
        if (w[i] < 'a' || w[i] > 'z') return 0;
    return 1;
}

static void game_write(FILE *f, const char *p, const Game *g) {
    char h[6];
    if (!valid_word(g->answer)) return;
    hide(g->answer, h);
    fprintf(f, "%s_word %s\n%s_done %d\n%s_hard %d\n%s_number %d\n%s_date %s\n", p, h, p, g->done, p, g->hard,
            p, g->number, p, g->date[0] ? g->date : "-");
    for (int r = 0; r < g->rows; ++r) fprintf(f, "%s_guess %s\n", p, g->guess[r]);
    if (g->ncur) fprintf(f, "%s_cur %s\n", p, g->cur);
}

static void stats_write(FILE *f, const char *p, const Stats *s) {
    fprintf(f, "%s_played %d\n%s_won %d\n%s_streak %d\n%s_best %d\n%s_last %d\n%s_dist %d %d %d %d %d %d\n", p,
            s->played, p, s->won, p, s->streak, p, s->best, p, s->last, p, s->dist[0], s->dist[1], s->dist[2],
            s->dist[3], s->dist[4], s->dist[5]);
}

static void save_write(void) {
    FILE *f = fopen(SAVE_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "hard %d\ndark %d\ncontrast %d\nrnd %d %d %d\n", sv.hard, sv.dark, sv.contrast, sv.rnd_a, sv.rnd_b,
            sv.rnd_i);
    game_write(f, "daily", &sv.g[MODE_DAILY]);
    game_write(f, "random", &sv.g[MODE_RANDOM]);
    stats_write(f, "daily", &sv.st[MODE_DAILY]);
    stats_write(f, "random", &sv.st[MODE_RANDOM]);
    fflush(f);
    fclose(f);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

static void save_read(void) {
    FILE *f = fopen(SAVE_FILE, "r");
    char line[128], key[40], val[80];
    while (f && fgets(line, sizeof line, f)) {
        val[0] = 0;
        if (sscanf(line, "%39s %79[^\n]", key, val) < 1) continue;
        int v = atoi(val);
        if (!strcmp(key, "hard")) sv.hard = v;
        else if (!strcmp(key, "dark")) sv.dark = v;
        else if (!strcmp(key, "contrast")) sv.contrast = v;
        else if (!strcmp(key, "rnd")) sscanf(val, "%d %d %d", &sv.rnd_a, &sv.rnd_b, &sv.rnd_i);
        int mode = !strncmp(key, "daily_", 6) ? MODE_DAILY : !strncmp(key, "random_", 7) ? MODE_RANDOM : -1;
        if (mode < 0) continue;
        const char *k = strchr(key, '_') + 1;
        Game *g = &sv.g[mode];
        Stats *s = &sv.st[mode];
        if (!strcmp(k, "word") && strlen(val) == 5) unhide(val, g->answer);
        else if (!strcmp(k, "done")) g->done = v;
        else if (!strcmp(k, "hard")) g->hard = v;
        else if (!strcmp(k, "number")) g->number = v;
        else if (!strcmp(k, "date") && strlen(val) == 10) memcpy(g->date, val, 11);
        else if (!strcmp(k, "guess") && g->rows < ROWS && valid_word(val)) memcpy(g->guess[g->rows++], val, 6);
        else if (!strcmp(k, "cur") && strlen(val) < 6) memcpy(g->cur, val, strlen(val) + 1), g->ncur = (int)strlen(val);
        else if (!strcmp(k, "played")) s->played = v;
        else if (!strcmp(k, "won")) s->won = v;
        else if (!strcmp(k, "streak")) s->streak = v;
        else if (!strcmp(k, "best")) s->best = v;
        else if (!strcmp(k, "last")) s->last = v;
        else if (!strcmp(k, "dist"))
            sscanf(val, "%d %d %d %d %d %d", &s->dist[0], &s->dist[1], &s->dist[2], &s->dist[3], &s->dist[4], &s->dist[5]);
    }
    if (f) fclose(f);
    for (int m = 0; m < 2; ++m) {
        Game *g = &sv.g[m];
        if (!valid_word(g->answer)) {
            memset(g, 0, sizeof *g);
            continue;
        }
        for (int r = 0; r < g->rows; ++r) score(g->guess[r], g->answer, g->mark[r]);
        if (g->done) g->ncur = 0, g->cur[0] = 0;
    }
}

/* wordle.cache: "YYYY-MM-DD number word" lines, the Daily's words fetched ahead */
static int cache_find(const char *date, char word[6], int *number) {
    FILE *f = fopen(CACHE_FILE, "r");
    char d[16], w[16];
    int n, found = 0;
    while (f && fscanf(f, "%15s %d %15s", d, &n, w) == 3)
        if (!strcmp(d, date) && strlen(w) == 5) unhide(w, word), *number = n, found = 1;
    if (f) fclose(f);
    return found;
}

/* keeps today's and later entries, adds or replaces these */
static void cache_store(const char dates[][11], const char words[][6], const int *numbers, int n, int today) {
    char keep[40][11], kw[40][6];
    int kn[40], k = 0;
    FILE *f = fopen(CACHE_FILE, "r");
    char d[16], w[16];
    int num;
    while (f && k < 32 && fscanf(f, "%15s %d %15s", d, &num, w) == 3) {
        if (strlen(d) != 10 || strlen(w) != 5 || date_index(d) < today) continue;
        int dup = 0;
        for (int i = 0; i < n; ++i) dup |= !strcmp(dates[i], d);
        if (dup) continue;
        memcpy(keep[k], d, 11), memcpy(kw[k], w, 6), kn[k] = num, ++k;
    }
    if (f) fclose(f);
    f = fopen(CACHE_FILE ".tmp", "w");
    if (!f) return;
    for (int i = 0; i < k; ++i) fprintf(f, "%s %d %s\n", keep[i], kn[i], kw[i]);
    for (int i = 0; i < n; ++i) {
        char h[6];
        hide(words[i], h);
        fprintf(f, "%s %d %s\n", dates[i], numbers[i], h);
    }
    fclose(f);
    rename(CACHE_FILE ".tmp", CACHE_FILE);
}

/* ---------------------------------------------------------------------------------------------
 * Layout */

/* The Q2's glass has 80 px rounded corners: nothing that matters goes outside a 375 x 320 box
 * rounded that much. So the keyboard's bottom row is narrower than the others, as it fits. */
#define TILE 26
#define TGAP 3
#define BOARD_X ((VIEW_W - (5 * TILE + 4 * TGAP)) / 2)
#define BOARD_Y 29
#define KB_Y 205
#define KEY_H 29
#define KEY_GAP 4
#define KEY_W 33
#define ROW3_W 31 /* the bottom row: letters, gaps and the two wide keys */
#define ROW3_GAP 3
#define WIDE_W 42
#define HEAD_H 24

enum { KEY_ENTER = 19, KEY_BACK = 27, NKEYS = 28 };
static const char KEYS[] = "qwertyuiopasdfghjkl?zxcvbnm?";

static void key_rect(int i, int *x, int *y, int *w, int *h) {
    *h = KEY_H;
    if (i < 10) *y = KB_Y, *w = KEY_W, *x = (VIEW_W - (10 * KEY_W + 9 * KEY_GAP)) / 2 + i * (KEY_W + KEY_GAP);
    else if (i < 19)
        *y = KB_Y + KEY_H + 3, *w = KEY_W, *x = (VIEW_W - (9 * KEY_W + 8 * KEY_GAP)) / 2 + (i - 10) * (KEY_W + KEY_GAP);
    else {
        int x0 = (VIEW_W - (2 * WIDE_W + 7 * ROW3_W + 8 * ROW3_GAP)) / 2;
        *y = KB_Y + 2 * (KEY_H + 3);
        if (i == KEY_ENTER) *x = x0, *w = WIDE_W;
        else if (i == KEY_BACK) *x = x0 + WIDE_W + ROW3_GAP + 7 * (ROW3_W + ROW3_GAP), *w = WIDE_W;
        else *x = x0 + WIDE_W + ROW3_GAP + (i - 20) * (ROW3_W + ROW3_GAP), *w = ROW3_W;
    }
}

/* the key at a touch, a little forgiving at the gaps; -1 none */
static int key_at(int tx, int ty) {
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < NKEYS; ++i) {
        int x, y, w, h;
        key_rect(i, &x, &y, &w, &h);
        int dx = tx < x ? x - tx : tx >= x + w ? tx - (x + w - 1) : 0;
        int dy = ty < y ? y - ty : ty >= y + h ? ty - (y + h - 1) : 0;
        int d = dx * dx + dy * dy;
        if (d < bd) bd = d, best = i;
    }
    return bd <= 6 * 6 ? best : -1;
}

/* ---------------------------------------------------------------------------------------------
 * Games */

static Game *G(void) { return &sv.g[ui.mode]; }

static void toast(const char *s, int hold) {
    snprintf(ui.toast, sizeof ui.toast, "%s", s);
    ui.toast_t = ms_now();
    ui.toast_hold = hold;
}

static void new_game(int mode, const char *word, const char *date, int number) {
    Game *g = &sv.g[mode];
    memset(g, 0, sizeof *g);
    memcpy(g->answer, word, 6);
    if (date) memcpy(g->date, date, 11);
    g->number = number;
    ui.reveal_t = ui.shake_t = ui.bounce_t = ui.end_t = 0;
    ui.toast[0] = 0;
    ui.toast_hold = 0;
    ui.stats_over = ui.stats_auto = 0;
    save_write();
}

static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }

/* each answer once before any comes again: index = a * i + b mod N, a coprime to N */
static void random_word(char out[6]) {
    int n = answer_count();
    const char *test = getenv("WD_RANDOM_WORD"); /* host tests */
    if (test && valid_word(test)) {
        memcpy(out, test, 6);
        return;
    }
    if (sv.rnd_a <= 0 || sv.rnd_a >= n || gcd(sv.rnd_a, n) != 1 || sv.rnd_i >= n) {
        unsigned seed = (unsigned)time(0) ^ (unsigned)now_us() * 2654435761u;
        do sv.rnd_a = 1 + (int)((seed = seed * 1103515245u + 12345u) >> 8) % (n - 1);
        while (gcd(sv.rnd_a, n) != 1);
        sv.rnd_b = (int)((seed = seed * 1103515245u + 12345u) >> 8) % n;
        sv.rnd_i = 0;
    }
    int idx = (int)(((long long)sv.rnd_a * sv.rnd_i + sv.rnd_b) % n);
    ++sv.rnd_i;
    answer_at(idx, out);
}

static void start_random(int fresh) {
    ui.mode = MODE_RANDOM;
    if (fresh || !valid_word(sv.g[MODE_RANDOM].answer)) {
        char w[6];
        random_word(w);
        new_game(MODE_RANDOM, w, 0, 0);
        printf("random: a new word (%d of the list)\n", sv.rnd_i);
    }
    ui.screen = S_PLAY;
    ui.stats_over = sv.g[MODE_RANDOM].done ? 1 : 0;
}

/* revealed rows: during a reveal the last row still shows its old face on the keyboard */
static int revealed_rows(const Game *g) {
    uint64_t t = ms_now();
    if (ui.reveal_t && t < ui.reveal_t + 4 * STAGGER_MS + FLIP_MS) return g->rows - 1;
    return g->rows;
}

static int key_state(const Game *g, char ch) {
    int best = L_NONE, n = revealed_rows(g);
    for (int r = 0; r < n; ++r)
        for (int i = 0; i < 5; ++i)
            if (g->guess[r][i] == ch && g->mark[r][i] > best) best = g->mark[r][i];
    return best;
}

static int busy(void) { /* a reveal running: no typing */
    return ui.reveal_t && ms_now() < ui.reveal_t + 4 * STAGGER_MS + FLIP_MS;
}

static void finish(Game *g) {
    Stats *s = &sv.st[ui.mode];
    int win = g->done == 1;
    ++s->played;
    if (win) {
        ++s->won, ++s->dist[g->rows - 1];
        if (ui.mode == MODE_DAILY) {
            int day = date_index(g->date);
            s->streak = s->last && day == s->last + 1 ? s->streak + 1 : 1;
            s->last = day;
        } else ++s->streak;
        if (s->streak > s->best) s->best = s->streak;
    } else s->streak = 0;
    printf("%s: %s in %d\n", ui.mode == MODE_DAILY ? "daily" : "random", win ? "won" : "lost", g->rows);
}

static void submit(void) {
    Game *g = G();
    char msg[64];
    if (g->done || busy()) return;
    if (g->ncur < 5) {
        toast("Not enough letters", 0), ui.shake_t = ms_now(), ui.shake_row = g->rows;
        return;
    }
    if (strcmp(g->cur, g->answer) && !word_ok(g->cur)) {
        toast("Not in word list", 0), ui.shake_t = ms_now(), ui.shake_row = g->rows;
        return;
    }
    if (g->rows == 0) g->hard = sv.hard;
    if (g->hard && hard_check((const char (*)[6])g->guess, (const unsigned char (*)[5])g->mark, g->rows, g->cur, msg, sizeof msg)) {
        toast(msg, 0), ui.shake_t = ms_now(), ui.shake_row = g->rows;
        return;
    }
    memcpy(g->guess[g->rows], g->cur, 6);
    score(g->cur, g->answer, g->mark[g->rows]);
    ui.reveal_row = g->rows++;
    ui.reveal_t = ms_now();
    g->ncur = 0, g->cur[0] = 0;
    ui.toast[0] = 0;
    if (!strcmp(g->guess[ui.reveal_row], g->answer)) g->done = 1;
    else if (g->rows == ROWS) g->done = 2;
    if (g->done) finish(g), ui.end_t = ui.reveal_t + 4 * STAGGER_MS + FLIP_MS, ui.stats_auto = 1;
    save_write();
}

static void type_key(int k) {
    Game *g = G();
    if (g->done) {
        ui.stats_over = 1;
        return;
    }
    if (busy()) return;
    if (k == KEY_ENTER) submit();
    else if (k == KEY_BACK) {
        if (g->ncur) g->cur[--g->ncur] = 0;
    } else if (g->ncur < 5) {
        g->cur[g->ncur] = KEYS[k];
        ui.pop_t[g->ncur] = ms_now();
        g->cur[++g->ncur] = 0;
    }
    ui.dirty = 1;
}

/* ---------------------------------------------------------------------------------------------
 * The Daily */

static void open_daily_for(int y, int m, int d, const char *word, int number) {
    char date[11];
    snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
    ui.today_y = y, ui.today_m = m, ui.today_d = d;
    ui.mode = MODE_DAILY;
    Game *g = &sv.g[MODE_DAILY];
    if (strcmp(g->date, date) || !valid_word(g->answer)) new_game(MODE_DAILY, word, date, number);
    else if (number && !g->number) g->number = number;
    ui.screen = S_PLAY;
    ui.stats_over = g->done ? 1 : 0;
}

static void start_daily(void) {
    int y, m, d;
    int clock_ok = clock_date(&y, &m, &d);
    char date[11];
    snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
    Game *g = &sv.g[MODE_DAILY];
    /* today's game, begun or done, needs no network */
    if (clock_ok && !strcmp(g->date, date) && valid_word(g->answer)) {
        open_daily_for(y, m, d, g->answer, g->number);
        return;
    }
    ui.screen = S_FETCH;
    ui.fetch_failed = 0, ui.fetch_sel = 0;
    net_start(y, m, d);
}

static void fetch_poll(void) {
    int st = net_state();
    if (st == NET_OK && ui.screen == S_FETCH) {
        char w[6];
        int n, y, m, d;
        long server;
        net_result(w, &n, &y, &m, &d, &server);
        printf("daily: No. %d for %04d-%02d-%02d\n", n, y, m, d);
        char dates[1][11];
        char words[1][6];
        snprintf(dates[0], 11, "%04d-%02d-%02d", y, m, d);
        memcpy(words[0], w, 6);
        cache_store((const char (*)[11])dates, (const char (*)[6])words, &n, 1, days_from_civil(y, m, d));
        open_daily_for(y, m, d, w, n);
        ui.dirty = 1;
    } else if (st == NET_FAIL && ui.screen == S_FETCH && !ui.fetch_failed) {
        int y, m, d, n;
        char w[6], date[11];
        snprintf(ui.fetch_err, sizeof ui.fetch_err, "%s", net_error());
        printf("daily: %s\n", ui.fetch_err);
        /* offline: a word fetched on an earlier day for today */
        if (clock_date(&y, &m, &d)) {
            snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
            if (cache_find(date, w, &n)) {
                printf("daily: today's word from the cache\n");
                open_daily_for(y, m, d, w, n);
                toast("Offline: a word saved earlier", 0);
                ui.dirty = 1;
                return;
            }
        } else
            snprintf(ui.fetch_err + strlen(ui.fetch_err), sizeof ui.fetch_err - strlen(ui.fetch_err),
                     " The Q2's clock isn't set, either.");
        ui.fetch_failed = 1;
        ui.dirty = 1;
    }
    /* the days ahead, once the run is over */
    static int stored;
    if (st != NET_OK) stored = 0;
    if (st == NET_OK && net_finished() && !stored) {
        stored = 1;
        char dates[8][11], words[8][6];
        int nums[8], k = 0;
        while (k < 8 && net_ahead(k, words[k], &nums[k], dates[k])) ++k;
        if (k) {
            int y, m, d;
            long server;
            char w[6];
            int n;
            net_result(w, &n, &y, &m, &d, &server);
            cache_store((const char (*)[11])dates, (const char (*)[6])words, nums, k, days_from_civil(y, m, d));
            printf("daily: %d days ahead saved for offline play\n", k);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Drawing: the board */

static uint32_t mark_colour(int m) { return m == L_CORRECT ? th.correct : m == L_PRESENT ? th.present : th.absent; }

static float ease_out(float t) { return 1 - (1 - t) * (1 - t); }

/* one tile: face 0 empty/typed, else a mark; sy the flip's height 0-256; pop a scale 256 = 1:1 */
static void draw_tile(float cx, float cy, char ch, int face, int sy, int pop, int typed) {
    float w = (float)TILE * pop / 256, h = (float)TILE * pop / 256 * sy / 256;
    float x = cx - w / 2, y = cy - h / 2;
    char s[2] = { (char)(ch - 32), 0 };
    if (face) {
        rect_a((int)(x + 0.5f), (int)(y + 0.5f), (int)(w + 0.5f), (int)(h + 0.5f), mark_colour(face), 256);
        if (ch) text_scaled(F_TILE, s, cx, cy, pop, pop * sy / 256, 0xffffff, 256);
    } else {
        int bx = (int)(x + 0.5f), by = (int)(y + 0.5f), bw = (int)(w + 0.5f), bh = (int)(h + 0.5f);
        rect(bx, by, bw, bh, typed ? th.filled : th.empty);
        if (bw > 4 && bh > 4) rect(bx + 2, by + 2, bw - 4, bh - 4, th.bg);
        if (ch) text_scaled(F_TILE, s, cx, cy, pop, pop * sy / 256, th.text, 256);
    }
}

static int draw_board(uint64_t t) {
    Game *g = G();
    int anim = 0;
    for (int r = 0; r < ROWS; ++r) {
        float dx = 0;
        if (ui.shake_t && r == ui.shake_row && t < ui.shake_t + SHAKE_MS) {
            float k = (float)(int)(t - ui.shake_t) / SHAKE_MS;
            dx = 6 * (1 - k) * wsin(k * 6.2831853f * 4);
            anim = 1;
        }
        for (int i = 0; i < 5; ++i) {
            float cx = BOARD_X + i * (TILE + TGAP) + TILE / 2.0f + dx;
            float cy = BOARD_Y + r * (TILE + TGAP) + TILE / 2.0f;
            if (r < g->rows) {
                int face = g->mark[r][i], sy = 256;
                if (ui.reveal_t && r == ui.reveal_row) {
                    int64_t e = (int64_t)(t - ui.reveal_t) - i * STAGGER_MS;
                    if (e < FLIP_MS) {
                        anim = 1;
                        if (e < 0) face = 0, sy = 256;
                        else if (e < FLIP_MS / 2) face = 0, sy = 256 - (int)(256 * e / (FLIP_MS / 2));
                        else sy = (int)(256 * (e - FLIP_MS / 2) / (FLIP_MS / 2));
                    }
                }
                if (ui.bounce_t && g->done == 1 && r == g->rows - 1) {
                    int64_t e = (int64_t)(t - ui.bounce_t) - i * BOUNCE_STEP;
                    if (e < BOUNCE_MS) anim = 1;
                    if (e > 0 && e < BOUNCE_MS) cy -= 12 * wsin(3.14159265f * (float)(int)e / BOUNCE_MS) * (1 - (float)(int)e / BOUNCE_MS / 2);
                }
                draw_tile(cx, cy, g->guess[r][i], face, sy < 2 ? 2 : sy, 256, 1);
            } else if (r == g->rows && !g->done) {
                char ch = i < g->ncur ? g->cur[i] : 0;
                int pop = 256;
                if (ch && ui.pop_t[i] && t < ui.pop_t[i] + POP_MS) {
                    float k = (float)(int)(t - ui.pop_t[i]) / POP_MS;
                    pop = 256 + (int)(28 * wsin(3.14159265f * ease_out(k)));
                    anim = 1;
                }
                draw_tile(cx, cy, ch, 0, 256, pop, ch != 0);
            } else draw_tile(cx, cy, 0, 0, 256, 256, 0);
        }
    }
    return anim;
}

static void backspace_icon(float cx, float cy, uint32_t c) {
    float w = 18, h = 12, x0 = cx - w / 2 + 2, y0 = cy - h / 2;
    line(x0 - 5, cy, x0, y0, 1.6f, c, 256);
    line(x0 - 5, cy, x0, y0 + h, 1.6f, c, 256);
    line(x0, y0, x0 + w - 2, y0, 1.6f, c, 256);
    line(x0, y0 + h, x0 + w - 2, y0 + h, 1.6f, c, 256);
    line(x0 + w - 2, y0, x0 + w - 2, y0 + h, 1.6f, c, 256);
    line(x0 + 4, y0 + 3, x0 + 10, y0 + h - 3, 1.6f, c, 256);
    line(x0 + 10, y0 + 3, x0 + 4, y0 + h - 3, 1.6f, c, 256);
}

static void draw_keyboard(void) {
    Game *g = G();
    for (int i = 0; i < NKEYS; ++i) {
        int x, y, w, h;
        key_rect(i, &x, &y, &w, &h);
        int st = i == KEY_ENTER || i == KEY_BACK ? L_NONE : key_state(g, KEYS[i]);
        uint32_t bg = st == L_NONE ? th.key : st == L_ABSENT ? th.key_absent : mark_colour(st);
        uint32_t fg = st == L_NONE ? th.key_text : 0xffffff;
        int lit = i == ui.touch_key;
        if (lit) bg = mix(bg, th.text, 60);
        rrect((float)x, (float)y, (float)w, (float)h, 4, bg, 256);
        if (i == KEY_ENTER) text(F_SMALL, "ENTER", x + w / 2, y + h / 2 + 4, fg, 0);
        else if (i == KEY_BACK) backspace_icon(x + w / 2.0f, y + h / 2.0f, fg);
        else {
            char s[2] = { (char)(KEYS[i] - 32), 0 };
            text(F_KEY, s, x + w / 2, y + h / 2 + fonts[F_KEY]->cap / 2, fg, 0);
        }
        if (i == ui.cursor && ui.touch_key < 0 && !g->done) {
            rrect(x - 2.5f, y - 2.5f, w + 5.0f, h + 5.0f, 6, th.focus, 256);
            rrect(x - 0.5f, y - 0.5f, w + 1.0f, h + 1.0f, 4.5f, th.bg, 256);
            rrect((float)x + 1, (float)y + 1, (float)w - 2, (float)h - 2, 3, bg, 256);
            if (i == KEY_ENTER) text(F_SMALL, "ENTER", x + w / 2, y + h / 2 + 4, fg, 0);
            else if (i == KEY_BACK) backspace_icon(x + w / 2.0f, y + h / 2.0f, fg);
            else {
                char s[2] = { (char)(KEYS[i] - 32), 0 };
                text(F_KEY, s, x + w / 2, y + h / 2 + fonts[F_KEY]->cap / 2, fg, 0);
            }
        }
    }
    /* the key under a finger, magnified above it */
    if (ui.touch_key >= 0 && ui.touch_key != KEY_ENTER && ui.touch_key != KEY_BACK) {
        int x, y, w, h;
        key_rect(ui.touch_key, &x, &y, &w, &h);
        int st = key_state(g, KEYS[ui.touch_key]);
        uint32_t bg = st == L_NONE ? th.key : st == L_ABSENT ? th.key_absent : mark_colour(st);
        uint32_t fg = st == L_NONE ? th.key_text : 0xffffff;
        int bw = 44, bh = 46, bx = x + w / 2 - bw / 2, by = y - bh - 4;
        if (bx < 2) bx = 2;
        if (bx + bw > VIEW_W - 2) bx = VIEW_W - 2 - bw;
        rrect((float)bx - 1, (float)by - 1, (float)bw + 2, (float)bh + 2, 7, th.text, 70);
        rrect((float)bx, (float)by, (float)bw, (float)bh, 6, bg, 256);
        char s[2] = { (char)(KEYS[ui.touch_key] - 32), 0 };
        text(F_TILE, s, bx + bw / 2, by + bh / 2 + fonts[F_TILE]->cap / 2, fg, 0);
    }
}

static void chevron_left(float x, float y, uint32_t c) {
    line(x + 5, y - 6, x, y, 2, c, 256);
    line(x, y, x + 5, y + 6, 2, c, 256);
}

static void stats_icon(float x, float y, uint32_t c) { /* three bars, bottom at y */
    rect((int)x, (int)y - 7, 3, 7, c);
    rect((int)x + 5, (int)y - 13, 3, 13, c);
    rect((int)x + 10, (int)y - 10, 3, 10, c);
}

static void draw_header(const char *right) {
    rect(0, HEAD_H, VIEW_W, 1, th.line);
    chevron_left(56, 13, th.text);
    text(F_HEAD, "Wordle", VIEW_W / 2, 19, th.text, 0);
    stats_icon(VIEW_W - 70, 19, th.text);
    if (right) text(F_SMALL, right, VIEW_W - 78, 17, th.dim, 1);
}

static int draw_toast(uint64_t t) {
    if (!ui.toast[0]) return 0;
    int age = (int)(t - ui.toast_t), alpha = 256;
    if (!ui.toast_hold) {
        if (age > TOAST_MS + 300) {
            ui.toast[0] = 0;
            return 0;
        }
        if (age > TOAST_MS) alpha = 256 - (age - TOAST_MS) * 256 / 300;
    }
    /* over the header, the one place the board never needs: the glass leaves it about 250 px wide */
    int w = text_w(F_BOLD, ui.toast) + 24;
    rrect((float)(VIEW_W - w) / 2, 1, (float)w, 24, 4, th.toast, alpha);
    uint32_t c = alpha >= 256 ? th.toast_text : mix(mix(th.bg, th.toast, alpha), th.toast_text, alpha);
    text(F_BOLD, ui.toast, VIEW_W / 2, 18, c, 0);
    return 1;
}

/* ---------------------------------------------------------------------------------------------
 * Drawing: the other screens */

static void pill(int x, int y, int w, int h, int focused, const char *label, const char *sub) {
    if (focused) rrect((float)x, (float)y, (float)w, (float)h, h / 2.0f, th.text, 256);
    else {
        rrect((float)x, (float)y, (float)w, (float)h, h / 2.0f, th.text, 256);
        rrect((float)x + 1.5f, (float)y + 1.5f, (float)w - 3, (float)h - 3, h / 2.0f - 1.5f, th.bg, 256);
    }
    uint32_t fg = focused ? th.bg : th.text;
    if (sub) {
        text(F_BOLD, label, x + w / 2, y + h / 2 - 2, fg, 0);
        text(F_SMALL, sub, x + w / 2, y + h / 2 + 12, focused ? mix(th.bg, th.text, 70) : th.dim, 0);
    } else text(F_BOLD, label, x + w / 2, y + h / 2 + 5, fg, 0);
}

static void logo(int cx, int y) { /* a little board: five tiles, two hinted */
    static const int face[5] = { L_ABSENT, L_PRESENT, L_ABSENT, L_CORRECT, L_CORRECT };
    int s = 18, gap = 3, x = cx - (5 * s + 4 * gap) / 2;
    for (int i = 0; i < 5; ++i) {
        rect(x + i * (s + gap), y, s, s, mark_colour(face[i]));
    }
    for (int i = 0; i < 5; ++i) {
        int bx = x + i * (s + gap), by = y + s + gap;
        rect(bx, by, s, s, th.filled);
        rect(bx + 2, by + 2, s - 4, s - 4, th.bg);
    }
}

static void daily_sub(char *out, int n) {
    int y, m, d;
    Game *g = &sv.g[MODE_DAILY];
    char date[11];
    int ok = clock_date(&y, &m, &d);
    snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
    if (ok && !strcmp(g->date, date) && valid_word(g->answer)) {
        if (g->done == 1) snprintf(out, (unsigned)n, "Solved in %d/6", g->rows);
        else if (g->done == 2) snprintf(out, (unsigned)n, "Done for today: X/6");
        else if (g->rows) snprintf(out, (unsigned)n, "In progress: %d/6", g->rows);
        else if (g->number) snprintf(out, (unsigned)n, "No. %d", g->number);
        else snprintf(out, (unsigned)n, "Not started");
    } else snprintf(out, (unsigned)n, "Today's word, over Wi-Fi");
}

static void draw_home(void) {
    char sub[48], sub2[48], date[48];
    logo(VIEW_W / 2, 14);
    text(F_LOGO, "Wordle", VIEW_W / 2, 96, th.text, 0);
    text(F_TEXT, "Get 6 chances to guess a 5-letter word.", VIEW_W / 2, 116, th.text, 0);
    daily_sub(sub, sizeof sub);
    Game *r = &sv.g[MODE_RANDOM];
    if (valid_word(r->answer) && !r->done && r->rows) snprintf(sub2, sizeof sub2, "In progress: %d/6", r->rows);
    else snprintf(sub2, sizeof sub2, "Any time, no Wi-Fi needed");
    pill(VIEW_W / 2 - 100, 128, 200, 38, ui.home_sel == 0, "Daily Puzzle", sub);
    pill(VIEW_W / 2 - 100, 172, 200, 38, ui.home_sel == 1, "Random Word", sub2);
    pill(VIEW_W / 2 - 176, 220, 112, 30, ui.home_sel == 2, "How to Play", 0);
    pill(VIEW_W / 2 - 56, 220, 112, 30, ui.home_sel == 3, "Statistics", 0);
    pill(VIEW_W / 2 + 64, 220, 112, 30, ui.home_sel == 4, "Settings", 0);
    int y, m, d;
    if (clock_date(&y, &m, &d)) {
        long_date(y, m, d, date, sizeof date);
        text(F_BOLD, date, VIEW_W / 2, 280, th.text, 0);
    }
    text(F_SMALL, "Wheel: Choose  Centre: Open", VIEW_W / 2, 302, th.dim, 0);
}

static int spinner(float cx, float cy, uint64_t t) {
    for (int i = 0; i < 8; ++i) {
        float a = i * 6.2831853f / 8;
        int lit = (int)((t / 90) % 8);
        int k = (i - lit + 8) % 8;
        disc(cx + 12 * wcos(a), cy + 12 * wsin(a), 2.6f, th.text, 256 - k * 28);
    }
    return 1;
}

static void draw_fetch(uint64_t t) {
    draw_header(0);
    if (!ui.fetch_failed) {
        text(F_BIG, "Getting today's word", VIEW_W / 2, 120, th.text, 0);
        text(F_TEXT, "from The New York Times...", VIEW_W / 2, 140, th.dim, 0);
        spinner(VIEW_W / 2.0f, 186, t);
        text(F_SMALL, "Return: cancel", VIEW_W / 2, 300, th.dim, 0);
        return;
    }
    text(F_BIG, "Couldn't get today's word", VIEW_W / 2, 62, th.text, 0);
    text_wrap(F_TEXT, ui.fetch_err, 44, 88, VIEW_W - 88, 17, th.text);
    pill(VIEW_W / 2 - 100, 172, 200, 32, ui.fetch_sel == 0, "Try Again", 0);
    pill(VIEW_W / 2 - 100, 212, 200, 32, ui.fetch_sel == 1, "Play a Random Word", 0);
    pill(VIEW_W / 2 - 100, 252, 200, 32, ui.fetch_sel == 2, "Back", 0);
}

static void stat_num(int cx, int value, const char *l1, const char *l2) {
    char s[16];
    snprintf(s, sizeof s, "%d", value);
    text(F_NUM, s, cx, 84, th.text, 0);
    text(F_SMALL, l1, cx, 98, th.text, 0);
    if (l2) text(F_SMALL, l2, cx, 110, th.text, 0);
}

static int today_index(void) {
    int y, m, d;
    if (ui.today_y) return days_from_civil(ui.today_y, ui.today_m, ui.today_d);
    return clock_date(&y, &m, &d) ? days_from_civil(y, m, d) : 0;
}

static void draw_stats(int tab, int from_game) {
    Stats *s = &sv.st[tab];
    Game *g = &sv.g[tab];
    rect(0, 0, VIEW_W, VIEW_H, th.bg);
    text(F_BOLD, "STATISTICS", VIEW_W / 2, 24, th.text, 0);
    /* the tab: Daily or Random */
    const char *names[2] = { "Daily", "Random" };
    rrect(VIEW_W / 2 - 62, 32, 124, 20, 10, th.line, 256);
    for (int i = 0; i < 2; ++i) {
        int x = VIEW_W / 2 - 62 + i * 62, w = 62;
        if (i == tab) rrect((float)x, 32, (float)w, 20, 10, th.text, 256);
        text(F_SMALL, names[i], x + w / 2, 46, i == tab ? th.bg : th.text, 0);
    }
    int streak = s->streak;
    if (tab == MODE_DAILY && s->last && today_index() && s->last < today_index() - 1) streak = 0; /* a day missed */
    stat_num(VIEW_W / 2 - 120, s->played, "Played", 0);
    stat_num(VIEW_W / 2 - 40, s->played ? (s->won * 100 + s->played / 2) / s->played : 0, "Win %", 0);
    stat_num(VIEW_W / 2 + 40, streak, "Current", "Streak");
    stat_num(VIEW_W / 2 + 120, s->best, "Max", "Streak");
    text(F_BOLD, "GUESS DISTRIBUTION", VIEW_W / 2, 134, th.text, 0);
    int most = 1;
    for (int i = 0; i < ROWS; ++i)
        if (s->dist[i] > most) most = s->dist[i];
    int this_row = valid_word(g->answer) && g->done == 1 ? g->rows - 1 : -1;
    for (int i = 0; i < ROWS; ++i) {
        int y = 142 + i * 19, x = 74, full = VIEW_W - 148;
        char n[12];
        snprintf(n, sizeof n, "%d", i + 1);
        text(F_BOLD, n, x - 8, y + 12, th.text, 1);
        int w = s->dist[i] * full / most;
        int nw = text_w(F_BOLD, "0") * (s->dist[i] >= 10 ? 2 : 1) + 12;
        if (w < nw) w = nw;
        rect(x, y, w, 15, i == this_row ? th.correct : th.absent);
        snprintf(n, sizeof n, "%d", s->dist[i]);
        text(F_BOLD, n, x + w - 5, y + 12, 0xffffff, 1);
    }
    if (tab == MODE_DAILY) {
        int y, m, d;
        Game *dg = &sv.g[MODE_DAILY];
        char date[11];
        int ok = clock_date(&y, &m, &d);
        snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
        if (ok && dg->done && !strcmp(dg->date, date)) {
            time_t t = time(0);
            struct tm tm;
            localtime_r(&t, &tm);
            int left = 86400 - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
            char cd[16];
            snprintf(cd, sizeof cd, "%02d:%02d:%02d", left / 3600, left / 60 % 60, left % 60);
            text(F_SMALL, "NEXT WORDLE", VIEW_W / 2, 271, th.text, 0);
            text(F_BIG, cd, VIEW_W / 2, 290, th.text, 0);
            if (dg->done == 2) {
                char w[24];
                snprintf(w, sizeof w, "The word was %c%c%c%c%c", dg->answer[0] - 32, dg->answer[1] - 32,
                         dg->answer[2] - 32, dg->answer[3] - 32, dg->answer[4] - 32);
                text(F_SMALL, w, VIEW_W / 2, 258, th.dim, 0);
            }
        }
        text(F_SMALL, "Return: Back  Wheel: Daily/Random",
             VIEW_W / 2, 308, th.dim, 0);
    } else {
        if (from_game) pill(VIEW_W / 2 - 70, 264, 140, 28, 1, "New Word", 0);
        text(F_SMALL, "Return: Back  Wheel: Daily/Random",
             VIEW_W / 2, 308, th.dim, 0);
    }
}

#define HELP_TILE 32

static void example(int x, int y, const char *word, int at, int face) {
    for (int i = 0; i < 5; ++i) {
        float cx = x + i * (HELP_TILE + 4) + HELP_TILE / 2.0f, cy = y + HELP_TILE / 2.0f;
        char s[2] = { (char)(word[i] - 32), 0 };
        int x0 = (int)cx - HELP_TILE / 2, y0 = (int)cy - HELP_TILE / 2;
        if (i == at) {
            rect(x0, y0, HELP_TILE, HELP_TILE, mark_colour(face));
            text_scaled(F_TILE, s, cx, cy, 256, 256, 0xffffff, 256);
        } else {
            rect(x0, y0, HELP_TILE, HELP_TILE, th.filled);
            rect(x0 + 2, y0 + 2, HELP_TILE - 4, HELP_TILE - 4, th.bg);
            text_scaled(F_TILE, s, cx, cy, 256, 256, th.text, 256);
        }
    }
}

/* the help's columns: clear of the glass's corners on the left and of the scroll bar on the right */
#define HELP_X 42
#define HELP_W (VIEW_W - HELP_X - 46)
#define HELP_LH 20

static int help_para(const char *s, int y) { return HELP_LH * text_wrap(F_HELP, s, HELP_X, y, HELP_W, HELP_LH, th.text); }

static int help_head(const char *s, int y) {
    text(F_HELPB, s, HELP_X, y, th.text, -1);
    return HELP_LH + 4;
}

/* drawn at an offset; returns the content's height */
static int draw_help(int oy) {
    int y = 50 - oy;
    text(F_HEAD, "How To Play", VIEW_W / 2, y, th.text, 0), y += 28;
    y += help_para("Find the hidden five-letter word in six guesses or fewer.", y) + 6;
    y += help_para("- Every guess has to be a real five-letter word.", y);
    y += help_para("- After each guess the tiles change colour to show how near you are.", y) + 10;
    y += help_head("Examples", y);
    y -= 12;
    example(HELP_X, y, "spare", 0, L_CORRECT), y += HELP_TILE + 20;
    y += help_para("S is in the word, in exactly that place.", y) + 8;
    example(HELP_X, y - 14, "cloud", 1, L_PRESENT), y += HELP_TILE + 6;
    y += help_para("L is in the word, but somewhere else.", y) + 8;
    example(HELP_X, y - 14, "piano", 3, L_ABSENT), y += HELP_TILE + 6;
    y += help_para("N isn't in the word at all.", y) + 14;
    y += help_head("On the Q2", y);
    y += help_para("Wheel: move round the keyboard.", y);
    y += help_para("Centre: press the key. After the fifth letter the highlight jumps to Enter.", y);
    y += help_para("Touch: put a finger on a key, slide to the one you want and lift to type it.", y);
    y += help_para("Return: delete a letter, or go back.", y);
    y += help_para("Hold Return: leave the game.", y) + 14;
    y += help_head("Daily Puzzle", y);
    y += help_para("The same word everyone gets today, fetched from The New York Times over Wi-Fi. "
                   "A new one comes at midnight. Words for the next few days are saved as they come, "
                   "so the Daily still works on a day without Wi-Fi.", y) + 14;
    y += help_head("Random Word", y);
    y += help_para("A word from the list built into the game: play as many as you like, no Wi-Fi needed.", y) + 14;
    y += help_head("Hard Mode", y);
    y += help_para("Every hint you've been given must be used: a green letter stays in its place, and "
                   "a yellow one must be in the guess.", y);
    return y + oy + 30; /* room below the last line for the glass's bottom corners */
}

/* the help's scroll bar: a track down the right side, inside the corners, and a thumb the size
 * of the screen's share of the page */
#define BAR_X (VIEW_W - 30)
#define BAR_Y0 40
#define BAR_Y1 (VIEW_H - 40)

static int help_max(void) {
    int m = ui.help_h - VIEW_H;
    return m > 0 ? m : 0;
}

static void draw_scrollbar(void) {
    int max = help_max();
    if (!max) return;
    float track = BAR_Y1 - BAR_Y0;
    float thumb = track * (float)VIEW_H / (float)ui.help_h;
    if (thumb < 24) thumb = 24;
    float y = BAR_Y0 + (track - thumb) * (float)ui.help_y / (float)max;
    rrect(BAR_X, BAR_Y0, 5, track, 2.5f, th.line, 256);
    rrect(BAR_X, y, 5, thumb, 2.5f, th.dim, 256);
}

static void toggle(int x, int y, int on) {
    rrect((float)x, (float)y, 36, 20, 10, on ? th.correct : th.dim, 256);
    disc(on ? x + 26.0f : x + 10.0f, y + 10.0f, 8, 0xffffff, 256);
}

static void draw_settings(void) {
    draw_header(0);
    text(F_BIG, "Settings", VIEW_W / 2, 52, th.text, 0);
    static const char *names[3] = { "Hard Mode", "Dark Theme", "High Contrast Mode" };
    static const char *subs[3] = { "Any revealed hints must be used in later guesses", "Light letters on a dark board",
                                   "Orange and blue in place of green and yellow" };
    int vals[3] = { sv.hard, sv.dark, sv.contrast };
    for (int i = 0; i < 3; ++i) {
        int y = 70 + i * 58;
        if (i == ui.set_sel) rrect(16, (float)y, VIEW_W - 32, 50, 8, mix(th.bg, th.text, 22), 256);
        text(F_BOLD, names[i], 30, y + 22, th.text, -1);
        text(F_SMALL, subs[i], 30, y + 38, th.dim, -1);
        toggle(VIEW_W - 72, y + 15, vals[i]);
        if (i < 2) rect(30, y + 54, VIEW_W - 60, 1, th.line);
    }
    text(F_SMALL, "Wheel: Choose  Centre: Switch", VIEW_W / 2, 302, th.dim, 0);
}

/* ---------------------------------------------------------------------------------------------
 * Frame */

static int render(void) {
    uint64_t t = ms_now();
    int anim = 0;
    ui.tick = 0;
    fill(th.bg);
    switch (ui.screen) {
    case S_HOME: draw_home(); break;
    case S_FETCH:
        draw_fetch(t);
        anim = !ui.fetch_failed;
        break;
    case S_PLAY: {
        Game *g = G();
        char right[24];
        if (ui.mode == MODE_DAILY && g->number) snprintf(right, sizeof right, "%sNo. %d", g->hard ? "Hard  " : "", g->number);
        else if (ui.mode == MODE_DAILY) snprintf(right, sizeof right, "%s%s", g->hard ? "Hard  " : "", g->date);
        else snprintf(right, sizeof right, "%sRandom", g->hard ? "Hard  " : "");
        draw_header(right);
        anim |= draw_board(t);
        draw_keyboard();
        anim |= draw_toast(t);
        anim |= busy();
        if (ui.stats_over) draw_stats(ui.stats_tab, ui.stats_tab == ui.mode), ui.tick = 1;
        break;
    }
    case S_STATS:
        draw_stats(ui.stats_tab, 0);
        ui.tick = 1;
        break;
    case S_HELP:
        if (ui.help_pos != ui.help_target) { /* a third of the way each frame: smooth, and done in about 1/4 s */
            float d = ui.help_target - ui.help_pos;
            ui.help_pos = d * d < 0.25f ? ui.help_target : ui.help_pos + d * 0.34f;
            anim = 1;
        }
        ui.help_y = (int)(ui.help_pos + (ui.help_pos < 0 ? -0.5f : 0.5f));
        ui.help_h = draw_help(ui.help_y);
        rect(0, 0, VIEW_W, HEAD_H, th.bg);
        draw_header(0);
        draw_scrollbar();
        break;
    case S_SETTINGS:
        draw_settings();
        anim |= draw_toast(t);
        break;
    }
    return anim;
}

static void write_state(void) {
    static const char *path;
    static int checked;
    if (!checked) path = getenv("WD_STATE"), checked = 1;
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    Game *g = G();
    fprintf(f, "screen %d\nmode %d\nrows %d\ndone %d\nncur %d\ncursor %d\nstats_over %d\nhome_sel %d\nfetch_failed %d\n"
               "hard %d\ndark %d\nplayed %d\nwon %d\nanswer %s\nnumber %d\ndate %s\ntoast %s\nhelp_y %d\n",
            ui.screen, ui.mode, g->rows, g->done, g->ncur, ui.cursor, ui.stats_over, ui.home_sel, ui.fetch_failed,
            sv.hard, sv.dark, sv.st[ui.mode].played, sv.st[ui.mode].won, g->answer, g->number,
            g->date[0] ? g->date : "-", ui.toast[0] ? ui.toast : "-", ui.help_y);
    fclose(f);
}

/* ---------------------------------------------------------------------------------------------
 * Input */

static void go_home(void) {
    if (ui.screen == S_FETCH) net_cancel(); /* the days ahead, after a fetch, run on */
    ui.screen = S_HOME;
    ui.touch_key = -1;
    ui.stats_over = 0;
}

static void home_pick(int i) {
    ui.home_sel = i;
    switch (i) {
    case 0: start_daily(); break;
    case 1: start_random(0); break;
    case 2: ui.screen = S_HELP, ui.help_y = 0, ui.help_pos = ui.help_target = 0; break;
    case 3: ui.screen = S_STATS, ui.stats_tab = MODE_DAILY; break;
    case 4: ui.screen = S_SETTINGS, ui.set_sel = 0; break;
    }
}

static void settings_pick(int i) {
    if (i == 0) {
        Game *gd = &sv.g[MODE_DAILY], *gr = &sv.g[MODE_RANDOM];
        int midgame = (valid_word(gd->answer) && !gd->done && gd->rows) || (valid_word(gr->answer) && !gr->done && gr->rows);
        sv.hard = !sv.hard;
        if (sv.hard && midgame) toast("Hard mode starts next game", 0);
        if (!sv.hard) sv.g[MODE_DAILY].hard = sv.g[MODE_RANDOM].hard = 0;
    } else if (i == 1) sv.dark = !sv.dark;
    else sv.contrast = !sv.contrast;
    set_theme();
    save_write();
}

static int wheel_steps(int travel) {
    ui.wheel_acc += travel;
    int n = 0;
    while (ui.wheel_acc <= -WHEEL_STEP) ui.wheel_acc += WHEEL_STEP, ++n;
    while (ui.wheel_acc >= WHEEL_STEP) ui.wheel_acc -= WHEEL_STEP, --n;
    return n;
}

static int in_box(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

static void on_back(void) {
    switch (ui.screen) {
    case S_PLAY: {
        Game *g = G();
        if (ui.stats_over) ui.stats_over = 0;
        else if (!g->done && g->ncur && !busy()) type_key(KEY_BACK);
        else go_home();
        break;
    }
    case S_HOME: break; /* Q2 Pod's hold leaves the game */
    default: go_home();
    }
}

static void on_centre(void) {
    switch (ui.screen) {
    case S_HOME: home_pick(ui.home_sel); break;
    case S_FETCH:
        if (!ui.fetch_failed) break;
        if (ui.fetch_sel == 0) start_daily();
        else if (ui.fetch_sel == 1) start_random(0);
        else go_home();
        break;
    case S_PLAY:
        if (ui.stats_over) {
            if (ui.mode == MODE_RANDOM && ui.stats_tab == MODE_RANDOM) start_random(1);
            else ui.stats_over = 0;
        } else if (G()->done) ui.stats_over = 1, ui.stats_tab = ui.mode;
        else {
            int k = ui.cursor;
            type_key(k);
            if (k != KEY_BACK && k != KEY_ENTER && G()->ncur == 5) ui.cursor = KEY_ENTER; /* the word's full: Enter next */
        }
        break;
    case S_SETTINGS: settings_pick(ui.set_sel); break;
    case S_STATS: go_home(); break;
    case S_HELP: go_home(); break;
    }
}

static void on_wheel(int steps) {
    if (!steps) return;
    switch (ui.screen) {
    case S_HOME: ui.home_sel = ((ui.home_sel + steps) % 5 + 5) % 5; break;
    case S_FETCH: ui.fetch_sel = ((ui.fetch_sel + steps) % 3 + 3) % 3; break;
    case S_PLAY:
        if (ui.stats_over) ui.stats_tab = !ui.stats_tab;
        else ui.cursor = ((ui.cursor + steps) % NKEYS + NKEYS) % NKEYS;
        break;
    case S_STATS: ui.stats_tab = !ui.stats_tab; break;
    case S_SETTINGS: ui.set_sel = ((ui.set_sel + steps) % 3 + 3) % 3; break;
    case S_HELP: break; /* raw travel: help_wheel */
    }
}

/* the wheel scrolls the help by its raw travel, not in steps: a turn (200) is 240 px, clockwise down */
static void help_wheel(int travel) {
    float max = (float)help_max();
    ui.help_target -= (float)travel * 1.2f;
    if (ui.help_target > max) ui.help_target = max;
    if (ui.help_target < 0) ui.help_target = 0;
}

static struct {
    int down, x0, y0, y_last, moved;
} tch;

static void on_touch(int phase, int x, int y) {
    if (phase == 'd') tch.down = 1, tch.x0 = x, tch.y0 = y, tch.y_last = y, tch.moved = 0;
    if (phase == 'm' && ((x - tch.x0) * (x - tch.x0) + (y - tch.y0) * (y - tch.y0) > 64)) tch.moved = 1;
    switch (ui.screen) {
    case S_PLAY: {
        Game *g = G();
        if (ui.stats_over) {
            if (phase != 'u') return;
            if (y < 60 && y > 26 && x < VIEW_W / 2 && x > VIEW_W / 2 - 80) ui.stats_tab = MODE_DAILY;
            else if (y < 60 && y > 26 && x >= VIEW_W / 2 && x < VIEW_W / 2 + 80) ui.stats_tab = MODE_RANDOM;
            else if (ui.mode == MODE_RANDOM && ui.stats_tab == MODE_RANDOM && in_box(x, y, VIEW_W / 2 - 80, 258, 160, 40))
                start_random(1);
            else ui.stats_over = 0;
            return;
        }
        if (phase == 'u' && y < HEAD_H + 4) {
            if (x < 90) go_home();
            else if (x > VIEW_W - 90) ui.stats_over = 1, ui.stats_tab = ui.mode;
            ui.touch_key = -1;
            return;
        }
        if (g->done) {
            if (phase == 'u') ui.stats_over = 1, ui.stats_tab = ui.mode;
            return;
        }
        if (phase == 'd' || phase == 'm') {
            int k = y >= KB_Y - 8 ? key_at(x, y) : -1;
            if (phase == 'm' && tch.y0 < KB_Y - 8) k = -1; /* a touch begun on the board types nothing */
            ui.touch_key = k;
        } else {
            int k = ui.touch_key >= 0 ? ui.touch_key : (tch.y0 >= KB_Y - 8 ? key_at(x, y) : -1);
            ui.touch_key = -1;
            if (k >= 0) ui.cursor = k, type_key(k);
        }
        break;
    }
    case S_HOME:
        if (phase != 'u' || tch.moved) return;
        if (in_box(x, y, VIEW_W / 2 - 100, 128, 200, 38)) home_pick(0);
        else if (in_box(x, y, VIEW_W / 2 - 100, 172, 200, 38)) home_pick(1);
        else if (in_box(x, y, VIEW_W / 2 - 176, 216, 112, 38)) home_pick(2);
        else if (in_box(x, y, VIEW_W / 2 - 56, 216, 112, 38)) home_pick(3);
        else if (in_box(x, y, VIEW_W / 2 + 64, 216, 112, 38)) home_pick(4);
        break;
    case S_FETCH:
        if (phase != 'u') return;
        if (y < HEAD_H + 4 && x < 90) go_home();
        else if (ui.fetch_failed) {
            for (int i = 0; i < 3; ++i)
                if (in_box(x, y, VIEW_W / 2 - 100, 172 + 40 * i, 200, 32)) ui.fetch_sel = i, on_centre();
        }
        break;
    case S_STATS:
        if (phase != 'u') return;
        if (y < 60 && y > 26 && x < VIEW_W / 2 && x > VIEW_W / 2 - 80) ui.stats_tab = MODE_DAILY;
        else if (y < 60 && y > 26 && x >= VIEW_W / 2 && x < VIEW_W / 2 + 80) ui.stats_tab = MODE_RANDOM;
        else go_home();
        break;
    case S_SETTINGS:
        if (phase != 'u' || tch.moved) return;
        if (y < HEAD_H + 4 && x < 90) go_home();
        for (int i = 0; i < 3; ++i)
            if (in_box(x, y, 16, 70 + i * 58, VIEW_W - 32, 50)) ui.set_sel = i, settings_pick(i);
        break;
    case S_HELP:
        if (phase == 'm') {
            float max = (float)help_max();
            ui.help_target -= (float)(y - tch.y_last);
            if (ui.help_target > max) ui.help_target = max;
            if (ui.help_target < 0) ui.help_target = 0;
            ui.help_pos = ui.help_target; /* a finger moves the page itself */
        }
        if (phase == 'u' && !tch.moved && y < HEAD_H + 4 && x < 90) go_home();
        break;
    }
    tch.y_last = y;
    if (phase == 'u') tch.down = 0;
}

/* ---------------------------------------------------------------------------------------------
 * Loop */

int game_main(void) {
    save_read();
    set_theme();
    ui.touch_key = -1;
    ui.screen = S_HOME;
    if (getenv("WD_SCREEN")) { /* host tests */
        const char *s = getenv("WD_SCREEN");
        if (!strcmp(s, "random")) start_random(0);
        else if (!strcmp(s, "daily")) start_daily();
    }
    printf("words: %d answers, %d more guesses\n", answers_count, allowed_count);
    int anim = 1;
    uint64_t last_frame = 0;
    ui.dirty = 1;
    while (!quit_asked()) {
        int kind, a, b, c, got = 0;
        while (next_event(&kind, &a, &b, &c)) {
            got = 1;
            if (kind == 'k' && a) {
                if (b == K_RETURN) on_back();
                else if (b == K_CENTRE) on_centre();
            } else if (kind == 'w' && ui.screen == S_HELP) help_wheel(a);
            else if (kind == 'w') on_wheel(wheel_steps(a));
            else if (kind == 't') on_touch(a, b, c);
        }
        if (ui.screen == S_FETCH) fetch_poll();
        else if (net_state() == NET_OK) fetch_poll(); /* the days ahead arriving after the game began */
        /* the end: the bounce after a win's reveal, then the stats */
        uint64_t t = ms_now();
        Game *g = G();
        if (ui.screen == S_PLAY && ui.end_t && t >= ui.end_t) {
            static const char *const praise[ROWS] = { "Genius", "Magnificent", "Impressive", "Splendid", "Great", "Phew" };
            if (g->done == 1) toast(praise[g->rows - 1], 0), ui.bounce_t = t;
            else {
                char w[8] = { (char)(g->answer[0] - 32), (char)(g->answer[1] - 32), (char)(g->answer[2] - 32),
                              (char)(g->answer[3] - 32), (char)(g->answer[4] - 32), 0 };
                toast(w, 1);
            }
            ui.end_t = 0;
            ui.dirty = 1;
        }
        if (ui.screen == S_PLAY && ui.stats_auto && !ui.end_t && t >= ui.toast_t + STATS_AFTER_MS) {
            ui.stats_auto = 0, ui.stats_over = 1, ui.stats_tab = ui.mode, ui.dirty = 1;
            save_write();
        }
        static long last_sec;
        if (ui.tick && (long)time(0) != last_sec) last_sec = (long)time(0), ui.dirty = 1;
        if (got || anim || ui.dirty || ui.stats_auto) {
            if (t - last_frame < 16) usleep((unsigned)(16 - (t - last_frame)) * 1000);
            last_frame = ms_now();
            anim = render();
            present(fb);
            write_state();
            ui.dirty = 0;
        } else usleep(12000);
    }
    save_write();
    return 0;
}
