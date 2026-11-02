# DSplay - Nintendo DSi home menu
#
#   make            -> build/HyperLauncher.nds (+ copies to assets/payloads/)
#   make test       -> host unit tests (library, launcher recipes, text, i18n)
#   make sim        -> host simulator that renders screenshots of the UI
#   make test-studio -> DSplay Studio (desktop app) tests
#   make studio     -> start DSplay Studio
#
# Requires devkitPro (devkitARM + calico + libnds + libfat-nds + ndstool) for `make`.
# Install: https://devkitpro.org/wiki/Getting_Started  ->  dkp-pacman -S nds-dev
# Keep devkitPro up to date (dkp-pacman -Syu): an older devkitARM together with a newer
# libfat-nds package crashes inside fatInitDefault().

.SUFFIXES:

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
SRCS_CORE := source/app.c source/cart_icons.c source/collections.c source/logo_data.c source/pad.c source/xfer.c source/photos.c source/photofx.c source/jpeg.c source/cheats.c source/apps.c source/store.c source/nearby.c source/touchcal.c source/profiles.c source/splash.c source/covers.c source/font_data.c source/gfx.c source/i18n.c \
             source/icon_data.c source/launcher.c source/library.c source/romdb.c \
             source/slot1.c source/sound_effects.c source/stats.c source/ui_widgets.c \
             source/upng.c

SRCS_ARM9 := $(SRCS_CORE) source/nds_loader_arm9.c source/main.c
# Wi-Fi + TCP/IP (devkitPro dswifi, calico version) for the Kumanda mode
WIFI_SRCS := $(wildcard third_party/dswifi/source/*.c third_party/dswifi/source/sgIP/*.c)
WIFI_OBJS := $(patsubst third_party/dswifi/source/%.c,build/wifi/%.o,$(WIFI_SRCS))
SRCS_TEST := $(filter-out source/app.c,$(SRCS_CORE)) tests/test_main.c
SRCS_SIM  := $(SRCS_CORE) source/sim_main.c
HEADERS   := $(wildcard include/*.h)

# --------------------------------------------------------------------------
# Host tools (tests / simulator)
# --------------------------------------------------------------------------
CC_HOST     ?= cc
CFLAGS_HOST := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude

# --------------------------------------------------------------------------
# devkitPro
# --------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
  ifneq ($(wildcard /opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc),)
    DEVKITPRO := /opt/devkitpro
  else ifneq ($(wildcard /tmp/dkp_root/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc),)
    DEVKITPRO := /tmp/dkp_root/opt/devkitpro
  else
    DEVKITPRO := /opt/devkitpro
  endif
endif
DEVKITARM ?= $(DEVKITPRO)/devkitARM
CALICO    := $(DEVKITPRO)/calico
LIBNDS    := $(DEVKITPRO)/libnds

CC_ARM   := $(DEVKITARM)/bin/arm-none-eabi-gcc
NDSTOOL  := $(DEVKITPRO)/tools/bin/ndstool
ICON_BMP := assets/icon.bmp

ARCH       := -mthumb -mthumb-interwork -march=armv5te -mtune=arm946e-s
CFLAGS_ARM := $(ARCH) -std=gnu11 -O2 -ffunction-sections -fdata-sections -ffast-math \
              -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
              -DARM9 -D__NDS__ -Iinclude -Ithird_party/dswifi/include -I$(LIBNDS)/include -I$(CALICO)/include
LDFLAGS_ARM := $(ARCH) -specs=$(CALICO)/share/ds9.specs -Wl,--gc-sections \
               -L$(CALICO)/lib -L$(LIBNDS)/lib
LIBS_ARM := -lfat -lnds9 -lcalico_ds9 -lm

TITLE := DSplay;Home menu;for Nintendo DSi

.PHONY: all nds test sim clean fonts studio test-studio test-pad

all: nds

nds: build/HyperLauncher.nds

build/HyperLauncher.elf: $(SRCS_ARM9) $(HEADERS) $(WIFI_OBJS)
	@mkdir -p build
	$(CC_ARM) $(CFLAGS_ARM) $(SRCS_ARM9) $(WIFI_OBJS) $(LDFLAGS_ARM) $(LIBS_ARM) -o $@

build/wifi/%.o: third_party/dswifi/source/%.c
	@mkdir -p $(dir $@)
	$(CC_ARM) $(ARCH) -std=gnu11 -O2 -ffunction-sections -fdata-sections -w -DARM9 -D__NDS__ \
	    -Ithird_party/dswifi/include -Ithird_party/dswifi/source -I$(LIBNDS)/include -I$(CALICO)/include -c $< -o $@

# DSplay's own ARM7 (calico's default one + the "power off" request of the power menu)
build/arm7.elf: arm7/main.c
	@mkdir -p build
	$(CC_ARM) -mthumb -mthumb-interwork -mcpu=arm7tdmi -std=gnu11 -O2 -ffunction-sections -fdata-sections \
	    -DARM7 -D__NDS__ -I$(CALICO)/include arm7/main.c -specs=$(CALICO)/share/ds7.specs \
	    -Wl,-T,arm7/ds7-lld.ld -Wl,--gc-sections -L$(CALICO)/lib -lcalico_ds7 -o $@

# BOOT.NDS uses DSplay's own ARM7 (arm7/main.c: POWER-button menu, "Turn off").
# DSplay_yedek.nds is the same menu with calico's stock ARM7, kept as a fallback.
build/HyperLauncher.nds: build/HyperLauncher.elf build/arm7.elf $(ICON_BMP)
	$(NDSTOOL) -c $@ -9 $< -7 build/arm7.elf -b $(ICON_BMP) "$(TITLE)"
	$(NDSTOOL) -c build/DSplay_yedek.nds -9 $< -7 $(CALICO)/bin/ds7_maine.elf -b $(ICON_BMP) "$(TITLE)"
	@mkdir -p assets/payloads
	@cp $@ assets/payloads/HyperLauncher.nds
	@cp $@ assets/payloads/BOOT.NDS
	@cp build/DSplay_yedek.nds assets/payloads/DSplay_yedek.nds
	@echo "Built $@ (also copied to assets/payloads/BOOT.NDS)"

test: build/tests
	./build/tests

build/tests: $(SRCS_TEST) $(HEADERS)
	@mkdir -p build
	$(CC_HOST) $(CFLAGS_HOST) $(SRCS_TEST) -lm -o $@

studio:
	python3 launcher.py

test-studio:
	python3 tests/test_studio.py
	python3 tests/test_safety.py

# Kumanda: the console code (host build) against Studio's receiver over UDP on this computer
build/pad_host: $(filter-out source/app.c,$(SRCS_CORE)) tests/pad_host.c $(HEADERS)
	@mkdir -p build
	$(CC_HOST) $(CFLAGS_HOST) $(filter-out source/app.c,$(SRCS_CORE)) tests/pad_host.c -lm -o $@

test-pad: build/pad_host
	python3 tests/test_pad.py

sim: build/sim

build/sim: $(SRCS_SIM) $(HEADERS)
	@mkdir -p build
	$(CC_HOST) $(CFLAGS_HOST) $(SRCS_SIM) -lm -o $@

# Regenerate the font / icon tables (needs Python 3 + Pillow and the Inter fonts)
fonts:
	python3 tools/gen_font.py
	python3 tools/gen_icons.py

clean:
	rm -rf build
