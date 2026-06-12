#include "dashboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────

sys_state_t     g_state;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────

static void shell(const char *cmd, char *out, int len) {
    out[0] = '\0';
    FILE *f = popen(cmd, "r");
    if (!f) { snprintf(out, len, "N/A"); return; }
    if (!fgets(out, len, f)) snprintf(out, len, "N/A");
    pclose(f);
    out[strcspn(out, "\n")] = 0;
}

static long long read_ll_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long long v = 0;
    fscanf(f, "%lld", &v);
    fclose(f);
    return v;
}

// ─────────────────────────────────────────────────────────
// CPU
// ─────────────────────────────────────────────────────────

static void collect_cpu(sys_state_t *s) {
    static long long lt[4] = {0}, li[4] = {0};
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[256];
    int core = 0;
    while (fgets(line, sizeof(line), f) && core < 4) {
        if (strncmp(line, "cpu", 3) || line[3] == ' ') continue;
        long long u,n,sy,id,iw,irq,sirq;
        sscanf(line, "cpu%*d %lld%lld%lld%lld%lld%lld%lld",
               &u,&n,&sy,&id,&iw,&irq,&sirq);
        long long tot = u+n+sy+id+iw+irq+sirq;
        long long dt  = tot - lt[core];
        long long di  = id  - li[core];
        if (dt > 0) s->cpu[core] = 100.f * (1.f - (float)di / dt);
        lt[core] = tot; li[core] = id;
        core++;
    }
    fclose(f);
}

// ─────────────────────────────────────────────────────────
// MEMORY
// ─────────────────────────────────────────────────────────

static void collect_mem(sys_state_t *s) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char key[64]; long val;
    long free = 0, buffers = 0, cached = 0, sfree = 0;
    while (fscanf(f, "%63s %ld kB", key, &val) == 2) {
        if (!strcmp(key, "MemTotal:"))   s->mem_total  = val * 1024;
        if (!strcmp(key, "MemFree:"))    free           = val * 1024;
        if (!strcmp(key, "Buffers:"))    buffers        = val * 1024;
        if (!strcmp(key, "Cached:"))     cached         = val * 1024;
        if (!strcmp(key, "SwapTotal:"))  s->swap_total  = val * 1024;
        if (!strcmp(key, "SwapFree:"))   sfree          = val * 1024;
    }
    fclose(f);
    // available = free + buffers + cached (matches `free` command)
    s->mem_used  = s->mem_total - free - buffers - cached;
    s->swap_used = s->swap_total - sfree;
}

// ─────────────────────────────────────────────────────────
// TEMPERATURE
// ─────────────────────────────────────────────────────────

static float collect_temp(void) {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 0.f;
    int t = 0; fscanf(f, "%d", &t); fclose(f);
    return t / 1000.f;
}

// ─────────────────────────────────────────────────────────
// DISKS
//
// Strategy: scan /proc/mounts for real mounted partitions,
// resolve their parent block device for I/O stats, then show
// the mount point as the label.  This correctly handles:
//   sda1  → /mnt/stash_data
//   sdb1  → /mnt/nextcloud_data
//   mmcblk0p2 → /
//
// We skip pseudo/virtual filesystems and only count real block
// devices under /dev/sd*, /dev/mmcblk*, /dev/nvme*.
// ─────────────────────────────────────────────────────────

// Strip trailing digits (and optional 'p' + digits) to get
// the parent block device name.
// e.g. "sda1" → "sda",  "mmcblk0p2" → "mmcblk0",  "nvme0n1p1" → "nvme0n1"
static void parent_blkdev(const char *part, char *parent, int plen) {
    strncpy(parent, part, plen - 1);
    parent[plen - 1] = '\0';
    int n = strlen(parent);

    // nvme: strip 'p' + digits suffix if present
    // mmcblk: same pattern
    // sd*: just strip trailing digits
    // Try 'p\d+' suffix first
    int i = n - 1;
    while (i > 0 && isdigit((unsigned char)parent[i])) i--;
    if (i > 0 && parent[i] == 'p' && i != (int)(strspn(parent,"abcdefghijklmnopqrstuvwxyz0123456789")-1)) {
        // has a 'p' before digits — mmcblk / nvme style
        parent[i] = '\0';
    } else {
        // sd* style — strip trailing digits
        while (n > 0 && isdigit((unsigned char)parent[n-1])) n--;
        parent[n] = '\0';
    }
}

