#pragma once

#include <stdint.h>
#include "dashboard.h"

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

extern FT_Library g_ft_lib;
extern FT_Face    g_ft_face;

// ─── primitives ───────────────────────────────────────────

static inline void px(uint16_t *buf, int x, int y, uint16_t c) {
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        buf[y * WIDTH + x] = c;
}

void fill_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c);
void draw_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c);
void draw_hline(uint16_t *buf, int x, int y, int len, uint16_t c);
void draw_vline(uint16_t *buf, int x, int y, int len, uint16_t c);

// Alpha-blend a pixel (alpha 0-255)
void px_blend(uint16_t *buf, int x, int y, uint16_t fg, uint8_t a);

// ─── text ─────────────────────────────────────────────────

// Returns advance width without drawing
int text_width(const char *s, int px_size);

// Draw, clip at x+max_w
void draw_text(uint16_t *buf, int x, int y, int max_w,
               const char *s, uint16_t color, int px_size);

// Right-align within [x, x+max_w)
void draw_text_right(uint16_t *buf, int x, int y, int max_w,
                     const char *s, uint16_t color, int px_size);

// ─── panel ────────────────────────────────────────────────

// Filled panel with 1px accent border on left edge
void draw_panel(uint16_t *buf, int x, int y, int w, int h, uint16_t accent);

// ─── graph ────────────────────────────────────────────────

// Filled area-chart from ring buffer, newest=right
void draw_graph(uint16_t *buf,
                int gx, int gy, int gw, int gh,
                ring_t *r,
                uint16_t line_col,
                uint16_t fill_col);

// Two-series overlay (e.g. rx + tx)
void draw_graph2(uint16_t *buf,
                 int gx, int gy, int gw, int gh,
                 ring_t *r1, uint16_t c1,
                 ring_t *r2, uint16_t c2);

// ─── progress bar ─────────────────────────────────────────

void draw_bar(uint16_t *buf, int x, int y, int w, int h,
              float pct,   // 0-100
              uint16_t fill, uint16_t track);

// ─── header ───────────────────────────────────────────────

// Universal page header: title left, temp right, dots bottom-right
void draw_header(uint16_t *buf, const char *title,
                 float temp, int page, int page_count);
