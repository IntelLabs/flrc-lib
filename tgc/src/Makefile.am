SOURCES = allocation.cpp barrier.cpp block_store.cpp compressed_references.cpp gc_for_orp.cpp gc_globals.cpp gc_heap_map.cpp gc_plan.cpp gc_threads.cpp gc_utils.cpp gc_v4.cpp hash_table.cpp mark_scan.cpp mark_scan_pools.cpp misc.cpp object_list.cpp old_sweep.cpp pair_table.cpp remembered_set.cpp roots.cpp scan_object.cpp sliding_compaction.cpp slot_offset_list.cpp sweep.cpp verify.cpp verify_live_heap.cpp work_packet_manager.cpp c_export.c

INCLUDES = -I../../include

EXTRA = -DBUILDING_ORP -DUSE_GC_STATIC -DBUILDING_GC -DFILTER_NON_HEAP -DORP_POSIX -DLINUX

AM_CFLAGS = $(INCLUDES) $(EXTRA)
AM_CXXFLAGS = $(INCLUDES) $(EXTRA)

lib_LIBRARIES = libtgc.a
libtgc_a_SOURCES = $(SOURCES)
libtgc_a_LIBADD = tgc_asm.o

.nasm.o:
	$(NASM) -f elf -o $@ $<

