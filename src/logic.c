/* Wordle's rules: scoring a guess, the word lists, and hard mode's checks. */
#include <stdio.h>
#include <string.h>

#include "wd.h"

static uint32_t pack(const char *w) {
    uint32_t v = 0;
    for (int i = 0; i < 5; ++i) v = v * 32 + (uint32_t)(w[i] - 'a');
    return v;
}

static int has(const uint32_t *a, int n, uint32_t v) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] == v) return 1;
        if (a[mid] < v) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

int word_ok(const char *w) {
    for (int i = 0; i < 5; ++i)
        if (w[i] < 'a' || w[i] > 'z') return 0;
    uint32_t v = pack(w);
    return has(answers_words, answers_count, v) || has(allowed_words, allowed_count, v);
}

int answer_count(void) { return answers_count; }

void answer_at(int i, char out[6]) {
    uint32_t v = answers_words[i];
    for (int k = 4; k >= 0; --k) out[k] = (char)('a' + (v & 31)), v >>= 5;
    out[5] = 0;
}

/* Greens first, then yellows left to right while the answer still has that letter unmatched:
 * guessing a letter more times than the answer holds it leaves the extra ones grey. */
void score(const char *guess, const char *answer, unsigned char out[5]) {
    int left[26] = { 0 };
    for (int i = 0; i < 5; ++i) {
        if (guess[i] == answer[i]) out[i] = L_CORRECT;
        else out[i] = L_ABSENT, ++left[answer[i] - 'a'];
    }
    for (int i = 0; i < 5; ++i)
        if (out[i] != L_CORRECT && left[guess[i] - 'a'] > 0) out[i] = L_PRESENT, --left[guess[i] - 'a'];
}

static const char *const ordinal[5] = { "1st", "2nd", "3rd", "4th", "5th" };

/* As the original's hard mode: each green must stay in its place, and each revealed letter must be
 * used at least as many times as the hints have shown it (greens and yellows of one guess). */
int hard_check(const char guesses[][6], const unsigned char marks[][5], int n, const char *g, char *msg, int msglen) {
    for (int r = 0; r < n; ++r)
        for (int i = 0; i < 5; ++i)
            if (marks[r][i] == L_CORRECT && g[i] != guesses[r][i]) {
                snprintf(msg, (unsigned)msglen, "%s letter must be %c", ordinal[i], guesses[r][i] - 32);
                return 1;
            }
    for (int r = 0; r < n; ++r) {
        int need[26] = { 0 }, got[26] = { 0 };
        for (int i = 0; i < 5; ++i)
            if (marks[r][i] == L_CORRECT || marks[r][i] == L_PRESENT) ++need[guesses[r][i] - 'a'];
        for (int i = 0; i < 5; ++i) ++got[g[i] - 'a'];
        for (int i = 0; i < 5; ++i) {
            int c = guesses[r][i] - 'a';
            if (need[c] > got[c]) {
                snprintf(msg, (unsigned)msglen, "Guess must contain %c", c + 'A');
                return 1;
            }
        }
    }
    return 0;
}
