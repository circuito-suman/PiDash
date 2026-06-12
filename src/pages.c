#include "dashboard.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ─────────────────────────────────────────────────────────
// TYPOGRAPHY CONSTANTS
// Two sizes only — keeps visual rhythm consistent across all pages.
//   FONT_BODY  : values, names, anything the user reads
//   FONT_LABEL : small tags, units, secondary info
// ─────────────────────────────────────────────────────────
#define FONT_BODY   8
#define FONT_LABEL  7

// ─────────────────────────────────────────────────────────
// UTIL
// ─────────────────────────────────────────────────────────

static void fmt_bytes(double b, char *out, int out_len) {
    const char *u[] = {"B","KB","MB","GB","TB"};
    int i=0;
    while (b >= 1024.0 && i < 4) { b /= 1024.0; i++; }
    snprintf(out, out_len, "%.1f%s", b, u[i]);
}

static void fmt_speed(double b, char *out, int out_len) {
    const char *u[] = {"B/s","KB/s","MB/s","GB/s"};
    int i=0;
    while (b >= 1024.0 && i < 3) { b /= 1024.0; i++; }
    snprintf(out, out_len, "%.1f%s", b, u[i]);
}

// ─────────────────────────────────────────────────────────
// PAGE 0 — CPU & MEMORY
//
// Color rules:
//   Labels  ("C0","C1","RAM","SWP") → C_LABEL  (mid-gray)
//   Values  ("45%","1.2GB")         → C_WHITE
//   CPU bar → C_BAR_CPU  (teal)     bar text → C_WHITE
//   RAM bar → C_BAR_RAM  (amber)    pct text → C_AMBER
//   SWP bar → C_BAR_SWP  (rose)     pct text → C_ROSE
//   Sparkline → teal (same as CPU bar — consistent)
// ─────────────────────────────────────────────────────────

void draw_page_cpu(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "CPU & MEM", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    // ── 4 CPU rows ────────────────────────────────────────
    // Each row: 14px tall
    //   [x=2]  label "C0" in C_LABEL
    //   [x=16] bar 80px wide, 5px tall
    //   [x=98] pct value right-aligned in C_WHITE
    //   [x=118] sparkline 40px wide
    int y = 18;
    for (int i = 0; i < 4; i++, y += 14) {
        char lbl[4];
        snprintf(lbl, sizeof(lbl), "C%d", i);
        draw_text(buf, 2, y+8, 12, lbl, C_LABEL, FONT_LABEL);

        draw_bar(buf, 16, y+2, 78, 5, d->cpu[i], C_BAR_CPU, C_BAR_TRACK);

        char val[8];
        snprintf(val, sizeof(val), "%3.0f%%", d->cpu[i]);
        draw_text(buf, 96, y+8, 22, val, C_WHITE, FONT_LABEL);

        // sparkline — teal, same hue as bar
        draw_graph(buf, 120, y+1, 38, 11,
                   &d->cpu_hist[i], C_TEAL, C_TEAL_DIM);
    }

    // ── separator ─────────────────────────────────────────
    draw_hline(buf, 2, y, WIDTH-4, C_PANEL_EDGE);
    y += 3;

    // ── RAM bar ───────────────────────────────────────────
    float mem_pct = d->mem_total
        ? (float)d->mem_used / d->mem_total * 100.f : 0.f;
    draw_text(buf, 2, y+8, 20, "RAM", C_LABEL, FONT_LABEL);
    draw_bar(buf, 24, y+2, 110, 5, mem_pct, C_BAR_RAM, C_BAR_TRACK);
    char pa[8]; snprintf(pa, sizeof(pa), "%3.0f%%", mem_pct);
    draw_text_right(buf, 136, y+8, 22, pa, C_AMBER, FONT_LABEL);
    y += 12;

    // ── SWP bar ───────────────────────────────────────────
    float swp_pct = d->swap_total
        ? (float)d->swap_used / d->swap_total * 100.f : 0.f;
    draw_text(buf, 2, y+8, 20, "SWP", C_LABEL, FONT_LABEL);
    draw_bar(buf, 24, y+2, 110, 5, swp_pct, C_BAR_SWP, C_BAR_TRACK);
    char pb[8]; snprintf(pb, sizeof(pb), "%3.0f%%", swp_pct);
    draw_text_right(buf, 136, y+8, 22, pb, C_ROSE, FONT_LABEL);
    y += 12;

    // ── memory detail ─────────────────────────────────────
    char ma[16], mb[16], mline[36];
    fmt_bytes(d->mem_used,  ma, sizeof(ma));
    fmt_bytes(d->mem_total, mb, sizeof(mb));
    snprintf(mline, sizeof(mline), "%s / %s", ma, mb);
    draw_text(buf, 2, y+7, WIDTH-4, mline, C_DIM, FONT_LABEL);
}

