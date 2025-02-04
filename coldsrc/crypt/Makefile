#CC = checkergcc
#CC = gcc

CFLAGS = -Wall -UNIT32  -O2 -ggdb
#CFLAGS = -Wall -UNIT32  -O2 -ggdb -DMEMSPY -DMEMSPYLINE

LDFLAGS = -ggdb

LIBS = /usr/src/pgp26/libpgp.a

COLDO = crypt.o order.o
OBJS =	genprime.o mpilib.o rsa.o random.o idea.o md5.o 80386.o
DOBJS = mpiio.o 

all:    crypt.a $(COLDO)

crypt.a: $(OBJS)
	/usr/bin/ar rc crypt.a $(OBJS)
	/usr/bin/ranlib crypt.a

mpiio.o:
	$(CC) -c -o mpiio.o $(CFLAGS) -DDEBUG mpiio.c

test:	$(OBJS) $(DOBJS) test.c order.o
	cc -o test -ggdb test.c $(OBJS) $(DOBJS) order.o _80386.o

testkg:	$(OBJS) $(DOBJS) testkg.c order.o
	cc -o testkg -ggdb testkg.c $(OBJS) $(DOBJS) order.o _80386.o

keygen:	$(OBJS) $(DOBJS) keygen.c order.o
	cc -o keygen -ggdb keygen.c $(OBJS) $(DOBJS) order.o _80386.o

clean:
	rm -f $(COLDO) $(OBJS) *.a *~

dep:
	$(CPP) -M *.c > .depend

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

