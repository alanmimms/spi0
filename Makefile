CFLAGS = -std=gnu99 -fPIC -I/usr/include/lua5.3 -Wno-pointer-to-int-cast
LDFLAGS = -shared -Llua5.3
DEBUGFLAGS = -g
DEBUGCFLAGS = -O0 -D_DEBUG

TARGETS = spiops.so
OBJS = spiops.o

all:	$(TARGETS)

spiops.o:	spiops.c
	$(CC) $(CFLAGS) $(DEBUGFLAGS) $(DEBUGCFLAGS) -c -o $@ $^

spiops.so:	spiops.o
	$(LD) $(LDFLAGS) $(DEBUGFLAGS) $(DEBUGLDFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS) $(OBJS)

