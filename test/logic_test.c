/* score(), word_ok() and hard_check() against hand-worked cases. Built by test/run.sh. */
#include <stdio.h>
#include <string.h>
#include "wd.h"

static int fails;
static void check(const char *g, const char *a, const char *want) { /* want: G Y . per letter */
    unsigned char m[5];
    char got[6] = { 0 };
    score(g, a, m);
    for (int i = 0; i < 5; ++i) got[i] = m[i] == L_CORRECT ? 'G' : m[i] == L_PRESENT ? 'Y' : '.';
    if (strcmp(got, want)) printf("FAIL score %s vs %s: %s, want %s\n", g, a, got, want), ++fails;
}

int main(void) {
    check("crane", "crane", "GGGGG");
    check("bobby", "abbey", "Y.G.G");  /* the answer's spare B goes to the first, the second is grey */
    check("eerie", "crane", "..Y.G");  /* one E in the answer, matched in place: the others grey */
    check("trees", "dress", ".GG.G");  /* the second E has no E left to match: grey */
    check("speed", "abide", "..Y.Y");
    check("geese", "those", "...GG");
    check("lever", "eerie", ".G.YY");
    if (!word_ok("crane") || !word_ok("aahed") || word_ok("xyzzy") || word_ok("CRANE")) printf("FAIL word_ok\n"), ++fails;
    char gs[2][6] = { "cabin", "" };
    unsigned char ms[2][5];
    score("cabin", "crane", ms[0]);
    char msg[64];
    if (!hard_check((const char (*)[6])gs, (const unsigned char (*)[5])ms, 1, "motel", msg, sizeof msg) ||
        strcmp(msg, "1st letter must be C")) printf("FAIL hard green: %s\n", msg), ++fails;
    if (!hard_check((const char (*)[6])gs, (const unsigned char (*)[5])ms, 1, "chess", msg, sizeof msg) ||
        strcmp(msg, "Guess must contain A")) printf("FAIL hard yellow: %s\n", msg), ++fails;
    if (hard_check((const char (*)[6])gs, (const unsigned char (*)[5])ms, 1, "canon", msg, sizeof msg))
        printf("FAIL hard ok: %s\n", msg), ++fails;
    printf(fails ? "logic: %d failed\n" : "logic: ok\n", fails);
    return fails != 0;
}
