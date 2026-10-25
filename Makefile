CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=gnu11
LDLIBS  = -lgmp

cfg: cfg.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test: cfg
	./cfg selftest expr.txt
	./cfg selftest dyck.txt
	./cfg selftest ebnf.txt
	./cfg selftest ops.txt
	./cfg selftest finite.txt

clean:
	rm -f cfg

.PHONY: test clean