// ─────────────────────────────────────────────────────────
// PAGE 1 — DISK
//
// Color rules:
//   Device name  → C_WHITE
//   Mount path   → C_DIM
//   Used% bar    → C_TEAL,  pct label → C_TEAL
//   Free label   → C_DIM
//   Read speed   → C_TEAL,  Write speed → C_ROSE
//   Graph        → teal (read) + rose (write)
// ─────────────────────────────────────────────────────────

// void draw_page_disk(uint16_t *buf, sys_state_t *d) {
//     draw_header(buf, "DISK", d->cpu_temp, d->current_page, PAGE_COUNT);
//     fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

//     if (d->disk_count == 0) {
//         draw_text(buf, 4, 68, WIDTH-8, "No block devices found", C_DIM, FONT_BODY);
//         return;
//     }

//     int shown   = d->disk_count < 2 ? d->disk_count : 2;
//     int panel_h = (HEIGHT - 17) / shown;

//     for (int i = 0; i < shown; i++) {
//         disk_t *dk = &d->disks[i];
//         int py = 16 + i * panel_h;

//         draw_panel(buf, 1, py, WIDTH-2, panel_h-2, C_TEAL);

//         int tx = 5;   // x start after accent stripe

//         // ── mount point (primary label) + device name (dim, right) ──
//         // Show mount point prominently; device name secondary
//         const char *mnt_label = dk->mount[0] ? dk->mount : dk->name;
//         draw_text(buf, tx, py+9, 96, mnt_label, C_WHITE, FONT_BODY);

//         char devshort[16];
//         snprintf(devshort, sizeof(devshort), "%s", dk->name);
//         draw_text_right(buf, 100, py+9, WIDTH-102, devshort, C_DIM, FONT_LABEL);

//         // ── used% bar ──
//         float used_pct = dk->total_bytes
//             ? (float)(dk->total_bytes - dk->free_bytes)
//               / dk->total_bytes * 100.f : 0.f;
//         draw_bar(buf, tx, py+12, 100, 4, used_pct, C_TEAL, C_BAR_TRACK);

//         // pct label
//         char pct_s[8]; snprintf(pct_s, sizeof(pct_s), "%.0f%%", used_pct);
//         draw_text(buf, tx+103, py+17, 22, pct_s, C_TEAL, FONT_LABEL);

//         // free space
//         char fa[12];
//         fmt_bytes(dk->free_bytes, fa, sizeof(fa));
//         char free_s[20]; snprintf(free_s, sizeof(free_s), "free %s", fa);
//         draw_text_right(buf, 126, py+17, WIDTH-128, free_s, C_DIM, FONT_LABEL);

//         // ── R/W speeds ──
//         char rs[12], ws[12];
//         fmt_speed(dk->read_bps,  rs, sizeof(rs));
//         fmt_speed(dk->write_bps, ws, sizeof(ws));

//         char rline[18], wline[18];
//         snprintf(rline, sizeof(rline), "R %s", rs);
//         snprintf(wline, sizeof(wline), "W %s", ws);
//         draw_text(buf, tx,    py+26, 74, rline, C_TEAL, FONT_LABEL);
//         draw_text(buf, tx+74, py+26, 74, wline, C_ROSE, FONT_LABEL);

//         // ── dual R/W graph ──
//         int gh = panel_h - 30;
//         if (gh < 8) gh = 8;
//         draw_graph2(buf, tx, py+29, WIDTH-7, gh,
//                     &dk->hist_r, C_TEAL,
//                     &dk->hist_w, C_ROSE);
//     }

//     if (d->disk_count > 2) {
//         char more[20];
//         snprintf(more, sizeof(more), "+%d more", d->disk_count-2);
//         draw_text_right(buf, 0, HEIGHT-1, WIDTH-2, more, C_DIM, FONT_LABEL);
//     }
// }

