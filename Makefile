CC = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O2

OBJECTS = main.o server.o request.o response.o cgi.o

all: sws

sws: $(OBJECTS)
	@libs=''; case `uname -s` in SunOS) libs='-lsocket -lnsl';; esac; \
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $$libs

clean:
	rm -f sws $(OBJECTS)

.PHONY: all clean
