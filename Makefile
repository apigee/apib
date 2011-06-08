CC = gcc
LD = gcc
OPT = -g
OPT = -O3 
INCL = -I/usr/include/apr-1.0 -I/usr/include/apr-1
CFLAGS = $(OPT) -std=gnu99 -pedantic -Wall -I. ${INCL} -D_GNU_SOURCE
LDFLAGS = $(OPT)
LIBS = -laprutil-1 -lapr-1

APIB_OBJS = apib_main.o apib_iothread.o apib_reporting.o

all: apib

apib: $(APIB_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(APIB_OBJS) $(LIBS)

clean:
	rm -f *.o apib

apib_main.o apib_iothread.o apib_reporting.o: apib.h
