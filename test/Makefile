# Host build of the Swiss Ephemeris core library plus test programs.
#   make smoke && ./smoke
#   make compare && ./compare [reference.json]

SWE_DIR := ../third_party/swisseph
BUILD   := build

# Library sources = upstream's SWEOBJ list (third_party/swisseph/Makefile).
# Deliberately excluded: swetest.c, swemini.c, swevents.c, sweasp.c,
# swephgen4.c (each defines main()), sweclips.c, sweephe4.c.
SWE_SRCS := swedate swehouse swejpl swemmoon swemplan sweph swephlib swecl swehel
SWE_OBJS := $(addprefix $(BUILD)/swe/,$(addsuffix .o,$(SWE_SRCS)))

CC      ?= gcc
CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11
# Upstream code is not -Wextra clean; keep -Wall on for it, drop -Wextra noise.
SWE_CFLAGS := -O2 -g -Wall -std=gnu11 -fno-strict-aliasing
LDLIBS  := -lm

.PHONY: all smoke compare clean
all: smoke compare

$(BUILD)/swe/%.o: $(SWE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SWE_CFLAGS) -I$(SWE_DIR) -c $< -o $@

$(BUILD)/libswe.a: $(SWE_OBJS)
	ar rcs $@ $^

smoke: smoke.c smoke_expected.h $(BUILD)/libswe.a
	$(CC) $(CFLAGS) -I$(SWE_DIR) smoke.c $(BUILD)/libswe.a $(LDLIBS) -o $@

compare: compare.c ../core/panchangam.c ../core/panchangam.h $(BUILD)/libswe.a
	$(CC) $(CFLAGS) -I$(SWE_DIR) compare.c ../core/panchangam.c $(BUILD)/libswe.a $(LDLIBS) -o $@

clean:
	rm -rf $(BUILD) smoke compare
