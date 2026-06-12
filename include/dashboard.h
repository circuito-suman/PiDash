#pragma once

#include <stdint.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────
// DISPLAY
// ─────────────────────────────────────────────────────────

#define WIDTH       160
#define HEIGHT      128
#define FB_DEV      "/dev/fb0"
#define FONT_PATH   "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

// ─────────────────────────────────────────────────────────
// PALETTE  — RGB565
//
// Design intent: dark slate background, single accent colour
// per data type. Labels always in C_LABEL (mid-gray). Values
// always in C_WHITE. Accents never used for text on top of
// themselves.
//
//  Teal   → CPU bars, RX, disk reads, primary accent
//  Amber  → RAM, temperature, warnings
//  Rose   → SWP, TX, disk writes
//  Green  → running / healthy / online
//  Red    → stopped / unhealthy / offline
//  Blue   → Tailscale
// ─────────────────────────────────────────────────────────

// Backgrounds
#define C_BG          0x0841   // #080810  — near-black with blue tint
#define C_PANEL       0x10A2   // #101420  — panel surface (slightly lighter)
#define C_PANEL_EDGE  0x2124   // #212448  — subtle border / grid lines

// Text hierarchy  (always readable on C_BG / C_PANEL)
#define C_WHITE       0xFFFF   // primary values, names
#define C_LABEL       0x8C71   // mid-gray — section labels ("RAM", "C0", etc.)
#define C_DIM         0x528A   // dimmer — secondary info, free-space, IPs

// Data accent colours
#define C_TEAL        0x4EFF   // #4CFFFF  — softer cyan (was full 0x07FF which clashed)
#define C_TEAL_DIM    0x04F3   // fill for teal graphs
#define C_ROSE        0xFBAF   // #FF7777  — warm rose instead of hot magenta
#define C_ROSE_DIM    0x7806   // fill for rose graphs
#define C_AMBER       0xFD60   // #FFB000  — amber / warnings
#define C_GREEN       0x2FC6   // #2BFF30  — slightly desaturated green
#define C_RED         0xE8A4   // #E04040  — softer red (less eyeburn)
#define C_BLUE        0x5C9F   // #5CB8FF  — tailscale blue

// Header
#define C_HEADER_BG   0x18C3   // dark slate
#define C_HEADER_ACC  C_TEAL

// Bar tracks
#define C_BAR_TRACK   0x2124
#define C_BAR_CPU     C_TEAL
#define C_BAR_RAM     C_AMBER
#define C_BAR_SWP     C_ROSE

// Backwards-compat aliases (used in pages.c / gfx.c)
#define C_PINK        C_ROSE
#define C_PINK_DIM    C_ROSE_DIM
#define C_SUBTEXT     C_DIM

// ─────────────────────────────────────────────────────────
// GRAPH HISTORY
// ─────────────────────────────────────────────────────────

#define HIST_LEN  80

typedef struct {
    double buf[HIST_LEN];
    int    head;
    int    count;
    double max;
} ring_t;

void ring_push(ring_t *r, double v);
double ring_get(ring_t *r, int age);   // age=0 → newest

// ─────────────────────────────────────────────────────────
// METRICS STRUCTS
// ─────────────────────────────────────────────────────────

#define MAX_DISKS      6
#define MAX_IFACES     4
#define MAX_CONTAINERS 8
#define MAX_TS_PEERS   16

typedef struct {
    char   name[32];
    char   mount[64];
    double read_bps;
    double write_bps;
    long   total_bytes;
    long   free_bytes;
    int    present;
    ring_t hist_r;
    ring_t hist_w;
} disk_t;

typedef struct {
    char   name[16];
    char   ip[40];
    double rx_bps;
    double tx_bps;
    int    present;
    ring_t hist_rx;
    ring_t hist_tx;
} iface_t;

typedef struct {
    char name[64];
    char status[16];
    char health[16];
    char image[64];
    long cpu_pct_x100;
    long mem_bytes;
    long mem_limit;
    int  changed;
} container_t;

typedef struct {
    char name[64];
    char ts_ip[40];
    char os[32];
    int  online;
} ts_peer_t;

typedef struct {
    float      cpu[4];
    ring_t     cpu_hist[4];

    long       mem_total;
    long       mem_used;
    long       swap_total;
    long       swap_used;

    float      cpu_temp;
    ring_t     temp_hist;

    disk_t     disks[MAX_DISKS];
    int        disk_count;

    iface_t    ifaces[MAX_IFACES];
    int        iface_count;
    char       local_ip[40];

    container_t containers[MAX_CONTAINERS];
    int         container_count;

    char        ts_ip[40];
    int         ts_up;
    ts_peer_t   ts_peers[MAX_TS_PEERS];
    int         ts_peer_count;
    ring_t      ts_activity;

    int         current_page;
} sys_state_t;

// ─────────────────────────────────────────────────────────
// PAGES
// ─────────────────────────────────────────────────────────

#define PAGE_COUNT    6
#define PAGE_TIME     7

#define PAGE_CPU      0
#define PAGE_DISK     1
#define PAGE_DISK_OTHER  2
#define PAGE_NETWORK  3
#define PAGE_DOCKER   4
#define PAGE_TAILSCALE 5


// ─────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────

extern sys_state_t g_state;
extern pthread_mutex_t g_lock;