// ── shared panel renderer (used by both disk pages) ──────
static void draw_disk_panel(uint16_t *buf, disk_t *dk, int py, int panel_h) {
    draw_panel(buf, 1, py, WIDTH-2, panel_h-2, C_TEAL);
    int tx = 5;

    const char *mnt_label = dk->mount[0] ? dk->mount : dk->name;
    draw_text(buf, tx, py+9, 96, mnt_label, C_WHITE, FONT_BODY);

    char devshort[16];
    snprintf(devshort, sizeof(devshort), "%s", dk->name);
    draw_text_right(buf, 100, py+9, WIDTH-102, devshort, C_DIM, FONT_LABEL);

    float used_pct = dk->total_bytes
        ? (float)(dk->total_bytes - dk->free_bytes) / dk->total_bytes * 100.f
        : 0.f;
    draw_bar(buf, tx, py+12, 100, 4, used_pct, C_TEAL, C_BAR_TRACK);

    char pct_s[8]; snprintf(pct_s, sizeof(pct_s), "%.0f%%", used_pct);
    draw_text(buf, tx+103, py+17, 22, pct_s, C_TEAL, FONT_LABEL);

    char fa[12];
    fmt_bytes(dk->free_bytes, fa, sizeof(fa));
    char free_s[20]; snprintf(free_s, sizeof(free_s), "free %s", fa);
    draw_text_right(buf, 126, py+17, WIDTH-128, free_s, C_DIM, FONT_LABEL);

    char rs[12], ws[12];
    fmt_speed(dk->read_bps,  rs, sizeof(rs));
    fmt_speed(dk->write_bps, ws, sizeof(ws));
    char rline[18], wline[18];
    snprintf(rline, sizeof(rline), "R %s", rs);
    snprintf(wline, sizeof(wline), "W %s", ws);
    draw_text(buf, tx,     py+26, 74, rline, C_TEAL, FONT_LABEL);
    draw_text(buf, tx+74,  py+26, 74, wline, C_ROSE, FONT_LABEL);

    int gh = panel_h - 30;
    if (gh < 8) gh = 8;
    draw_graph2(buf, tx, py+29, WIDTH-7, gh,
                &dk->hist_r, C_TEAL, &dk->hist_w, C_ROSE);
}

// ─────────────────────────────────────────────────────────
// PAGE 1 — HARD DISKS (sda, sdb)
// ─────────────────────────────────────────────────────────

void draw_page_disk_hdd(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "HARD DISKS", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    disk_t *disks[MAX_DISKS];
    int shown = 0;
    for (int i = 0; i < d->disk_count && shown < MAX_DISKS; i++)
        if (strncmp(d->disks[i].name, "sd", 2) == 0)
            disks[shown++] = &d->disks[i];

    if (shown == 0) {
        draw_text(buf, 4, 68, WIDTH-8, "No HDDs found", C_DIM, FONT_BODY);
        return;
    }

    int panel_h = (HEIGHT - 17) / shown;
    for (int i = 0; i < shown; i++)
        draw_disk_panel(buf, disks[i], 16 + i * panel_h, panel_h);
}

// ─────────────────────────────────────────────────────────
// PAGE 2 — OTHER STORAGE (mmcblk, nvme, loop, etc.)
// ─────────────────────────────────────────────────────────

void draw_page_disk_other(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "STORAGE", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    disk_t *disks[MAX_DISKS];
    int shown = 0;
    for (int i = 0; i < d->disk_count && shown < MAX_DISKS; i++)
        if (strncmp(d->disks[i].name, "sd", 2) != 0)
            disks[shown++] = &d->disks[i];

    if (shown == 0) {
        draw_text(buf, 4, 68, WIDTH-8, "No other storage", C_DIM, FONT_BODY);
        return;
    }

    int panel_h = (HEIGHT - 17) / shown;
    for (int i = 0; i < shown; i++)
        draw_disk_panel(buf, disks[i], 16 + i * panel_h, panel_h);
}

// ─────────────────────────────────────────────────────────
// PAGE 2 — NETWORK
//
// Color rules:
//   Interface name → C_WHITE
//   IP address     → C_DIM
//   RX (download)  → C_TEAL,  TX (upload) → C_ROSE
//   Speed values   → same color as their direction
//   Graph          → teal (rx) + rose (tx)
// ─────────────────────────────────────────────────────────

