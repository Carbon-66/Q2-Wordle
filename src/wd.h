/* Wordle for the Shanling Q2 under Q2 Pod: guess the five-letter word in six tries.
 *
 * Two games: the Daily, today's New York Times word fetched over Wi-Fi (net.c), and Random, a word
 * picked from the list built in (words_data.c). Shared declarations for every file in src/. */
#ifndef WD_H
#define WD_H
#include <stdint.h>

#define VIEW_W 375 /* the UI's landscape view: the portrait panel turned */
#define VIEW_H 320

/* Q2 Pod's AWTK key codes. Play/Pause and the side buttons stay the music player's (no audio=1). */
#define K_RETURN 170
#define K_CENTRE 218

/* ---------------------------------------------------------------------------------------------
 * q2.c: the platform */
uint64_t now_us(void);
void present(const uint32_t *frame); /* a VIEW_W x VIEW_H XRGB frame onto the panel */
int quit_asked(void);
int next_event(int *kind, int *a, int *b, int *c); /* 'k' (a 1 down/0 up, b code), 't' (a 'd'/'m'/'u', b x, c y), 'w' (a travel) */
void message(const char *title, const char *body); /* a plain full-screen note */
extern char app_dir[256];

/* ---------------------------------------------------------------------------------------------
 * gfx.c: drawing into the frame */
typedef struct { short w, h, x, y, adv; unsigned off; } Glyph; /* x, y: the bitmap's top left from the pen on the baseline */
typedef struct { short ascent, descent, cap; const Glyph *g; } Font;
enum { F_TILE, F_KEY, F_SMALL, F_TEXT, F_BOLD, F_BIG, F_NUM, F_HEAD, F_LOGO, F_HELP, F_HELPB };
extern const Font *const fonts[];
extern const unsigned char *const font_data;

extern uint32_t fb[VIEW_W * VIEW_H];
void fill(uint32_t c);
void rect(int x, int y, int w, int h, uint32_t c);
void rect_a(int x, int y, int w, int h, uint32_t c, int alpha); /* alpha 0-256 */
void rrect(float x, float y, float w, float h, float r, uint32_t c, int alpha); /* rounded, antialiased */
void frame_box(int x, int y, int w, int h, int t, uint32_t c); /* a t px border */
void line(float x0, float y0, float x1, float y1, float w, uint32_t c, int alpha);
void disc(float cx, float cy, float r, uint32_t c, int alpha);
int text(int font, const char *s, int x, int y, uint32_t c, int align); /* y the baseline; align -1, 0, 1; returns width */
int text_w(int font, const char *s);
/* centred on (cx, cy) by cap height, scaled sx/256 across and sy/256 down */
void text_scaled(int font, const char *s, float cx, float cy, int sx, int sy, uint32_t c, int alpha);
int text_wrap(int font, const char *s, int x, int y, int w, int lh, uint32_t c); /* left-aligned lines; returns lines */
void darken(uint32_t c, int alpha); /* the whole frame toward c */
uint32_t mix(uint32_t a, uint32_t b, int t); /* t 0-256 of b */

/* ---------------------------------------------------------------------------------------------
 * logic.c: the rules */
enum { L_NONE, L_ABSENT, L_PRESENT, L_CORRECT }; /* a letter's state, worst to best */
void score(const char *guess, const char *answer, unsigned char out[5]);
int word_ok(const char *w);   /* five lower-case letters in either list */
int answer_count(void);
void answer_at(int i, char out[6]);
/* hard mode: the hints so far must be used; 0 ok, else writes why into msg */
int hard_check(const char guesses[][6], const unsigned char marks[][5], int n, const char *g, char *msg, int msglen);
extern const uint32_t *const answers_words, *const allowed_words;
extern const int answers_count, allowed_count;

/* ---------------------------------------------------------------------------------------------
 * net.c: today's word from the New York Times, on a thread */
enum { NET_IDLE, NET_BUSY, NET_OK, NET_FAIL };
void net_start(int y, int m, int d); /* the local date to ask for */
int net_state(void);                /* NET_* */
/* after NET_OK: the word, the puzzle number, its date; the server's clock (0 unknown) */
void net_result(char word[6], int *number, int *y, int *m, int *d, long *server_time);
const char *net_error(void);
int net_ahead(int i, char word[6], int *number, char date[11]); /* the days after, once net_finished */
int net_finished(void);
void net_cancel(void);
void net_shutdown(void);

/* sine and cosine for the animations, close enough to the eye, and no libm */
static inline float wsin(float x) {
    const float tau = 6.2831853f, pi = 3.14159265f;
    x -= tau * (float)(int)(x / tau);
    if (x > pi) x -= tau;
    if (x < -pi) x += tau;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static inline float wcos(float x) { return wsin(x + 1.57079633f); }

int game_main(void);
#endif
