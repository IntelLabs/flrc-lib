/*
 * COPYRIGHT_NOTICE_1
 */

// Globals in the GC - need to eliminate.
//

#ifndef _gc_globals_h_
#define _gc_globals_h_

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "iflclibconfig.h"
#include "tgc/pair_table.h"
#include "tgc/slot_offset_list.h"
#include "tgc/gcv4_synch.h"
#include "tgc/gc_plan.h"
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <list>
#else
#include <..\stlport\list>
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern bool stats_gc;
extern bool verbose_gc;
extern bool verify_gc;
extern char *p_plan_file_name;
extern Gc_Plan *_p_gc_plan;
extern POINTER_SIZE_INT initial_heap_size_bytes;
extern POINTER_SIZE_INT final_heap_size_bytes;
extern unsigned int g_num_cpus;

// Constants used to size the number of objects that can be colocated.
const int MAX_FUSABLE_OBJECT_SCAN_STACK = 64;
const int MAX_FUSED_OBJECT_COUNT        = 64;

//
// The size to determine if object is large or small.
//

extern unsigned los_threshold_bytes;


//
// This flag is initially false during ORP startup. It becomes true
// when the ORP is fully initialized. This signals the GC to feel
// free to do a stop-the-world at any time.
//
extern volatile unsigned orp_initialized;

extern ExpandInPlaceArray<slot_offset_entry> *interior_pointer_table;
extern ExpandInPlaceArray<slot_offset_entry> *compressed_pointer_table;


extern GC_Thread_Info *active_thread_gc_info_list;
#ifdef USE_LOCKCMPEX_FOR_THREAD_LOCK
extern volatile long active_thread_gc_info_list_lock;

static inline void get_active_thread_gc_info_list_lock () {
    while (InterlockedCompareExchange (&active_thread_gc_info_list_lock, 1, 0) != 0) {
        while (active_thread_gc_info_list_lock == 1) {
            ; // Loop until it is 0 and try again.
        }
    }
}

static inline void release_active_thread_gc_info_list_lock () {
    active_thread_gc_info_list_lock = 0;
}

#else // USE_LOCKCMPEX_FOR_THREAD_LOCK
#ifdef HAVE_PTHREAD_H
extern pthread_mutex_t *active_thread_gc_info_list_lock;
static inline void get_active_thread_gc_info_list_lock () {
	pthread_mutex_lock(active_thread_gc_info_list_lock);
}

static inline void release_active_thread_gc_info_list_lock () {
	pthread_mutex_unlock(active_thread_gc_info_list_lock);
}
#else // HAVE_PTHREAD_H
extern McrtTicketLock *active_thread_gc_info_list_lock;
static inline void get_active_thread_gc_info_list_lock () {
	mcrtTicketLockAcquire(active_thread_gc_info_list_lock);
}

static inline void release_active_thread_gc_info_list_lock () {
	mcrtTicketLockRelease(active_thread_gc_info_list_lock);
}
#endif // HAVE_PTHREAD_H
#endif // USE_LOCKCMPEX_FOR_THREAD_LOCK

extern SynchCriticalSectionHandle g_chunk_lock;

#ifdef LINUX
#define INFINITE -1
#endif

#endif // _gc_globals_h_
