CC = gcc
LD = gcc
OPT = -g
OPT = -O3 
INCL = -I/usr/include/apr-1.0 -I/usr/include/apr-1
CFLAGS = $(OPT) -std=gnu99 -pedantic -Wall -I. ${INCL} -D_GNU_SOURCE
LDFLAGS = $(OPT)
APR_LIBS = -laprutil-1 -lapr-1 
SSL_LIBS = -lssl

APIB_OBJS = apib_main.o apib_iothread.o apib_reporting.o apib_cpu.o apib_lines.o

APIB_MON_OBJS = apib_mon.o apib_lines.o apib_cpu.o

all: apib apibmon

apib: $(APIB_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(APIB_OBJS) $(APR_LIBS) $(SSL_LIBS)

apibmon: $(APIB_MON_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(APIB_MON_OBJS) $(APR_LIBS)

clean:
	rm -f *.o apib

$(APIB_OBJS) $(APIB_MON_OBJS): apib.h apib_common.h

