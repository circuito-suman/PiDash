#include "gfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FT_Library g_ft_lib;
FT_Face    g_ft_face;

// ─────────────────────────────────────────────────────────
// PRIMITIVES
// ─────────────────────────────────────────────────────────

void fill_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c) {
    for (int yy = y; yy < y+h; yy++) {
        if ((unsigned)yy >= HEIGHT) continue;
        for (int xx = x; xx < x+w; xx++) {
            if ((unsigned)xx >= WIDTH) continue;
            buf[yy*WIDTH+xx] = c;
        }
    }
}

void draw_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c) {
    for (int i = 0; i < w; i++) { px(buf,x+i,y,c); px(buf,x+i,y+h-1,c); }
    for (int i = 0; i < h; i++) { px(buf,x,y+i,c); px(buf,x+w-1,y+i,c); }
}

void draw_hline(uint16_t *buf, int x, int y, int len, uint16_t c) {
    for (int i = 0; i < len; i++) px(buf, x+i, y, c);
}

void draw_vline(uint16_t *buf, int x, int y, int len, uint16_t c) {
    for (int i = 0; i < len; i++) px(buf, x, y+i, c);
}

void px_blend(uint16_t *buf, int x, int y, uint16_t fg, uint8_t a) {
    if ((unsigned)x >= WIDTH || (unsigned)y >= HEIGHT) return;
    if (a == 0)   return;
    if (a == 255) { buf[y*WIDTH+x] = fg; return; }

    uint16_t bg = buf[y*WIDTH+x];
    uint32_t rb  = bg  & 0xF81F, g  = bg  & 0x07E0;
    uint32_t frb = fg  & 0xF81F, fg2 = fg & 0x07E0;
    rb  += ((frb - rb)  * a) >> 8;
    g   += ((fg2 - g)   * a) >> 8;
    buf[y*WIDTH+x] = (rb & 0xF81F) | (g & 0x07E0);
}

// ─────────────────────────────────────────────────────────
// TEXT
// FreeType renders glyphs as 8-bit alpha maps.
// We support basic Latin (U+0020..U+00FF) only.
// Characters outside that range are silently skipped —
// this prevents the "square block" artefact caused by
// missing glyphs when byte values like 0xB0 (degree sign)
// are passed as raw Latin-1 through the unsigned-char path.
// Use the ASCII suffix " C" instead of the degree symbol.
// ─────────────────────────────────────────────────────────

// Internal: render a single Unicode codepoint
static void draw_glyph(uint16_t *buf, int *pen_x, int baseline_y,
                        int clip_x, int clip_right,
                        uint32_t codepoint, uint16_t color) {
    if (FT_Load_Char(g_ft_face, codepoint, FT_LOAD_RENDER)) return;
    FT_GlyphSlot slot = g_ft_face->glyph;
    int adv = (slot->advance.x >> 6);
    // clip: if glyph would exceed right boundary, stop
    if (*pen_x + (int)slot->bitmap.width > clip_right) {
        *pen_x += adv;
        return;
    }
    for (int row = 0; row < (int)slot->bitmap.rows; row++)
        for (int col = 0; col < (int)slot->bitmap.width; col++)
            px_blend(buf,
                *pen_x + slot->bitmap_left + col,
                baseline_y - slot->bitmap_top + row,
                color,
                slot->bitmap.buffer[row * slot->bitmap.width + col]);
    *pen_x += adv;
}

int text_width(const char *s, int size) {
    FT_Set_Pixel_Sizes(g_ft_face, 0, size);
    int w = 0;
    while (*s) {
        unsigned char ch = (unsigned char)*s;
        // skip non-printable / high bytes that lack glyphs
        if (ch >= 0x20 && ch <= 0x7E) {
            if (!FT_Load_Char(g_ft_face, ch, FT_LOAD_ADVANCE_ONLY))
                w += (g_ft_face->glyph->advance.x >> 6);
        }
        s++;
    }
    return w;
}

void draw_text(uint16_t *buf, int x, int y, int max_w,
               const char *s, uint16_t color, int size) {
    FT_Set_Pixel_Sizes(g_ft_face, 0, size);
    int pen = x;
    int clip_right = x + max_w;
    while (*s) {
        unsigned char ch = (unsigned char)*s;
        // only render printable ASCII; skip anything else silently
        if (ch >= 0x20 && ch <= 0x7E)
            draw_glyph(buf, &pen, y, x, clip_right, ch, color);
        s++;
    }
}

void draw_text_right(uint16_t *buf, int x, int y, int max_w,
                     const char *s, uint16_t color, int size) {
    int tw = text_width(s, size);
    int ox = x + max_w - tw;
    if (ox < x) ox = x;
    draw_text(buf, ox, y, max_w, s, color, size);
}

// ─────────────────────────────────────────────────────────
// PANEL — 2px left accent stripe, dark fill, dim border
// ─────────────────────────────────────────────────────────

void draw_panel(uint16_t *buf, int x, int y, int w, int h, uint16_t accent) {
    fill_rect(buf, x, y, w, h, C_PANEL);
    fill_rect(buf, x, y, 2, h, accent);
    draw_rect(buf, x, y, w, h, C_PANEL_EDGE);
}

