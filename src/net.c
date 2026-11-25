/* Today's word from the New York Times: the JSON its own Wordle page loads,
 *   https://www.nytimes.com/svc/wordle/v2/YYYY-MM-DD.json  ->  { "solution": "...", "days_since_launch": N, ... }
 * fetched on a thread so the screen stays live and Return can cancel.
 *
 * libcurl is the rootfs's own, the one Q2 Pod's scrobbling, lyrics and radio use. It is opened with
 * dlopen rather than linked, so a firmware without it costs only the Daily: Random still runs.
 * Certificates are checked against cacert.pem in the card folder (Mozilla's roots, shipped with the
 * game), else the firmware's /etc/scrobble-ca.pem. As Q2 Pod's own handles do (tls_untimed), no
 * certificate's dates are checked, so a Q2 whose clock was never set can still connect, and then
 * the server's Date header tells us what day it is.
 *
 * After today's word the next days' are fetched too (the page serves a few weeks ahead), so the
 * Daily still works on a day Wi-Fi is asleep when the game starts; game.c keeps them in its cache. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wd.h"

int pthread_timedjoin_np(pthread_t, void **, const struct timespec *);

#define URL_BASE "https://www.nytimes.com/svc/wordle/v2/"
#define USER_AGENT "Q2-Wordle/1.0 (Shanling Q2, Q2 Pod card game)"
#define REPLY_MAX 16384
#define AHEAD 7 /* days after today fetched for the cache */

/* libcurl's numbers, stable since 7.x */
enum {
    O_URL = 10002, O_NOSIGNAL = 99, O_CONNECTTIMEOUT = 78, O_TIMEOUT = 13, O_CAINFO = 10065,
    O_VERIFYPEER = 64, O_VERIFYHOST = 81, O_SSL_CTX_FUNCTION = 20108, O_WRITEFUNCTION = 20011,
    O_WRITEDATA = 10001, O_HEADERFUNCTION = 20079, O_HEADERDATA = 10029, O_USERAGENT = 10018,
    O_FOLLOWLOCATION = 52, O_NOPROGRESS = 43, O_XFERINFOFUNCTION = 20219, O_XFERINFODATA = 10057,
    I_RESPONSE_CODE = 0x200002,
};
#define X509_V_FLAG_NO_CHECK_TIME 0x200000

static struct {
    void *(*init)(void);
    int (*setopt)(void *, int, ...);
    int (*perform)(void *);
    int (*getinfo)(void *, int, ...);
    void (*cleanup)(void *);
    const char *(*strerror)(int);
    time_t (*getdate)(const char *, const time_t *);
    void *(*ctx_param)(void *);
    int (*param_flags)(void *, unsigned long);
    int loaded, failed;
} cu;

typedef struct {
    char date[11];
    char word[6];
    int number;
} Day;

static struct {
    pthread_t th;
    int running;
    volatile int state, cancel, finished;
    int y, m, d;
    Day today, ahead[AHEAD];
    int n_ahead;
    long server_t;
    char err[128];
} nt;

static int load_curl(void) {
    if (cu.loaded) return 1;
    if (cu.failed) return 0;
    void *h = dlopen("libcurl.so.4", RTLD_NOW);
    if (!h) h = dlopen("libcurl.so", RTLD_NOW);
    if (!h) {
        printf("net: no libcurl: %s\n", dlerror());
        cu.failed = 1;
        return 0;
    }
    cu.init = (void *(*)(void))dlsym(h, "curl_easy_init");
    cu.setopt = (int (*)(void *, int, ...))dlsym(h, "curl_easy_setopt");
    cu.perform = (int (*)(void *))dlsym(h, "curl_easy_perform");
    cu.getinfo = (int (*)(void *, int, ...))dlsym(h, "curl_easy_getinfo");
    cu.cleanup = (void (*)(void *))dlsym(h, "curl_easy_cleanup");
    cu.strerror = (const char *(*)(int))dlsym(h, "curl_easy_strerror");
    cu.getdate = (time_t(*)(const char *, const time_t *))dlsym(h, "curl_getdate");
    if (!cu.init || !cu.setopt || !cu.perform || !cu.getinfo || !cu.cleanup) {
        printf("net: libcurl lacks the easy interface\n");
        cu.failed = 1;
        return 0;
    }
    /* libcurl has loaded its OpenSSL: these find it, by either soname */
    void *s = dlopen("libssl.so.1.1", RTLD_NOW);
    if (!s) s = dlopen("libssl.so.3", RTLD_NOW);
    if (s) {
        cu.ctx_param = (void *(*)(void *))dlsym(s, "SSL_CTX_get0_param");
        cu.param_flags = (int (*)(void *, unsigned long))dlsym(s, "X509_VERIFY_PARAM_set_flags");
    }
    printf("net: libcurl loaded%s\n", cu.ctx_param && cu.param_flags ? ", certificate dates unchecked" : "");
    cu.loaded = 1;
    return 1;
}

