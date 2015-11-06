CC=../../scripts/linux/pilicl
INCLUDES = -I.. -I../include -I../../prt/include
AM_CFLAGS = $(INCLUDES) -p2c -c -Zi -nologo -I. -std=c99

all-local: ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o

CLEANFILES = ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o

ptkfutures_p2c_sequential.o: ptkfuture.c
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_sequential.obj" ptkfuture.c

ptkfutures_p2c_parallel.o: ptkfuture.c
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_parallel.obj" ptkfuture.c