void draw_page_network(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "NETWORK", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    if (d->iface_count == 0) {
        draw_text(buf, 4, 68, WIDTH-8, "No interfaces", C_DIM, FONT_BODY);
        return;
    }

    int shown   = d->iface_count < 2 ? d->iface_count : 2;
    int panel_h = (HEIGHT - 17) / shown;

    for (int i = 0; i < shown; i++) {
        iface_t *ifc = &d->ifaces[i];
        int py = 16 + i * panel_h;
        // primary iface = teal accent, secondary = blue
        uint16_t acc = (i == 0) ? C_TEAL : C_BLUE;

        draw_panel(buf, 1, py, WIDTH-2, panel_h-2, acc);

        int tx = 5;

        // interface name
        draw_text(buf, tx, py+9, 60, ifc->name, C_WHITE, FONT_BODY);

        // IP right-aligned in DIM
        draw_text_right(buf, 66, py+9, WIDTH-68,
                        ifc->ip[0] ? ifc->ip : "no ip",
                        C_DIM, FONT_LABEL);

        // mini relative bars (show rx vs tx relative to their peak)
        double mx = ifc->hist_rx.max > ifc->hist_tx.max
                  ? ifc->hist_rx.max : ifc->hist_tx.max;
        if (mx < 1) mx = 1;
        float rx_pct = (float)(ifc->rx_bps / mx * 100.f);
        float tx_pct = (float)(ifc->tx_bps / mx * 100.f);

        draw_bar(buf, tx, py+12, WIDTH-8, 3, rx_pct, C_TEAL, C_BAR_TRACK);
        draw_bar(buf, tx, py+16, WIDTH-8, 3, tx_pct, C_ROSE, C_BAR_TRACK);

        // speed labels — direction arrows + value, color-matched
        char rs[14], ts[14];
        fmt_speed(ifc->rx_bps, rs, sizeof(rs));
        fmt_speed(ifc->tx_bps, ts, sizeof(ts));

        char rl[20], tl[20];
        snprintf(rl, sizeof(rl), "v %s", rs);   // down arrow ASCII
        snprintf(tl, sizeof(tl), "^ %s", ts);   // up arrow ASCII
        draw_text(buf, tx,      py+27, 76, rl, C_TEAL, FONT_LABEL);
        draw_text(buf, tx+76,   py+27, 76, tl, C_ROSE, FONT_LABEL);

        // dual graph
        int gh = panel_h - 32;
        if (gh < 8) gh = 8;
        draw_graph2(buf, tx, py+30, WIDTH-7, gh,
                    &ifc->hist_rx, C_TEAL,
                    &ifc->hist_tx, C_ROSE);
    }
}

// ─────────────────────────────────────────────────────────
// PAGE 3 — DOCKER
//
// Color rules:
//   Container name  → C_WHITE
//   CPU %           → C_AMBER  (warning-tone, not info)
//   Status dot:
//     running   → C_GREEN
//     exited    → C_AMBER
//     dead/err  → C_RED
//   Health badge:
//     healthy   → C_GREEN
//     unhealthy → C_RED
//     none/???  → C_LABEL
//   Row divider → C_PANEL_EDGE
// ─────────────────────────────────────────────────────────

void draw_page_docker(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "DOCKER", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    if (d->container_count == 0) {
        draw_text(buf, 4, 68, WIDTH-8, "No containers found", C_DIM, FONT_BODY);
        return;
    }

    int shown = d->container_count < 6 ? d->container_count : 6;
    int row_h = (HEIGHT-17) / shown;
    if (row_h > 18) row_h = 18;

    for (int i = 0; i < shown; i++) {
        container_t *c = &d->containers[i];
        int ry = 16 + i * row_h;

        // status dot color
        uint16_t sc = !strcmp(c->status, "running") ? C_GREEN
                    : !strcmp(c->status, "exited")  ? C_AMBER
                    :                                  C_RED;
        fill_rect(buf, 3, ry+4, 4, 4, sc);

        // container name — C_WHITE, clipped to leave room for cpu+badge
        draw_text(buf, 10, ry+9, 86, c->name, C_WHITE, FONT_LABEL);

        // CPU % — amber (warning-tone number, not just info)
        char cpu_s[10];
        snprintf(cpu_s, sizeof(cpu_s), "%.1f%%", c->cpu_pct_x100 / 100.f);
        draw_text_right(buf, 96, ry+9, 26, cpu_s, C_AMBER, FONT_LABEL);

        // health badge (single char in a small box)
        uint16_t hc = !strcmp(c->health, "healthy")   ? C_GREEN
                    : !strcmp(c->health, "unhealthy")  ? C_RED
                    :                                    C_LABEL;
        char badge[2] = {'?', 0};
        if      (!strcmp(c->health, "healthy"))   badge[0] = 'H';
        else if (!strcmp(c->health, "unhealthy")) badge[0] = '!';

        fill_rect(buf, WIDTH-13, ry+2, 11, 9, C_PANEL);
        draw_text(buf, WIDTH-11, ry+9, 9, badge, hc, FONT_LABEL);

        if (i < shown-1)
            draw_hline(buf, 2, ry+row_h-1, WIDTH-4, C_PANEL_EDGE);
    }

    if (d->container_count > shown) {
        char more[20];
        snprintf(more, sizeof(more), "+%d more", d->container_count - shown);
        draw_text_right(buf, 0, HEIGHT-1, WIDTH-2, more, C_DIM, FONT_LABEL);
    }
}

