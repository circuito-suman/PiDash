# pidash — Raspberry Pi TFT Dashboard

A lightweight, non-blocking realtime system dashboard for Raspberry Pi
with a 160×128 RGB565 TFT framebuffer display.

```
┌─────────────────────────────────────────────────────────────────────┐
│  ARCHITECTURE                                                        │
│                                                                      │
│  ┌──────────────┐  1s tick   ┌──────────────────────────────────┐  │
│  │  data_thread │ ─────────► │  sys_state_t  (mutex-protected)  │  │
│  │              │            └──────────────┬───────────────────┘  │
│  │  fast (1s):  │                           │  snapshot             │
│  │  • CPU/mem   │            ┌──────────────▼───────────────────┐  │
│  │  • temp      │            │  render loop  (33ms / ~30fps)    │  │
│  │  • net/disk  │            │  • fill backbuffer               │  │
│  │              │            │  • draw active page              │  │
│  │  medium (5s):│            │  • wipe transition on page flip  │  │
│  │  • Docker    │            │  • memcpy → /dev/fb0             │  │
│  │              │            └──────────────────────────────────┘  │
│  │  slow (10s): │                                                   │
│  │  • Tailscale │                                                   │
│  └──────────────┘                                                   │
└─────────────────────────────────────────────────────────────────────┘
```

## Pages (7s each, auto-advance)

| # | Page      | Content                                          |
|---|-----------|--------------------------------------------------|
| 0 | CPU & MEM | 4-core bars + sparklines, RAM/SWP bars, temp     |
| 1 | DISK      | R/W speed, used%, free space, dual area graph    |
| 2 | NETWORK   | Per-iface RX/TX speeds, IP, dual-colour graph    |
| 3 | DOCKER    | Container list, status dots, CPU%, health badge  |
| 4 | TAILSCALE | Status, local TS IP, peer list with OS tags      |

## Colour Palette (RGB565)

| Role       | Hex (RGB888) | Usage                          |
|------------|--------------|--------------------------------|
| Background | #081218      | Page fill                      |
| Teal       | #00FFFF      | Primary accent, read, RX       |
| Pink       | #FF00FF      | Write, TX                      |
| Amber      | #FFA000      | Temperature, RAM, warnings     |
| Green      | #00FF00      | Running / healthy / connected  |
| Red        | #FF0000      | Error / stopped / disconnected |
| Blue       | #3D9F--      | Tailscale accent               |

## Design Principles

- **Accent-left panels**: every panel has a 2px coloured left stripe
  (teal for primary, pink for secondary) — gives visual rhythm without
  wasted space.
- **Dual-colour graphs**: read vs write, RX vs TX rendered as overlapping
  translucent area fills so both series are readable simultaneously.
- **Temperature colour-coding**: teal → amber → red as temperature rises
  through 55°C and 70°C thresholds, shown in every page header.
- **Non-blocking architecture**: render loop never calls any syscall that
  could block; all I/O is on a separate thread. CPU overhead ≈ 1-2%.

---

## Hardware Requirements

- Raspberry Pi (any model with ARMv7/v8)
- TFT display connected as `/dev/fb0` or `/dev/fb1`
- Common tested displays:
  - **Waveshare 1.8" 160×128** (ST7735, SPI)
  - **Adafruit 1.8" TFT Breakout** (ST7735)
  - Any framebuffer device with 160×128 RGB565

### SPI / fbtft setup (if not already configured)

Add to `/boot/config.txt`:
```ini
# ST7735 160x128 display on SPI0
dtoverlay=spi0-1cs
dtoverlay=st7735r,speed=40000000,fps=60,width=160,height=128
```

Or for the generic `fbtft` driver:
```bash
# /etc/modprobe.d/fbtft.conf
options fbtft_device name=adafruit18 rotate=90
```

---

## Dependencies

```bash
# Raspberry Pi OS (Debian-based)
sudo apt update
sudo apt install -y \
    gcc make \
    libfreetype6-dev \
    pkg-config \
    docker.io \
    tailscale

# Verify FreeType is found
pkg-config --libs freetype2
```