static void read_blk_stat(const char *dev, long long *r, long long *w) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/block/%s/stat", dev);
    FILE *f = fopen(path, "r");
    if (!f) { *r = *w = 0; return; }
    // fields: read_ios read_merges read_sectors read_ticks
    //         write_ios write_merges write_sectors ...
    fscanf(f, "%*d%*d%lld%*d%*d%*d%lld", r, w);
    fclose(f);
}

// Virtual / pseudo filesystems to skip
static int is_real_fs(const char *fstype) {
    const char *skip[] = {
        "tmpfs","devtmpfs","sysfs","proc","devpts","cgroup",
        "cgroup2","pstore","bpf","tracefs","debugfs","hugetlbfs",
        "mqueue","fusectl","overlay","squashfs","nsfs",NULL
    };
    for (int i = 0; skip[i]; i++)
        if (!strcmp(fstype, skip[i])) return 0;
    return 1;
}

static void collect_disks(sys_state_t *s, double dt_sec) {
    // per-device previous sector counts; keyed by slot index
    static long long prev_r[MAX_DISKS] = {0};
    static long long prev_w[MAX_DISKS] = {0};
    static char      prev_dev[MAX_DISKS][32] = {""};

    FILE *mf = fopen("/proc/mounts", "r");
    if (!mf) return;

    int cnt = 0;
    char dev[128], mnt[256], fstype[32], opts[256];

    while (fscanf(mf, "%127s%255s%31s%255s%*d%*d",
                  dev, mnt, fstype, opts) == 4 && cnt < MAX_DISKS) {

        // only real /dev/* block devices
        if (strncmp(dev, "/dev/", 5) != 0) continue;
        if (!is_real_fs(fstype)) continue;

        const char *partname = dev + 5;   // e.g. "sda1", "mmcblk0p2"

        // must be a disk we recognise
        if (strncmp(partname,"sd",2)     != 0 &&
            strncmp(partname,"mmcblk",6) != 0 &&
            strncmp(partname,"nvme",4)   != 0) continue;

        disk_t *d = &s->disks[cnt];
        memset(d, 0, sizeof(*d));

        // store partition name (for display) and mount point
        strncpy(d->name,  partname,  sizeof(d->name)  - 1);
        strncpy(d->mount, mnt,       sizeof(d->mount) - 1);
        d->present = 1;

        // derive parent block device for I/O stats
        char blkdev[32];
        parent_blkdev(partname, blkdev, sizeof(blkdev));

        long long cur_r = 0, cur_w = 0;
        read_blk_stat(blkdev, &cur_r, &cur_w);

        // match to previous slot by device name to keep rates stable
        int slot = cnt;   // default: reuse by position
        for (int k = 0; k < MAX_DISKS; k++) {
            if (!strcmp(prev_dev[k], blkdev)) { slot = k; break; }
        }
        strncpy(prev_dev[slot], blkdev, sizeof(prev_dev[0]) - 1);

        d->read_bps  = (cur_r - prev_r[slot]) * 512.0 / dt_sec;
        d->write_bps = (cur_w - prev_w[slot]) * 512.0 / dt_sec;
        if (d->read_bps  < 0) d->read_bps  = 0;
        if (d->write_bps < 0) d->write_bps = 0;
        prev_r[slot] = cur_r;
        prev_w[slot] = cur_w;

        // preserve ring history across ticks (copy from g_state)
        // (memset above zeroed them, so restore from global if same dev)
        pthread_mutex_lock(&g_lock);
        for (int k = 0; k < g_state.disk_count; k++) {
            if (!strcmp(g_state.disks[k].name, d->name)) {
                d->hist_r = g_state.disks[k].hist_r;
                d->hist_w = g_state.disks[k].hist_w;
                break;
            }
        }
        pthread_mutex_unlock(&g_lock);

        ring_push(&d->hist_r, d->read_bps);
        ring_push(&d->hist_w, d->write_bps);

        // disk space via statvfs on mount point
        struct statvfs sv;
        if (statvfs(mnt, &sv) == 0) {
            d->total_bytes = (long)sv.f_blocks * (long)sv.f_frsize;
            d->free_bytes  = (long)sv.f_bavail * (long)sv.f_frsize;
        }

        cnt++;
    }
    fclose(mf);


// Sort: put mmcblk* last so HDDs appear first
for (int i = 0; i < cnt - 1; i++) {
    for (int j = i+1; j < cnt; j++) {
        if (strncmp(s->disks[i].name,"mmcblk",6)==0 &&
            strncmp(s->disks[j].name,"mmcblk",6)!=0) {
            disk_t tmp = s->disks[i];
            s->disks[i] = s->disks[j];
            s->disks[j] = tmp;
        }
    }
}
    s->disk_count = cnt;
}

