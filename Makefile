CC = gcc
CFLAGS = -Wall -Wextra -Wno-implicit-fallthrough -std=gnu23 -fPIC -O2
LDFLAGS = -shared -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=reallocarray -Wl,--wrap=free -Wl,--wrap=strdup -Wl,--wrap=strndup

.PHONY: all clean test

all: librstack.so

librstack.so: rstack.o memory_tests.o
	$(CC) $(LDFLAGS) -o $@ $^

rstack.o: rstack.c rstack.h memory_tests.h
	$(CC) $(CFLAGS) -c $< -o $@

memory_tests.o: memory_tests.c memory_tests.h
	$(CC) $(CFLAGS) -c $< -o $@

test: rstack_example.o rstack.o memory_tests.o
	$(CC) -O2 -o test_program $^ -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=reallocarray -Wl,--wrap=free -Wl,--wrap=strdup -Wl,--wrap=strndup

rstack_example.o: rstack_example.c rstack.h memory_tests.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o librstack.so test_program

