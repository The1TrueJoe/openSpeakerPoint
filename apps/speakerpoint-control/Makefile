# speakerpoint-control - HTTP control daemon for the SpeakerPoint.
#
# Buildroot invokes this Makefile's `make` with CFLAGS set as a command-line
# variable (via TARGET_CONFIGURE_OPTS), which always takes priority over a
# plain `CFLAGS +=` inside the Makefile - so -std=c11/-D_GNU_SOURCE would be
# silently dropped under the cross build even though a standalone host
# `make` (no command-line CFLAGS) picks them up fine. `override` forces them
# in regardless of what's passed on the command line.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
override CFLAGS += -std=c11 -D_GNU_SOURCE
LDFLAGS ?=

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

speakerpoint-control: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) speakerpoint-control

.PHONY: clean
