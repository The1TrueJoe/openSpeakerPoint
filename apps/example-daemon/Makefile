CC ?= cc
CFLAGS ?= -O2 -Wall

example-daemon: main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f example-daemon

.PHONY: clean
