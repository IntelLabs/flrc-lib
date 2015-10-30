INCLUDES = -I../include
PRT_FLAGS = -fno-reorder-functions -I../../prt/include
EXTRA_OBJS = pillar2c_crt_begin.o pillar2c_crt_end.o pillar2c.asm.o

AM_CFLAGS = $(INCLUDES)
AM_CXXFLAGS = $(INCLUDES)
AM_YFLAGS = -d

bin_PROGRAMS = pillar2c
pillar2c_SOURCES = pillar2c_yacc.yy pillar2c.main.cpp
pillar2c_DEPENDENCIES = $(EXTRA_OBJS) pillar2c_lex.cc

pillar2c_lex.cc: pillar2c_lex.ll
	$(LEX) -o $@ -s $<

install-exec-hook: $(EXTRA_OBJS)
	 $(MKDIR_P) $(DESTDIR)$(libdir) && $(INSTALL_DATA) $(EXTRA_OBJS) "$(DESTDIR)$(libdir)"

pillar2c.asm.o: pillar2c.nasm
	$(NASM) -f elf -o $@ $<

pillar2c_crt_begin.o:   pillar2c_crt_begin.c
	$(COMPILE) $(PRT_FLAGS) -c pillar2c_crt_begin.c -o pillar2c_crt_begin.o

pillar2c_crt_end.o:     pillar2c_crt_end.c
	$(COMPILE) $(PRT_FLAGS) -c pillar2c_crt_end.c -o pillar2c_crt_end.o