// ─────────────────────────────────────────────────────────
// NETWORK
//
// Only show interfaces that are actually useful:
//   - skip: lo, docker0, br-*, veth*, tailscale0
//   - keep: wlan0, eth0, and similar physical/vpn ifaces
//   - tailscale gets its own page, so skip tailscale0 here
//
// IP is taken directly from the interface that has one.
// ─────────────────────────────────────────────────────────

static int should_skip_iface(const char *name) {
    // exact matches
    const char *exact[] = {"lo", "docker0", NULL};
    for (int i = 0; exact[i]; i++)
        if (!strcmp(name, exact[i])) return 1;

    // prefix matches
    const char *pfx[] = {"br-", "veth", "tailscale", "tun", "virbr", NULL};
    for (int i = 0; pfx[i]; i++)
        if (!strncmp(name, pfx[i], strlen(pfx[i]))) return 1;

    return 0;
}

static long long read_net_bytes(const char *iface, const char *dir) {
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/class/net/%s/statistics/%s_bytes", iface, dir);
    return read_ll_file(path);
}

// Read the IPv4 address for an interface from /proc/net/fib_trie
// (avoids shelling out to ip/ifconfig)
static void get_iface_ip(const char *iface, char *out, int outlen) {
    out[0] = '\0';
    // Use /proc/net/if_inet6 is for IPv6 only; simplest approach:
    // read from /sys/../address is MAC only.
    // Use a small shell — but fast: just grep /proc/net/fib_trie
    // Alternative: read /proc/net/arp — not useful here.
    // Easiest reliable method on Pi: read from hostname -I filtered by iface
    // We'll use the ip command which is always available on Pi OS.
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "ip -4 addr show %s 2>/dev/null | awk '/inet /{split($2,a,\"/\");print a[1];exit}'",
             iface);
    shell(cmd, out, outlen);
}