/* ---------------------------------------------------------------------------------------------
 * One request */

typedef struct {
    char body[REPLY_MAX];
    unsigned len;
    char date_hdr[64];
} Reply;

static size_t on_body(const char *p, size_t size, size_t n, void *arg) {
    Reply *r = arg;
    size_t k = size * n;
    if (r->len + k >= sizeof r->body) return 0; /* too big: not the page we want */
    memcpy(r->body + r->len, p, k);
    r->len += (unsigned)k;
    r->body[r->len] = 0;
    return k;
}

static size_t on_header(const char *p, size_t size, size_t n, void *arg) {
    Reply *r = arg;
    size_t k = size * n;
    if (k > 5 && k < 70 && (p[0] == 'D' || p[0] == 'd') && !strncmp(p + 1, "ate:", 4)) {
        size_t i = 5;
        while (i < k && p[i] == ' ') ++i;
        size_t len = k - i;
        while (len && (p[i + len - 1] == '\r' || p[i + len - 1] == '\n')) --len;
        memcpy(r->date_hdr, p + i, len);
        r->date_hdr[len] = 0;
    }
    return k;
}

static int on_progress(void *arg, long long a, long long b, long long c, long long d) {
    (void)arg, (void)a, (void)b, (void)c, (void)d;
    return nt.cancel;
}

static int on_ssl_ctx(void *curl, void *ctx, void *arg) {
    (void)curl, (void)arg;
    void *param = cu.ctx_param(ctx);
    if (param) cu.param_flags(param, X509_V_FLAG_NO_CHECK_TIME);
    return 0;
}

/* the text after "key": up to the next quote or comma */
static int json_field(const char *body, const char *key, char *out, int max) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') ++p;
    if (*p == '"') ++p;
    int n = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && n < max - 1) out[n++] = *p++;
    out[n] = 0;
    return n > 0;
}