// ─────────────────────────────────────────────────────────
// PAGE 4 — TAILSCALE
//
// Color rules:
//   Status banner text:
//     connected    → C_GREEN
//     disconnected → C_RED
//   TS IP          → C_BLUE  (distinct from general IPs)
//   Peer name      → C_WHITE
//   OS tag         → C_DIM
//   Online dot     → C_GREEN,  offline → C_PANEL_EDGE
//   Section labels → C_LABEL
// ─────────────────────────────────────────────────────────

void draw_page_tailscale(uint16_t *buf, sys_state_t *d) {
    draw_header(buf, "TAILSCALE", d->cpu_temp, d->current_page, PAGE_COUNT);
    fill_rect(buf, 0, 15, WIDTH, HEIGHT-15, C_BG);

    // ── status banner ─────────────────────────────────────
    uint16_t banner_c = d->ts_up ? C_GREEN : C_RED;
    const char *banner_s = d->ts_up ? "CONNECTED" : "DISCONNECTED";

    fill_rect(buf, 0, 16, WIDTH, 13, C_PANEL);
    fill_rect(buf, 0, 16, 3, 13, banner_c);   // accent stripe

    draw_text(buf, 6, 25, 80, banner_s, banner_c, FONT_BODY);

    // TS IP right-aligned in blue
    if (d->ts_up && d->ts_ip[0]) {
        draw_text_right(buf, 86, 25, WIDTH-88, d->ts_ip, C_BLUE, FONT_LABEL);
    }

    // separator
    draw_hline(buf, 2, 30, WIDTH-4, C_PANEL_EDGE);

    // ── peers header ──────────────────────────────────────
    draw_text(buf, 3, 38, 40, "PEERS", C_LABEL, FONT_LABEL);
    if (d->ts_up)
        draw_text_right(buf, 0, 38, WIDTH-3, "OS", C_LABEL, FONT_LABEL);

    // ── peer list ─────────────────────────────────────────
    int shown = d->ts_peer_count < 5 ? d->ts_peer_count : 5;
    int row_h = shown > 0 ? (HEIGHT - 42) / shown : 16;
    if (row_h > 16) row_h = 16;

    for (int i = 0; i < shown; i++) {
        ts_peer_t *p = &d->ts_peers[i];
        int ry = 41 + i * row_h;

        // online dot: filled = online, dim = offline
        uint16_t dot_c = p->online ? C_GREEN : C_PANEL_EDGE;
        fill_rect(buf, 3, ry+3, 4, 4, dot_c);

        // peer name in white
        draw_text(buf, 10, ry+9, 96, p->name, C_WHITE, FONT_LABEL);

        // OS first 4 chars, dim
        char os4[5]; strncpy(os4, p->os, 4); os4[4] = 0;
        draw_text_right(buf, 0, ry+9, WIDTH-3, os4, C_DIM, FONT_LABEL);
    }

    // ── empty states ──────────────────────────────────────
    if (shown == 0 && d->ts_up)
        draw_text(buf, 10, 55, WIDTH-12, "No peers visible", C_DIM, FONT_BODY);
    else if (!d->ts_up)
        draw_text(buf, 10, 55, WIDTH-12, "Run: tailscale up", C_DIM, FONT_BODY);
}