static void collect_net(sys_state_t *s, double dt_sec) {
    static long long prev_rx[MAX_IFACES] = {0};
    static long long prev_tx[MAX_IFACES] = {0};
    static char      prev_name[MAX_IFACES][16] = {""};

    DIR *nd = opendir("/sys/class/net");
    if (!nd) return;

    int cnt = 0;
    struct dirent *de;

    while ((de = readdir(nd)) && cnt < MAX_IFACES) {
        if (de->d_name[0] == '.') continue;
        if (should_skip_iface(de->d_name)) continue;

        iface_t *ifc = &s->ifaces[cnt];
        memset(ifc, 0, sizeof(*ifc));
        strncpy(ifc->name, de->d_name, sizeof(ifc->name) - 1);
        ifc->present = 1;

        long long rx = read_net_bytes(ifc->name, "rx");
        long long tx = read_net_bytes(ifc->name, "tx");

        // match previous slot by name for stable rates
        int slot = cnt;
        for (int k = 0; k < MAX_IFACES; k++) {
            if (!strcmp(prev_name[k], ifc->name)) { slot = k; break; }
        }
        strncpy(prev_name[slot], ifc->name, sizeof(prev_name[0]) - 1);

        ifc->rx_bps = (double)(rx - prev_rx[slot]) / dt_sec;
        ifc->tx_bps = (double)(tx - prev_tx[slot]) / dt_sec;
        if (ifc->rx_bps < 0) ifc->rx_bps = 0;
        if (ifc->tx_bps < 0) ifc->tx_bps = 0;
        prev_rx[slot] = rx;
        prev_tx[slot] = tx;

        // restore ring history
        pthread_mutex_lock(&g_lock);
        for (int k = 0; k < g_state.iface_count; k++) {
            if (!strcmp(g_state.ifaces[k].name, ifc->name)) {
                ifc->hist_rx = g_state.ifaces[k].hist_rx;
                ifc->hist_tx = g_state.ifaces[k].hist_tx;
                break;
            }
        }
        pthread_mutex_unlock(&g_lock);

        ring_push(&ifc->hist_rx, ifc->rx_bps);
        ring_push(&ifc->hist_tx, ifc->tx_bps);

        // get IP for this interface
        get_iface_ip(ifc->name, ifc->ip, sizeof(ifc->ip));

        cnt++;
    }
    closedir(nd);
    s->iface_count = cnt;

    // local_ip = IP of first interface that has one
    s->local_ip[0] = '\0';
    for (int i = 0; i < cnt; i++) {
        if (s->ifaces[i].ip[0]) {
            strncpy(s->local_ip, s->ifaces[i].ip, sizeof(s->local_ip) - 1);
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────
// DOCKER
//
// Optimised: one single `docker stats --no-stream` call
// returns all containers in one shot instead of N sequential
// calls.  Format string pulls Name, CPUPerc, MemUsage in
// one pass; health needs a separate inspect but batched.
// ─────────────────────────────────────────────────────────

static void collect_docker(sys_state_t *s) {
    // ── Step 1: list containers with status/image ─────────
    FILE *f = popen(
        "docker ps -a --format '{{.ID}}|{{.Names}}|{{.Status}}|{{.Image}}' 2>/dev/null",
        "r");
    if (!f) { s->container_count = 0; return; }

    int cnt = 0;
    char line[256];
    char ids[MAX_CONTAINERS][16];

    while (fgets(line, sizeof(line), f) && cnt < MAX_CONTAINERS) {
        line[strcspn(line, "\n")] = 0;
        container_t *c = &s->containers[cnt];
        memset(c, 0, sizeof(*c));
        char id[16] = "";
        sscanf(line, "%15[^|]|%63[^|]|%15[^|]|%63[^\n]",
               id, c->name, c->status, c->image);
        strncpy(ids[cnt], id, sizeof(ids[0]) - 1);

        if      (strstr(c->status, "Up"))     strcpy(c->status, "running");
        else if (strstr(c->status, "Exited")) strcpy(c->status, "exited");
        else                                  strcpy(c->status, "dead");

        strcpy(c->health, "none");
        cnt++;
    }
    pclose(f);
    s->container_count = cnt;
    if (cnt == 0) return;

    // ── Step 2: ONE docker stats call for all containers ──
    // Output format: "name|cpu%"  e.g. "myapp|3.42%"
    // We use --format to get Name and CPUPerc together.
    FILE *sf = popen(
        "docker stats --no-stream --format '{{.Name}}|{{.CPUPerc}}' 2>/dev/null",
        "r");
    if (sf) {
        char sline[128];
        while (fgets(sline, sizeof(sline), sf)) {
            sline[strcspn(sline, "\n")] = 0;
            char cname[64] = "", cpct[16] = "";
            if (sscanf(sline, "%63[^|]|%15s", cname, cpct) != 2) continue;
            // remove trailing '%'
            cpct[strcspn(cpct, "%")] = 0;
            double pct = atof(cpct);
            for (int i = 0; i < cnt; i++) {
                if (!strcmp(s->containers[i].name, cname)) {
                    s->containers[i].cpu_pct_x100 = (long)(pct * 100);
                    break;
                }
            }
        }
        pclose(sf);
    }

    // ── Step 3: batch health via single docker inspect ────
    // Build space-separated name list
    char namelist[MAX_CONTAINERS * 66];
    namelist[0] = '\0';
    for (int i = 0; i < cnt; i++) {
        strncat(namelist, s->containers[i].name,
                sizeof(namelist) - strlen(namelist) - 2);
        strcat(namelist, " ");
    }
    // One inspect call: outputs "name|health" per line
    char icmd[512];
    snprintf(icmd, sizeof(icmd),
        "docker inspect --format '{{.Name}}|{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' %s 2>/dev/null",
        namelist);
    FILE *hf = popen(icmd, "r");
    if (hf) {
        char hline[128];
        while (fgets(hline, sizeof(hline), hf)) {
            hline[strcspn(hline, "\n")] = 0;
            char hname[68] = "", hstatus[16] = "";
            if (sscanf(hline, "%67[^|]|%15s", hname, hstatus) != 2) continue;
            // docker inspect prefixes name with '/'
            const char *n = hname[0] == '/' ? hname + 1 : hname;
            for (int i = 0; i < cnt; i++) {
                if (!strcmp(s->containers[i].name, n)) {
                    strncpy(s->containers[i].health, hstatus,
                            sizeof(s->containers[i].health) - 1);
                    break;
                }
            }
        }
        pclose(hf);
    }
}

// ─────────────────────────────────────────────────────────
// TAILSCALE
// ─────────────────────────────────────────────────────────

static void collect_tailscale(sys_state_t *s) {
    shell("tailscale ip -4 2>/dev/null", s->ts_ip, sizeof(s->ts_ip));
    s->ts_up = (s->ts_ip[0] && strcmp(s->ts_ip, "N/A") != 0);

    s->ts_peer_count = 0;
    FILE *f = popen("tailscale status --peers 2>/dev/null", "r");
    if (!f) return;

    char line[256];
    int cnt = 0;
    while (fgets(line, sizeof(line), f) && cnt < MAX_TS_PEERS) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        ts_peer_t *p = &s->ts_peers[cnt];
        char status[32] = "";
        sscanf(line, "%39s %63s %*s %31s %31s",
               p->ts_ip, p->name, p->os, status);
        p->online = (strstr(status, "active")  != NULL ||
                     strstr(status, "direct")  != NULL ||
                     strstr(status, "relay")   != NULL);
        cnt++;
    }
    pclose(f);
    s->ts_peer_count = cnt;
}

// ─────────────────────────────────────────────────────────
// DATA THREAD
// ─────────────────────────────────────────────────────────

void *data_thread(void *arg) {
    (void)arg;

    struct timespec last_ts, now;
    clock_gettime(CLOCK_MONOTONIC, &last_ts);

    int tick = 0;

    while (1) {
        usleep(1000000);   // 1-second tick

        clock_gettime(CLOCK_MONOTONIC, &now);
        double dt = (now.tv_sec  - last_ts.tv_sec)
                  + (now.tv_nsec - last_ts.tv_nsec) * 1e-9;
        last_ts = now;
        if (dt < 0.001) dt = 1.0;

        // Work on a local copy — avoid holding lock during slow I/O
        sys_state_t tmp;
        pthread_mutex_lock(&g_lock);
        memcpy(&tmp, &g_state, sizeof(tmp));
        pthread_mutex_unlock(&g_lock);

        // ── fast (every tick, ~1s) ─────────────────────
        collect_cpu(&tmp);
        collect_mem(&tmp);
        tmp.cpu_temp = collect_temp();

        for (int i = 0; i < 4; i++) ring_push(&tmp.cpu_hist[i], tmp.cpu[i]);
        ring_push(&tmp.temp_hist, tmp.cpu_temp);

        collect_disks(&tmp, dt);
        collect_net(&tmp, dt);

        // ── medium (every 5s) ──────────────────────────
        // Docker stats is now a single batched call — much faster
        if (tick % 5 == 0) collect_docker(&tmp);

        // ── slow (every 30s) ──────────────────────────
        // Tailscale status can be slow; 30s is plenty
        if (tick % 30 == 0) collect_tailscale(&tmp);

        // ── commit ─────────────────────────────────────
        pthread_mutex_lock(&g_lock);
        tmp.current_page = g_state.current_page;
        memcpy(&g_state, &tmp, sizeof(tmp));
        pthread_mutex_unlock(&g_lock);

        tick++;
    }
    return NULL;
}