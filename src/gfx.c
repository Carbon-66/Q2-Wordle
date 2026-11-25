/* Drawing into the 375 x 320 XRGB frame: boxes (rounded and antialiased), lines, and text from
 * the baked fonts (font_data.c), plain or scaled for the tiles' flips and pops. */
#include <string.h>

#include "wd.h"

uint32_t fb[VIEW_W * VIEW_H];

uint32_t mix(uint32_t a, uint32_t b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    uint32_t rb = ((a & 0xff00ff) * (unsigned)(256 - t) + (b & 0xff00ff) * (unsigned)t) >> 8 & 0xff00ff;
    uint32_t g = ((a & 0x00ff00) * (unsigned)(256 - t) + (b & 0x00ff00) * (unsigned)t) >> 8 & 0x00ff00;
    return rb | g;
}

static inline void plot(int x, int y, uint32_t c, int alpha) {
    if ((unsigned)x >= VIEW_W || (unsigned)y >= VIEW_H || alpha <= 0) return;
    uint32_t *p = &fb[y * VIEW_W + x];
    *p = alpha >= 256 ? c : mix(*p, c, alpha);
}

void fill(uint32_t c) {
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) fb[i] = c;
}

void rect_a(int x, int y, int w, int h, uint32_t c, int alpha) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > VIEW_W ? VIEW_W : x + w, y1 = y + h > VIEW_H ? VIEW_H : y + h;
    if (alpha <= 0) return;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t *p = &fb[yy * VIEW_W];
        for (int xx = x0; xx < x1; ++xx) p[xx] = alpha >= 256 ? c : mix(p[xx], c, alpha);
    }
}

void rect(int x, int y, int w, int h, uint32_t c) { rect_a(x, y, w, h, c, 256); }

void frame_box(int x, int y, int w, int h, int t, uint32_t c) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - 2 * t, c);
    rect(x + w - t, y + t, t, h - 2 * t, c);
}

void darken(uint32_t c, int alpha) {
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) fb[i] = mix(fb[i], c, alpha);
}

static float fclamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* coverage of the rounded box at a pixel centre: distance to the corner circle, eased over 1 px */
void rrect(float x, float y, float w, float h, float r, uint32_t c, int alpha) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w + 1), y1 = (int)(y + h + 1);
    for (int py = y0; py < y1; ++py) {
        float cy = py + 0.5f;
        float vy = fclamp(cy + 0.5f - y, 0, 1) * fclamp(y + h - cy + 0.5f, 0, 1); /* edges' partial rows */
        for (int px = x0; px < x1; ++px) {
            float cx = px + 0.5f;
            float cov = vy * fclamp(cx + 0.5f - x, 0, 1) * fclamp(x + w - cx + 0.5f, 0, 1);
            float dx = cx < x + r ? x + r - cx : cx > x + w - r ? cx - (x + w - r) : 0;
            float dy = cy < y + r ? y + r - cy : cy > y + h - r ? cy - (y + h - r) : 0;
            if (dx > 0 && dy > 0) {
                float d = __builtin_sqrtf(dx * dx + dy * dy);
                cov *= fclamp(r - d + 0.5f, 0, 1);
            }
            if (cov > 0) plot(px, py, c, (int)(cov * (float)alpha));
        }
    }
}

void line(float x0, float y0, float x1, float y1, float w, uint32_t c, int alpha) {
    float minx = (x0 < x1 ? x0 : x1) - w, maxx = (x0 > x1 ? x0 : x1) + w;
    float miny = (y0 < y1 ? y0 : y1) - w, maxy = (y0 > y1 ? y0 : y1) + w;
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int py = (int)miny; py <= (int)maxy; ++py)
        for (int px = (int)minx; px <= (int)maxx; ++px) {
            float cx = px + 0.5f - x0, cy = py + 0.5f - y0;
            float t = len2 > 0 ? fclamp((cx * dx + cy * dy) / len2, 0, 1) : 0;
            float ex = cx - t * dx, ey = cy - t * dy;
            float d = __builtin_sqrtf(ex * ex + ey * ey);
            float cov = fclamp(w / 2 - d + 0.5f, 0, 1);
            if (cov > 0) plot(px, py, c, (int)(cov * (float)alpha));
        }
}

