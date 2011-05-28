CC = gcc
LD = gcc
OPT = -g
CFLAGS = $(OPT) -std=gnu99 -pedantic -Wall -I. -I/usr/include/apr-1.0 -D_GNU_SOURCE
LDFLAGS = $(OPT)
LIBS = -laprutil-1 -lapr-1

APIB_OBJS = apib_main.o apib_iothread.o apib_reporting.o

all: apib

apib: $(APIB_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(APIB_OBJS) $(LIBS)

clean:
	rm -f *.o apib

apib_main.c apib_iothread.c apib_reporting.c: apib.h