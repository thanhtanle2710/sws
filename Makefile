CC = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O2

OBJECTS = main.o server.o request.o response.o cgi.o
TEST_BINARIES = tests/test_sws tests/cgi_fixture

all: sws

sws: $(OBJECTS)
	@libs=''; case `uname -s` in SunOS) libs='-lsocket -lnsl';; esac; \
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $$libs

tests/test_sws: tests/test_sws.c
	@libs=''; case `uname -s` in SunOS) libs='-lsocket -lnsl';; esac; \
	$(CC) $(CFLAGS) -o $@ tests/test_sws.c $$libs

tests/cgi_fixture: tests/cgi_fixture.c
	$(CC) $(CFLAGS) -o $@ tests/cgi_fixture.c

test-build: $(TEST_BINARIES)

test: sws test-build
	./tests/test_sws

clean:
	rm -f sws $(OBJECTS) $(TEST_BINARIES)

.PHONY: all clean test test-build
