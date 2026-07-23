# speakerpoint-control - HTTP control daemon for the SpeakerPoint.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += -std=c11 -D_GNU_SOURCE
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
