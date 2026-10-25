CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=gnu11
LDLIBS  = -lgmp

GRAMMARS = expr dyck ebnf ops finite

cfg: src/cfg.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test: cfg
	for g in $(GRAMMARS); do ./cfg selftest grammars/$$g.txt || exit 1; done

clean:
	rm -f cfg

.PHONY: test clean