// ─────────────────────────────────────────────────────────
// GRAPH — filled area chart, newest sample on right
// ─────────────────────────────────────────────────────────

void draw_graph(uint16_t *buf,
                int gx, int gy, int gw, int gh,
                ring_t *r,
                uint16_t line_col, uint16_t fill_col) {
    if (r->count < 2) return;

    // subtle 50% grid line
    int grid_y = gy + gh/2;
    for (int x = gx; x < gx+gw; x += 4)
        px(buf, x, grid_y, C_PANEL_EDGE);

    double scale = r->max > 0 ? (double)(gh-1) / r->max : 1.0;

    for (int col = 0; col < gw; col++) {
        int age = gw - 1 - col;
        if (age >= r->count) continue;

        double v  = ring_get(r, age);
        int    ph = (int)(v * scale);
        if (ph > gh-1) ph = gh-1;

        int px_x = gx + col;
        int top  = gy + gh - 1 - ph;

        for (int row = top; row < gy+gh; row++)
            px_blend(buf, px_x, row, fill_col, row == top ? 200 : 70);
        px(buf, px_x, top, line_col);
    }
}

void draw_graph2(uint16_t *buf,
                 int gx, int gy, int gw, int gh,
                 ring_t *r1, uint16_t c1,
                 ring_t *r2, uint16_t c2) {
    double mx = r1->max > r2->max ? r1->max : r2->max;
    if (mx < 1.0) mx = 1.0;
    double scale = (double)(gh-1) / mx;

    int grid_y = gy + gh/2;
    for (int x = gx; x < gx+gw; x += 4)
        px(buf, x, grid_y, C_PANEL_EDGE);

    int count = r1->count < r2->count ? r1->count : r2->count;

    for (int col = 0; col < gw; col++) {
        int age = gw - 1 - col;
        if (age >= count) continue;

        double v1 = ring_get(r1, age);
        double v2 = ring_get(r2, age);
        int px_x = gx + col;

        int h1 = (int)(v1 * scale); if (h1 > gh-1) h1 = gh-1;
        int h2 = (int)(v2 * scale); if (h2 > gh-1) h2 = gh-1;

        int top1 = gy + gh - 1 - h1;
        int top2 = gy + gh - 1 - h2;

        for (int row = top1; row < gy+gh; row++)
            px_blend(buf, px_x, row, c1, row == top1 ? 200 : 65);
        for (int row = top2; row < gy+gh; row++)
            px_blend(buf, px_x, row, c2, row == top2 ? 200 : 50);

        px(buf, px_x, top1, c1);
        px(buf, px_x, top2, c2);
    }
}

// ─────────────────────────────────────────────────────────
// PROGRESS BAR
// ─────────────────────────────────────────────────────────

void draw_bar(uint16_t *buf, int x, int y, int w, int h,
              float pct, uint16_t fill, uint16_t track) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    fill_rect(buf, x, y, w, h, track);
    int fw = (int)((pct / 100.f) * w);
    if (fw > 0) fill_rect(buf, x, y, fw, h, fill);
}

// ─────────────────────────────────────────────────────────
// HEADER
//
// Layout  (160px wide, 14px tall):
//   [3px accent] [title 8px, max 88px] ... [temp 8px, 36px] [dots 5×5px each]
//
//   Total right section: 5 dots × 6px gap = 30px  → dots from x=130..159
//   Temperature: right-aligned in x=90..128  (38px wide)  ← always visible
//
// Temperature uses ASCII "degC" notation ("42.1 C") — no Unicode degree
// symbol — so it renders cleanly with any monospace font without missing-
// glyph boxes.
// ─────────────────────────────────────────────────────────

void draw_header(uint16_t *buf, const char *title,
                 float temp, int page, int page_count) {

    // header background
    fill_rect(buf, 0, 0, WIDTH, 14, C_HEADER_BG);

    // 3px teal left accent stripe
    fill_rect(buf, 0, 0, 3, 14, C_TEAL);

    // title — capped at 86px so it never reaches the temp zone
    draw_text(buf, 6, 10, 86, title, C_WHITE, 8);

    // ── temperature ──────────────────────────────────────
    // Format: "42.1C" (no degree symbol — avoids missing-glyph blocks)
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%.1fC", temp);
    uint16_t tc = (temp >= 70.f) ? C_RED
                : (temp >= 55.f) ? C_AMBER
                :                  C_TEAL;
    // Right-align in the 38px zone just left of the dots
    draw_text_right(buf, 78, 10, 38, tmp, tc, 8);

    // ── page indicator dots ───────────────────────────────
    // 4px squares, 6px apart, flush right (last dot ends at x=158)
    int dot_area_start = WIDTH - 2 - page_count * 6;
    for (int i = 0; i < page_count; i++) {
        uint16_t dc = (i == page) ? C_TEAL : C_BAR_TRACK;
        fill_rect(buf, dot_area_start + i*6, 5, 4, 4, dc);
    }

    // bottom separator line
    draw_hline(buf, 0, 14, WIDTH, C_PANEL_EDGE);
}
