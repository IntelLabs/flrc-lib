/*
 * Redistribution and use in source and binary forms, with or without modification, are permitted 
 * provided that the following conditions are met:
 * 1.   Redistributions of source code must retain the above copyright notice, this list of 
 * conditions and the following disclaimer.
 * 2.   Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "flrclibconfig.h"

// System header files
#include <iostream>

// GC header files
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_v4.h"
#include "tgc/remembered_set.h"
#include "tgc/block_store.h"
#include "tgc/object_list.h"
#include "tgc/work_packet_manager.h"
#include "tgc/garbage_collector.h"
#include "tgc/gc_plan.h"
#include "tgc/gc_globals.h"
#include "tgc/gcv4_synch.h"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Gc_Plan *_p_gc_plan = NULL;

char *p_plan_file_name = NULL;

bool verbose_gc = false;
bool stats_gc = false;
bool delta_dynopt = false;
bool delta_verbose = false;
unsigned int delta_cutoff_percent = 5;
bool delta_stat = false;
long max_chunks_ephemeral = 0;
long cur_ephemeral_chunk  = 0;
bool promote_on_escape = false;

//
// If the user's command line doesn't set this, then the
// default from the plan file will.
//
POINTER_SIZE_INT initial_heap_size_bytes = 0;
POINTER_SIZE_INT final_heap_size_bytes = 0;

//
// This remembered set has an entry per loaded class.
// It is used for determining valid vtable pointers
// when examining candidate objects.
//

Hash_Table *p_loaded_vtable_directory = NULL;



Hash_Table *dup_removal_enum_hash_table = NULL;

//
// Global to specify the size differentiating
//
unsigned los_threshold_bytes = 0;


bool garbage_collector_is_initialized = false;


//
// This flag is initially false, and is set to true when the
// ORP is fully initialized. This signals that any stop-the-world
// collections can occur.
//
volatile unsigned orp_initialized = 0;


//
// This is the table holding all the interior pointers used in any given execution
// of a GC. It is reused each time a GC is done.

ExpandInPlaceArray<slot_offset_entry> *interior_pointer_table = NULL;
ExpandInPlaceArray<slot_offset_entry> *compressed_pointer_table = NULL;



unsigned long enumeration_time;

GC_Thread_Info *active_thread_gc_info_list = NULL;
#ifdef USE_LOCKCMPEX_FOR_THREAD_LOCK
long volatile active_thread_gc_info_list_lock = 0;
#else
#ifdef HAVE_PTHREAD_H
pthread_mutex_t *active_thread_gc_info_list_lock = NULL;
#else  // HAVE_PTHREAD_H
McrtTicketLock  *active_thread_gc_info_list_lock = NULL;
#endif // HAVE_PTHREAD_H
#endif

// end file gc\gc_globals.cpp
