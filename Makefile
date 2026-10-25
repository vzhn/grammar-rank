CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=gnu11
LDLIBS  = -lgmp

SRCS    = $(wildcard src/*.c)
OBJS    = $(SRCS:.c=.o)
HDRS    = $(wildcard src/*.h)

GRAMMARS = expr.txt dyck.txt ebnf.txt ops.txt finite.txt iso-arith.ebnf exceptions.ebnf iso-14977.ebnf

cfg: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

test: cfg
	sh tests/arithmetic.sh ./cfg
	sh tests/iso.sh ./cfg
	for g in $(GRAMMARS); do ./cfg selftest grammars/$$g || exit 1; done

clean:
	rm -f cfg $(OBJS)

.PHONY: test clean