/* 0 ok, else nt.err says why */
static int fetch(int y, int m, int d, Day *out) {
    char url[200], ca[300];
    const char *base = getenv("WORDLE_URL_BASE"); /* host tests serve a folder of files */
    snprintf(url, sizeof url, "%s%04d-%02d-%02d.json", base ? base : URL_BASE, y, m, d);
    snprintf(ca, sizeof ca, "%s/cacert.pem", app_dir);
    if (access(ca, 0)) snprintf(ca, sizeof ca, "/etc/scrobble-ca.pem");

    static Reply r;
    memset(&r, 0, sizeof r);
    void *c = cu.init();
    if (!c) {
        snprintf(nt.err, sizeof nt.err, "The internet library didn't start.");
        return 1;
    }
    cu.setopt(c, O_URL, url);
    cu.setopt(c, O_NOSIGNAL, 1L);
    cu.setopt(c, O_CONNECTTIMEOUT, 10L);
    cu.setopt(c, O_TIMEOUT, 20L);
    cu.setopt(c, O_USERAGENT, USER_AGENT);
    cu.setopt(c, O_FOLLOWLOCATION, 1L);
    cu.setopt(c, O_CAINFO, ca);
    cu.setopt(c, O_VERIFYPEER, 1L);
    cu.setopt(c, O_VERIFYHOST, 2L);
    if (cu.ctx_param && cu.param_flags) cu.setopt(c, O_SSL_CTX_FUNCTION, on_ssl_ctx);
    cu.setopt(c, O_WRITEFUNCTION, on_body);
    cu.setopt(c, O_WRITEDATA, &r);
    cu.setopt(c, O_HEADERFUNCTION, on_header);
    cu.setopt(c, O_HEADERDATA, &r);
    cu.setopt(c, O_NOPROGRESS, 0L);
    cu.setopt(c, O_XFERINFOFUNCTION, on_progress);
    int res = cu.perform(c);
    long code = 0;
    cu.getinfo(c, I_RESPONSE_CODE, &code);
    cu.cleanup(c);
    printf("net: %s -> curl %d, HTTP %ld, %u bytes, date \"%s\"\n", url, res, code, r.len, r.date_hdr);
    if (r.date_hdr[0] && cu.getdate) {
        time_t t = cu.getdate(r.date_hdr, 0);
        if (t > 0) nt.server_t = (long)t;
    }
    if (nt.cancel) {
        snprintf(nt.err, sizeof nt.err, "Cancelled.");
        return 1;
    }
    if (res) {
        snprintf(nt.err, sizeof nt.err, "Couldn't reach the New York Times (%s). Is Wi-Fi on and connected?",
                 cu.strerror ? cu.strerror(res) : "no connection");
        return 1;
    }
    if (code && code != 200) {
        snprintf(nt.err, sizeof nt.err, "The New York Times answered HTTP %ld for %04d-%02d-%02d.", code, y, m, d);
        return 1;
    }
    char word[16], num[16], date[16];
    if (!json_field(r.body, "solution", word, sizeof word) || strlen(word) != 5) {
        snprintf(nt.err, sizeof nt.err, "The New York Times sent no word for %04d-%02d-%02d.", y, m, d);
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        char ch = word[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if (ch < 'a' || ch > 'z') {
            snprintf(nt.err, sizeof nt.err, "The word sent wasn't five letters.");
            return 1;
        }
        out->word[i] = ch;
    }
    out->word[5] = 0;
    out->number = json_field(r.body, "days_since_launch", num, sizeof num) ? atoi(num) : 0;
    if (json_field(r.body, "print_date", date, sizeof date) && strlen(date) == 10) memcpy(out->date, date, 11);
    else snprintf(out->date, sizeof out->date, "%04d-%02d-%02d", y, m, d);
    return 0;
}

/* the local calendar date d days after (y, m, d0) */
static void add_days(int *y, int *m, int *d, int n) {
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    while (n-- > 0) {
        int leap = (*y % 4 == 0 && *y % 100 != 0) || *y % 400 == 0;
        int days = len[*m - 1] + (*m == 2 && leap);
        if (++*d > days) {
            *d = 1;
            if (++*m > 12) *m = 1, ++*y;
        }
    }
}

static void *work(void);
static void *worker(void *arg) {
    (void)arg;
    work();
    nt.finished = 1;
    return 0;
}

static void *work(void) {
    if (!load_curl()) {
        snprintf(nt.err, sizeof nt.err, "This firmware has no internet library (libcurl) for the game to use.");
        nt.state = NET_FAIL;
        return 0;
    }
    int y = nt.y, m = nt.m, d = nt.d;
    if (fetch(y, m, d, &nt.today)) {
        nt.state = NET_FAIL;
        return 0;
    }
    /* A clock never set (or far off) asked for the wrong day: the server's says which is today here */
    long now = (long)time(0);
    if (nt.server_t && (nt.server_t - now > 3600 || now - nt.server_t > 3600)) {
        time_t st = (time_t)nt.server_t;
        struct tm tm;
        localtime_r(&st, &tm);
        if (tm.tm_year + 1900 != y || tm.tm_mon + 1 != m || tm.tm_mday != d) {
            y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;
            printf("net: the clock is off by %ld s; asking for %04d-%02d-%02d\n", nt.server_t - now, y, m, d);
            if (fetch(y, m, d, &nt.today)) {
                nt.state = NET_FAIL;
                return 0;
            }
        }
    }
    nt.state = NET_OK;
    /* the days ahead, quietly; a failure ends the run */
    for (int i = 0; i < AHEAD && !nt.cancel; ++i) {
        add_days(&y, &m, &d, 1);
        Day day;
        if (fetch(y, m, d, &day)) break;
        nt.ahead[i] = day;
        nt.n_ahead = i + 1;
    }
    return 0;
}

static void join(void) {
    if (!nt.running) return;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += 25;
    pthread_timedjoin_np(nt.th, 0, &t);
    nt.running = 0;
}

void net_start(int y, int m, int d) {
    nt.cancel = 1; /* a run still fetching the days ahead stops at its next progress call */
    join();
    nt.cancel = 0;
    nt.y = y, nt.m = m, nt.d = d;
    nt.n_ahead = 0;
    nt.finished = 0;
    nt.server_t = 0;
    nt.err[0] = 0;
    nt.state = NET_BUSY;
    if (pthread_create(&nt.th, 0, worker, 0)) {
        snprintf(nt.err, sizeof nt.err, "Couldn't start the download.");
        nt.state = NET_FAIL;
        return;
    }
    nt.running = 1;
}

int net_state(void) { return nt.state; }
const char *net_error(void) { return nt.err; }

void net_result(char word[6], int *number, int *y, int *m, int *d, long *server_time) {
    memcpy(word, nt.today.word, 6);
    *number = nt.today.number;
    sscanf(nt.today.date, "%d-%d-%d", y, m, d);
    *server_time = nt.server_t;
}

/* the days ahead fetched so far: i from 0; 0 when there is no i-th */
int net_ahead(int i, char word[6], int *number, char date[11]) {
    if (i >= nt.n_ahead) return 0;
    memcpy(word, nt.ahead[i].word, 6);
    *number = nt.ahead[i].number;
    memcpy(date, nt.ahead[i].date, 11);
    return 1;
}

int net_finished(void) { return nt.finished; }

void net_cancel(void) {
    nt.cancel = 1;
    if (nt.state == NET_BUSY) nt.state = NET_IDLE;
}

void net_shutdown(void) {
    nt.cancel = 1;
    join();
}
