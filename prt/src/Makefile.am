SOURCES = prtcims.cpp  prtcodeinfo.cpp  prtconcurrency.cpp  prtmain.cpp  prtstackiterator.cpp  prttls.cpp  prtvirtualstack.cpp prtstubs.nasm

INCLUDES = -I../include

EXTRA = -DBUILDING_PILLAR -DTASK_OVERRIDE -DNO_PRSCALL -D_STL=std -DLINUX

AM_CFLAGS = $(INCLUDES) $(EXTRA)
AM_CXXFLAGS = $(INCLUDES) $(EXTRA)

lib_LIBRARIES = libprt.a
libprt_a_SOURCES = $(SOURCES)

.nasm.o:
	$(NASM) -f elf -o $@ $<

