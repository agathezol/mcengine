CC=gcc

CFLAGS+= -g -D_GNU_SOURCE -fno-stack-protector -fPIC 

LDFLAGS+=

OBJS= libaga.o agalog.o

TARGETS=

.c.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: clean $(OBJS) $(TARGETS)

libaga.o: libaga.h
agalog.o: agalog.h

noctol:
	make clean; CFLAGS+=-DNOAGACTOL make all

clean: forcelook
	rm -f $(OBJS) *.o $(TARGETS)

forcelook:
	true
