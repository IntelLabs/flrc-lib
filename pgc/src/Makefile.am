SOURCES = pgc.cpp

INCLUDES = -I$(top_srcdir)/include

EXTRA = -DPILLAR -D_LIB -DUSE_GC_STATIC -DORP_POSIX

AM_CXXFLAGS = $(INCLUDES) $(EXTRA)

lib_LIBRARIES = libpgc.a
libpgc_a_SOURCES = $(SOURCES)


