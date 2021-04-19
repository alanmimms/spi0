CFLAGS = -Wno-pointer-to-int-cast -g
TARGETS = spi0

all:	$(TARGETS)

spi0:	spi0.c

clean:
	rm -f $(TARGETS)
