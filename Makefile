
buildtime=`date`

CC=gcc

INCLUDE= -I. -I../include -I../bigpipes -I../libaga
LIBS= 

# External Directories
DIRS= ../libaga
DIRLIBS= ../libaga/agalog.o 

CFLAGS= -Wall -g -Wunused -Wno-pragmas -Dbuildtime="\"${buildtime}\"" -D_GNU_SOURCE ${INCLUDE}
LDFLAGS= -Wl,--no-as-needed ${LIBS}

OBJS=

TARGETS= skeleton

.c.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: $(TARGETS)

clean:
	rm -f $(OBJS) $(TARGETS) *.o
	-for d in $(DIRS); do (cd $$d; $(MAKE) clean ); done

forcelook:
	true

tags: $(TARGETS)
	ctags *.h *.c 

../libaga/agalog.o: forcelook
	cd ../libaga; $(MAKE) $(MFLAGS)


skeleton.o:

skeleton: ../libaga/agalog.o ../libaga/libaga.o

