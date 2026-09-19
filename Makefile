# Host build and test. Not used by the firmware build.
SWE  = third_party/swisseph
SWE_SRC = $(addprefix $(SWE)/,swedate.c swehouse.c swejpl.c swemmoon.c swemplan.c sweph.c swephlib.c swecl.c)
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD = build

.PHONY: test clean
test: $(BUILD)/compare
	$(BUILD)/compare $(REF)

REF ?= test/reference.json

$(BUILD)/compare: test/compare.c core/panchangam.c core/panchangam.h $(SWE_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -I$(SWE) -o $@ test/compare.c core/panchangam.c $(SWE_SRC) -lm

clean:
	rm -rf $(BUILD)
