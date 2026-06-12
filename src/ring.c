#include "dashboard.h"
#include <string.h>

void ring_push(ring_t *r, double v) {
    r->buf[r->head] = v;
    r->head = (r->head + 1) % HIST_LEN;
    if (r->count < HIST_LEN) r->count++;

    // rolling max: recompute every time (HIST_LEN is small)
    r->max = 0.0;
    for (int i = 0; i < r->count; i++) {
        int idx = (r->head - 1 - i + HIST_LEN) % HIST_LEN;
        if (r->buf[idx] > r->max) r->max = r->buf[idx];
    }
    if (r->max < 1.0) r->max = 1.0;  // prevent div-by-zero
}

// age=0 → most recent sample
double ring_get(ring_t *r, int age) {
    if (age >= r->count) return 0.0;
    int idx = (r->head - 1 - age + HIST_LEN) % HIST_LEN;
    return r->buf[idx];
}
