SCRIPT_DIR=$(top_srcdir)/$(PIL_SCRIPT_DIR)
CC=IFLC_BIN_DIR=$(top_srcdir)/pillar2c/src IFLC_DATA_DIR=$(top_srcdir)/pillar2c/src IFLC_INCLUDE_DIR=$(includedir) $(SCRIPT_DIR)/pilicl
INCLUDES = -I"$(top_srcdir)"/include
AM_CFLAGS = $(INCLUDES) -p2c -c -Zi -nologo -I. -std=c99

all-local: ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o
bin_SCRIPTS = $(SCRIPT_DIR)/pilicl $(SCRIPT_DIR)/pilink
CLEANFILES = ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o $(bin_SCRIPTS)
EXTRA_OBJS = ptkfutures_p2c_sequential.o ptkfutures_p2c_parallel.o
EXTRA_DIST = $(SCRIPT_DIR)/pilicl.in \
			       $(SCRIPT_DIR)/pilink.in

install-exec-hook: $(EXTRA_OBJS)
	$(MKDIR_P) $(DESTDIR)$(libdir) && $(INSTALL_DATA) $(EXTRA_OBJS) "$(DESTDIR)$(libdir)"

ptkfutures_p2c_sequential.o: ptkfuture.c $(SCRIPT_DIR)/pilicl
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_sequential.obj" ptkfuture.c

ptkfutures_p2c_parallel.o: ptkfuture.c $(SCRIPT_DIR)/pilicl
	$(CC) $(AM_CFLAGS) /Fo"ptkfutures_p2c_parallel.obj" ptkfuture.c

do_subst = sed -e 's,[@]bindir[@],$(bindir),g' \
			         -e 's,[@]datadir[@],$(datadir),g' \
		      	   -e 's,[@]includedir[@],$(includedir),g' \
		    	     -e 's,[@]libdir[@],$(libdir),g'

$(SCRIPT_DIR)/pilicl: $(SCRIPT_DIR)/pilicl.in Makefile
	$(do_subst) < $(SCRIPT_DIR)/pilicl.in > $(SCRIPT_DIR)/pilicl
	chmod +x $(SCRIPT_DIR)/pilicl

$(SCRIPT_DIR)/pilink: $(SCRIPT_DIR)/pilink.in Makefile
	$(do_subst) < $(SCRIPT_DIR)/pilink.in > $(SCRIPT_DIR)/pilink
	chmod +x $(SCRIPT_DIR)/pilink
