# ─────────────────────────────────────────────────────────
# pidash — Makefile
# Target: Raspberry Pi (ARMv7/ARMv8), Debian/Raspbian
# ─────────────────────────────────────────────────────────

TARGET   := pidash
CC       := gcc

# ARM-optimised flags
# On Pi 4 (Cortex-A72): use -mcpu=cortex-a72 -mfpu=neon-fp-armv8
# On Pi 3 (Cortex-A53): use -mcpu=cortex-a53
# On Pi 5 (Cortex-A76): use -mcpu=cortex-a76
ARCHFLAGS ?= -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard

CFLAGS := -O2 -pipe -fno-plt \
           $(ARCHFLAGS) \
           -Wall -Wextra -Wshadow \
           -Iinclude \
           $(shell pkg-config --cflags freetype2)

LDFLAGS := -lm -lpthread \
            $(shell pkg-config --libs freetype2)

SRCS := src/main.c  \
        src/gfx.c   \
        src/data.c  \
        src/pages.c \
        src/ring.c

OBJS := $(SRCS:src/%.c=build/%.o)

.PHONY: all clean install run

all: build/. $(TARGET)

build/.:
	mkdir -p build

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✓ Built $(TARGET)"

clean:
	rm -rf build $(TARGET)

# Install as a systemd service
install: $(TARGET)
	@[ "$(shell id -u)" = "0" ] || { echo "Run: sudo make install"; exit 1; }
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	install -m 644 pidash.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable --now pidash
	@echo "✓ Service installed and started"

run: $(TARGET)
	@echo "Running on $(FB_DEV) — Ctrl-C to stop"
	FB_DEV=/dev/fb1 ./$(TARGET)
