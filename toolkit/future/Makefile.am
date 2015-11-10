if OS_WIN32
PIL_SCRIPT_SUBDIR=windows
endif

if OS_LINUX
PIL_SCRIPT_SUBDIR=linux
endif

PIL_SCRIPT_DIR=../../scripts/$(PIL_SCRIPT_SUBDIR)
bin_SCRIPTS = $(PIL_SCRIPT_DIR)/pilicl $(PIL_SCRIPT_DIR)/pilink

CC=$(PIL_SCRIPT_DIR)/pilicl
INCLUDES = -I../../include
AM_CFLAGS = $(INCLUDES) -p2c -c -Zi -nologo -I. -std=c99

all-local: ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o

CLEANFILES = ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o

ptkfutures_p2c_sequential.o: ptkfuture.c
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_sequential.obj" ptkfuture.c

ptkfutures_p2c_parallel.o: ptkfuture.c
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_parallel.obj" ptkfuture.c