---

## Build

```bash
git clone <repo> pidash && cd pidash

# Pi 4 (Cortex-A72)
make ARCHFLAGS="-mcpu=cortex-a72"
# Pi 3 (Cortex-A53) 32bit
make ARCHFLAGS="-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard"

# Pi 5 (Cortex-A76, AArch64 — use aarch64-linux-gnu-gcc) 
make CC=aarch64-linux-gnu-gcc ARCHFLAGS="-mcpu=cortex-a76"

# Generic (safe default)
make ARCHFLAGS=""
```

---

## Run

```bash
# Test on /dev/fb1 (second framebuffer = TFT)
sudo FB_DEV=/dev/fb1 ./pidash

# Or just /dev/fb0 if TFT is primary
sudo ./pidash
```

---

## Install as Service (auto-start on boot)

```bash
# Edit pidash.service and set correct FB_DEV if needed
sudo make install

# Check status
sudo systemctl status pidash
sudo journalctl -u pidash -f
```

---

## Customisation

### Change page duration
Edit in `include/dashboard.h`:
```c
#define PAGE_TIME  7    // seconds per page
```

### Add more Docker containers
Automatically discovered — all containers visible to `docker ps -a`.

### Change display device
Edit `include/dashboard.h`:
```c
#define FB_DEV  "/dev/fb1"
```

### Adjust font size
In `src/pages.c`, the last argument to `draw_text()` is pixel size.
Recommended range for 160×128: 7–10px.

### Tune polling intervals
In `src/data.c`, `data_thread()`:
```c
if (tick % 5  == 0)  collect_docker(&tmp);     // every 5s
if (tick % 10 == 0)  collect_tailscale(&tmp);  // every 10s
```

---

## File Structure

```
pidash/
├── include/
│   ├── dashboard.h     # types, constants, extern globals
│   └── gfx.h           # graphics API declarations
├── src/
│   ├── main.c          # framebuffer setup, render loop, transitions
│   ├── gfx.c           # drawing primitives, text, graphs, bars
│   ├── data.c          # data thread, all metric collectors
│   ├── pages.c         # 5 page renderers
│   └── ring.c          # ring buffer for time-series history
├── Makefile
├── pidash.service      # systemd unit
└── README.md
```

---

## Performance Notes

| Metric        | Typical value         |
|---------------|-----------------------|
| CPU (idle)    | < 1% on Pi 4         |
| CPU (busy)    | ~2–4% during render  |
| RAM           | ~6–8 MB RSS           |
| Render fps    | ~30 fps               |
| Data latency  | 1s (fast), 5s/10s slow |

The render loop and data thread run fully independent — a slow Docker
query (which can take 1-2s) never delays the display.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `fb0 open: Permission denied` | Run with `sudo` or add user to `video` group |
| Black screen | Check `fbset /dev/fb0` — confirm 160x128 16bpp |
| Font fail | Install `fonts-dejavu-core` or change `FONT_PATH` |
| Tailscale shows N/A | Install tailscale, run `tailscale up` |
| Docker shows nothing | Ensure user is in `docker` group or run as root |

```bash
# Verify framebuffer settings
fbset -fb /dev/fb0 -i

# Expected output for 160x128 RGB565:
# geometry 160 128 160 128 16
# timings ...
# rgba 5/11,6/5,5/0,0/0
```
```bash
# My configurations

mode "160x128"
    geometry 160 128 160 128 16
    timings 0 0 0 0 0 0 0
    nonstd 1
    rgba 5/11,6/5,5/0,0/0
endmode

Frame buffer device information:
    Name        : fb_st7735r
    Address     : 0
    Size        : 40960
    Type        : PACKED PIXELS
    Visual      : TRUECOLOR
    XPanStep    : 0
    YPanStep    : 0
    YWrapStep   : 0
    LineLength  : 320
    Accelerator : No
'''