void disc(float cx, float cy, float r, uint32_t c, int alpha) {
    for (int py = (int)(cy - r - 1); py <= (int)(cy + r + 1); ++py)
        for (int px = (int)(cx - r - 1); px <= (int)(cx + r + 1); ++px) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float cov = fclamp(r - __builtin_sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
            if (cov > 0) plot(px, py, c, (int)(cov * (float)alpha));
        }
}

/* ---------------------------------------------------------------------------------------------
 * Text */

static const Glyph *glyph(const Font *f, char ch) {
    if (ch < 32 || ch > 126) ch = '?';
    return &f->g[ch - 32];
}

int text_w(int font, const char *s) {
    const Font *f = fonts[font];
    int w = 0;
    for (; *s; ++s) w += glyph(f, *s)->adv;
    return w;
}

static void draw_glyph(const Glyph *g, int x, int y, uint32_t c, int alpha) {
    const unsigned char *src = font_data + g->off;
    for (int r = 0; r < g->h; ++r)
        for (int k = 0; k < g->w; ++k) {
            int a = src[r * g->w + k];
            if (a) plot(x + g->x + k, y + g->y + r, c, a == 255 ? alpha : a * alpha / 255);
        }
}

int text(int font, const char *s, int x, int y, uint32_t c, int align) {
    const Font *f = fonts[font];
    int w = text_w(font, s);
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    for (; *s; ++s) {
        const Glyph *g = glyph(f, *s);
        draw_glyph(g, x, y, c, 256);
        x += g->adv;
    }
    return w;
}

/* each target pixel samples the glyph's coverage bilinearly */
void text_scaled(int font, const char *s, float cx, float cy, int sx, int sy, uint32_t c, int alpha) {
    if (sx <= 0 || sy <= 0) return;
    const Font *f = fonts[font];
    float kx = (float)sx / 256, ky = (float)sy / 256;
    float pen = cx - (float)text_w(font, s) * kx / 2;
    float base = cy + (float)f->cap * ky / 2;
    for (; *s; ++s) {
        const Glyph *g = glyph(f, *s);
        const unsigned char *src = font_data + g->off;
        float gx0 = pen + (float)g->x * kx, gy0 = base + (float)g->y * ky;
        float gw = (float)g->w * kx, gh = (float)g->h * ky;
        for (int py = (int)gy0; py <= (int)(gy0 + gh); ++py)
            for (int px = (int)gx0; px <= (int)(gx0 + gw); ++px) {
                float u = (px + 0.5f - gx0) / kx - 0.5f, v = (py + 0.5f - gy0) / ky - 0.5f;
                int iu = (int)(u + 1) - 1, iv = (int)(v + 1) - 1;
                float fu = u - (float)iu, fv = v - (float)iv;
                float sum = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        int qx = iu + i, qy = iv + j;
                        if (qx < 0 || qy < 0 || qx >= g->w || qy >= g->h) continue;
                        sum += (float)src[qy * g->w + qx] * (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
                    }
                if (sum > 0.5f) plot(px, py, c, (int)(sum * (float)alpha / 255));
            }
        pen += (float)g->adv * kx;
    }
}

/* greedy word wrap; '\n' breaks */
int text_wrap(int font, const char *s, int x, int y, int w, int lh, uint32_t c) {
    char buf[160];
    int lines = 0;
    while (*s) {
        int n = 0, cut = 0, width = 0;
        const Font *f = fonts[font];
        while (s[n] && s[n] != '\n') {
            int a = glyph(f, s[n])->adv;
            if (width + a > w && cut) break;
            if (s[n] == ' ') cut = n;
            width += a, ++n;
        }
        if (s[n] && s[n] != '\n' && cut) n = cut;
        if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
        memcpy(buf, s, (unsigned)n), buf[n] = 0;
        text(font, buf, x, y + lines * lh, c, -1);
        ++lines;
        s += n;
        if (*s == ' ' || *s == '\n') ++s;
    }
    return lines;
}
