#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>

#include "dashboard.h"
#include "gfx.h"

// ─────────────────────────────────────────────────────────
// PAGE FUNCTION DECLARATIONS
// ─────────────────────────────────────────────────────────

void draw_page_cpu(uint16_t *buf, sys_state_t *d);
void draw_page_disk_hdd(uint16_t *buf, sys_state_t *d);
void draw_page_disk_other(uint16_t *buf, sys_state_t *d);
void draw_page_network(uint16_t *buf, sys_state_t *d);
void draw_page_docker(uint16_t *buf, sys_state_t *d);
void draw_page_tailscale(uint16_t *buf, sys_state_t *d);

// DATA THREAD
void *data_thread(void *arg);

// ─────────────────────────────────────────────────────────
// TRANSITION: horizontal wipe (cosmetic, minimal CPU)
// ─────────────────────────────────────────────────────────

static void wipe_transition(uint16_t *fb, uint16_t *old_bb, uint16_t *new_bb) {
    // 8-step swipe: each step copies a slice
    for (int step = 0; step <= 8; step++) {
        int split = (WIDTH * step) / 8;
        // new frame occupies [0, split), old occupies [split, WIDTH)
        for (int y = 0; y < HEIGHT; y++) {
            memcpy(&fb[y*WIDTH],
                   &new_bb[y*WIDTH],
                   split * 2);
            memcpy(&fb[y*WIDTH + split],
                   &old_bb[y*WIDTH + split],
                   (WIDTH - split) * 2);
        }
        usleep(16000);  // ~60fps for each step ≈ 130ms total
    }
}

// ─────────────────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────────────────

int main(void) {

    // ── framebuffer ──────────────────────────────────────

    int fd = open(FB_DEV, O_RDWR);
    if (fd < 0) { perror("fb0 open"); return 1; }

    size_t fb_size = WIDTH * HEIGHT * sizeof(uint16_t);

    uint16_t *fbp = mmap(0, fb_size,
                         PROT_READ|PROT_WRITE,
                         MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) { perror("mmap"); return 1; }

    // double-buffer: bb = current, prev_bb = last frame
    uint16_t *bb      = malloc(fb_size);
    uint16_t *prev_bb = malloc(fb_size);
    if (!bb || !prev_bb) { fputs("oom\n",stderr); return 1; }

    memset(bb,      0, fb_size);
    memset(prev_bb, 0, fb_size);

    // ── FreeType ─────────────────────────────────────────

    if (FT_Init_FreeType(&g_ft_lib)) {
        fputs("FreeType init failed\n",stderr); return 1;
    }
    if (FT_New_Face(g_ft_lib, FONT_PATH, 0, &g_ft_face)) {
        fputs("Font load failed: " FONT_PATH "\n",stderr); return 1;
    }

    // ── shared state ─────────────────────────────────────

    memset(&g_state, 0, sizeof(g_state));

    // ── data thread ──────────────────────────────────────

    pthread_t th;
    pthread_create(&th, NULL, data_thread, NULL);

    // ── render loop ──────────────────────────────────────

    int last_page = -1;

    while (1) {
        // ── snapshot state ────────────────────────────────

        sys_state_t local;
        pthread_mutex_lock(&g_lock);
        memcpy(&local, &g_state, sizeof(local));
        pthread_mutex_unlock(&g_lock);

        // ── advance page by time ──────────────────────────

        int page = ((int)(time(NULL)) / PAGE_TIME) % PAGE_COUNT;
        local.current_page = page;

        // ── clear back-buffer ─────────────────────────────

        fill_rect(bb, 0, 0, WIDTH, HEIGHT, C_BG);

        // ── draw active page ──────────────────────────────

        switch (page) {
            case PAGE_CPU:       draw_page_cpu(bb, &local);       break;
            case PAGE_DISK:       draw_page_disk_hdd(bb, &local);   break;
            case PAGE_DISK_OTHER: draw_page_disk_other(bb, &local); break;
            case PAGE_NETWORK:   draw_page_network(bb, &local);   break;
            case PAGE_DOCKER:    draw_page_docker(bb, &local);    break;
            case PAGE_TAILSCALE: draw_page_tailscale(bb, &local); break;
        }

        // ── page transition wipe ──────────────────────────

        if (page != last_page && last_page != -1) {
            wipe_transition(fbp, prev_bb, bb);
            last_page = page;
        } else {
            last_page = page;
            // direct blit
            memcpy(fbp, bb, fb_size);
        }

        // save for next transition
        memcpy(prev_bb, bb, fb_size);

        // 33ms ≈ 30fps render cadence
        usleep(33000);
    }

    // unreachable, but tidy
    free(bb);
    free(prev_bb);
    munmap(fbp, fb_size);
    close(fd);
    return 0;
}
