CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=gnu11
LDLIBS  = -lgmp

SRCS    = $(wildcard src/*.c)
OBJS    = $(SRCS:.c=.o)
HDRS    = $(wildcard src/*.h)

GRAMMARS = expr dyck ebnf ops finite

cfg: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

test: cfg
	sh tests/arithmetic.sh ./cfg
	for g in $(GRAMMARS); do ./cfg selftest grammars/$$g.txt || exit 1; done

clean:
	rm -f cfg $(OBJS)

.PHONY: test clean
