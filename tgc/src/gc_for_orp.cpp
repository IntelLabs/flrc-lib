/*
 * COPYRIGHT_NOTICE_1
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// System header files
#include <iostream>
#include <fstream>

// GC header files
#include "gc_cout.h"
#include "gc_header.h"
#include "gc_v4.h"
#include "remembered_set.h"
#include "block_store.h"
#include "object_list.h"
#include "work_packet_manager.h"
#include "garbage_collector.h"
#include "gc_plan.h"
#include "gc_globals.h"
#include "micro_nursery.h"
#include "descendents.h"
#include "gcv4_synch.h"

#ifdef USE_PTHREADS
#include "pthread.h"
#endif

#ifdef CONCURRENT
extern enum CONCURRENT_GC_STATE g_concurrent_gc_state;
extern volatile unsigned concurrent_gc_thread_id;
extern volatile bool stop_concurrent;
extern volatile void *g_sweep_ptr;
unsigned start_concurrent_gc = 0;
#include "mark.h"
#endif // CONCURRENT

SynchCriticalSectionHandle g_chunk_lock;
unsigned active_thread_count = 0;

extern "C" void STDCALL_FUNC_OUT (STDCALL_FUNC_IN *gc_heap_slot_write_barrier_indirect)(
			Managed_Object_Handle p_base_of_object_with_slot,
            Managed_Object_Handle *p_slot,
            Managed_Object_Handle value);
extern "C" STDCALL_FUNC_OUT void (STDCALL_FUNC_IN *gc_heap_slot_write_interior_indirect)(
			Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
			unsigned offset);
extern "C" STDCALL_FUNC_OUT void (STDCALL_FUNC_IN *gc_heap_slot_write_barrier_indirect_prt)(
			Managed_Object_Handle p_base_of_object_with_slot,
            Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
#ifndef NO_P2C_TH
            PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
            void *prev_frame);
extern "C" STDCALL_FUNC_OUT void (STDCALL_FUNC_IN *gc_heap_slot_write_interior_indirect_prt)(
			Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
			unsigned offset,
#ifndef NO_P2C_TH
            PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
            void *prev_frame);

// CAS versions
extern "C" STDCALL_FUNC_OUT Managed_Object_Handle (STDCALL_FUNC_IN *gc_cas_write_barrier_indirect)(
			Managed_Object_Handle p_base_of_object_with_slot,
            Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
            Managed_Object_Handle cmp);
extern "C" STDCALL_FUNC_OUT Managed_Object_Handle (STDCALL_FUNC_IN *gc_cas_write_interior_indirect)(
			Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
			unsigned offset,
            Managed_Object_Handle cmp);
extern "C" STDCALL_FUNC_OUT Managed_Object_Handle (STDCALL_FUNC_IN *gc_cas_write_barrier_indirect_prt)(
			Managed_Object_Handle p_base_of_object_with_slot,
            Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
            Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
            PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
            void *prev_frame);
extern "C" STDCALL_FUNC_OUT Managed_Object_Handle (STDCALL_FUNC_IN *gc_cas_write_interior_indirect_prt)(
			Managed_Object_Handle *p_slot,
            Managed_Object_Handle value,
			unsigned offset,
            Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
            PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
            void *prev_frame);

extern "C" void *gc_heap_slot_write_ref_p_end;
extern "C" void *gc_heap_slot_write_interior_ref_p_end;
extern "C" void *gc_heap_slot_write_ref_p_nonconcurrent_section;
extern "C" void *gc_heap_slot_write_interior_ref_p_nonconcurrent_section;

extern "C" void *gc_heap_slot_write_ref_p_prt_end;
extern "C" void *gc_heap_slot_write_interior_ref_p_prt_end;
extern "C" void *gc_heap_slot_write_ref_p_prt_nonconcurrent_section;
extern "C" void *gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section;

extern "C" void *gc_cas_write_ref_p_end;
extern "C" void *gc_cas_write_interior_ref_p_end;
extern "C" void *gc_cas_write_ref_p_nonconcurrent_section;
extern "C" void *gc_cas_write_interior_ref_p_nonconcurrent_section;

extern "C" void *gc_cas_write_ref_p_prt_end;
extern "C" void *gc_cas_write_interior_ref_p_prt_end;
extern "C" void *gc_cas_write_ref_p_prt_nonconcurrent_section;
extern "C" void *gc_cas_write_interior_ref_p_prt_nonconcurrent_section;

extern "C" void *gc_heap_slot_gen_write_ref_p_prt_end;
extern "C" void *gc_heap_slot_gen_write_interior_ref_p_prt_end;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////// GC EXPORT FUNCTION LIST////////////////////////////////
/*
 * DECLARATION:
 *   1. You need to declare an exported function through GCEXPORT, for example
 *         DECLARE
 *             type func(arg list) 
 *         AS:
 *             GCEXPORT(type, func)(arg list).
 *   2. If you want to call one exported function "func" from another exported function,
 *      you need to write the call as:
 *             INTERNAL(func)(arg list);
 *   3. Check gc_for_orp.h for the definition of GCEXPORT and INTERNAL
*/

/* EXPLANATION:
 *   On Linux, system will be confused by function pointer variable pointing to a exported 
 *     function with the same name, for example,
 *   In ORP, we have:
 *      void (*gc_thread_init)(void *);
 *      gc_thread_init = dlsym(handle, "gc_thread_init");
 *   In GC DLL, we have:
 *      void gc_thread_init(void *);
 *   Now in DLL context, when encountering a call like
 *      gc_thread_init(XXX);
 *   System may think it's a function pointer variable, and DLL context seems to have
 *      problem in calling the function pointer correctly.
 *   The current solution is declare the implementation function as:
 *      internal_gc_thread_init(void *);
 *   through GCEXPORT macro.
 *
 *   And in DLL context, if you need to call the exported function, you need to use:
 *      INTERNAL(gc_thread_init)(XXX);
 *
 *   In Dll_GC.cpp, you need to use INTERNAL_FUNC_NAME to refer the function 
 *      names.
 *   
 */
 
/* NOTE:
 *   There're also exported functions in allocation.cpp and scan_object.cpp.
 */
GCEXPORT(Boolean, gc_supports_compressed_references) ();
GCEXPORT(void, gc_next_command_line_argument) (const char *name, const char *arg);
GCEXPORT(void, gc_class_loaded) (VTable_Handle vth);
GCEXPORT(void, gc_init)();
GCEXPORT(void, gc_orp_initialized)();
GCEXPORT(void, gc_wrapup)();
GCEXPORT(Boolean, gc_requires_barriers)();
GCEXPORT(void, gc_write_barrier)(Managed_Object_Handle p_base_of_object_holding_ref);
GCEXPORT(void, gc_heap_wrote_object) (Managed_Object_Handle p_base_of_object);
GCEXPORT(void, gc_heap_slot_write_ref) (Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value);
GCEXPORT(void, gc_heap_slot_write_interior_ref) (
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset);

#ifdef ALLOW_COMPRESSED_REFS
GCEXPORT(void, gc_heap_slot_write_ref_compressed)(Managed_Object_Handle p_base_of_object_with_slot,
                                                  uint32 *p_slot,
                                                  Managed_Object_Handle value);
GCEXPORT(void, gc_heap_write_global_slot_compressed)(uint32 *p_slot,
                               Managed_Object_Handle value);
#endif

GCEXPORT(void, gc_heap_write_global_slot)(volatile uint32 *txnRec, 
                                          Managed_Object_Handle *p_slot,
                                          Managed_Object_Handle value);

GCEXPORT(void, gc_add_root_set_entry_interior_pointer) (void **slot, int offset, Boolean is_pinned);
GCEXPORT(void, gc_add_root_set_entry)(Managed_Object_Handle *ref1, Boolean is_pinned);
GCEXPORT(void, gc_add_weak_root_set_entry)(Managed_Object_Handle *ref1, Boolean is_pinned,Boolean is_short_weak);
GCEXPORT(void, gc_add_compressed_root_set_entry)(uint32 *ref, Boolean is_pinned);
GCEXPORT(Managed_Object_Handle, gc_malloc)(unsigned size, Allocation_Handle ah);
GCEXPORT(Managed_Object_Handle, gc_malloc_or_null)(unsigned size, Allocation_Handle ah);
GCEXPORT(Managed_Object_Handle, gc_malloc_with_thread_pointer)(unsigned size, Allocation_Handle ah, void *tp);
GCEXPORT(Managed_Object_Handle, gc_malloc_with_thread_pointer_escaping)(unsigned size, Allocation_Handle ah, void *tp);
GCEXPORT(Boolean, gc_can_allocate_without_collection)(unsigned size, void *tp);
GCEXPORT(void, gc_require_allocate_without_collection)(unsigned size, void *tp);
GCEXPORT(Managed_Object_Handle, gc_malloc_or_null_with_thread_pointer)(unsigned size, Allocation_Handle ah, void *tp);
GCEXPORT(Managed_Object_Handle, gc_pinned_malloc_noclass)(unsigned size) ;
GCEXPORT(void, gc_thread_init)(void *gc_information, void *thread_handle) ;
GCEXPORT(void, gc_thread_init_no_nursery)(void *gc_information, void *thread_handle) ;
GCEXPORT(void, gc_thread_kill)(void *gc_information) ;
GCEXPORT(void, gc_force_gc)() ;
GCEXPORT(int64, gc_total_memory)(); 
GCEXPORT(int64, gc_free_memory)();
GCEXPORT(int64, gc_max_memory)();
GCEXPORT(unsigned int, gc_time_since_last_gc)();

GCEXPORT(void *, gc_heap_base_address)();
GCEXPORT(void *, gc_heap_ceiling_address)();
// default second paramter to 2 from DYNOPT
GCEXPORT(void, gc_register_delinquent_regions)(void **, int);
GCEXPORT(void *, gc_get_latest_path_information)();

GCEXPORT(Boolean, gc_is_heap_object(void *p_obj));
// Returns true the object has survived at least one GC cycle.
GCEXPORT(Boolean, gc_is_object_long_lived(void *p_obj));

GCEXPORT(Boolean, gc_is_object_pinned) (Managed_Object_Handle obj);
GCEXPORT(void, gc_class_prepared) (Class_Handle ch, VTable_Handle vth);

GCEXPORT(Boolean, gc_supports_frontier_allocation) (unsigned *offset_of_current, unsigned *offset_of_limit);

GCEXPORT(void, gc_suppress_finalizer)(Managed_Object_Handle obj);
GCEXPORT(void, gc_register_finalizer)(Managed_Object_Handle obj);

GCEXPORT(Boolean, gc_update_vtable)(Managed_Object_Handle object, Allocation_Handle new_vt);
GCEXPORT(unsigned, gc_get_tenure_offset)(void);

////////////////////////// GC EXPORT FUNCTION LIST////////////////////////////////

unsigned long long g_public_read  = 0;
unsigned long long g_private_read = 0;

extern void (*wpo_finalizer_callback)(void *);

//*****************************************************
//
// Some helper functions.
// If these need to be changed then the class.h function
// probable need to be looked at also.
//*****************************************************

const int BITS_PER_BYTE = 8;

// We want to use signed arithmetic when we do allocation pointer/limit compares.
// In order to do this all sizes must be positive so when we want to overflow instead of 
// setting the high bit we set the next to high bit. If we set the high bit and the allocation buffer
// is at the top of memory we might not detect an overflow the unsigned overflow would produce
// a small positive number that is smaller then the limit.

const int NEXT_TO_HIGH_BIT_SET_MASK = (1<<((sizeof(unsigned) * BITS_PER_BYTE)-2));
const int NEXT_TO_HIGH_BIT_CLEAR_MASK = ~NEXT_TO_HIGH_BIT_SET_MASK;

inline unsigned int get_instance_data_size (unsigned int encoded_size) {
    return (encoded_size & NEXT_TO_HIGH_BIT_CLEAR_MASK);
}
 
// ******************************************************************
/****
*
*  Routines to support the initialization and termination of GC.
* 
*****/

//
// Used by the class prepare routine to enable the placement of special objects that
// are application specific. This is turned on with -gc object_placement 
//

#ifdef GC_VERIFY_VM
// Forward reference.
void turn_test_vm_on(unsigned int skip);
#endif

//
// API for the ORP to hand to the GC any arguments starting with -gc. 
// It is up to the GC to parse these arguments.
//
// Input: name - a string holding the name of the parameter 
//               assumed to begin with "-gc"
//        arg  - a string holding the argument following name on 
//               the command line.
//
extern bool fullheapcompact_at_forcegc;
extern bool incremental_compaction;
extern bool machine_is_hyperthreaded;
extern bool mark_scan_load_balanced;
extern bool verify_live_heap;
extern bool single_threaded_gc;
extern bool use_large_pages;
extern bool sweeps_during_gc;
extern bool force_unaligned_heap;
extern bool use_finalization;
extern unsigned g_number_of_active_processors;
extern bool zero_after_compact;
extern bool randomize_roots;
extern bool do_not_zero;
extern bool parallel_clear;
extern bool promote_on_escape;
extern unsigned local_nursery_size;
extern bool adaptive_nursery_size;
extern unsigned ADAPTIVE_DECREASING_SAMPLE_SIZE;
extern unsigned ADAPTIVE_SEARCHING_SAMPLE_SIZE;
extern unsigned ADAPTIVE_PHASE_SAMPLE_SIZE;
extern unsigned ADAPTIVE_HIGH_LOW_SAMPLE_SIZE;
extern float    ADAPTIVE_HIGH_LOW_FACTOR;
extern float    ADAPTIVE_PHASE_THRESHOLD; 
unsigned ARENA_SIZE = 1024;
bool use_pillar_watermarks = true;
bool pre_tenure = false;
bool no_use_sweep_ptr = false;
extern bool concurrent_torture;
int GC_MIN_FREE_AREA_SIZE = (GC_BLOCK_INFO_SIZE_BYTES / 2);
bool separate_immutable = false;
bool pn_history = false;
#ifdef PUB_PRIV
extern "C" unsigned g_use_pub_priv;
#endif // PUB_PRIV
#ifdef CONCURRENT
unsigned max_pause_us = 1000000;
extern unsigned g_concurrent_transition_wait_time;
#endif // CONCURRENT
float g_preemptive_collect = 1.0;
#ifdef CONCURRENT_DEBUG_2
FILE *cgcdump = NULL;
#endif
extern bool g_num_threads_specified;
extern int  g_num_threads;
unsigned g_heap_compact_cmd_override = 0;
extern bool g_show_all_pncollect;
extern bool g_dump_rs_vtables;
extern bool g_keep_mutable;
extern bool g_keep_mutable_closure;
extern bool g_keep_all;
extern bool g_determine_dead_rs;
extern bool g_treat_wpo_as_normal;
extern bool g_gen;
unsigned g_init_sleep_time = 0;
unsigned g_remove_indirections = 0;
#ifdef _DEBUG
std::ofstream * profile_out = NULL;
std::ofstream * live_dump = NULL;
#endif
POINTER_SIZE_INT g_cmd_line_heap_base = 0;
extern bool g_zero_dead;
#ifdef _DEBUG
bool g_maximum_debug = false;
#endif // _DEBUG
extern bool g_prevent_rs;
FREE_BLOCK_POLICY g_return_free_block_policy = MAINTAIN_RATIO;

int numdigits(int n) {
	int count = 0;
    do {
		n /= 10;
		++count;
	} while (n != 0);
	return count;
}

void get_adaptive_params(char *str) {
	if(*str != '\0') {
		sscanf(str,"%d,%d,%d,%d,%f,%f",
			&ADAPTIVE_DECREASING_SAMPLE_SIZE,
			&ADAPTIVE_SEARCHING_SAMPLE_SIZE,
			&ADAPTIVE_PHASE_SAMPLE_SIZE,
			&ADAPTIVE_HIGH_LOW_SAMPLE_SIZE,
			&ADAPTIVE_HIGH_LOW_FACTOR,
			&ADAPTIVE_PHASE_THRESHOLD);
	}
	printf("Adaptive params = %d %d %d %d %f %f\n",
		ADAPTIVE_DECREASING_SAMPLE_SIZE,
		ADAPTIVE_SEARCHING_SAMPLE_SIZE,
		ADAPTIVE_PHASE_SAMPLE_SIZE,
		ADAPTIVE_HIGH_LOW_SAMPLE_SIZE,
		ADAPTIVE_HIGH_LOW_FACTOR,
		ADAPTIVE_PHASE_THRESHOLD);
} // get_adaptive_params

GCEXPORT(void, gc_next_command_line_argument) (const char *name, const char *arg) {
    const char *gc_str = "-gc";
    const char *ms_str = "-ms";
    const char *mx_str = "-mx";
    const char *fixed_str  = "fixed";
    const char *copy_str = "copy";
#ifdef GC_VERIFY_VM
    const char *verify_vm_str = "verify_vm";
#endif
    const char *zeroing_thread_str = "zeroing_thread";
    const char *fetcher_thread_str = "fetcher_thread";
    const char *cheney_clock_str = "cheney_clock";
    const char *Xms_str = "-Xms";
    const char *Xmx_str = "-Xmx";
    int unit_k = 'k';
    int unit_m = 'm';
    int unit_g = 'g';
    
    const char *nofullheapcompact_str = "nofullcompactforcegc";
    const char *incrementalcompact_str = "incrementalcompact";
    const char *verify_str = "verify";
    const char *ht_str = "ht";
    const char *mslb_str = "load_balanced";
    const char *vlh_str = "verify_live_heap";
    const char *st_str = "single_threaded";
    const char *verbosegc_str = "-verbosegc";
    const char *plan_str = "PLAN=";
    const char *stats_str = "stats";
    const char *use_large_pages_str = "large_pages";
    const char *sweeps_during_gc_str = "sweeps_during_gc";
    const char *delta_str = "delta";
    const char *delta_stat_str = "delta_stat";
    const char *unalign_str = "unalign_heap";
    const char *finalization_str = "use_finalization";
    const char *no_finalization_str = "no_finalization";
	const char *proc_str = "num_procs=";
	const char *threads_str = "numThreads=";
	const char *zero_str = "zero_after_compact";
	const char *random_roots = "randomize_roots";
	const char *do_not_zero_str = "do_not_zero";
	const char *parallel_clear_str = "parallel_clear";
	const char *micro_nursery_str = "micro_nursery=";
	const char *arena_size_str = "arena_size=";
	const char *no_use_prt_watermarks = "no_pillar_watermarks";
	const char *use_pretenure = "pretenure";
	const char *no_sweep_ptr = "no_sweep_ptr";
	const char *con_torture = "concurrent_torture";
	const char *con_torture_off = "concurrent_minimal";
	const char *min_free_area_str = "min_free_area=";
	const char *separate_immutable_str = "separate_immutable";
	const char *pn_history_str = "pn_history";
	const char *max_pause_us_str = "max_pause=";
	const char *transition_wait_str = "transwait=";
	const char *preemptive_collect_str = "preemptive_collect=";
	const char *use_pub_priv_str = "use_pub_priv";
	const char *heap_compact_str = "heap_compact_ratio=";
    const char *show_all_pn_str = "show_all_pn_collect";
    const char *dump_rs_vtables_str = "dump_rs_vtables";
    const char *keep_mutable_str = "keep_mutable";
    const char *keep_mutable_closure_str = "keep_mutable_closure";
    const char *keep_all_str = "keep_all";
    const char *dead_rs_str = "find_dead_rs";
    const char *wpo_normal_str = "wpo_normal";
    const char *gen_str = "generational";
    const char *init_sleep_str = "init_sleep=";
    const char *remove_indir_str = "remove_indirections";
    const char *profile_str = "profile";
    const char *live_dump_str = "live_dump";
    const char *cheney_str = "cheney";
    const char *no_cheney_str = "no_cheney";
    const char *heap_base_str = "base=";
	const char *zero_dead_str = "zero_dead";
	const char *debug_str = "debug";
	const char *two_space_pn_str = "two_space_pn";
	const char *pure_two_space_str = "pure_two_space";
	const char *eager_promote_str = "eager_promote";
	const char *free_block_policy_str = "rfb_policy=";

    // Compatability requires that we accept -verbosegc, -ms and -mx

    if (strcmp(name, ms_str) == 0) {
        initial_heap_size_bytes = atoi(arg);
        initial_heap_size_bytes = (initial_heap_size_bytes & GC_BLOCK_HIGH_MASK); // Round down a block.
        if (verbose_gc) {
            orp_cout << "initial heap size is " << (void *)initial_heap_size_bytes << std::endl;
        }
        return;
    }
    if (strcmp(name, mx_str) == 0) {
        final_heap_size_bytes = atoi(arg);
        final_heap_size_bytes = (final_heap_size_bytes & GC_BLOCK_HIGH_MASK);
        if (verbose_gc) {
            orp_cout << "final heap size is " << (void *)final_heap_size_bytes << std::endl;
        }
        return;
    }
    
    if (strncmp(name, Xms_str,4) == 0) {
        const char * newarg = arg + 4;
        /*int*/ size_t len = strlen(newarg);
        int unit = 1;
        if (tolower(newarg[len - 1]) == unit_k) {
            unit = 1024;
        } else if (tolower(newarg[len - 1]) == unit_m) {
            unit = 1024 * 1024;
        } else if (tolower(newarg[len - 1]) == unit_g) {
            unit = 1024 * 1024 * 1024;
        }
        initial_heap_size_bytes = atoi(newarg);
        initial_heap_size_bytes *= unit;
        
        initial_heap_size_bytes = (initial_heap_size_bytes & GC_BLOCK_HIGH_MASK); // Round down a block.
        
        if (verbose_gc) {
            orp_cout << "initial heap size is " << (void *)initial_heap_size_bytes << std::endl;
        }
        return;
    }

    if (strncmp(name, Xmx_str,4) == 0) {
        const char * newarg = arg + 4;
        /*int*/ size_t len = strlen(newarg);
        int unit = 1;
        if (tolower(newarg[len - 1]) == unit_k) {
            unit = 1024;
        } else if (tolower(newarg[len - 1]) == unit_m) {
            unit = 1024 * 1024;
        } else if (tolower(newarg[len - 1]) == unit_g) {
            unit = 1024 * 1024 * 1024;
        }
        final_heap_size_bytes = atoi(newarg);
        final_heap_size_bytes *= unit;
        final_heap_size_bytes = (final_heap_size_bytes & GC_BLOCK_HIGH_MASK);
        if (verbose_gc) {
            orp_cout << "final heap size is " << (void *)final_heap_size_bytes << std::endl;
        }
        return;
    }


    if (strcmp(name, verbosegc_str) == 0) {
        verbose_gc = true;
        orp_cout << "TGC, $Name:  $" << std::endl;
        orp_cout << "verbose_gc is " << verbose_gc << std::endl;
        return;
    }

    // ignore all other strings that aren't gc.

    if(strcmp(name, gc_str) == 0) {
        if (strcmp(arg, stats_str) == 0) {
            stats_gc = true;
            orp_cout << "stats_gc is " << stats_gc << std::endl;
        } else if (strcmp(arg, fixed_str) == 0) {
            // We will not compact the heap since this will be a non-moving collector
            incremental_compaction = false;
            orp_cout << "GC will not move any objects -- fixed collector " << std::endl;
        } else if (strcmp(arg, copy_str) == 0) {
            assert(0);
//            copy_gc = true;
        } else if (strcmp(arg, zeroing_thread_str) == 0) {
//            use_zeroing_thread = true;
            assert(0);

//////////////   COMPACTION PARAMS /////////////////////////////////////////////////////////
        } else if (strcmp(arg, nofullheapcompact_str) == 0) {
            fullheapcompact_at_forcegc = false;
            if (verbose_gc) {
                orp_cout << "GC will not compact the full heap when gc_force_gc() is called.\n";
            }
        } else if (strcmp(arg, incrementalcompact_str) == 0) {
            incremental_compaction = true;
            if (verbose_gc) {
                orp_cout << "GC will incrementally slide compact at each GC.\n";
            }
        } else if (strcmp(arg, verify_str) == 0) {
            verify_gc = true;
            orp_cout << "GC will verify heap and GC data structures before and after GC.\n";
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, ht_str) == 0) {
            machine_is_hyperthreaded = true;
            orp_cout << "This is a HyperThreaded machine.\n";
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, mslb_str) == 0) {
            mark_scan_load_balanced = true;
            if (verbose_gc) {
                orp_cout << "GC will try to load balance effectively\n";
            }
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, vlh_str) == 0) {
            verify_live_heap = true;
            if (verbose_gc) {
                orp_cout << "GC will verify the live heap thoroughly before and after GC\n";
            }
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, st_str) == 0) {
            single_threaded_gc = true;
            if (verbose_gc) {
                orp_cout << "GC will use only one thread for Collection\n";
            }
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, sweeps_during_gc_str) == 0) {
            sweeps_during_gc = true;
            if (verbose_gc) {
                orp_cout << "Chunks will be swept during GC\n";
            }
////////////////////////////////////////////////////////////////////////////////////////////
            
#ifdef _IA64_
        } else if (strcmp(arg, use_large_pages_str) == 0) {
            use_large_pages = true;
            if (verbose_gc) {
                orp_cout << "GC will use large pages" << std::endl;
            }
#endif // _IA64_
////////////////////////////////////////////////////////////////////////////////////////////
        } else if (strcmp(arg, fetcher_thread_str) == 0) {
            assert(0);
        } else if (strcmp(arg, cheney_clock_str) == 0) {
            assert(0);
#ifdef GC_VERIFY_VM
        } else if (strncmp(arg, verify_vm_str, 9) == 0) {
            const char * newarg = arg + 9;
            /*int*/ size_t len = strlen(newarg);
            int unit = 1;
            if (tolower(newarg[len - 1]) == unit_k) {
                unit = 1024;
            } else if (tolower(newarg[len - 1]) == unit_m) {
                unit = 1024 * 1024;
            }
            unsigned int local_enable_skip = atoi(newarg);
            local_enable_skip *= unit;
            turn_test_vm_on(local_enable_skip);
            if (verbose_gc) {
                orp_cout << "Verifying VM but skipping the first " << local_enable_skip << " calls to enable_gc." << std::endl;
            }
#endif
        } else if (strcmp(arg, unalign_str) == 0) {
//            force_unaligned_heap = true;
                // This is deprecated so ignore the command line. RLH - 12-14-2006
        } else if (strcmp(arg, finalization_str) == 0) {
            use_finalization = true;
        } else if (strcmp(arg, no_finalization_str) == 0) {
            use_finalization = false;
		} else if (strncmp(arg, micro_nursery_str, strlen(micro_nursery_str)) == 0) {
    		const char *nursery_size = arg + strlen(micro_nursery_str);
	    	local_nursery_size = atoi(nursery_size); 
			char *next = (char*)nursery_size + numdigits(local_nursery_size);
			char unit = *next;
			next++;
			switch(unit) {
			case '\0':
			case 'm':
			case 'M':
				local_nursery_size *= 1024 * 1024;
				break;
			case 'k':
			case 'K':
				local_nursery_size *= 1024;
				break;
			case 'b':
			case 'B':
                // Intentionally do nothing.
				break;
			case 'a':
			case 'A':
				local_nursery_size *= 1024 * 1024;
				adaptive_nursery_size = true;
				get_adaptive_params(next);
				unit = '\0';
				break;
			default:
				printf("Illegal unit on private nursery size.\n");
				exit(17066);
			}
			if(unit != '\0') {
				unit = *next;
				if(unit == 'a' || unit == 'A') {
					adaptive_nursery_size = true;
					get_adaptive_params(next);
				} else {
                    if(unit != '\0') {
					    printf("Illegal character following private nursery size unit identifier.\n");
					    exit(17066);
                    }
				}
			}
		} else if (strncmp(arg, arena_size_str, strlen(arena_size_str)) == 0) {
    		const char *arena_size = arg + strlen(arena_size_str);
	    	ARENA_SIZE = atoi(arena_size);
		} else if (strncmp(arg, min_free_area_str, strlen(min_free_area_str)) == 0) {
    		const char *min_free_area = arg + strlen(min_free_area_str);
	    	GC_MIN_FREE_AREA_SIZE = atoi(min_free_area);
        } else if (strcmp(arg, zero_str) == 0) {
            zero_after_compact = true;
        } else if (strcmp(arg, do_not_zero_str) == 0) {
            do_not_zero = true;
			if(verbose_gc) {
				printf("GC will NOT zero memory before giving it to a nursery.\n");
			}
        } else if (strcmp(arg, parallel_clear_str) == 0) {
            parallel_clear = true;
        } else if (strcmp(arg, random_roots) == 0) {
            randomize_roots = true;
        } else if (strcmp(arg, no_use_prt_watermarks) == 0) {
            use_pillar_watermarks = false;
        } else if (strcmp(arg, no_sweep_ptr) == 0) {
            no_use_sweep_ptr = true;
        } else if (strcmp(arg, con_torture) == 0) {
            concurrent_torture = true;
        } else if (strcmp(arg, con_torture_off) == 0) {
            concurrent_torture = false;
        } else if (strcmp(arg, separate_immutable_str) == 0) {
            separate_immutable = true;
        } else if (strcmp(arg, pn_history_str) == 0) {
            pn_history = true;
        } else if (strcmp(arg, show_all_pn_str) == 0) {
            g_show_all_pncollect = true;
        } else if (strcmp(arg, dump_rs_vtables_str) == 0) {
            g_dump_rs_vtables = true;
        } else if (strcmp(arg, keep_mutable_str) == 0) {
            g_keep_mutable = true;
            use_pillar_watermarks = false;
        } else if (strcmp(arg, keep_mutable_closure_str) == 0) {
            g_keep_mutable_closure = true;
            g_keep_mutable = true;
            use_pillar_watermarks = false;
        } else if (strcmp(arg, keep_all_str) == 0) {
            g_keep_all = true;
            use_pillar_watermarks = false;
        } else if (strcmp(arg, dead_rs_str) == 0) {
            g_determine_dead_rs = true;
        } else if (strcmp(arg, wpo_normal_str) == 0) {
            g_treat_wpo_as_normal = true;
        } else if (strcmp(arg, gen_str) == 0) {
            g_gen = true;
        } else if (strcmp(arg, use_pretenure) == 0) {
            pre_tenure = true;
#ifdef PUB_PRIV
        } else if (strcmp(arg, use_pub_priv_str) == 0) {
            g_use_pub_priv = 1;
#endif // PUB_PRIV
        } else if (strncmp(arg, proc_str, strlen(proc_str)) == 0) {
    		const char *proc_value = arg + strlen(proc_str);
            if(g_num_threads_specified) {
//                printf("GC warning: redundant specification of num_procs/numThreads.  Using latest value.\n");
            }
            g_num_threads_specified = true;
	    	g_num_threads = atoi(proc_value); // use the specified number of procs
        } else if (strncmp(arg, threads_str, strlen(threads_str)) == 0) {
    		const char *proc_value = arg + strlen(threads_str);
            if(g_num_threads_specified) {
//                printf("GC warning: redundant specification of num_procs/numThreads.  Using latest value.\n");
            }
            g_num_threads_specified = true;
	    	g_num_threads = atoi(proc_value); // use the specified number of procs
        } else if(strncmp(arg,remove_indir_str,strlen(remove_indir_str)) == 0) {
			const char *next = arg + strlen(remove_indir_str);
			if(*next == '=') {
				next++;
				sscanf(next,"%x",&g_remove_indirections);
			} else {
				g_remove_indirections = 0xffffffff;
			}
        } else if(strcmp(arg,profile_str) == 0) {
#ifdef _DEBUG
            if(!profile_out) {
                profile_out = new std::ofstream("tgc_profile.txt");
            }
#else
            orp_cout << "Profiling option only available in debug mode." << std::endl;
#endif
        } else if(strcmp(arg,live_dump_str) == 0) {
#ifdef _DEBUG
            if(!live_dump) {
                live_dump = new std::ofstream("live_dump.txt");
            }
#else
            orp_cout << "Live dump option only available in debug mode." << std::endl;
#endif
        } else if(strcmp(arg,cheney_str) == 0) {
            g_cheney = true;
        } else if(strcmp(arg,no_cheney_str) == 0) {
            g_cheney = false;
		} else if(strcmp(arg,zero_dead_str) == 0) {
			g_zero_dead = true;
#ifdef _DEBUG
		} else if(strcmp(arg,debug_str) == 0) {
			g_maximum_debug = true;
#endif // _DEBUG
		} else if(strcmp(arg,two_space_pn_str) == 0) {
			g_two_space_pn = true;
            use_pillar_watermarks = false;
		} else if(strcmp(arg,pure_two_space_str) == 0) {
			g_pure_two_space = true;
			g_two_space_pn = true;
            use_pillar_watermarks = false;
		} else if(strcmp(arg,eager_promote_str) == 0) {
			g_prevent_rs = true;
        } else if (strncmp(arg, preemptive_collect_str, strlen(preemptive_collect_str)) == 0) {
    		const char *preemptive_collect_value = arg + strlen(preemptive_collect_str);
	    	g_preemptive_collect = atof(preemptive_collect_value);
		} else if (strncmp(arg, heap_compact_str, strlen(heap_compact_str)) == 0) {
    		const char *heap_compact_value = arg + strlen(heap_compact_str);
			g_heap_compact_cmd_override = atoi(heap_compact_value);
		} else if (strncmp(arg, init_sleep_str, strlen(init_sleep_str)) == 0) {
    		const char *init_sleep_value = arg + strlen(init_sleep_str);
			g_init_sleep_time = atoi(init_sleep_value);
		} else if (strncmp(arg, free_block_policy_str, strlen(free_block_policy_str)) == 0) {
    		const char *rfb_policy_value = arg + strlen(free_block_policy_str);
			g_return_free_block_policy = (FREE_BLOCK_POLICY)atoi(rfb_policy_value);
		} else if (strncmp(arg, heap_base_str, strlen(heap_base_str)) == 0) {
    		const char *heap_base_value = arg + strlen(heap_base_str);
#ifdef POINTER64
			g_cmd_line_heap_base = strtoull(heap_base_value,NULL,16);
#else    
			g_cmd_line_heap_base = strtoul(heap_base_value,NULL,16);
#endif
#ifdef CONCURRENT
		} else if (strncmp(arg, max_pause_us_str, strlen(max_pause_us_str)) == 0) {
    		const char *max_pause_us_value = arg + strlen(max_pause_us_str);
			max_pause_us = atoi(max_pause_us_value);
		} else if (strncmp(arg, transition_wait_str, strlen(transition_wait_str)) == 0) {
    		const char *transition_wait_ms_value = arg + strlen(transition_wait_str);
			g_concurrent_transition_wait_time = atoi(transition_wait_ms_value);
#endif // CONCURRENT
        } else {
            orp_cout << "-gc ignoring argument it does not understand." << name << " " << arg << std::endl;   
        }
        return;
    }

    orp_cout << "gc next_command_line_argument passed argument it does not understand." << name << " " << arg << std::endl;
} //gc::next_command_line_argument

 

//
// gc_init initialized the GC internal data structures.
// This routine is called after the last call to gc_next_command_line_argument.
// The ORP should call this *before* any other calls to this interface except
// calls to gc_next_command_line_argument.
//
// 
// gc_init needs a couple of routines to build empty classes so it can
// generated filler objects when it needs to align an object.
//

// THESE ROUTINES GO OUTSIDE THE EXPECTED GC/ORE INTERFACES AND SHOULD
// BE ELIMINATED......

//
// A couple of simple helper function to support gc_init.
//

//
// A function that is not part of the interface but can be used to make sure 
// our compile flags make sense.
// 

//
// The class loader is notifying us that another class has
// just been loaded.
//

GCEXPORT(void, gc_class_loaded) (VTable_Handle vth) {
    assert(0); // This is dead code see gc_class_prepared instead. RLH 2003-4-14
}

//
// API for the ORP to ask the GC to initialize itself.
//

void gc_v4_init();

void _initialize_plan_and_hooks(char *p_plan_file_name) {
    //
    // Create a plan object. This process will check if the
    // user has a .plan file in the right place. If so, the
    // contents of that file override the default settings
    // in the plan.
    //  
    if (p_plan_file_name) {
        _p_gc_plan = new Gc_Plan(p_plan_file_name);
    } else {
        _p_gc_plan = new Gc_Plan();
    }
}


void init_blocks_mrl_gc_v4(char *p_plan_file_name) {
    //
    // Set up the plan object which defines GC parameters,
    // and set up the right level of hooks, depending on
    // the debug level specified by the build.
    //
    _initialize_plan_and_hooks(p_plan_file_name);
    
    // Initialization (perhaps later the ORP should set defaults):
    if (initial_heap_size_bytes == 0) {
        initial_heap_size_bytes = _p_gc_plan->default_initial_heap_size_bytes();
    }
    
    if (final_heap_size_bytes == 0) {
        final_heap_size_bytes = _p_gc_plan->default_final_heap_size_bytes();
    }
    
    unsigned int sub_block_size_bytes = _p_gc_plan->sub_block_size_bytes();
    
    p_global_gc = new Garbage_Collector(initial_heap_size_bytes, final_heap_size_bytes, sub_block_size_bytes);
}

void local_nursery_collection(GC_Thread_Info *tls_for_gc,struct PrtStackIterator *si,Partial_Reveal_Object *escaping_object,bool is_future);


POINTER_SIZE_INT Partial_Reveal_Object::vtable_base;
POINTER_SIZE_INT Partial_Reveal_Object::heap_base = 0;

#ifdef __X86_64__

static char *wbGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->ripPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "Write Barrier frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else
    sprintf(buffer, "Write Barrier frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

static char *wbGetStringForFrame_prt(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->ripPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "Write Barrier prt frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else
    sprintf(buffer, "Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

#else  // __X86_64__

static char *wbGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->eipPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "Write Barrier frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#else
    sprintf(buffer, "Write Barrier frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

static char *wbGetStringForFrame_prt(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->eipPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#else
    sprintf(buffer, "Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

#endif // __X86_64__


#ifdef __X86_64__

static char *cas_wbGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->ripPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "CAS Write Barrier frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else
    sprintf(buffer, "CAS Write Barrier frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

static char *cas_wbGetStringForFrame_prt(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->ripPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "CAS Write Barrier prt frame: ip=%p, rsp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#else
    sprintf(buffer, "CAS Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->ripPtr, si->rsp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

#else  // __X86_64__

static char *cas_wbGetStringForFrame(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->eipPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "CAS Write Barrier frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#else
    sprintf(buffer, "CAS Write Barrier frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

static char *cas_wbGetStringForFrame_prt(PrtStackIterator *si, char *buffer, size_t bufferSize, PrtCimSpecificDataType opaqueData) {
    assert(si);
    assert(si->eipPtr);
#ifdef _WINDOWS
    _snprintf(buffer, bufferSize, "CAS Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#else
    sprintf(buffer, "CAS Write Barrier prt frame: ip=%p, esp=%p, vsh=%p", *si->eipPtr, si->esp, si->vsh);
#endif
    return buffer;
} //wbGetStringForFrame

#endif // __X86_64__


// =============== REGULAR WRITE-BARRIERS =========================================

extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value);
extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset);
extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value);
extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset);
extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p_prt(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p_prt(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p_prt_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p_prt_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);




// =============== CAS VERSIONS =========================================

extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p_prt(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p_prt(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p_prt_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);
extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p_prt_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prev_frame);


// =============== GENERATIONAL VERSIONS =========================================

extern "C" void PRT_STDCALL gc_heap_slot_gen_write_ref_p_prt(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         PrtTaskHandle taskHandle, void *prev_frame);
extern "C" void PRT_STDCALL gc_heap_slot_gen_write_interior_ref_p_prt(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         PrtTaskHandle taskHandle, void *prev_frame);



extern "C" void * PRT_STDCALL gc_heap_object_read_stats(Managed_Object_Handle p_object);
extern "C" void * PRT_STDCALL gc_heap_object_read_stats_null(Managed_Object_Handle p_object);

struct unmanaged_add_entry_callsite_stack_args {
	POINTER_SIZE_INT thread_info;
	POINTER_SIZE_INT p_value;
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
};

struct unmanaged_add_entry_callsite_stack {
	unmanaged_add_entry_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
};

struct unmanaged_add_entry_interior_callsite_stack_args {
	POINTER_SIZE_INT thread_info;
	POINTER_SIZE_INT p_value;
	POINTER_SIZE_INT offset;
};

struct unmanaged_add_entry_interior_callsite_stack {
	unmanaged_add_entry_interior_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
};

struct unmanaged_add_entry_callsite_stack_args_prt {
	POINTER_SIZE_INT thread_info;
	POINTER_SIZE_INT p_value;
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT saved_ebx;
};

struct unmanaged_add_entry_prt_callsite_stack {
	unmanaged_add_entry_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

struct unmanaged_add_entry_interior_callsite_stack_args_prt {
	POINTER_SIZE_INT thread_info;
	POINTER_SIZE_INT p_value;
	POINTER_SIZE_INT offset;
	POINTER_SIZE_INT saved_ebx;
};

struct unmanaged_add_entry_interior_prt_callsite_stack {
	unmanaged_add_entry_interior_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

// mark phase

struct unmanaged_mark_phase_callsite_stack_args {
	POINTER_SIZE_INT arg1;
	POINTER_SIZE_INT arg2;
	POINTER_SIZE_INT arg3;
	POINTER_SIZE_INT arg4;
};

struct unmanaged_mark_phase_interior_callsite_stack_args {
	POINTER_SIZE_INT arg1;
	POINTER_SIZE_INT arg2;
	POINTER_SIZE_INT arg3;
	POINTER_SIZE_INT arg4;
};

struct unmanaged_mark_phase_callsite_stack_args_prt {
	POINTER_SIZE_INT arg1;
	POINTER_SIZE_INT arg2;
	POINTER_SIZE_INT arg3;
	POINTER_SIZE_INT arg4;
	POINTER_SIZE_INT ebx;
};

struct unmanaged_mark_phase_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
};

struct unmanaged_mark_phase_interior_callsite_stack {
	unmanaged_mark_phase_interior_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
};

struct unmanaged_mark_phase_prt_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

struct unmanaged_mark_phase_interior_prt_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};









struct cas_unmanaged_add_entry_callsite_stack {
	unmanaged_add_entry_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
    POINTER_SIZE_INT cmp;
};

struct cas_unmanaged_add_entry_interior_callsite_stack {
	unmanaged_add_entry_interior_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
    POINTER_SIZE_INT cmp;
};

struct cas_unmanaged_add_entry_prt_callsite_stack {
	unmanaged_add_entry_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
    POINTER_SIZE_INT cmp;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

struct cas_unmanaged_add_entry_interior_prt_callsite_stack {
	unmanaged_add_entry_interior_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
    POINTER_SIZE_INT cmp;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

// mark phase

struct cas_unmanaged_mark_phase_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
    POINTER_SIZE_INT cmp;
};

struct cas_unmanaged_mark_phase_interior_callsite_stack {
	unmanaged_mark_phase_interior_callsite_stack_args args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
    POINTER_SIZE_INT cmp;
};

struct cas_unmanaged_mark_phase_prt_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT base;
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
    POINTER_SIZE_INT cmp;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};

struct cas_unmanaged_mark_phase_interior_prt_callsite_stack {
	unmanaged_mark_phase_callsite_stack_args_prt args;
	POINTER_SIZE_INT return_eip;
	// args to the write barrier itself
	POINTER_SIZE_INT p_slot;
	POINTER_SIZE_INT value;
	POINTER_SIZE_INT offset;
    POINTER_SIZE_INT cmp;
#ifndef NO_P2C_TH
    POINTER_SIZE_INT prtTaskHandle;
#endif  // NO_P2C_TH
    POINTER_SIZE_INT prevFrame;
};















#ifdef __X86_64__

static void wbGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_rsp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->ripPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_callsite_stack_args)), 
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_mark_phase_callsite_stack)), 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*ripPtr*/ (PrtCodeAddress*)(cur_rsp + sizeof(unmanaged_add_entry_callsite_stack_args)), 
                   /*rsp*/    (PrtRegister    )(cur_rsp + sizeof(unmanaged_add_entry_callsite_stack)), 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame

static void wbGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_rsp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_prt_nonconcurrent_section) {
        unmanaged_mark_phase_prt_callsite_stack *umppcs = (unmanaged_mark_phase_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)      umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    unmanaged_add_entry_prt_callsite_stack *uaepcs = (unmanaged_add_entry_prt_callsite_stack *)cur_rsp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)      uaepcs->prevFrame, 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame_prt

static void wbEnumerateRoots(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_nonconcurrent_section) {
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagBase,
		       *(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)));
        }
        return;
	}
#endif // CONCURRENT

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->p_slot)),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->p_slot)),
		    PrtGcTagBase,
	       *(void**)(&(((struct unmanaged_add_entry_callsite_stack*)cur_esp)->base)));
    }
} // wbEnumerateRoots

static void wbEnumerateRoots_prt(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_prt_nonconcurrent_section) {
        struct unmanaged_mark_phase_prt_callsite_stack * stack = (struct unmanaged_mark_phase_prt_callsite_stack*)cur_esp;
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(stack->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagBase,
			    *(void**)(&(stack->base)));
        }
		return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_prt_callsite_stack * stack = (struct unmanaged_add_entry_prt_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(stack->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagBase,
		    *(void**)(&(stack->base)));
    }
} // wbEnumerateRoots_prt

static void wbInteriorGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_callsite_stack_args)),
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_mark_phase_interior_callsite_stack)),
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_add_entry_callsite_stack_args)),
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_add_entry_interior_callsite_stack)),
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame

static void wbInteriorGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section) {
        unmanaged_mark_phase_interior_prt_callsite_stack *umppcs = (unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    unmanaged_add_entry_interior_prt_callsite_stack *uaepcs = (unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)uaepcs->prevFrame, 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame_prt

static void wbInteriorEnumerateRoots(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_nonconcurrent_section) {
        struct unmanaged_mark_phase_interior_callsite_stack *umpics = (struct unmanaged_mark_phase_interior_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpics->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpics->offset)));
        return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_interior_callsite_stack *uaeics = (struct unmanaged_add_entry_interior_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeics->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeics->offset)));
} // wbInteriorEnumerateRoots

static void wbInteriorEnumerateRoots_prt(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section) {
        struct unmanaged_mark_phase_interior_prt_callsite_stack *umpipcs = (struct unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpipcs->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpipcs->offset)));
        return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_interior_prt_callsite_stack *uaeipcs = (struct unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeipcs->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeipcs->offset)));
} // wbInteriorEnumerateRoots_prt











static void cas_wbGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_rsp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->ripPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(cas_unmanaged_mark_phase_callsite_stack_args)), 
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_mark_phase_callsite_stack)), 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*ripPtr*/ (PrtCodeAddress*)(cur_rsp + sizeof(    unmanaged_add_entry_callsite_stack_args)), 
                   /*rsp*/    (PrtRegister    )(cur_rsp + sizeof(cas_unmanaged_add_entry_callsite_stack)), 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame

static void cas_wbGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_rsp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_prt_nonconcurrent_section) {
        cas_unmanaged_mark_phase_prt_callsite_stack *umppcs = (cas_unmanaged_mark_phase_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)      umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    cas_unmanaged_add_entry_prt_callsite_stack *uaepcs = (cas_unmanaged_add_entry_prt_callsite_stack *)cur_rsp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)      uaepcs->prevFrame, 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame_prt

static void cas_wbEnumerateRoots(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_nonconcurrent_section) {
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagBase,
		       *(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)));
        }
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->cmp)),
			PrtGcTagDefault,0);
		return;
	}
#endif // CONCURRENT

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->p_slot)),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->p_slot)),
		    PrtGcTagBase,
	       *(void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->base)));
    }
    rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(((struct cas_unmanaged_add_entry_callsite_stack*)cur_esp)->cmp)),
		PrtGcTagDefault,0);
} // wbEnumerateRoots

static void cas_wbEnumerateRoots_prt(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_prt_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_prt_callsite_stack * stack = (struct cas_unmanaged_mark_phase_prt_callsite_stack*)cur_esp;
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(stack->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagBase,
			    *(void**)(&(stack->base)));
        }
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(stack->cmp)),
			PrtGcTagDefault,0);
		return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_prt_callsite_stack * stack = (struct cas_unmanaged_add_entry_prt_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(stack->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagBase,
		    *(void**)(&(stack->base)));
    }
    rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(stack->cmp)),
		PrtGcTagDefault,0);
} // wbEnumerateRoots_prt

static void cas_wbInteriorGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(cas_unmanaged_mark_phase_callsite_stack_args)),
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_mark_phase_interior_callsite_stack)),
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(    unmanaged_add_entry_callsite_stack_args)),
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_add_entry_interior_callsite_stack)),
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame

static void cas_wbInteriorGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_prt_nonconcurrent_section) {
        cas_unmanaged_mark_phase_interior_prt_callsite_stack *umppcs = (cas_unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    cas_unmanaged_add_entry_interior_prt_callsite_stack *uaepcs = (cas_unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)uaepcs->prevFrame, 
                   /*rbxPtr*/ si->rbxPtr, 
                   /*rbpPtr*/ si->rbpPtr, 
                   /*r12Ptr*/ si->r12Ptr, 
                   /*r13Ptr*/ si->r13Ptr, 
                   /*r14Ptr*/ si->r14Ptr, 
                   /*r15Ptr*/ si->r15Ptr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame_prt

static void cas_wbInteriorEnumerateRoots(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_interior_callsite_stack *umpics = (struct cas_unmanaged_mark_phase_interior_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpics->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpics->offset)));
        return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_interior_callsite_stack *uaeics = (struct cas_unmanaged_add_entry_interior_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeics->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeics->offset)));
} // wbInteriorEnumerateRoots

static void cas_wbInteriorEnumerateRoots_prt(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->rsp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_prt_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_interior_prt_callsite_stack *umpipcs = (struct cas_unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpipcs->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpipcs->offset)));
        return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_interior_prt_callsite_stack *uaeipcs = (struct cas_unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeipcs->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeipcs->offset)));
} // wbInteriorEnumerateRoots_prt









#else  // __X86_64__

static void wbGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_callsite_stack_args)), 
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_mark_phase_callsite_stack)), 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_add_entry_callsite_stack_args)), 
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_add_entry_callsite_stack)), 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame

static void wbGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_prt_nonconcurrent_section) {
        unmanaged_mark_phase_prt_callsite_stack *umppcs = (unmanaged_mark_phase_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)      umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    unmanaged_add_entry_prt_callsite_stack *uaepcs = (unmanaged_add_entry_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)      uaepcs->prevFrame, 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame_prt

static void wbEnumerateRoots(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_nonconcurrent_section) {
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)((((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagBase,
		       *(void**)(&(((struct unmanaged_mark_phase_callsite_stack*)cur_esp)->base)));
        }
		return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_callsite_stack * stack = (struct unmanaged_add_entry_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&stack->value),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&stack->base) == NULL) {
        // is this right??? FIX FIX FIX
        rootSetInfo->callback(rootSetInfo->env,
            (void**)(stack->p_slot),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&stack->base),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&stack->p_slot),
		    PrtGcTagBase,
	       *(void**)(&stack->base));
    }
} // wbEnumerateRoots

static void wbEnumerateRoots_prt(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_ref_p_prt_nonconcurrent_section) {
        struct unmanaged_mark_phase_prt_callsite_stack * stack = (struct unmanaged_mark_phase_prt_callsite_stack*)cur_esp;
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(stack->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(stack->p_slot),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagBase,
			    *(void**)(&(stack->base)));
        }
		return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_prt_callsite_stack * stack = (struct unmanaged_add_entry_prt_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(stack->base)) == NULL) {
        // is this right?  FIX FIX FIX
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(stack->p_slot),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagBase,
		    *(void**)(&(stack->base)));
    }
} // wbEnumerateRoots_prt

static void wbInteriorGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_interior_callsite_stack_args)),
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_mark_phase_interior_callsite_stack)),
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_add_entry_interior_callsite_stack_args)),
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(unmanaged_add_entry_interior_callsite_stack)),
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame

static void wbInteriorGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section) {
        unmanaged_mark_phase_interior_prt_callsite_stack *umppcs = (unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    unmanaged_add_entry_interior_prt_callsite_stack *uaepcs = (unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)uaepcs->prevFrame, 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame_prt

static void wbInteriorEnumerateRoots(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_nonconcurrent_section) {
        struct unmanaged_mark_phase_interior_callsite_stack *umpics = (struct unmanaged_mark_phase_interior_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpics->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpics->offset)));
        return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_interior_callsite_stack *uaeics = (struct unmanaged_add_entry_interior_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeics->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeics->offset)));
} // wbInteriorEnumerateRoots

static void wbInteriorEnumerateRoots_prt(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section) {
        struct unmanaged_mark_phase_interior_prt_callsite_stack *umpipcs = (struct unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpipcs->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpipcs->offset)));
        return;
	}
#endif // CONCURRENT

    struct unmanaged_add_entry_interior_prt_callsite_stack *uaeipcs = (struct unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeipcs->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeipcs->offset)));
} // wbInteriorEnumerateRoots_prt









static void cas_wbGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_callsite_stack_args)), 
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_mark_phase_callsite_stack)), 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_add_entry_callsite_stack_args)), 
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_add_entry_callsite_stack)), 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame

static void cas_wbGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_prt_nonconcurrent_section) {
        cas_unmanaged_mark_phase_prt_callsite_stack *umppcs = (cas_unmanaged_mark_phase_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)      umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    cas_unmanaged_add_entry_prt_callsite_stack *uaepcs = (cas_unmanaged_add_entry_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)      uaepcs->prevFrame, 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbGetPreviousFrame_prt

static void cas_wbEnumerateRoots(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_nonconcurrent_section) {
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)((((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->p_slot)),
			    PrtGcTagBase,
		       *(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->base)));
        }
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(((struct cas_unmanaged_mark_phase_callsite_stack*)cur_esp)->cmp)),
			PrtGcTagDefault,0);
		return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_callsite_stack * stack = (struct cas_unmanaged_add_entry_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&stack->value),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(stack->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
            (void**)(stack->p_slot),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
            (void**)(&(stack->p_slot)),
		    PrtGcTagBase,
	       *(void**)(&(stack->base)));
    }
    rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(stack->cmp)),
		PrtGcTagDefault,0);
} // cas_wbEnumerateRoots

static void cas_wbEnumerateRoots_prt(struct PrtStackIterator *si, 
                             struct PrtRseInfo *rootSetInfo, 
                             PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_ref_p_prt_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_prt_callsite_stack * stack = (struct cas_unmanaged_mark_phase_prt_callsite_stack*)cur_esp;
		// at the unmanaged_mark_phase callsite
		// enumerate value as a root
		rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
		// enumerate p_slot as an interior root
		if(*(void**)(&(stack->base)) == NULL) {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(stack->p_slot),
			    PrtGcTagDefault,0);
		} else {
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->base)),
			    PrtGcTagDefault,0);
		    rootSetInfo->callback(rootSetInfo->env,
			    (void**)(&(stack->p_slot)),
			    PrtGcTagBase,
			    *(void**)(&(stack->base)));
        }
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(stack->cmp)),
			PrtGcTagDefault,0);
		return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_prt_callsite_stack * stack = (struct cas_unmanaged_add_entry_prt_callsite_stack*)cur_esp;

	// at the unmanaged_add_entry callsite
	// enumerate value as a root
    rootSetInfo->callback(rootSetInfo->env,(void**)(&(stack->value)),PrtGcTagDefault,0);
	// enumerate p_slot as an interior root
	if(*(void**)(&(stack->base)) == NULL) {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(stack->p_slot),
		    PrtGcTagDefault,0);
	} else {
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->base)),
		    PrtGcTagDefault,0);
        rootSetInfo->callback(rootSetInfo->env,
		    (void**)(&(stack->p_slot)),
		    PrtGcTagBase,
		    *(void**)(&(stack->base)));
    }
    rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(stack->cmp)),
		PrtGcTagDefault,0);
} // cas_wbEnumerateRoots_prt

static void cas_wbInteriorGetPreviousFrame(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_nonconcurrent_section) {
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_mark_phase_interior_callsite_stack_args)),
					   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_mark_phase_interior_callsite_stack)),
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    prtSetStackIteratorFields(si, 
                   /*eipPtr*/ (PrtCodeAddress*)(cur_esp + sizeof(unmanaged_add_entry_interior_callsite_stack_args)),
                   /*esp*/    (PrtRegister)    (cur_esp + sizeof(cas_unmanaged_add_entry_interior_callsite_stack)),
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame

static void cas_wbInteriorGetPreviousFrame_prt(PrtStackIterator *si, PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_prt_nonconcurrent_section) {
        cas_unmanaged_mark_phase_interior_prt_callsite_stack *umppcs = (cas_unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		prtSetStackIteratorFields(si, 
					   /*eipPtr*/ (PrtCodeAddress*)&(umppcs->return_eip), 
					   /*esp*/    (PrtRegister)umppcs->prevFrame, 
					   /*ebxPtr*/ si->ebxPtr, 
					   /*ebpPtr*/ si->ebpPtr, 
					   /*esiPtr*/ si->esiPtr, 
					   /*ediPtr*/ si->ediPtr, 
					   /*vsh*/    si->vsh, 
					   /*virtualFrameNumber*/ 0);
        return;
	}
#endif // CONCURRENT

    cas_unmanaged_add_entry_interior_prt_callsite_stack *uaepcs = (cas_unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
    prtSetStackIteratorFields(si, 
				   /*eipPtr*/ (PrtCodeAddress*)&(uaepcs->return_eip), 
				   /*esp*/    (PrtRegister)uaepcs->prevFrame, 
                   /*ebxPtr*/ si->ebxPtr, 
                   /*ebpPtr*/ si->ebpPtr, 
                   /*esiPtr*/ si->esiPtr, 
                   /*ediPtr*/ si->ediPtr, 
                   /*vsh*/    si->vsh, 
                   /*virtualFrameNumber*/ 0);
} //wbInteriorGetPreviousFrame_prt

static void cas_wbInteriorEnumerateRoots(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_interior_callsite_stack *umpics = (struct cas_unmanaged_mark_phase_interior_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpics->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpics->offset)));
        return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_interior_callsite_stack *uaeics = (struct cas_unmanaged_add_entry_interior_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeics->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeics->offset)));
} // wbInteriorEnumerateRoots

static void cas_wbInteriorEnumerateRoots_prt(struct PrtStackIterator *si, 
                                     struct PrtRseInfo *rootSetInfo, 
                                     PrtCimSpecificDataType opaqueData) {
	POINTER_SIZE_INT cur_esp = (POINTER_SIZE_INT)si->esp;

#ifdef CONCURRENT
	if(*(si->eipPtr) < (PrtCodeAddress)&gc_cas_write_interior_ref_p_prt_nonconcurrent_section) {
        struct cas_unmanaged_mark_phase_interior_prt_callsite_stack *umpipcs = (struct cas_unmanaged_mark_phase_interior_prt_callsite_stack *)cur_esp;
		rootSetInfo->callback(rootSetInfo->env,
			(void**)(&(umpipcs->value)),
			PrtGcTagOffset,
			(void*)*(POINTER_SIZE_INT *)(&(umpipcs->offset)));
        return;
	}
#endif // CONCURRENT

    struct cas_unmanaged_add_entry_interior_prt_callsite_stack *uaeipcs = (struct cas_unmanaged_add_entry_interior_prt_callsite_stack *)cur_esp;
	rootSetInfo->callback(rootSetInfo->env,
		(void**)(&(uaeipcs->value)),
		PrtGcTagOffset,
		(void*)*(POINTER_SIZE_INT *)(&(uaeipcs->offset)));
} // wbInteriorEnumerateRoots_prt















#endif // __X86_64__

// =====================================================================================

class intra_root_info {
public:
	pn_info *ls;
	unsigned    *has_intra_slot;
};

extern "C" void frame_contains_intra_root(void *env, void **rootAddr, PrtGcTag tag, void *parameter) {
	if(!rootAddr) return;

	intra_root_info *iri = (intra_root_info*)env;
    if(*rootAddr >= iri->ls->local_nursery_start && *rootAddr < iri->ls->local_nursery_end) {
		*(iri->has_intra_slot) = 1;
	}
}

#ifndef USE_PTHREADS
void __cdecl privateNurseryTaskSplitCallback(struct PrtStackIterator *si,
						  				     PrtTaskHandle original_task,
										     PrtTaskHandle new_task)
{
	if(!local_nursery_size) {
		printf("privateNurseryTaskSplitCallback called when private nurseries not in use.\n");
		return;
	}

#ifndef CLIENT_CALLBACK_BEFORE_SPLIT
    PrtRseInfo rse;
	rse.callback = &frame_contains_intra_root;
	unsigned result = 0;
	intra_root_info iri;
	GC_Thread_Info *gc_thread_tls = orp_local_to_gc_local(prtGetTlsForTask(original_task));
	iri.ls = gc_thread_tls->get_private_nursery()->local_gc_info;
	iri.has_intra_slot = &result;
	rse.env = &iri;
    prtEnumerateRootsOfActivation(si, &rse);

	// Frame contains a root into the private nursery.
	if(result) {
#if 0
        // BTL 20081102 Debug
        printf("      privateNurseryTaskSplitCallback: frame has root into private nursery, gc_thread_tls=%p\n", gc_thread_tls);
        fflush(stdout);
#endif //0

        // Create a copy of the stack iterator that was passed in so that changes here don't affect the caller.
        PrtStackIterator si_copy;
        memcpy(&si_copy, si, sizeof(PrtStackIterator));
		gc_thread_tls->get_private_nursery()->local_gc_info->m_original_task = original_task;
//		gc_thread_tls->get_private_nursery()->local_gc_info->m_new_task      = new_task;
		local_nursery_collection(gc_thread_tls, &si_copy, (Partial_Reveal_Object*)1, false);
		gc_thread_tls->get_private_nursery()->local_gc_info->m_original_task = NULL;
//		gc_thread_tls->get_private_nursery()->local_gc_info->m_new_task      = NULL;
	}
#else  // CLIENT_CALLBACK_BEFORE_SPLIT
	GC_Thread_Info *gc_thread_tls = orp_local_to_gc_local(prtGetTlsForTask(original_task));
	gc_thread_tls->get_private_nursery()->local_gc_info->m_original_task = original_task;
	local_nursery_collection(gc_thread_tls, NULL, (Partial_Reveal_Object*)1, false);
	gc_thread_tls->get_private_nursery()->local_gc_info->m_original_task = NULL;
#endif // CLIENT_CALLBACK_BEFORE_SPLIT
} // privateNurseryTaskSplitCallback
#endif // !USE_PTHREADS

unsigned g_tls_offset = 0;
//unsigned g_tls_offset_bytes;   // now in c_export.c
extern "C" unsigned g_tls_offset_bytes;

#if defined USE_PTHREADS && !defined __GNUC__
extern "C" int ptw32_processInitialize();
#endif

unsigned int _machine_page_size_bytes;

#ifdef PUB_PRIV
extern "C" void __stdcall cph_destructor(void);
extern "C" void cph_rse_callback(PrtVseHandle theHandle, struct PrtRseInfo *rootSetInfo);
#endif // PUB_PRIV

//
// This also needs to do the work of gc_thread_init for the main thread.
//
GCEXPORT(void, gc_init)() {
    unsigned _number_of_active_processors;
#ifdef _WINDOWS
	SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    _number_of_active_processors = system_info.dwNumberOfProcessors;
	_machine_page_size_bytes     = system_info.dwPageSize;
#elif defined LINUX
    _number_of_active_processors = sysconf(_SC_NPROCESSORS_ONLN);
    _machine_page_size_bytes = getpagesize();
#else
    assert(0);
    orp_exit(-1);
#endif
    if (machine_is_hyperthreaded) {
        _number_of_active_processors /= 2;
    }

#ifdef _WINDOWS
    Sleep(g_init_sleep_time);
#endif // _WINDOWS

    // First set the global number of threads everyone is to use.
    if(single_threaded_gc) {
        g_num_cpus = 1;
    } else if(g_num_threads_specified) {
        if(g_num_threads > 0) {
            g_num_cpus = g_num_threads;
        } else {
            if((int)_number_of_active_processors + g_num_threads <= 0) {
                g_num_cpus = 1;
            } else {
                g_num_cpus = _number_of_active_processors + g_num_threads;
            }
        }
    } else {
        g_num_cpus = _number_of_active_processors;
    }

	if(g_two_space_pn) {
		if(!local_nursery_size || !g_use_pub_priv) {
			orp_cout << "Two space private-nursery mode requires that private mode is being used." << std::endl;
			orp_exit(18000);
		}
	}
	if(g_use_pub_priv) {
		if(!local_nursery_size) {
			orp_cout << "Private heap mode requires that private nurseries be in use." << std::endl;
			orp_exit(18001);
		}
	}

    // At development time, check to ensure that the compile time
    // flags were set correctly. If verbosegc is on announce what
    // gc is running.
    //
    if (orp_number_of_gc_bytes_in_vtable() < sizeof(void *)) {
        orp_cout << "GC_V4 error: orp_number_of_gc_bytes_in_vtable() returns " << (unsigned)(orp_number_of_gc_bytes_in_vtable()) <<
            " bytes, minimum " << (unsigned)sizeof(Partial_Reveal_VTable) << " needed." << std::endl;
        orp_exit(17005);
    }

    if (orp_number_of_gc_bytes_in_thread_local() < sizeof(GC_Thread_Info)) {
        orp_cout << "GC_V4 error: orp_number_of_gc_bytes_in_thread_local() returns " << (unsigned)(orp_number_of_gc_bytes_in_thread_local()) <<
            " bytes, minimum " << (unsigned)sizeof(GC_Thread_Info) << " needed." << std::endl;
        orp_exit(17006);
    }

    bool compressed = (orp_vtable_pointers_are_compressed() ? true : false);
    if (Partial_Reveal_Object::use_compressed_vtable_pointers() != compressed) {
        printf("GC error: mismatch between Partial_Reveal_Object::use_compressed_vtable_pointers()=%d\n", Partial_Reveal_Object::use_compressed_vtable_pointers());
        printf("          and orp_vtable_pointers_are_compressed()=%d\n", compressed);
        orp_exit(17007);
    }
    Partial_Reveal_Object::vtable_base = orp_get_vtable_base();

    if((GC_NUM_LIVE_OBJECT_CARDS_PER_GC_BLOCK - GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO) % GC_NUM_BITS_PER_DWORD != 0) {
        printf("Mark bit vector length is not a multiple of 4.\n");
        orp_exit(20000);
    }

#ifdef ALLOW_COMPRESSED_REFS
#ifndef DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
    gc_references_are_compressed = (orp_references_are_compressed() ? true : false);
#endif // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
#endif

    // The following flag will be set to true 
    // (by the ORP calling gc_orp_initialized) when the ORP is fully initialized
    // Till then we can't do a stop-the-world.
    //
    orp_initialized = 0;
    
    interior_pointer_table   = new ExpandInPlaceArray<slot_offset_entry>(DEFAULT_OBJECT_SIZE_IN_ENTRIES);
    compressed_pointer_table = new ExpandInPlaceArray<slot_offset_entry>(DEFAULT_OBJECT_SIZE_IN_ENTRIES);
    
    assert(p_global_gc == NULL);
    
    // Creates the garbage Collector
    init_blocks_mrl_gc_v4(p_plan_file_name);

    assert(p_global_gc);

#ifndef USE_LOCKCMPEX_FOR_THREAD_LOCK
#ifdef USE_PTHREADS
#ifndef __GNUC__
    ptw32_processInitialize();
#endif // __GNUC__
	active_thread_gc_info_list_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(active_thread_gc_info_list_lock,NULL);
#else  // USE_PTHREADS
	active_thread_gc_info_list_lock = mcrtTicketLockNew();
#endif // USE_PTHREADS
#endif

    g_chunk_lock = orp_synch_create_critical_section();

    // Create and initialize the garbage collector
    p_global_gc->gc_v4_init();

    unsigned int block_info_size = sizeof(block_info);
    assert (block_info_size < 4096); 

    // If we hit this we should take a close look at our defines. While it is possible
    // to use more than one page that seems real expensive.

    g_tls_offset = ptkGetNextTlsOffset(sizeof(GC_Thread_Info));
    g_tls_offset_bytes = g_tls_offset * 4;

#if 0
	// Re-enable this part once we have the right initialization order in the P main.
	if(tls_offset != 0) {
		printf("TGC must be given a TLS offset of 0...exiting\n");
		exit(17009);
	}
#endif

    if(local_nursery_size) {
        gc_heap_slot_write_barrier_indirect      = gc_heap_slot_write_ref_p;
        gc_heap_slot_write_interior_indirect     = gc_heap_slot_write_interior_ref_p;
        gc_heap_slot_write_barrier_indirect_prt  = gc_heap_slot_write_ref_p_prt;
        gc_heap_slot_write_interior_indirect_prt = gc_heap_slot_write_interior_ref_p_prt;

        gc_cas_write_barrier_indirect            = gc_cas_write_ref_p;
        gc_cas_write_interior_indirect           = gc_cas_write_interior_ref_p;
        gc_cas_write_barrier_indirect_prt        = gc_cas_write_ref_p_prt;
        gc_cas_write_interior_indirect_prt       = gc_cas_write_interior_ref_p_prt;

        // --- Regular write-barrier code region registration.
	    static struct PrtCodeInfoManagerFunctions wbFuncs = {
            wbGetStringForFrame, wbGetPreviousFrame, wbEnumerateRoots
        };

        PrtCodeInfoManager wbCim = prtRegisterCodeInfoManager("WriteBarrier CodeInfoManager", wbFuncs);
        prtAddCodeRegion(wbCim, (PrtCodeAddress)&gc_heap_slot_write_ref_p, ((PrtCodeAddress)&gc_heap_slot_write_ref_p_end)-1, NULL);

        static struct PrtCodeInfoManagerFunctions wbInteriorFuncs = {
            wbGetStringForFrame, wbInteriorGetPreviousFrame, wbInteriorEnumerateRoots
        };

        PrtCodeInfoManager wbInteriorCim = prtRegisterCodeInfoManager("WriteBarrier Interior CodeInfoManager", wbInteriorFuncs);
        prtAddCodeRegion(wbInteriorCim, (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p, ((PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_end)-1, NULL);

	    static struct PrtCodeInfoManagerFunctions wbFuncs_prt = {
            wbGetStringForFrame_prt, wbGetPreviousFrame_prt, wbEnumerateRoots_prt
        };

        PrtCodeInfoManager wbCim_prt = prtRegisterCodeInfoManager("WriteBarrier_prt CodeInfoManager", wbFuncs_prt);
        prtAddCodeRegion(wbCim_prt, (PrtCodeAddress)&gc_heap_slot_write_ref_p_prt, ((PrtCodeAddress)&gc_heap_slot_write_ref_p_prt_end)-1, NULL);

        static struct PrtCodeInfoManagerFunctions wbInteriorFuncs_prt = {
            wbGetStringForFrame_prt, wbInteriorGetPreviousFrame_prt, wbInteriorEnumerateRoots_prt
        };

        PrtCodeInfoManager wbInteriorCim_prt = prtRegisterCodeInfoManager("WriteBarrier_prt Interior CodeInfoManager", wbInteriorFuncs_prt);
        prtAddCodeRegion(wbInteriorCim_prt, (PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt, ((PrtCodeAddress)&gc_heap_slot_write_interior_ref_p_prt_end)-1, NULL);

        // --- CAS write-barrier code region registration.
        static struct PrtCodeInfoManagerFunctions cas_wbFuncs = {
            cas_wbGetStringForFrame, cas_wbGetPreviousFrame, cas_wbEnumerateRoots
        };

        // --- CAS write-barrier code region registration.
        PrtCodeInfoManager cas_wbCim = prtRegisterCodeInfoManager("CAS WriteBarrier CodeInfoManager", cas_wbFuncs);
        prtAddCodeRegion(cas_wbCim, (PrtCodeAddress)&gc_cas_write_ref_p, ((PrtCodeAddress)&gc_cas_write_ref_p_end)-1, NULL);

        static struct PrtCodeInfoManagerFunctions cas_wbInteriorFuncs = {
            cas_wbGetStringForFrame, cas_wbInteriorGetPreviousFrame, cas_wbInteriorEnumerateRoots
        };

        PrtCodeInfoManager cas_wbInteriorCim = prtRegisterCodeInfoManager("CAS WriteBarrier Interior CodeInfoManager", cas_wbInteriorFuncs);
        prtAddCodeRegion(cas_wbInteriorCim, (PrtCodeAddress)&gc_cas_write_interior_ref_p, ((PrtCodeAddress)&gc_cas_write_interior_ref_p_end)-1, NULL);

	    static struct PrtCodeInfoManagerFunctions cas_wbFuncs_prt = {
            cas_wbGetStringForFrame_prt, cas_wbGetPreviousFrame_prt, cas_wbEnumerateRoots_prt
        };

        PrtCodeInfoManager cas_wbCim_prt = prtRegisterCodeInfoManager("CAS WriteBarrier_prt CodeInfoManager", cas_wbFuncs_prt);
        prtAddCodeRegion(cas_wbCim_prt, (PrtCodeAddress)&gc_cas_write_ref_p_prt, ((PrtCodeAddress)&gc_cas_write_ref_p_prt_end)-1, NULL);

        static struct PrtCodeInfoManagerFunctions cas_wbInteriorFuncs_prt = {
            cas_wbGetStringForFrame_prt, cas_wbInteriorGetPreviousFrame_prt, cas_wbInteriorEnumerateRoots_prt
        };

        PrtCodeInfoManager cas_wbInteriorCim_prt = prtRegisterCodeInfoManager("CAS WriteBarrier_prt Interior CodeInfoManager", cas_wbInteriorFuncs_prt);
        prtAddCodeRegion(cas_wbInteriorCim_prt, (PrtCodeAddress)&gc_cas_write_interior_ref_p_prt, ((PrtCodeAddress)&gc_cas_write_interior_ref_p_prt_end)-1, NULL);
    } else if(g_gen) {
        gc_heap_slot_write_barrier_indirect      = gc_heap_slot_write_ref_p_null;
        gc_heap_slot_write_interior_indirect     = gc_heap_slot_write_interior_ref_p_null;
        gc_heap_slot_write_barrier_indirect_prt  = gc_heap_slot_gen_write_ref_p_prt;
        gc_heap_slot_write_interior_indirect_prt = gc_heap_slot_gen_write_interior_ref_p_prt;

        gc_cas_write_barrier_indirect            = gc_cas_write_ref_p_null;
        gc_cas_write_interior_indirect           = gc_cas_write_interior_ref_p_null;
        gc_cas_write_barrier_indirect_prt        = gc_cas_write_ref_p_prt_null;
        gc_cas_write_interior_indirect_prt       = gc_cas_write_interior_ref_p_prt_null;

        // We don't need to register write-barrier code regions here because write barriers cannot lead to GCs.
    } else {
        gc_heap_slot_write_barrier_indirect      = gc_heap_slot_write_ref_p_null;
        gc_heap_slot_write_interior_indirect     = gc_heap_slot_write_interior_ref_p_null;
        gc_heap_slot_write_barrier_indirect_prt  = gc_heap_slot_write_ref_p_prt_null;
        gc_heap_slot_write_interior_indirect_prt = gc_heap_slot_write_interior_ref_p_prt_null;

        gc_cas_write_barrier_indirect            = gc_cas_write_ref_p_null;
        gc_cas_write_interior_indirect           = gc_cas_write_interior_ref_p_null;
        gc_cas_write_barrier_indirect_prt        = gc_cas_write_ref_p_prt_null;
        gc_cas_write_interior_indirect_prt       = gc_cas_write_interior_ref_p_prt_null;
    }

#ifndef USE_PTHREADS
	if(local_nursery_size) {
		prtRegisterTaskSplitCallback(privateNurseryTaskSplitCallback);
	}
#endif // !USE_PTHREADS

#ifdef CONCURRENT_DEBUG_2
    cgcdump = fopen("cgc_lives.txt","w");
#endif

#if 0
    char wd[1000];
    printf("no sweep regular blocks %s\n",getcwd(wd,1000));
#endif
#ifdef PUB_PRIV
	prtRegisterVseRseFunction((PrtCodeAddress)&cph_destructor, &cph_rse_callback);
#endif // PUB_PRIV
}

extern "C" GC_Thread_Info * get_gc_thread_local(void) {
    unsigned *orp_local = (unsigned*)orp_get_gc_thread_local();
    return (GC_Thread_Info*)(orp_local + g_tls_offset);
}

extern "C" GC_Thread_Info * orp_local_to_gc_local(void *tp) {
    return (GC_Thread_Info*)((unsigned*)tp + g_tls_offset);
}


//
// This API is used by the ORP to notify the GC that the
// ORP has completed bootstrapping and initialization, and 
// is henceforth ready to field requests for enumerating 
// live references.
//
// Prior to this function being called the GC might see some
// strange sights such as NULL or incomplete vtables. The GC will
// need to consider these as normal and work with the ORP to ensure 
// that bootstrapping works. This means that the GC will make few
// demands on the ORP prior to this routine being called.
//
// However, once called the GC will feel free to do 
// stop-the-world collections and will assume that the entire
// orp_for_gc interface is available and fully functioning.
//
// If this routine is called twice the result is undefined.
//
GCEXPORT(void, gc_orp_initialized)() {
#if 0
    if (orp_initialized) {
        orp_cout << " Internal error - Multiple calls to gc_orp_initialized encountered."
            << std::endl;
    }
#endif // 0
    orp_initialized = 13; // any old non-zero will do
}


//
// This is called once the ORP has no use for the heap or the 
// garbage collector data structures. The assumption is that the 
// ORP is exiting but needs to give the GC time to run destructors 
// and free up memory it has gotten from the OS.
// After this routine has been called the ORP can not relie on any
// data structures created by the GC.
//

std::map<void*,unsigned>     g_escaping_vtables;
std::map<void*,EscapeIpInfo> g_escape_ips;
std::map<void*,unsigned>     g_barrier_base;
std::map<unsigned,unsigned>  g_barrier_base_sizes;

//unsigned alloc_escaping = 0;

#ifdef RECORD_IPS
class ip_info {
public:
	unsigned count;
	std::string frame_name;
};
extern std::map<void *,ip_info> g_record_ips;
#endif // RECORD_IPS

#include "pgc.h"
#include <fstream>

#ifdef TRACK_IPS
extern std::set<void *> previous_ips, this_ips;
#endif // TRACK_IPS

void thread_stats_print(GC_Small_Nursery_Info *private_nursery) {
	pn_info *local_collector = private_nursery->local_gc_info;

	if(local_collector->num_micro_collections) {
		unsigned average = get_microseconds(local_collector->sum_micro_time) / local_collector->num_micro_collections;
#if defined _DEBUG
		printf("u-collection: %d %d %d Time = %d ms",
			local_collector->num_micro_collections,
			private_nursery->num_promote_on_escape,
			private_nursery->num_promote_on_escape_interior,
			get_microseconds(local_collector->sum_micro_time) / 1000);
#else
		printf("u-collection: %d Time = %d ms",
			local_collector->num_micro_collections,
			get_microseconds(local_collector->sum_micro_time) / 1000);
#endif
		printf(" Avg = %d us",average);
		printf(" Max = %d us",local_collector->max_collection_time);
#if defined _DEBUG
     	printf(" Size = %I64u",private_nursery->space_used / local_collector->num_micro_collections);
     	printf(" PerByteSurvive = %f",100.0 * ((float)private_nursery->size_objects_escaping/private_nursery->space_used));
		printf(" Frames = %.2f",(float)private_nursery->frame_count / local_collector->num_micro_collections);
#endif // __DEBUG
		printf("\n");

#ifdef DETAILED_PN_TIMES
        unsigned a,b,c,d,e,f;
		a =	local_collector->roots_and_mark_time.QuadPart / local_collector->num_micro_collections;
		b =	local_collector->allocate_time.QuadPart / local_collector->num_micro_collections;
		c =	local_collector->update_root_time.QuadPart / local_collector->num_micro_collections;
		d =	local_collector->move_time.QuadPart / local_collector->num_micro_collections;
		e =	local_collector->clear_time.QuadPart / local_collector->num_micro_collections;
		f =	local_collector->memset_time.QuadPart / local_collector->num_micro_collections;

		f -= e;
		e -= d;
		d -= c;
		c -= b;
		b -= a;
        
		printf("RootsMark: %d, Allocate: %d, UpdateRoots: %d, Move: %d, Clear: %d, Memset: %d\n",a,b,c,d,e,f);

		a =	local_collector->mark_prepare_time.QuadPart / local_collector->num_micro_collections;
		b =	local_collector->mark_stackwalk_time.QuadPart / local_collector->num_micro_collections;
		c =	local_collector->mark_push_roots_escaping_time.QuadPart / local_collector->num_micro_collections;
		d =	local_collector->mark_all_escaping_time.QuadPart / local_collector->num_micro_collections;
		e =	local_collector->mark_push_roots_time.QuadPart / local_collector->num_micro_collections;
		f =	local_collector->mark_all_time.QuadPart / local_collector->num_micro_collections;

		f -= e;
		e -= d;
		d -= c;
		c -= b;
		b -= a;

		printf("Prepare: %d, Stackwalk: %d, Push Roots Escaping: %d, Mark Escaping: %d, Push Roots: %d, Mark: %d\n",a,b,c,d,e,f);

#ifdef CLEAR_STATS
		a =	local_collector->root_clear.QuadPart / local_collector->num_micro_collections;
		b =	local_collector->ipt_private_clear.QuadPart / local_collector->num_micro_collections;
		c =	local_collector->ipt_public_clear.QuadPart / local_collector->num_micro_collections;
		d =	local_collector->pi_clear.QuadPart / local_collector->num_micro_collections;
		e =	local_collector->intra_slots_clear.QuadPart / local_collector->num_micro_collections;
		f =	local_collector->inter_slots_clear.QuadPart / local_collector->num_micro_collections;
		g =	local_collector->live_objects_clear.QuadPart / local_collector->num_micro_collections;

		g -= f;
		f -= e;
		e -= d;
		d -= c;
		c -= b;
		b -= a;
        
		printf("Clear=> root: %d, ipt_priv: %d, ipt_pub: %d, pi: %d, intra: %d, inter: %d, live: %d\n",a,b,c,d,e,f,g);
#endif

		if(pn_history) {
			char buf[100];
			sprintf(buf,"%p",private_nursery);
			FILE *f = fopen(buf,"w");
			if(f) {
				std::list<pn_collection_stats>::iterator iter;
				for(iter  = local_collector->pn_stats->begin();
					iter != local_collector->pn_stats->end();
					++iter) {
					fprintf(f,"%I64u %I64u %d %d %d %d\n",
						iter->start_time,
						iter->end_time,
						get_time_in_microseconds(iter->start_time, iter->end_time),
						iter->size_at_collection,
						iter->num_lives,
						iter->size_lives);
				}
				fclose(f);
			}
		}
#endif
#if defined _DEBUG
		printf("Num marks = %I64u, Num slots = %I64u, IIMM/RLRL %I64u %.2f %I64u %.2f %I64u %.2f %I64u %.2f\n",
			local_collector->num_marks,
			local_collector->num_slots,
			local_collector->num_immutable_ref,local_collector->num_immutable_ref / (float)local_collector->num_marks,
			local_collector->num_immutable_refless,local_collector->num_immutable_refless / (float)local_collector->num_marks,
			local_collector->num_mutable_ref,local_collector->num_mutable_ref / (float)local_collector->num_marks,
			local_collector->num_mutable_refless,local_collector->num_mutable_refless / (float)local_collector->num_marks);
		printf("%p, Intra size = %d, Inter size = %d, Mark stack = %d\n",local_collector,
			local_collector->intra_slots->capacity(),
			local_collector->inter_slots->capacity(),
			local_collector->mark_stack.capacity());
        printf("Escaping live: %I64u, Stack live: %I64u, Remembered Set live: %I64u\n",
            local_collector->num_escape_live, local_collector->num_stack_live, local_collector->num_rs_live);
#ifdef TYPE_SURVIVAL
		printf("VTable survival from private nursery.\n");
		std::multimap<LONGLONG, struct Partial_Reveal_VTable*, std::greater<LONGLONG> > sorter;
		std::map<struct Partial_Reveal_VTable *,TypeSurvivalInfo>::iterator ts_iter;
		std::multimap<LONGLONG, struct Partial_Reveal_VTable*, std::greater<LONGLONG> >::iterator sort_iter;
		for(ts_iter  = local_collector->m_type_survival.begin();
		    ts_iter != local_collector->m_type_survival.end();
			++ts_iter) {
			sorter.insert(std::pair<unsigned,struct Partial_Reveal_VTable*>(ts_iter->second.m_num_objects,ts_iter->first));
		}
		for(sort_iter  = sorter.begin();
		    sort_iter != sorter.end();
			++sort_iter) {
			ts_iter = local_collector->m_type_survival.find(sort_iter->second);
			assert(ts_iter != local_collector->m_type_survival.end());
            printf("%p => %f objects (%I64u) %f bytes (%I64u) \n",ts_iter->first,
				(float)ts_iter->second.m_num_objects / local_collector->num_marks,
				       ts_iter->second.m_num_objects,
				(float)ts_iter->second.m_num_bytes   / private_nursery->size_objects_escaping,
				       ts_iter->second.m_num_bytes);
		}
#endif // TYPE_SURVIVAL
#endif

#ifdef SURVIVE_WHERE
		std::map<struct Partial_Reveal_VTable*,SurviveStat>::iterator m_sw_value_iter;
		std::map<struct Partial_Reveal_VTable*,SurviveStat>::iterator m_sw_base_iter;
		std::map<PrtCodeAddress,SurviveStat>::iterator m_sw_eip_iter;

		printf("Value vtables and percent surviving (vtable, survive percent, num instances, percent instances.\n");
		for(m_sw_value_iter  = local_collector->m_sw_value.begin();
		    m_sw_value_iter != local_collector->m_sw_value.end();
			++m_sw_value_iter) {
			printf("%p %lf %d %f\n",m_sw_value_iter->first,
				m_sw_value_iter->second.sum_survive_percent / m_sw_value_iter->second.num_instance,
				m_sw_value_iter->second.num_instance,
				(float)m_sw_value_iter->second.num_instance / local_collector->num_micro_collections);
		}
		printf("Base vtables and percent surviving.\n");
		for(m_sw_base_iter  = local_collector->m_sw_base.begin();
		    m_sw_base_iter != local_collector->m_sw_base.end();
			++m_sw_base_iter) {
			printf("%p %lf %d %f\n",m_sw_base_iter->first,
				m_sw_base_iter->second.sum_survive_percent / m_sw_base_iter->second.num_instance,
				m_sw_base_iter->second.num_instance,
				(float)m_sw_base_iter->second.num_instance / local_collector->num_micro_collections);
		}
		printf("Eip locations and percent surviving.\n");
		for(m_sw_eip_iter  = local_collector->m_sw_eip.begin();
		    m_sw_eip_iter != local_collector->m_sw_eip.end();
			++m_sw_eip_iter) {
			printf("%p %lf %d %f\n",m_sw_eip_iter->first,
				m_sw_eip_iter->second.sum_survive_percent / m_sw_eip_iter->second.num_instance,
				m_sw_eip_iter->second.num_instance,
				(float)m_sw_eip_iter->second.num_instance / local_collector->num_micro_collections);
		}
#endif // SURVIVE_WHERE

		if(adaptive_nursery_size) {
			printf("Last nursery size = %d\n",(unsigned)((char*)private_nursery->tls_current_ceiling - (char*)local_collector->local_nursery_start));
		}
	}
#ifdef PUB_PRIV
	if(local_collector->num_private_heap_collections) {
		printf("PH-collections: %d, Total time = %d ms, Average = %d us\n",
			local_collector->num_private_heap_collections,
			local_collector->sum_micro_time.QuadPart / 1000,
			local_collector->sum_micro_time.QuadPart / local_collector->num_micro_collections);
        printf("PH-objects: %d, PH-escaping: %d = %f\n",
            local_collector->num_private_collection_objects,
            local_collector->num_private_collection_escaping,
            (float)local_collector->num_private_collection_escaping / local_collector->num_private_collection_objects);
	}
#endif // PUB_PRIV
}

#include <list>
std::list<Partial_Reveal_VTable *> g_tgc_vtable_list;

GCEXPORT(void, gc_wrapup)() {
#ifdef CONCURRENT
	stop_concurrent = true;
	start_concurrent_gc = 1; // this gets the concurrent GC thread to start and recognize it needs to stop
	p_global_gc->num_threads_remaining_until_next_phase = 0;
    while(concurrent_gc_thread_id) {
#ifdef USE_PTHREADS
        sched_yield();
#else  // USE_PTHREADS
        mcrtThreadYield();
#endif // USE_PTHREADS
	}
#endif // CONCURRENT

    if (verify_gc) {
//      orp_cout << "gc_wrapup(): FILL ME\n";
    }

    unsigned wpo_index;
    for(wpo_index = 0;
        wpo_index < p_global_gc->m_wpos.size();
        ++wpo_index) {
        weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];

        wpo_finalizer_callback(root);
    }

	if(verbose_gc && local_nursery_size) {
		get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

		while (active_thread_gc_info_list) {
			GC_Small_Nursery_Info *private_nursery = active_thread_gc_info_list->get_private_nursery();

			thread_stats_print(private_nursery);

			active_thread_gc_info_list = active_thread_gc_info_list->p_active_gc_thread_info; // Do it for all the threads.
		}
	    
		release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

#ifdef TRACK_ESCAPING_VTABLES
    printf("# of escaping vtables = %d\n",g_escaping_vtables.size());
	std::map<void*,unsigned>::iterator iter;
	for(iter  = g_escaping_vtables.begin();
	    iter != g_escaping_vtables.end();
		++iter) {
		printf("%p = %d , immutable = %d\n",iter->first,iter->second,pgc_is_vtable_immutable((struct VTable*)iter->first));
	}

    printf("# of barrier bases = %d\n",g_barrier_base.size());
	for(iter  = g_barrier_base.begin();
	    iter != g_barrier_base.end();
		++iter) {
		printf("%p = %d\n",iter->first,iter->second);
	}

	std::ofstream base_sizes("barrier_base_sizes.txt");
	std::map<unsigned,unsigned>::iterator uuiter;
	for(uuiter  = g_barrier_base_sizes.begin();
	    uuiter != g_barrier_base_sizes.end();
		++uuiter) {
		base_sizes << uuiter->first << " " << uuiter->second << std::endl;
	}
	base_sizes.close();

#endif // TRACK_ESCAPING_VTABLES

#ifdef TRACK_ESCAPING_IPS
    printf("# of escaping ips = %d\n",g_escape_ips.size());
	std::map<void*,EscapeIpInfo>::iterator ip_iter;
	for(ip_iter  = g_escape_ips.begin();
	    ip_iter != g_escape_ips.end();
		++ip_iter) {
		printf("%p = %d, %s\n",ip_iter->first,ip_iter->second.m_count,ip_iter->second.m_description);
	}
#endif // TRACK_ESCAPING_VTABLES

#ifdef RECORD_IPS
	std::multimap<unsigned,void*> sorter;

    printf("size = %d\n",g_record_ips.size());
	std::map<void*,ip_info>::iterator rip_iter;
	for(rip_iter  = g_record_ips.begin();
	    rip_iter != g_record_ips.end();
		++rip_iter) {
		sorter.insert(std::pair<unsigned,void*>(rip_iter->second.count,rip_iter->first));
	}

	printf("IPs enumerated during local nursery collections\n");
	printf("EIP       count     name\n");
	printf("===============================================\n");
	std::multimap<unsigned,void*>::reverse_iterator sort_iter;
	for(sort_iter  = sorter.rbegin();
	    sort_iter != sorter.rend();
		++sort_iter) {
		rip_iter = g_record_ips.find(sort_iter->second);
		assert(rip_iter != g_record_ips.end());
		printf("%p, %d, %s\n",rip_iter->first,rip_iter->second.count,rip_iter->second.frame_name.c_str());
	}
#endif // RECORD_IPS

#ifdef TRACK_IPS
	std::ofstream good_ips("good_ips.txt");

	std::set<void *>::iterator iter;
	for(iter  = this_ips.begin();
		iter != this_ips.end();
		++iter) {
		good_ips << *iter << std::endl;
#if 0
		if(previous_ips.find(*iter) == previous_ips.end()) {
			printf("Different IP = %p\n",*iter);
		}
#endif
	}
#endif

    while(!g_tgc_vtable_list.empty()) {
        Partial_Reveal_VTable *vt = g_tgc_vtable_list.front();

        free(vt->get_gcvt());

        g_tgc_vtable_list.pop_front();
    }

    if(g_public_read || g_private_read) {
        printf("Read stats - public %% = %f, private %% = %f\n", (float)g_public_read / (g_public_read + g_private_read), (float)g_private_read / (g_public_read + g_private_read));
    }

#ifdef _DEBUG
    if(profile_out) {
        profile_out->close();
        delete profile_out;
    }
    if(live_dump) {
        live_dump->close();
        delete live_dump;
    }
#endif
}

/****
*
*  Routines to support barriers such as read and write barriers.
* 
*****/
 
volatile gc_phase current_gc_phase;

GCEXPORT(void, gc_heap_write_global_slot)(volatile uint32 *txnRec, Managed_Object_Handle *p_slot, Managed_Object_Handle value) {
    *p_slot = value;
}


//
// The following routines are the only way to alter any value in the gc heap.  
//

// In a place with gc disabled an entire object was written, for example inside
// clone. This means that the entire object must be scanned and treated as if
// all the fields had been altered. 

GCEXPORT(void, gc_heap_wrote_object) (Managed_Object_Handle p_base_of_object) {
    INTERNAL(gc_write_barrier)(p_base_of_object);
}

struct add_entry_args {
	POINTER_SIZE_INT arg1;
	POINTER_SIZE_INT arg2;
};

void add_entry_proxy(ExpandInPlaceArray<Managed_Object_Handle*> *array,Managed_Object_Handle *p_slot) {
	array->add_entry(p_slot);
}


Partial_Reveal_Object * move_immutable_recursive(Partial_Reveal_Object *obj, 
												 GC_Small_Nursery_Info *private_nursery, 
												 Partial_Reveal_VTable *vt,
												 GC_Thread_Info *thread);

Partial_Reveal_Object * move_immutable_slot (Slot p_slot, GC_Small_Nursery_Info *private_nursery, GC_Thread_Info *thread) {
	assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return NULL;
    }
	Partial_Reveal_Object *p_obj = p_slot.dereference();

	if(p_obj >= private_nursery->local_gc_info->local_nursery_start && p_obj <= private_nursery->local_gc_info->local_nursery_end) {
		Partial_Reveal_VTable *vt = p_obj->vt();
		return move_immutable_recursive(p_obj, private_nursery, vt, thread);
	} else {
		return p_obj;
	}
}

void move_immutable_array_entries(Partial_Reveal_Object *p_object, 
						          GC_Small_Nursery_Info *private_nursery, 
							      Partial_Reveal_VTable *vt,
								  GC_Thread_Info *thread) {
    Type_Info_Handle tih = class_get_element_type_info(vt->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.
    
        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,vt);
        for (int32 i=array_length-1; i>=0; i--) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, vt));
		    if (!p_element.is_null()) {
				p_element.unchecked_update(move_immutable_slot (p_element, private_nursery, thread));
			}
        }
    } else if(type_info_is_primitive(tih)) { 
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(vt->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,vt);
        Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
        unsigned int *offset_scanner = NULL;
        // Offsets assume that the object starts with vtable pointer.  We'll set our
        // fake object to scan 4 bytes before the start of the value type to adjust.
        void *cur_value_type_entry_as_object = (Byte*)p_object + first_elem_offset - base_offset;
        for(int i = 0; i < array_length; i++) {
            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
            Slot pp_target_object(NULL);
            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
			    if (!pp_target_object.is_null()) {
					pp_target_object.unchecked_update(move_immutable_slot (pp_target_object, private_nursery, thread));
			    }
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            // handle weak refs
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
			    if (!pp_target_object.is_null()) {
					pp_target_object.unchecked_update(move_immutable_slot (pp_target_object, private_nursery, thread));
			    }
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
}

Partial_Reveal_Object * move_immutable_recursive(Partial_Reveal_Object *obj, 
												 GC_Small_Nursery_Info *private_nursery, 
												 Partial_Reveal_VTable *vt,
												 GC_Thread_Info *thread) {
#ifdef RECORD_IMMUTABLE_COPIES
	std::map<Partial_Reveal_Object*,Partial_Reveal_Object*>::iterator dup_iter;
	dup_iter = private_nursery->local_gc_info->promoted_immutables.find(obj);
	if(dup_iter == private_nursery->local_gc_info->promoted_immutables.end()) {
#endif // RECORD_IMMUTABLE_COPIES
    unsigned obj_size = get_object_size_bytes_with_vt(obj,vt);
	Partial_Reveal_Object *new_loc = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
		obj_size,
		(Allocation_Handle)vt,
		thread,
		separate_immutable ? thread->get_public_immutable_nursery() : thread->get_public_nursery()
#ifdef PUB_PRIV
		, false
#endif // PUB_PRIV
		);
#ifdef RECORD_IMMUTABLE_COPIES
		private_nursery->local_gc_info->promoted_immutables.insert(std::pair<Partial_Reveal_Object*,Partial_Reveal_Object*>(obj,new_loc));
#endif // RECORD_IMMUTABLE_COPIES

		if (vt->get_gcvt()->gc_object_has_slots) {
			if (is_array(obj)) {
				move_immutable_array_entries(obj, private_nursery, vt, thread);
			} 
			unsigned int *offset_scanner = init_object_scanner (obj);
			Slot pp_target_object(NULL);
			while ((pp_target_object.set(p_get_ref(offset_scanner, obj))) != NULL) {
				// Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
				// So, the logic here is independent of whether the object pointed to by this slot has already been reached and 
				// marked by the collector...we still need to collect edge counts since this edge is being visited for the first 
				// and last time...
				// If parent is not a delinquent type we are not interested in this edge at all....
				if (!pp_target_object.is_null()) {
					pp_target_object.unchecked_update(move_immutable_slot (pp_target_object, private_nursery, thread));
				}
				// Move the scanner to the next reference.
				offset_scanner = p_next_ref (offset_scanner);
			}
            // handle weak refs
			offset_scanner = init_object_scanner_weak (obj);
			while ((pp_target_object.set(p_get_ref(offset_scanner, obj))) != NULL) {
				// Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
				// So, the logic here is independent of whether the object pointed to by this slot has already been reached and 
				// marked by the collector...we still need to collect edge counts since this edge is being visited for the first 
				// and last time...
				// If parent is not a delinquent type we are not interested in this edge at all....
				if (!pp_target_object.is_null()) {
					pp_target_object.unchecked_update(move_immutable_slot (pp_target_object, private_nursery, thread));
				}
				// Move the scanner to the next reference.
				offset_scanner = p_next_ref (offset_scanner);
			}
		}

		memcpy((char*)new_loc+4,(char*)(obj)+4,obj_size-4);
		obj = new_loc;
#ifdef RECORD_IMMUTABLE_COPIES
	} else {
		obj = dup_iter->second;
	}
#endif // RECORD_IMMUTABLE_COPIES

	return obj;
}

Partial_Reveal_Object * move_immutable(Partial_Reveal_Object *obj, 
									   GC_Small_Nursery_Info *private_nursery, 
									   Partial_Reveal_VTable *vt,
									   GC_Thread_Info *thread) {
    unsigned obj_size = get_object_size_bytes_with_vt(obj,vt);
	Partial_Reveal_Object *new_loc = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
		obj_size,
		(Allocation_Handle)vt,
		thread,
		separate_immutable ? thread->get_public_immutable_nursery() : thread->get_public_nursery()
#ifdef PUB_PRIV
		, false
#endif // PUB_PRIV
		);

	memcpy((char*)new_loc+4,(char*)(obj)+4,obj_size-4);
	obj = new_loc;

	return obj;
}

extern "C" void record_escaping_vtables(Managed_Object_Handle value, 
										unsigned num_micro_collections,
										Partial_Reveal_Object *base) {
	std::pair<std::map<void*,unsigned>::iterator, bool> res;
	Partial_Reveal_Object *pro = (Partial_Reveal_Object*)value;
    if(!value) return;

	Partial_Reveal_VTable *vt = pro->vt();
	res = g_escaping_vtables.insert(std::pair<void*,unsigned>(vt,1));
	if(res.second == false) {
		(res.first)->second++;
	}

	Partial_Reveal_VTable *base_vt = base->vt();
	res = g_barrier_base.insert(std::pair<void*,unsigned>(base_vt,1));
	if(res.second == false) {
		(res.first)->second++;
	}

	std::pair<std::map<unsigned,unsigned>::iterator, bool> uures;
	
	uures = g_barrier_base_sizes.insert(std::pair<unsigned,unsigned>(get_object_size_bytes_with_vt(base,base_vt),1));
	if(uures.second == false) {
		(uures.first)->second++;
	}

	if(pre_tenure) {
		static float percent_already_pretenured = 1.0;
		float percent_for_type = (res.first)->second / (float)num_micro_collections;
		if(num_micro_collections > 100 && percent_for_type - percent_already_pretenured > 0.25) {
//		if(vt == (Partial_Reveal_VTable*)0x53df70) {
			pgc_pretenure(vt);
#if defined _DEBUG
			printf("Pre-tenuring vtable %p\n",vt);
#endif
			percent_already_pretenured += percent_for_type;
		}
	}
}

bool can_copy(Partial_Reveal_Object *obj, 
			  GC_Small_Nursery_Info *private_nursery, 
			  GC_Thread_Info *thread) {
	Partial_Reveal_VTable *vt = obj->vt();
	if(!pgc_is_vtable_immutable((struct VTable*)vt)) return false;
    if(!vt->get_gcvt()->gc_object_has_slots) return true;

	if (is_vt_array(vt)) {
		Type_Info_Handle tih = class_get_element_type_info(vt->get_gcvt()->gc_clss);
		if(type_info_is_reference(tih) ||
		   type_info_is_vector(tih) ||
		   type_info_is_general_array(tih)) {
			// Initialize the array scanner which will scan the array from the
			// top to the bottom. IE from the last element to the first element.
    
			int32 array_length = vector_get_length_with_vt((Vector_Handle)obj,vt);
			for (int32 i=array_length-1; i>=0; i--) {
				Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)obj, i, vt));
                if(is_object_pointer(p_element.dereference())) {
                    return false;
                }
			}
		} else if(type_info_is_primitive(tih)) { 
			// intentionally do nothing
		} else if(type_info_is_unboxed(tih)) {
			Class_Handle ech = type_info_get_class(tih);
			assert(ech);
			int first_elem_offset = vector_first_element_offset_unboxed(ech);
			int base_offset = (int)class_get_unboxed_data_offset(ech);
			int elem_size = class_element_size(vt->get_gcvt()->gc_clss);
			int array_length = vector_get_length_with_vt((Vector_Handle)obj,vt);
			Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
			unsigned int *offset_scanner = NULL;
			// Offsets assume that the object starts with vtable pointer.  We'll set our
			// fake object to scan 4 bytes before the start of the value type to adjust.
			void *cur_value_type_entry_as_object = (Byte*)obj + first_elem_offset - base_offset;
			for(int i = 0; i < array_length; i++) {
				// taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
				offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
				Slot pp_target_object(NULL);
				while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                    if(is_object_pointer(pp_target_object.dereference())) {
                        return false;
                    }
					// Move the scanner to the next reference.
					offset_scanner = p_next_ref (offset_scanner);
				}
				// taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
				offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
				while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                    if(is_object_pointer(pp_target_object.dereference())) {
                        return false;
                    }
					// Move the scanner to the next reference.
					offset_scanner = p_next_ref (offset_scanner);
				}
				// advance to the next value struct
				cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
			}
		} else assert(!"Tried to scan an array of unknown internal type.");
	} 

	unsigned int *offset_scanner = init_object_scanner (obj);
	Slot pp_target_object(NULL);
	while ((pp_target_object.set(p_get_ref(offset_scanner, obj))) != NULL) {
        if(is_object_pointer(pp_target_object.dereference())) {
            return false;
        }
		// Move the scanner to the next reference.
		offset_scanner = p_next_ref (offset_scanner);
	}
	return true;
}

#if 0
bool check_immutable_recursive(Partial_Reveal_Object *obj, 
							   GC_Small_Nursery_Info *private_nursery, 
							   GC_Thread_Info *thread);

bool check_immutable_slot (Slot p_slot, 
	                       GC_Small_Nursery_Info *private_nursery, 
	                       GC_Thread_Info *thread)
{
	assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return true;
    }
	Partial_Reveal_Object *p_obj = p_slot.dereference();

	if(p_obj >= private_nursery->local_gc_info->local_nursery_start && p_obj <= private_nursery->local_gc_info->local_nursery_end) {
		return check_immutable_recursive(p_obj,private_nursery,thread);
	} else {
		return true;
	}
}

bool check_immutable_recursive(Partial_Reveal_Object *obj, 
							   GC_Small_Nursery_Info *private_nursery, 
							   GC_Thread_Info *thread) {
	Partial_Reveal_VTable *vt = obj->vt();
	if(!pgc_is_vtable_immutable((struct VTable*)vt)) return false;
    if(!vt->get_gcvt()->gc_object_has_slots) return true;

	if (is_vt_array(vt)) {
		Type_Info_Handle tih = class_get_element_type_info(vt->get_gcvt()->gc_clss);
		if(type_info_is_reference(tih) ||
		   type_info_is_vector(tih) ||
		   type_info_is_general_array(tih)) {
			// Initialize the array scanner which will scan the array from the
			// top to the bottom. IE from the last element to the first element.
    
			int32 array_length = vector_get_length_with_vt((Vector_Handle)obj,vt);
			for (int32 i=array_length-1; i>=0; i--) {
				Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)obj, i, vt));
				if(check_immutable_slot (p_element, private_nursery, thread) == false) {
					return false;
				}
			}
		} else if(type_info_is_primitive(tih)) { 
			// intentionally do nothing
		} else if(type_info_is_unboxed(tih)) {
			Loader_Exception exc;
			Class_Handle ech = type_info_get_class(tih, &exc);
			assert(ech);
			int first_elem_offset = vector_first_element_offset_unboxed(ech);
			int base_offset = (int)class_get_unboxed_data_offset(ech);
			int elem_size = class_element_size(vt->get_gcvt()->gc_clss);
			int array_length = vector_get_length_with_vt((Vector_Handle)obj,vt);
			Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
			unsigned int *offset_scanner = NULL;
			// Offsets assume that the object starts with vtable pointer.  We'll set our
			// fake object to scan 4 bytes before the start of the value type to adjust.
			void *cur_value_type_entry_as_object = (Byte*)obj + first_elem_offset - base_offset;
			for(int i = 0; i < array_length; i++) {
				// taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
				offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
				Slot pp_target_object(NULL);
				while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
					if(check_immutable_slot (pp_target_object, private_nursery, thread) == false) {
						return false;
					}
					// Move the scanner to the next reference.
					offset_scanner = p_next_ref (offset_scanner);
				}
				// taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
				offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
				while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
					if(check_immutable_slot (pp_target_object, private_nursery, thread) == false) {
						return false;
					}
					// Move the scanner to the next reference.
					offset_scanner = p_next_ref (offset_scanner);
				}
				// advance to the next value struct
				cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
			}
		} else assert(!"Tried to scan an array of unknown internal type.");
	} 

	unsigned int *offset_scanner = init_object_scanner (obj);
	Slot pp_target_object(NULL);
	while ((pp_target_object.set(p_get_ref(offset_scanner, obj))) != NULL) {
		if(check_immutable_slot (pp_target_object, private_nursery, thread) == false) {
			return false;
		}
		// Move the scanner to the next reference.
		offset_scanner = p_next_ref (offset_scanner);
	}
	return true;
}
#endif // 0

#if 0
bool is_object_pointer(Partial_Reveal_Object *p_obj) {
    if(!p_obj) return false;
    if((POINTER_SIZE_INT)p_obj & 0x3) return false;
    return true;
}
#endif

#ifdef PUB_PRIV
void collect_private_heap(GC_Small_Nursery_Info *private_nursery,
						  GC_Thread_Info *thread,
						  Partial_Reveal_Object *escaping_object);
#endif // PUB_PRIV

extern "C" void add_gen_rs(GC_Thread_Info *thread,
                           Partial_Reveal_Object **p_pslot) {
    if(!thread->elder_roots_to_younger) {
        thread->elder_roots_to_younger = new ExpandInPlaceArray<Managed_Object_Handle *>;
    }
    thread->elder_roots_to_younger->add_entry((Managed_Object_Handle*)p_pslot);
}

extern "C" void unmanaged_add_entry(GC_Thread_Info *thread, 
									Partial_Reveal_Object **p_value, 
									Partial_Reveal_Object *base,
                                    Partial_Reveal_Object **p_slot) {
	if(!is_object_pointer(*p_value)) return;

	GC_Small_Nursery_Info *private_nursery = thread->get_private_nursery();

#if 0
#ifdef _DEBUG
    if(base) {
	    assert(!pgc_is_vtable_immutable((struct VTable*)base->vt()));
    }
#endif // _DEBUG
#endif

#ifdef PUB_PRIV
	if(g_use_pub_priv) {
		block_info *slot_block = NULL;
        if(p_global_gc->is_in_heap(p_slot)) {
    		slot_block = GC_BLOCK_INFO(p_slot);
        }
		// We get here via a write barrier where the slot is in the heap regardless of where
		// the value is, either local nursery or the heap.  We need to detect 3 cases:
		// 1) local nursery writes into the public heap in which case we fall through to the regular code below,
		// 2) local nursery writes into that thread's private heap in which case we record the pointer in the thread's data structure.
		// 3) private heap writes into the public heap in which case a collection of the private heap must be arranged.
		OBJECT_LOCATION value_location = get_object_location(*p_value,private_nursery->local_gc_info);
		// Due to our invariant, if we have a pointer to a block whose thread_owner is non-NULL then it must be our own.
		if(slot_block && slot_block->thread_owner) {
			if(value_location == PRIVATE_NURSERY) {
                Partial_Reveal_VTable *vt = (*p_value)->vt();
                if(pgc_is_vtable_immutable((struct VTable*)vt) && !vt->get_gcvt()->gc_object_has_slots) {
//                if(can_copy(*p_value,private_nursery,thread)) {
            	    *p_value = move_immutable(*p_value, private_nursery, vt, thread);
#ifdef CONCURRENT
            		printf("SHOULD NOT GET HERE MONITOR_SWITCH\n");
            		exit(-1);
#endif // CONCURRENT
                    return;
                }

				// case #2
                if(!private_nursery->is_duplicate_last_external_pointer(p_slot)) {
    		        external_pointer *new_ep = (external_pointer*)private_nursery->allocate_external_pointer();
			        if(new_ep) {
                        new_ep->base = base;
				        new_ep->slot.set(p_slot,false /* don't check in the heap */);
                        return;
                    } else {
            		    local_nursery_collection(thread,NULL,*p_value,false);
                    }
                } else {
                    return;
                }
			} else {
				// Base is in the private heap and the value is either in the public or private heap.
				// In either of those cases, nothing needs to be done so just return.
				return;
			}
		} else {
			// The base must be in the public heap.
            // case #1 or #3
			if(value_location == PRIVATE_NURSERY || value_location == PRIVATE_HEAP) {
                Partial_Reveal_VTable *vt = (*p_value)->vt();
                if(pgc_is_vtable_immutable((struct VTable*)vt) && !vt->get_gcvt()->gc_object_has_slots) {
//                if(can_copy(*p_value,private_nursery,thread)) {
            	    *p_value = move_immutable(*p_value, private_nursery, vt, thread);
#ifdef CONCURRENT
            		printf("SHOULD NOT GET HERE MONITOR_SWITCH\n");
            		exit(-1);
#endif // CONCURRENT
                    return;
                }
                collect_private_heap(private_nursery, thread, *p_value);
                return;
			} else {
				// Base is in the public heap and the value is either a global or another value from the public heap.
				// In either of those cases, nothing needs to be done so just return.
				return;
			}
		}
	} else {
        Partial_Reveal_VTable *vt = (*p_value)->vt();
        if(pgc_is_vtable_immutable((struct VTable*)vt) && !vt->get_gcvt()->gc_object_has_slots) {
//        if(can_copy(*p_value,private_nursery,thread)) {
	        *p_value = move_immutable(*p_value, private_nursery, vt, thread);
#ifdef CONCURRENT
		    printf("SHOULD NOT GET HERE MONITOR_SWITCH\n");
		    exit(-1);
#endif // CONCURRENT
            return;
        }

#ifdef TRACK_ESCAPING_VTABLES
		record_escaping_vtables(*p_value,private_nursery->local_gc_info->num_micro_collections,base);
#endif // TRACK_ESCAPING_VTABLES
   		local_nursery_collection(thread,NULL,*p_value,false);
#if defined _DEBUG
   		private_nursery->num_promote_on_escape++;
#endif // defined _DEBUG
    }

#else // PUB_PRIV =====================================================================================

    Partial_Reveal_VTable *vt = (*p_value)->vt();
    if(pgc_is_vtable_immutable((struct VTable*)vt) && !vt->get_gcvt()->gc_object_has_slots) {
        *p_value = move_immutable_recursive(*p_value, private_nursery, vt, thread);
#ifdef CONCURRENT
	    printf("SHOULD NOT GET HERE MONITOR_SWITCH\n");
        exit(-1);
#endif // CONCURRENT
        return;
    }

#ifdef TRACK_ESCAPING_VTABLES
	record_escaping_vtables(*p_value,private_nursery->local_gc_info->num_micro_collections,base);
#endif // TRACK_ESCAPING_VTABLES
	local_nursery_collection(thread,NULL,*p_value,false);

#ifdef CONCURRENT
    assert(0);
#if 0
    if(GetLocalConcurrentGcState(private_nursery) == CONCURRENT_MARKING) {
		printf("Concurrent state changed during unmanaged_add_entry.\n");
	}
#endif
#endif // CONCURRENT

#if defined _DEBUG
		private_nursery->num_promote_on_escape++;
#endif // defined _DEBUG
#endif // PUB_PRIV =====================================================================================
} // unmanaged_add_entry

#ifdef CONCURRENT
extern "C" void unmanaged_mark_phase(GC_Thread_Info *thread, 
                                     Partial_Reveal_Object **p_value,
									 unsigned offset,
                                     Partial_Reveal_Object **p_slot) {
	GC_Small_Nursery_Info *private_nursery = thread->get_private_nursery();

    Partial_Reveal_Object *orig_obj;
	switch(GetLocalConcurrentGcState(private_nursery)) {
	case CONCURRENT_SWEEPING:
	case CONCURRENT_IDLE:
		// intentionally do nothing
		break;
	case CONCURRENT_MARKING:
		orig_obj = *p_slot;
		if(is_object_pointer(orig_obj)) {
			// Make the original object gray in case a thread has acquired a pointer to it.
			make_object_gray_local(private_nursery->local_gc_info,(Partial_Reveal_Object *)((char*)*p_slot - offset));
		}
		break;
	default:
		assert(0);
	}

	if(is_object_pointer(*p_value)) {
		// Pointer to the new object.
		Partial_Reveal_Object *obj = (Partial_Reveal_Object *)((char*)*p_value - offset);
		bool value_in_private_nursery  = (obj >= private_nursery->start && obj <= private_nursery->tls_current_ceiling);
		// If the new pointer is for an object in the private nursery.
		if(value_in_private_nursery) {
			Partial_Reveal_VTable *vt = obj->vt();             // Get the vtable of the object.
			// If the object is immutable and has no refs then create a duplicate object in the public heap and
			// forgoe a collection.
			if(pgc_is_vtable_immutable((struct VTable*)vt) && !vt->get_gcvt()->gc_object_has_slots) {
#ifdef RECORD_IMMUTABLE_COPIES
				std::map<Partial_Reveal_Object*,Partial_Reveal_Object*>::iterator dup_iter;
				dup_iter = private_nursery->local_gc_info->promoted_immutables.find(obj);
				// If this is the first time we've copied this object.
				if(dup_iter == private_nursery->local_gc_info->promoted_immutables.end()) {
#endif // RECORD_IMMUTABLE_COPIES
					unsigned obj_size = get_object_size_bytes_with_vt(obj,vt);
					Partial_Reveal_Object *new_loc = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
						obj_size,
						(Allocation_Handle)vt,
						thread,
						separate_immutable ? thread->get_immutable_nursery() : thread->get_nursery()
#ifdef PUB_PRIV
    					, g_use_pub_priv
#endif // PUB_PRIV
						);
					memcpy((char*)new_loc+4,(char*)(obj)+4,obj_size-4);
#ifdef RECORD_IMMUTABLE_COPIES
					private_nursery->local_gc_info->promoted_immutables.insert(std::pair<Partial_Reveal_Object*,Partial_Reveal_Object*>(obj,new_loc));
#endif // RECORD_IMMUTABLE_COPIES
					*p_value = (Partial_Reveal_Object*)((char*)new_loc + offset);
#ifdef RECORD_IMMUTABLE_COPIES
				} else {
					// This is not the first time we've created a copy of this object.
					*p_value = (Partial_Reveal_Object*)((char*)dup_iter->second + offset);
				}
#endif // RECORD_IMMUTABLE_COPIES
				obj = (Partial_Reveal_Object *)((char*)*p_value - offset);

				switch(GetLocalConcurrentGcState(private_nursery)) {
				case CONCURRENT_SWEEPING:
					// We are now in the sweep phase so mark this object if greater than the sweep pointer.
					if(obj >= g_sweep_ptr) {
						bool has_slots = vt->get_gcvt()->gc_object_has_slots;
						if(has_slots) {
							printf("THIS SHOULD NOT HAPPEN RIGHT NOW.\n");
						}
#ifndef NO_GRAY_SWEEPING
                        bool mark_res = mark_header_and_block_atomic(obj);
                        if(mark_res) {
	    					add_to_grays_local(private_nursery->local_gc_info,obj);
                        }
#endif // NO_GRAY_SWEEPING
  					}
					break;
				case CONCURRENT_MARKING:
                    mark_header_and_block_atomic(obj);
					break;
				case CONCURRENT_IDLE:
					// intentionally do nothing
					break;
				default:
					assert(0);
				}
			} else {
#ifdef TRACK_ESCAPING_VTABLES
				record_escaping_vtables(*p_value,private_nursery->local_gc_info->num_micro_collections);
#endif // TRACK_ESCAPING_VTABLES
				local_nursery_collection(thread,NULL,(Partial_Reveal_Object *)((char*)*p_value - offset),offset>0);
#if defined _DEBUG
				private_nursery->num_promote_on_escape++;
#endif
			}
		}

		switch(GetLocalConcurrentGcState(private_nursery)) {
		case CONCURRENT_SWEEPING:
		case CONCURRENT_IDLE:
			// intentionally do nothing
			break;
		case CONCURRENT_MARKING:
			if(is_object_pointer(*p_value)) {
				if(separate_immutable && incremental_compaction) {
#ifndef NO_IMMUTABLE_UPDATES
					MovedObjectIterator moved_iter;
					moved_iter = g_moved_objects.find((Partial_Reveal_Object *)((char*)*p_value - offset));
					if(moved_iter != g_moved_objects.end()) {
#ifdef _DEBUG
						char buf[100];
						sprintf(buf,"Updating relocated pointer unmanaged_mark_phase %p to %p.",(Partial_Reveal_Object *)((char*)*p_value - offset),moved_iter->second);
						gc_trace ((Partial_Reveal_Object *)((char*)*p_value - offset), buf);
						gc_trace (moved_iter->second.m_new_location, buf);
#endif // _DEBUG
#if 1
						make_object_gray_local(private_nursery->local_gc_info,(Partial_Reveal_Object *)((char*)*p_value - offset));
#endif // 0/1
						*p_value = (Partial_Reveal_Object *)((char*)moved_iter->second.m_new_location + offset);
					}
#endif // NO_IMMUTABLE_UPDATES
				}

				make_object_gray_local(private_nursery->local_gc_info,(Partial_Reveal_Object *)((char*)*p_value - offset));
			}
			break;
		default:
			assert(0);
		}
	}

	*p_slot = *p_value;
} // unmanaged_mark_phase
#endif // CONCURRENT

extern "C" void unmanaged_add_entry_interior(GC_Thread_Info *thread, 
											 Partial_Reveal_Object **p_value,
											 unsigned offset) {
#ifdef PUB_PRIV
	if(g_use_pub_priv) {
        collect_private_heap(thread->get_private_nursery(), thread, (Partial_Reveal_Object *)((char*)*p_value - offset));
        return;
    }
#endif // PUB_PRIV

	local_nursery_collection(thread,NULL,(Partial_Reveal_Object *)((char*)*p_value - offset),true);
#if defined _DEBUG
	thread->get_private_nursery()->num_promote_on_escape_interior++;
#endif // defined _DEBUG
} // unmanaged_add_entry_interior

extern "C" void * PRT_STDCALL gc_heap_object_read_stats_null(
						 Managed_Object_Handle p_object) {
	return p_object;
}

extern "C" void * PRT_STDCALL gc_heap_object_read_stats(
						 Managed_Object_Handle p_object) {
    if(p_object >= gc_heap_base_address() && p_object <= gc_heap_ceiling_address()) {
        ++g_public_read;
    } else {
        ++g_private_read;
    }
	return p_object;
}

extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value) {
	*p_slot = value;
}

extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset) {
	*p_slot = value;
}

extern "C" void PRT_STDCALL gc_heap_slot_write_ref_p_prt_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prevFrame) {
	*p_slot = value;
}

extern "C" void PRT_STDCALL gc_heap_slot_write_interior_ref_p_prt_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prevFrame) {
	*p_slot = value;
}




extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp) {
    return (Managed_Object_Handle)LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT*)p_slot,(POINTER_SIZE_INT)value,(POINTER_SIZE_INT)cmp);
}

extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp) {
    return (Managed_Object_Handle)LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT*)p_slot,(POINTER_SIZE_INT)value,(POINTER_SIZE_INT)cmp);
}

extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_ref_p_prt_null(
						 Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prevFrame) {
    return (Managed_Object_Handle)LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT*)p_slot,(POINTER_SIZE_INT)value,(POINTER_SIZE_INT)cmp);
}

extern "C" Managed_Object_Handle PRT_STDCALL gc_cas_write_interior_ref_p_prt_null(
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value,
						 unsigned offset,
                         Managed_Object_Handle cmp,
#ifndef NO_P2C_TH
                         PrtTaskHandle taskHandle, 
#endif  // NO_P2C_TH
                         void *prevFrame) {
    return (Managed_Object_Handle)LockedCompareExchangePOINTER_SIZE_INT((POINTER_SIZE_INT*)p_slot,(POINTER_SIZE_INT)value,(POINTER_SIZE_INT)cmp);
}







GCEXPORT(void, gc_heap_slot_write_ref) (Managed_Object_Handle p_base_of_object_with_slot,
                         Managed_Object_Handle *p_slot,
                         Managed_Object_Handle value) {
    *p_slot = value;
}

#ifdef ALLOW_COMPRESSED_REFS
GCEXPORT(void, gc_heap_slot_write_ref_compressed)(Managed_Object_Handle p_base_of_object_with_slot,
                                                  uint32 *p_slot,
                                                  Managed_Object_Handle value) {
    assert(gc_references_are_compressed);
    assert(p_slot != NULL);
    if (value == NULL) {
        *p_slot = 0;
    } else {
        *p_slot = (uint32) ((uint64)value - (uint64)INTERNAL(gc_heap_base_address)());
    }
    INTERNAL(gc_write_barrier) (p_base_of_object_with_slot);
}
#endif



//
// This API is used by the ORP (JIT) to find out if
// the GC requires read or write barriers. If the GC requests
// write barriers and the JIT does not support write barriers the results
// are undefined.
//
// Output: 
//         1 if the garbage collector expects to be notified whenever
//              a slot holding a pointer to an object is modified in the heap.
//         0 otherwise.
//
// Comments: Future version might extend possible return values 
//           so that the system can support other barriers such as a 
//           read barrier or a write barrier on all heap writes, not 
//           just pointer writes.
//
//


GCEXPORT(Boolean, gc_requires_barriers)() {
    return TRUE;
}

//
// If gc_requires_write_barriers() returns 1, the Jit-ted code 
// should call this after doing the putfield or an array store or
// a reference.
//
// Sapphire -
// If gc_requires_write_barrier() returns 2 (Sapphire) then this
// is called whenever anything is stored into the heap, not only 
// references.
// 
//
// Likewise the ORP needs to call it whenever it stores a pointer into the
// heap.
//
// Input: p_base_of_obj_with_ref - the base of the object that
//                                 had a pointer stored in it.
//
// For Sapphire we need to find out if processing the entire object is faster
// than just processing the parts that have been changed. For the initial implementation
// we will process the entire object, mostly because it is easier and keeps the normal
// case write barrier simpler.
//
#ifndef LINUX
void __fastcall gc_write_barrier_fastcall(Partial_Reveal_Object *p_base_of_object_holding_ref) {
    INTERNAL(gc_write_barrier)(p_base_of_object_holding_ref);
} //gc_write_barrier_fastcall
#endif



GCEXPORT(void, gc_write_barrier)(Managed_Object_Handle p_base_of_object_holding_ref) {}

#ifdef ALLOW_COMPRESSED_REFS
// p_slot is the address of a 32 bit global slot holding the offset of a referenced object in the heap.
// That slot is being updated, so store the heap offset of "value"'s object. If value is NULL, store a 0 offset.
GCEXPORT(void, gc_heap_write_global_slot_compressed)(uint32 *p_slot,
                               Managed_Object_Handle value) {
    assert(gc_references_are_compressed);
    assert(p_slot != NULL);
    if (value == NULL)
    {
        *p_slot = 0;
    }
    else
    {
        *p_slot = (uint32) ((uint64)value - (uint64)INTERNAL(gc_heap_base_address)());
    }
    // BTL 20030501 No write barrier is needed because this function is only called when updating static fields,
    // and these are explicitly enumerated as GC roots.
    //
    // RLH 20030502
    // While this is true for STW collectors, concurrent collectors need to be aware of 
    // all pointers that can be passed from one thread to another. For example 
    // this is needed to ensure the no black to white invariant or whatever tri color 
    // invariant you are using.
};
#endif

// The following should generate a st.rel followed by a mf to get sequential consistency
// for volatiles. As of June 12, 2000  this is my (RLH) assumption of what the ORP spec 
// will/should be.

void gc_volatile_heap_write_ref (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         Partial_Reveal_Object *value) { 
    assert (p_base_of_object_with_slot != NULL);

    Partial_Reveal_Object **p_slot = 
        (Partial_Reveal_Object **)(((char *)p_base_of_object_with_slot) + offset);
    
#if (defined(GC_COPY_V2) || defined(DISABLE_GC_WRITE_BARRIERS)) 
    *p_slot = value;
#else
    *p_slot = value;
    gc_write_barrier (p_base_of_object_with_slot);
#endif
}

void gc_volatile_heap_slot_write_ref (Partial_Reveal_Object *p_base_of_object_with_slot,
                         Partial_Reveal_Object **p_slot,
                         Partial_Reveal_Object *value) {
    gc_volatile_heap_write_ref (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

// Non reference writes caller does conversion.

void gc_volatile_heap_write_int8 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         int8 value) { 
    assert (p_base_of_object_with_slot != NULL);
    int8 *p_slot = 
        (int8 *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_int8 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         int8 *p_slot,
                         int8 value) {
    gc_volatile_heap_write_int8 (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_int16 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         int16 value) { 
    assert (p_base_of_object_with_slot != NULL);
    int16 *p_slot = 
        (int16 *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_int16 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         int16 *p_slot,
                         int16 value) {
    gc_volatile_heap_write_int16 (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_uint16 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         uint16 value) { 
    assert (p_base_of_object_with_slot != NULL);
    uint16 *p_slot = 
        (uint16 *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_uint16 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         uint16 *p_slot,
                         uint16 value) {
    gc_volatile_heap_write_uint16 (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_int32 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         int32 value) { 
    assert (p_base_of_object_with_slot != NULL);
    int32 *p_slot = 
        (int32 *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_int32 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         int32 *p_slot,
                         int32 value) {
    gc_volatile_heap_write_int32 (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_int64 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         int64 value) { 
    assert (p_base_of_object_with_slot != NULL);
    int64 *p_slot = 
        (int64 *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_write_float (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         float value) { 
    assert (p_base_of_object_with_slot != NULL);
    float *p_slot = 
        (float *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_float (Partial_Reveal_Object *p_base_of_object_with_slot,
                         float *p_slot,
                         float value) {
    gc_volatile_heap_write_float (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_double (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         double value) { 
    assert (p_base_of_object_with_slot != NULL);
    double *p_slot = 
        (double *)(((char *)p_base_of_object_with_slot) + offset);
    *p_slot = value;
}

void gc_volatile_heap_slot_write_double (Partial_Reveal_Object *p_base_of_object_with_slot,
                         double *p_slot,
                         double value) {
    gc_volatile_heap_write_double (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_write_pointer_size_int (Partial_Reveal_Object *p_base_of_object_with_slot,
                         unsigned offset,
                         POINTER_SIZE_INT value) { 
    assert (p_base_of_object_with_slot != NULL);
#ifdef _IA64_
    gc_volatile_heap_write_int64 (p_base_of_object_with_slot,
                                  offset,
                                  (int64)value);
#else
    gc_volatile_heap_write_int32 (p_base_of_object_with_slot,
                                  offset,
                                  (int32)value);
#endif
}

void gc_volatile_heap_slot_write_pointer_size_int (Partial_Reveal_Object *p_base_of_object_with_slot,
                         POINTER_SIZE_INT *p_slot,
                         POINTER_SIZE_INT value) {
    gc_volatile_heap_write_pointer_size_int (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}

void gc_volatile_heap_slot_write_int64 (Partial_Reveal_Object *p_base_of_object_with_slot,
                         int64 *p_slot,
                         int64 value) {
    gc_volatile_heap_write_int64 (p_base_of_object_with_slot,
        (unsigned)((char *)p_slot - (char *)p_base_of_object_with_slot),
        value);
}


/****
*
*  Routines to support building the root set during a collection.
* 
*****/

// *slot is guaranteed not to the beginning of an object unless it has fallen off the
// end of an array.  We'll subtract 1 in places to make sure the pointer is in the
// interior of the object to which it points.
// 
// Managed pointers are like interior pointers except we don't know what the offset is.
GCEXPORT(void, gc_add_root_set_entry_managed_pointer)(void **slot, Boolean is_pinned) {
    // By subtracting one byte we guarantee the pointer is in the memory for the object.
    void *true_slot = ((Byte*)*slot) - 1;

    // Check if the interior pointer points to a heap address managed by this GC
    if ( p_global_gc->is_in_heap((Partial_Reveal_Object*)true_slot) ) {
        // Find the starting block whether it is a single_object_block or not
        // We subtract 1 to guarantee that the pointer is into the object and not
        // one past the end of an array.
        block_info *pBlockInfo = p_global_gc->find_start_of_multiblock(true_slot);
        
        Partial_Reveal_Object *p_obj = NULL;

        // check for single object block
        if ( pBlockInfo->is_single_object_block ) {
            // In a single object block, the object will start at the beginning of the block's allocation area.
            p_obj = (Partial_Reveal_Object*)GC_BLOCK_ALLOC_START(pBlockInfo);
        } else {
            typedef free_area tightly_filled_area;
            tightly_filled_area current_area_being_scanned;
            // current_area_being_scanned.area_base will keep track of the base of the current used area
            current_area_being_scanned.area_base    = GC_BLOCK_ALLOC_START(pBlockInfo);
            current_area_being_scanned.area_ceiling = 0;
            unsigned int i=0;
            
            // Handle the case where there are no free areas in the block.
            if (pBlockInfo->block_free_areas[0].area_base == 0) {
                // Will bypass the following for loop.
                i = pBlockInfo->size_block_free_areas;
                // Compute the ending address of this block's allocation area.
                current_area_being_scanned.area_ceiling = GC_BLOCK_CEILING(current_area_being_scanned.area_base);
            }
            
            // loop through the free areas...free areas are the inverse of used areas and demarcate them.
            for (; i < pBlockInfo->size_block_free_areas; ++i) {
                // Make sure that the array of free areas is ordered.
                // This invariant is supposed to be maintained by Garbage_Collector::sweep_one_block(...) 
                // in GCv4_sweep.cpp.
                // This check will also make sure we haven't failed to find the used block before
                // going into the unused portion of the free areas array.
                assert(current_area_being_scanned.area_base <= pBlockInfo->block_free_areas[i].area_base);
                // Algorithm
                // ---------
                // 1. We already know that *slot must be >= current_area_being_scanned.area_base because we initialized 
                // current_area_being_scanned.area_base to be the start of the allocation block and the list of free areas
                // is monotonically increasing.
                // 2. Any of the "free" areas could be the current allocation area.  The most up-to-date
                // base for the current allocation free area is in TLS so the interior pointer could
                // point into what looks like a free area.
                // 3. Therefore, consider adjacent used and free areas as one big area and see if the
                // interior pointer is in that area.  See diagram of a block below:
                // [block info][used area 0][free area 0][used1][free1][used2][free2]...
                // We will group [used N] with [free N].  Note, this approach handles the case of
                // a block just starting to be allocated where the free area starts at the beginning
                // of the block allocation area.  So, [used area 0] is really optional.
                // 4. This approach seemed simpler than having a loop that first determined whether
                // the interior pointer was in (the optional) used area 0, then checked [free0] then
                // checked [used1], [free1], [used2], etc.  Combining them together makes the loop
                // simpler and reduces the amount of corner case code.
                // 5. We can still extract better bounds later by seeing if the interior pointer is
                // greater or less than pBlockInfo->block_free_areas[i].area_base.  If it is greater then
                // the base is the free area base (this free area must be the current allocation area)
                // and the ceiling is that free area's ceiling.  Otherwise, the base is [used0].base
                // and the ceiling is [free0].base-1.
                //
                // The object can reside in a used area that falls between the last free area and the end 
                // of the block. 
                // Here, if true we've found the combined area where the interior pointer falls.
                if (true_slot <= pBlockInfo->block_free_areas[i].area_ceiling) {
                    // Check if the interior pointer appears to be in a used or free area.
                    if (true_slot >= pBlockInfo->block_free_areas[i].area_base) {
                        // The interior pointer is in a free area, which must be the current allocation area.
                        current_area_being_scanned.area_base    = pBlockInfo->block_free_areas[i].area_base;
                        current_area_being_scanned.area_ceiling = pBlockInfo->block_free_areas[i].area_ceiling;
                    } else {
                        // The interior pointer is in a used area.
                        current_area_being_scanned.area_ceiling = (Byte*)pBlockInfo->block_free_areas[i].area_base - 1;
                    }
                    break;
                } else {
                    // We haven't found the area yet so save the start of the next used area
                    current_area_being_scanned.area_base = (Byte*)pBlockInfo->block_free_areas[i].area_ceiling + 1;
                    if ((pBlockInfo->num_free_areas_in_block - 1) == i) {
                        current_area_being_scanned.area_ceiling = GC_BLOCK_CEILING(current_area_being_scanned.area_base);
                        // The object is in the area between the last free area and the end of the block.
                        assert (pBlockInfo->block_free_areas[i+1].area_base == 0x0);
                        break;   
                    }
                }
            }
            assert(current_area_being_scanned.area_ceiling);
            
            // Get the first live object in this used area
            Partial_Reveal_Object *p_next_obj = (Partial_Reveal_Object*)current_area_being_scanned.area_base;
            
            do {
                p_obj = p_next_obj;
                // Keep getting the next live object until the interior pointer is less than the next object we just found
                p_next_obj = (Partial_Reveal_Object*)((Byte*)p_obj + get_object_size_bytes(p_obj));
                assert(p_next_obj <= current_area_being_scanned.area_ceiling);
            } while (true_slot > p_next_obj);
        }

        // We can reuse the normal code below by now computing the correct offset
        int offset = ((POINTER_SIZE_INT)*slot) - ((POINTER_SIZE_INT)p_obj);

        assert (p_obj->vt());
        interior_pointer_table->add_entry (slot_offset_entry(slot, p_obj, offset));
        // pass the slot from the interior pointer table to the gc.
		slot_offset_entry *addr = interior_pointer_table->get_last_addr();
        p_global_gc->gc_internal_add_root_set_entry(&(addr->base));

        if (is_pinned) {
            // Add the current block as one that can't be compacted since it has a pinned root
            // pointing into it.
            p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(p_obj));
        }
    }
}

#ifdef IGNORE_SOME_ROOTS
unsigned num_roots_ignored = 0;
unsigned roots_to_ignore   = 20000;
#endif // IGNORE_SOME_ROOTS

// 
// Call from the ORP to the gc to enumerate an interior pointer. **ref is a slot holding a pointer
// into the interior of an object. The base of the object is located at *ref - offset. The strategy
// employed is to place the slot, the object base and the offset into a slot_base_offset table. We then
// call gc_add_root_set_entry with the slot in the table holding the base of the object. Upon completion
// of the garbage collection the routine fixup_interior_pointers is called and the slot_base_offset table
// is traversed and the new interior pointer is calculated by adding the base of the object and the offset.
// This new interior pointer value is then placed into the slot.
//
// This routine can be called multiple times with the same interior pointer without any problems.
// The offset is checked to make sure it is positive but the logic is not dependent on this fact.

GCEXPORT(void, gc_add_root_set_entry_interior_pointer) (void **slot, int offset, Boolean is_pinned) {
    assert(offset > 0);

    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)((Byte*)*slot - offset);
#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(p_obj) ) {
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP
    assert (p_obj->vt());
#ifdef CONCURRENT
	if(prtGetTls()) {
		printf("Got here for non-gc thread.\n");
	}

    if(separate_immutable && incremental_compaction) {
#ifndef NO_IMMUTABLE_UPDATES
		MovedObjectIterator moved_iter;
		moved_iter = g_moved_objects.find(p_obj);
		if(moved_iter != g_moved_objects.end()) {
#ifdef _DEBUG
			char buf[100];
			sprintf(buf,"Updating relocated pointer interior %p to %p.",*slot,moved_iter->second);
			gc_trace (*slot, buf);
			gc_trace (moved_iter->second.m_new_location, buf);
#endif // _DEBUG

		    make_object_gray_in_concurrent_thread(p_obj,g_concurrent_gray_list);
			p_obj = moved_iter->second.m_new_location;
			*slot = ((Byte*)p_obj) + offset;
		}
#endif // NO_IMMUTABLE_UPDATES
	}
    make_object_gray_in_concurrent_thread(p_obj,g_concurrent_gray_list);
#else // CONCURRENT
    interior_pointer_table->add_entry (slot_offset_entry(slot, p_obj, offset));
    // pass the slot from the interior pointer table to the gc.
	slot_offset_entry *addr = interior_pointer_table->get_last_addr();
    p_global_gc->gc_internal_add_root_set_entry(&(addr->base));

    if (is_pinned) {
        // Add the current block as one that can't be compacted since it has a pinned root
        // pointing into it.
        p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(p_obj));
    }
#endif // CONCURRENT
} // gc_add_root_set_entry_interior_pointer



//
// Call from the ORP to the GC to enumerate another
// live reference.
//
// Input: ref - the location of a slot holding a pointer that
//              is NULL or points to a valid object in the heap.
//



GCEXPORT(void, gc_add_root_set_entry)(Managed_Object_Handle *ref1, Boolean is_pinned) {
    Partial_Reveal_Object **ref = (Partial_Reveal_Object **) ref1;
#ifndef DISALLOW_TAGGED_POINTERS
	// If either of the lower two bits are set then this is not a valid pointer but
	// must be being used by the language for some alternative representation.
	// Used for P tagged integer rational representation.
	if(!is_object_pointer(*ref)) {
		// Returning NULL will treat this slot as if it were a NULL pointer which
		// is accurate since it doesn't pointer to anything.
		return;
	}
#endif // DISALLOW_TAGGED_POINTERS
#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(*ref) ) {
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP
    p_global_gc->gc_internal_add_root_set_entry(ref);

    if (is_pinned) {
        // Add the current block as one that can't be compacted since it has a pinned root
        // pointing into it.
        p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(*ref));
    }
}


GCEXPORT(void, gc_add_root_set_entry_mid_pn_collect)(Managed_Object_Handle *ref1) {
    Partial_Reveal_Object **ref    = (Partial_Reveal_Object **)ref1;
    Partial_Reveal_Object *old_obj = (Partial_Reveal_Object*)ref;
    Partial_Reveal_Object *new_obj = (Partial_Reveal_Object*)old_obj->get_raw_forwarding_pointer();

    if(!old_obj->isLowFlagSet()) {
        printf("Forwarding bit is not set for mid pn collect object.\n");
        assert(0);
        exit(-1);
    }
    assert(p_global_gc->is_in_heap(new_obj));
    p_global_gc->gc_internal_add_root_set_entry(ref);
}


GCEXPORT(void, gc_add_weak_root_set_entry)(Managed_Object_Handle *ref1, Boolean is_pinned,Boolean is_short_weak) {
    Partial_Reveal_Object **ref = (Partial_Reveal_Object **) ref1;
	if(!is_object_pointer(*ref)) return;

#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(*ref) ) {
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP
    p_global_gc->gc_internal_add_weak_root_set_entry(ref,0,is_short_weak);

    if (is_pinned) {
        // Add the current block as one that can't be compacted since it has a pinned root
        // pointing into it.
        p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(*ref));
    }
}


GCEXPORT(void, gc_add_weak_root_set_entry_interior_pointer)(void **rootAddr, int offset) {
    Boolean is_short_weak = TRUE;
    Boolean is_pinned = FALSE;
    Partial_Reveal_Object *ref = (Partial_Reveal_Object *) ((char *)*rootAddr - offset);
#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(ref) ) {
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP
    p_global_gc->gc_internal_add_weak_root_set_entry((Partial_Reveal_Object **)rootAddr,offset,is_short_weak);

    if (is_pinned) {
        // Add the current block as one that can't be compacted since it has a pinned root
        // pointing into it.
        p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(ref));
    }
}


// Resembles gc_add_root_set_entry() but is passed the address of a slot containing a compressed reference.
GCEXPORT(void, gc_add_compressed_root_set_entry)(uint32 *ref, Boolean is_pinned) {
    // BTL 20030328 This implementation is a placeholder and must be corrected.
    //assert(0);
    if (*ref == 0)
        return;
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *) (*ref + (POINTER_SIZE_INT)INTERNAL(gc_heap_base_address)());
    assert(p_obj->vt());
    compressed_pointer_table->add_entry(slot_offset_entry((void **)ref, p_obj, (POINTER_SIZE_INT)INTERNAL(gc_heap_base_address)()));
	slot_offset_entry *addr = compressed_pointer_table->get_last_addr();
    p_global_gc->gc_internal_add_root_set_entry(&(addr->base));

    if (is_pinned) {
        // Add the current block as one that can't be compacted since it has a pinned root
        // pointing into it.
        p_global_gc->gc_internal_block_contains_pinned_root(GC_BLOCK_INFO(p_obj));
    }
}


GCEXPORT(void, gc_add_root_set_entry_nonheap)(Managed_Object_Handle *ref1) {
    Partial_Reveal_Object **ref = (Partial_Reveal_Object **) ref1;
	if(!is_object_pointer(*ref)) return;
#ifdef FILTER_NON_HEAP
    assert( !p_global_gc->is_in_heap(*ref) );
#endif // FILTER_NON_HEAP
//    return; // temporary workaround
    p_global_gc->gc_internal_add_root_set_entry(ref);
}



/****
*
*  Routines to support the allocation and initialization of objects.
* 
*****/

//
// Allocation of objects.
//
// There is a tension between fast allocation of objects and 
// honoring various constraints the ORP might place on the object. 
// These constraints include registering the objects for 
// finalization, aligning the objects on multiple word boundaries, 
// pinning objects for performance reasons, registering objects 
// related to weak pointers and so forth.
//
// We have tried to resolve this tension by overloading the 
// size argument that is passed to the allocation routine. If 
// the size of the argument has a high bit of 0, then the 
// allocation routine will assume that no constraints exist 
// on the allocation of this object and allocation can potentially 
// be made very fast. If on the other hand the size is large then 
// the routine will query the class data structure to determine 
// what constraints are being made on the allocation of this object.
//
//
// See orp_for_gc interface for masks that allow the gc to quickly 
// determine the constraints.

//
// This routine is the primary routine used to allocate objects. 
// It assumes nothing about the state of the ORP internal data 
// structures or the runtime stack. If gc_malloc_or_null is able 
// to allocate the object without invoking a GC or calling the ORP
// then it does so. It places p_vtable into the object, ensures 
// that the object is zeroed and then returns a Partial_Reveal_Object 
// pointer to the object. If it is not able to allocate the object 
// without invoking a GC then it returns NULL.
//
// Input: size - the size of the object to allocate. If the high bit
//               set then various constraints as described above are
//               placed on the allocation of this object.
//        p_vtable - a pointer to the vtable of the class being 
//                   allocated. This routine will place this value 
//                   in the appropriate slot of the new object.
//

#ifdef _IA64_
#define ALIGN8
#endif

//
// This routine is used to allocate an object. See the above 
// discussion on the overloading of size.
// The GC assumes that the ORP is ready to support a GC if it 
// calls this function.
//
// Input: size - the size of the object to allocate. If the high bit
//               set then various constraints as described above are
//               placed on the allocation of this object.
//        p_vtable - a pointer to the vtable of the class being allocated.
//                   This routine will place this value in the 
//                   appropriate slot of the new object.
//

Managed_Object_Handle gc_malloc_slow(unsigned size, Allocation_Handle ah, void *tp,Boolean do_not_relocate, Boolean escaping);

GCEXPORT(Managed_Object_Handle, gc_malloc)(unsigned size, Allocation_Handle ah) {
    // All chunks of data requested need to be multiples of GC_OBJECT_ALIGNMENT
    assert((size % GC_OBJECT_ALIGNMENT) == 0);
    
    assert (ah);

    unsigned int real_size = get_instance_data_size (size);
    // If the next to high bit is set, that indicates that this object needs to be pinned.
    Partial_Reveal_Object *result = (Partial_Reveal_Object *) gc_malloc_slow (real_size, ah, get_gc_thread_local(), false, false);

    assert (result->vt());
    gc_trace ((void *)result, "object is allocated");

    // if MANGLEPOINTERS is defined this is used for debugging.
    result = (Partial_Reveal_Object *)mangleBits((HeapObject *)result);

    return result; 
}


GCEXPORT(Managed_Object_Handle, gc_malloc_with_thread_pointer)(unsigned size, Allocation_Handle ah, void *tp) {
    // All chunks of data requested need to be multiples of GC_OBJECT_ALIGNMENT
    assert((size % GC_OBJECT_ALIGNMENT) == 0);
    
    assert (ah);

    unsigned int real_size = get_instance_data_size (size);
    // If the next to high bit is set, that indicates that this object needs to be pinned.
    Partial_Reveal_Object *result = (Partial_Reveal_Object *) gc_malloc_slow (real_size, ah, tp, false, false);

    assert (result->vt());
    gc_trace ((void *)result, "object is allocated");
    
    // if MANGLEPOINTERS is defined this is used for debugging.
    result = (Partial_Reveal_Object *)mangleBits((HeapObject *)result);

    return result; 
} // gc_malloc_with_thread_pointer


#if 0
GCEXPORT(Managed_Object_Handle, gc_malloc_with_thread_pointer_escaping)(unsigned size, Allocation_Handle ah, void *tp) {
    // All chunks of data requested need to be multiples of GC_OBJECT_ALIGNMENT
    assert((size % GC_OBJECT_ALIGNMENT) == 0);
    
    assert (ah);

//	++alloc_escaping;

    unsigned int real_size = get_instance_data_size (size);
    // If the next to high bit is set, that indicates that this object needs to be pinned.
    Partial_Reveal_Object *result = (Partial_Reveal_Object *) gc_malloc_slow (real_size, ah, tp, false, true);

    assert (result->vt());
    gc_trace ((void *)result, "object is allocated");
    
    // if MANGLEPOINTERS is defined this is used for debugging.
    result = (Partial_Reveal_Object *)mangleBits((HeapObject *)result);

    return  result; 
}
#endif // 0

//
// A helper function that is prepared to make sure that space is available for
// the object. We assume that the orp is prepared for a gc to happen within this
// routine.
//
Managed_Object_Handle gc_malloc_slow(
	unsigned size, 
	Allocation_Handle ah, 
	void *tp,
	Boolean do_not_relocate, 
	Boolean escaping) {

    GC_Thread_Info *gc_tls = orp_local_to_gc_local(tp);

	assert(!escaping); // escaping should only be used for pre-tenuring which isn't working right now

    Managed_Object_Handle p_return_object = NULL;

    struct Partial_Reveal_VTable *p_vtable = Partial_Reveal_Object::allocation_handle_to_vtable(ah);
    unsigned int class_constraints = p_vtable->get_gcvt()->gc_class_properties;
    //assert (p_vtable->gc_class_properties == p_vtable->class_properties);

    if (class_constraints == 0) {
#ifdef _DEBUGxxx
        p_TLS_orpthread->number_of_objects_allocated++;  
        p_TLS_orpthread->number_of_bytes_of_objects_allocated += size;  
#endif
        
        p_return_object = gc_malloc_slow_no_constraints (size, ah, gc_tls
#ifdef PUB_PRIV
			, g_use_pub_priv
#endif // PUB_PRIV
			);
        return p_return_object;
    }

    if (class_get_alignment(p_vtable->get_gcvt()->gc_clss)) {
#ifdef _IA64_
        // There is no special alignment hack needed for IA64
        assert(0);
#endif
        
        // We hava a special object that needs 
        //
        // In phase 1 of alignment, re-direct all objects
        // with special alignment needs to the LOS.
        // CLEANUP -- remove this cast....
        
        p_return_object = (Partial_Reveal_Object *)gc_pinned_malloc (size,
                                                  ah,
                                                  false, // returnNullOnFail is false
                                                  true,
												  gc_tls);
    }

    // See what constraints are placed on this allocation and call the appropriate routine.
    // CLEANUP  -- remove this cast.
    if (p_return_object == NULL) {
        if (class_is_pinned(p_vtable->get_gcvt()->gc_clss) || do_not_relocate) {
            p_return_object = (Partial_Reveal_Object *)gc_pinned_malloc(size,
                                                     ah,
                                                     false,
                                                     false,
													 gc_tls);
        }
    }

    if (p_return_object == NULL) {
        // alloc up an array or a normal object that needs to be finalized.
        p_return_object = gc_malloc_slow_no_constraints (size, ah, gc_tls
#ifdef PUB_PRIV
			, g_use_pub_priv
#endif // PUB_PRIV
			);
    }

    assert (p_return_object);


#ifdef _DEBUGxxx
    p_TLS_orpthread->number_of_objects_allocated++;  
    p_TLS_orpthread->number_of_bytes_of_objects_allocated += size;  
#endif
    
    gc_trace ((void *)p_return_object, "Allocated in gc_malloc_slow.");
    return p_return_object;
}


Partial_Reveal_Object *gc_los_malloc_noclass (unsigned size, bool in_a_gc) {
	printf("gc_los_malloc_noclass not yet supported.\n");
	assert(0);
	exit(-1);

    //
    // The size can't be zero, since there is a requisite VTable pointer
    // word that is the part of every object, and the size supplied
    // to us in this routine should include that VTable pointer word.
    //
    assert(size!=0);
    // Classes loaded before java.lang.Class must use this API, because the
    // vtable for java.lang.Class hasn't been constructed yet.
    // Currently only three classes should be allocated through this call:
    // java.lang.Object, java.io.Serializable and java.lang.Class.
    
    assert ((size % GC_OBJECT_ALIGNMENT) == 0);
    unsigned int real_size_bytes = size;
    Partial_Reveal_Object *result_start = NULL;

    result_start = gc_pinned_malloc(size, 0, false, false, NULL /* will need to fix to re-enable this function */);
    memset (result_start, 0x0, size); 
    return result_start;
} // gc_los_malloc_noclass


//
// For bootstrapping situations, when we still don't have
// a class for the object. This routine is only available prior to 
// a call to the call gc_orp_initialized. If it is called after
// the call to gc_orp_initialized then the results are undefined. 
// The GC places NULL in the vtable slot of the newly allocated
// object.
// 
// The object allocated will be pinned, not finalizable and not an array.
//
// Input: size - the size of the object to allocate. The high bit
//               will never be set on this argument.
// Output: The newly allocated object
//
GCEXPORT(Managed_Object_Handle, gc_pinned_malloc_noclass)(unsigned size) {
    // CLEANUP -- remove this cast.
    Partial_Reveal_Object *p_return = gc_los_malloc_noclass (size, false);
    gc_trace((void *)p_return, "Allocated as a pinned object."); 
    // we can't characterize an object without a vtable.
    return p_return;
} // gc_pinned_malloc_noclass


/****
*
*  Routines to support threads.
* 
*****/
//
// This routine is called during thread startup to set
// an initial nursery for the thread.
//
// Comment - gc_thread_init and gc_thread_kill assume that
//           the current thread is the one we are interested in
//           If we passed in the thread then these things could be
//           cross inited and cross killed.
//

void virtual_free(void *start) {
#ifdef ORP_POSIX
    free(start);
#else
    VirtualFree(start,0,MEM_RELEASE);
#endif
} // virtual_free

void * virtual_malloc(unsigned size) {
    void *gc_free;
#ifdef ORP_POSIX
    
    gc_free = malloc(size + 0xFFFF);
    // alignment happens later
    
    if(gc_free == NULL) {
#ifdef _WINDOWS        
        DWORD error_code = GetLastError();
        orp_cout << "Error: Garbage Collector failed to reserve ";
        orp_cout << final_heap_size_bytes;
        orp_cout << " bytes of virtual address space. Error code = ";
        orp_cout << error_code << std::endl;
        assert(0);
        orp_exit(error_code);
#else
        perror("malloc failure");
        printf("%d %d\n",size,size+0xFFFF);
        assert(0);
        orp_exit(-1);
#endif
    }
    return gc_free;
#else // !ORP_POSIX
    
    // JMS 2003-05-23.  Getting virtual memory from Windows.  If large_pages is
    // specified, we either acquire the entire heap with large pages or we exit.
    // If compressed_references is specified, we ask for an additional 4GB and
    // then adjust the result so that it is 4GB aligned.  If we don't manage to
    // get that much, we default to asking for the original amount and accept that
    // it's not 4GB aligned.  We only commit the actual heap size requested even
    // though an extra 4GB is reserved, so the memory footprint still appears
    // reasonable.
    //
    // Since VirtualAlloc with large pages seems to require committing up front,
    // we actually waste 4GB.
    
#ifdef _IA64_
    if (use_large_pages) {
        // Using large pages on Win64 seems to require MEM_COMMIT and PAGE_READWRITE.
        int64 minimum_page_size = (int64)adjustprivileges();
        page_size = minimum_page_size;
        action = MEM_COMMIT;
        action_large = MEM_LARGE_PAGES;
        protection = PAGE_READWRITE;
        final_heap_size = (final_heap_size+minimum_page_size-1)&
            ~(minimum_page_size-1); 
    }
#endif // _IA64_
    gc_free = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if(!gc_free) {
        printf("VirtualAlloc failed...falling back to malloc\n");
        return malloc(size);
    }
    return gc_free;
#endif // !ORP_POSIX
} // virtual_malloc

void alloc_and_init_pn(GC_Small_Nursery_Info *private_nursery) {
#if 1
        private_nursery->start = virtual_malloc(local_nursery_size);
#else
		private_nursery->start = malloc(local_nursery_size);
#endif

#ifdef _DEBUG
		if(g_maximum_debug) {
			printf("New private nursery: Start = %x, End = %x\n",private_nursery->start,(POINTER_SIZE_INT)private_nursery->start + local_nursery_size);
			fflush(stdout);
		}
#endif
		memset(private_nursery->start,0,local_nursery_size);
#ifdef USE_STL_PN_ALLOCATOR
		private_nursery->tls_arena = alloc_arena(NULL,ARENA_SIZE);
#endif // USE_STL_PN_ALLOCATOR
		private_nursery->local_gc_info = new pn_info(private_nursery);

#ifdef PUB_PRIV
#ifndef NEW_APPROACH
        if(g_keep_mutable || g_keep_all) {
#endif
            // number of entries in the array
            unsigned smb_size = (1 + (local_nursery_size / sizeof(void*) / (sizeof(unsigned)*8)));
            private_nursery->local_gc_info->staying_mark_bits = (unsigned*)malloc(smb_size * sizeof(unsigned)); // size in bytes
            memset(private_nursery->local_gc_info->staying_mark_bits,0,smb_size * sizeof(unsigned));
            private_nursery->local_gc_info->smb_size = smb_size;
#ifndef NEW_APPROACH
        }
#endif
#endif // PUB_PRIV

#ifdef CONCURRENT
//		private_nursery->concurrent_state_copy = CONCURRENT_IDLE;
		private_nursery->concurrent_state_copy = g_concurrent_gc_state;
		private_nursery->current_state = g_concurrent_gc_state;
#endif // CONCURRENT
#if defined _DEBUG
		private_nursery->num_write_barriers = 0;
		private_nursery->slot_outside_nursery = 0;
		private_nursery->value_inside_nursery = 0;
		private_nursery->useful = 0;
		private_nursery->num_survivors = 0;
		private_nursery->size_survivors = 0;
		private_nursery->space_used = 0;
		private_nursery->size_objects_escaping = 0;
		private_nursery->num_promote_on_escape = 0;
		private_nursery->num_promote_on_escape_interior = 0;
		private_nursery->frame_count = 0;
#endif // _DEBUG
#ifdef DETAILED_PN_TIMES
		if(pn_history) {
			private_nursery->local_gc_info->pn_stats = new std::list<pn_collection_stats>;
		}
#endif // DETAILED_PN_TIMES
} // alloc_and_init_pn

// Code to allow manipulation of active_thread_gc_info_list in a thread safe manner.

static void gc_thread_init_base (GC_Thread_Info *info) {
    static bool gc_thread_init_bool = true;
    if (gc_thread_init_bool) {
        // orp_cout << "gc_thread_init() : FILL ME" << std::endl;
        gc_thread_init_bool = false;
    }

#if 0
    block_info *alloc_block;

	GC_Nursery_Info *nursery_info = info->get_nursery();
    nursery_info->chunk = p_global_gc->p_cycle_chunk(NULL, true, false, NULL, info);
    nursery_info->curr_alloc_block = nursery_info->chunk;
    alloc_block = (block_info *)(nursery_info->chunk);
    nursery_info->tls_current_free = NULL;
    nursery_info->tls_current_ceiling = NULL;
#endif

#if 0
    if(local_nursery_size) {
	    GC_Nursery_Info *level_1_mutable_info = info->get_level_1_mutable_nursery();
	    level_1_mutable_info->chunk = p_global_gc->p_cycle_chunk(NULL, true, false, gc_information, info);
	    level_1_mutable_info->curr_alloc_block = level_1_mutable_info->chunk;
	    alloc_block = (block_info *)(level_1_mutable_info->chunk);
	    level_1_mutable_info->tls_current_free = NULL;
	    level_1_mutable_info->tls_current_ceiling = NULL;
    }
#endif // 0/1

#if 0
	if(separate_immutable) {
		GC_Nursery_Info *immutable_info = info->get_immutable_nursery();
		immutable_info->chunk = p_global_gc->p_cycle_chunk(NULL, true, true, NULL, info);
		immutable_info->curr_alloc_block = immutable_info->chunk;
		alloc_block = (block_info *)(immutable_info->chunk);
		immutable_info->tls_current_free = NULL;
		immutable_info->tls_current_ceiling = NULL;

#if 0
		GC_Nursery_Info *level_1_immutable_info = info->get_level_1_immutable_nursery();
		level_1_immutable_info->chunk = p_global_gc->p_cycle_chunk(NULL, true, true, gc_information, info);
		level_1_immutable_info->curr_alloc_block = level_1_immutable_info->chunk;
		alloc_block = (block_info *)(level_1_immutable_info->chunk);
		level_1_immutable_info->tls_current_free = NULL;
		level_1_immutable_info->tls_current_ceiling = NULL;
#endif // 0/1
	}
#endif

	if(local_nursery_size) {
		GC_Small_Nursery_Info *private_nursery = info->get_private_nursery();

        alloc_and_init_pn(private_nursery);
	}

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    volatile GC_Thread_Info *temp = active_thread_gc_info_list;
    while (temp) {
        if ((void *)temp == info) {
            // THIS IS A BUG IF WE GET HERE AND JIM HAS FIXED IT BUT FOR NOW I'M IGNORING IT RLH 1-30-03
//            orp_cout << "Why is this gc_information block on the active list already???? " << std::endl;
//            assert (0);
            break;
        }
        temp = temp->p_active_gc_thread_info; // Do it for all the threads.
    }
    if (temp == NULL) {
        // If temp == gc_information then this thread is already on the active list
        // so we can't add it again. This is because for some reason the VM
        // is calling thread init twice..
        info->p_active_gc_thread_info = active_thread_gc_info_list;
        active_thread_gc_info_list = info;
		active_thread_count++;
    }
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

GCEXPORT(void, gc_thread_init)(void *gc_information, void *thread_handle) { 
    GC_Thread_Info *info = orp_local_to_gc_local(gc_information);
    info->thread_handle = thread_handle;
    gc_thread_init_base(info);
}

/*
 * The "PGC" shim layer that adapts between Pillar and GCv4 uses an approach
 * where when not in a GC the thread list consists of processor local storage
 * and only the nursery fields are used.  When a GC is starting, the processor
 * local storage "threads" are "killed" with gc_thread_kill and all the Pillar
 * threads are stopped and enumerated to GCv4 via this function.  This function
 * does not allocate nurseries for these threads for two reasons.  First, these
 * threads don't need or use nurseries since that is handled via processor local
 * storage.  Second, such nursery allocation is impossible when the cause of the
 * GC is already a lack of allocatable nurseries.
 */
GCEXPORT(void, gc_thread_init_no_nursery)(void *gc_information, void *thread_handle) { 
    GC_Thread_Info *info = orp_local_to_gc_local(gc_information);
    info->thread_handle = thread_handle;
//    info->gray_set = new Gray_Ssb();

	GC_Nursery_Info *nursery_info = info->get_nursery();

    nursery_info->chunk = NULL;
    nursery_info->curr_alloc_block = nursery_info->chunk;
    nursery_info->tls_current_free = NULL;
    nursery_info->tls_current_ceiling = NULL;

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    volatile GC_Thread_Info *temp = active_thread_gc_info_list;
    while (temp) {
        if ((void *)temp == info) {
            // THIS IS A BUG IF WE GET HERE AND JIM HAS FIXED IT BUT FOR NOW I'M IGNORING IT RLH 1-30-03
//            orp_cout << "Why is this gc_information block on the active list already???? " << std::endl;
//            assert (0);
            break;
        }
        temp = temp->p_active_gc_thread_info; // Do it for all the threads.
    }
    if (temp == NULL) {
        // If temp == gc_info then this thread is already on the active list
        // so we can't add it again. This is because for some reason the VM
        // is calling thread init twice..
        info->p_active_gc_thread_info = active_thread_gc_info_list;
        active_thread_gc_info_list = info;
    }
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

//
// If the thread has a nursery it nolonger will use this
// is called. Typically after gc_thread_kill is called.
//
//extern "C"
void gc_release_nursery (void *a_nursery) {
    block_info *the_nursery = (block_info *)a_nursery;

    //
    // Store away that thread's nursery for subsequent
    // scavenging (during the next stop-the-world collection).
    //
    
    if (the_nursery != NULL) {
        // orphan the nursery.
        assert (the_nursery->get_nursery_status() == active_nursery);
        the_nursery->set_nursery_status(active_nursery,spent_nursery);
    }
}
//
// This is called just before the thread is reclaimed.
//

GCEXPORT(void, gc_thread_kill)(void *gc_information) {
    GC_Thread_Info *gc_info = orp_local_to_gc_local(gc_information);

	GC_Nursery_Info *nursery_info = gc_info->get_nursery();

    // Return the chunk used for allocation by this thread to the store.
    block_info *spent_block = (block_info *)nursery_info->chunk;
    
    while (spent_block) {
        assert(spent_block);
        assert (spent_block->get_nursery_status() == active_nursery);
        spent_block->set_nursery_status(active_nursery,spent_nursery);
        
        spent_block = spent_block->next_free_block;
    }

	bool found = false;
    
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    volatile GC_Thread_Info *temp_active_thread_gc_info_list = active_thread_gc_info_list;   

    if ((void *)temp_active_thread_gc_info_list == gc_info) {
        // it is at the head of the list.
        active_thread_gc_info_list = active_thread_gc_info_list->p_active_gc_thread_info;
		found = true;
    } else {
        while (temp_active_thread_gc_info_list &&
			   temp_active_thread_gc_info_list->p_active_gc_thread_info != gc_info) {
            temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
            assert (temp_active_thread_gc_info_list->p_active_gc_thread_info);
        }
		if(temp_active_thread_gc_info_list) {
			assert (gc_info == temp_active_thread_gc_info_list->p_active_gc_thread_info);
			temp_active_thread_gc_info_list->p_active_gc_thread_info = gc_info->p_active_gc_thread_info;
			found = true;
		}
    }

	active_thread_count--;
//    delete (gc_info->gray_set);
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#ifdef GC_CONCURRENT
    // We need to let the GC know about any gray objects in the gc_tls before we return and gc_info becomes
    // invalid.
    orp_cout << " BUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUG    fill me up gc_thread_kill " << std::endl;
#endif


	if(local_nursery_size) {
		GC_Small_Nursery_Info *private_nursery = gc_info->get_private_nursery();

		if(found) {
			thread_stats_print(private_nursery);
		}

		virtual_free(private_nursery->start);
#if 0
		printf("PN free = %p, GC thread = %p\n",private_nursery->start,gc_info);
#endif
	}
}

/****
*
*  Routines to support the functionality required by the Java language specification.
* 
*****/

//
// API for the ORP to force a GC, typically in response to a call to 
// java.lang.Runtime.gc
//
GCEXPORT(void, gc_force_gc)() {
    if(!orp_initialized) {
        return;
    }

    if (fullheapcompact_at_forcegc) {
        if (verbose_gc) {
            printf("Compacting full heap...\n");
            fflush(stdout);
        }
    }
    
    orp_gc_lock_enum();
    
#ifdef GC_VERIFY_VM
    assert(!running_gc); // If we are testing the gc we had better have the GC lock.
#endif // GC_VERIFY_VM

    if (verbose_gc) {
        printf("gc_force_gc()....calling reclaim_full_heap()\n");
        fflush(stdout);
    }

    p_global_gc->reclaim_full_heap(0, true, false);

    if(local_nursery_size) {
        GC_Thread_Info *cur_thread_node = active_thread_gc_info_list;
        GC_Thread_Info *this_thread = (struct GC_Thread_Info *)get_gc_thread_local();

        // For each thread do a private nursery collection.
        while (cur_thread_node) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

            pn_info *local_collector = private_nursery->local_gc_info;
            assert(local_collector);
            while(1) {
                if (cur_thread_node == this_thread) {
                    local_nursery_collection(cur_thread_node, NULL, NULL, false);
                    break;
                } else if(local_collector->gc_state == LOCAL_MARK_IDLE) {
                    bool suspend_success = orp_suspend_thread_for_enumeration ((PrtTaskHandle)cur_thread_node->thread_handle);
                    // The thread can enter a pn-collection while we are trying to suspend it.
                    if(local_collector->gc_state == LOCAL_MARK_IDLE) {
                        // The thread didn't enter a pn-collection so we do one for it.
                        local_nursery_collection(cur_thread_node, NULL, NULL, false);
                    } else {
                        // The thread entered a pn-collection so we just wait for it to be done.
                        while(local_collector->gc_state != LOCAL_MARK_IDLE);
                    }
                    orp_resume_thread_after_enumeration ((PrtTaskHandle)cur_thread_node->thread_handle);
                    break;
                }
            }

            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }
    }

    orp_gc_unlock_enum();
}

//
// API for the ORP to determine the total GC heap, typically in response to a
// call to java.lang.Runtime.totalMemory
//
GCEXPORT(int64, gc_total_memory)() {
    static bool gc_total_memory_bool = true;
    if (gc_total_memory_bool) {
        if (verify_gc) {
            // orp_cout << "gc_total_memory() : FILL ME" << std::endl;
        }
        gc_total_memory_bool = false;
    }
    return 0;
}

//
// API for the ORP to get an approximate view of the free space, 
// typically in response to a call to java.lang.Runtime.freeMemory
//
GCEXPORT(int64, gc_free_memory)() {
    static bool gc_free_memory_bool = true;
    if (gc_free_memory_bool) {
        if (verify_gc) {
            // orp_cout << "gc_free_memory() : FILL ME" << std::endl;
        }
        gc_free_memory_bool = false;
    }
    return 0;
}


//
// API for the ORP to determine the maximum allowed GC heap,
// typically in response to a call to java.lang.Runtime.maxMemory
//
GCEXPORT(int64, gc_max_memory)() {
    return 0;
}


// Support for Delinquent Regions and Data EAR work

GCEXPORT(void *, gc_heap_base_address)() {
    return p_global_gc->get_gc_heap_base_address();
}
 
 
 
GCEXPORT(void *, gc_heap_ceiling_address)() {
#ifndef DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
#ifdef _IA64_
    return p_global_gc->get_gc_heap_ceiling_address();
#else // !_IA64_
    return (void *) 0xFFFFffff;
#endif // !_IA64_
#endif // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
    return p_global_gc->get_gc_heap_ceiling_address();
}
 
extern void capture_pmu_data(void **);

GCEXPORT(void, gc_register_delinquent_regions)(void **regions, int) {}

GCEXPORT(void *, gc_get_latest_path_information)() {
    return NULL;
}

// has this a valid object or not?
GCEXPORT(Boolean, gc_is_heap_object)(void *p_obj) {    
    Partial_Reveal_Object *the_obj = (Partial_Reveal_Object *)p_obj;
    if ( !p_global_gc->is_in_heap(the_obj) ) {
        return false;
    }
    POINTER_SIZE_INT mask;

#ifdef _IA64_ 
    mask = 7; 
#else
    mask = 3;
#endif
    // It had better be aligned properly
    if ((mask & (POINTER_SIZE_INT)p_obj) != 0) {
        return false;
    }

    block_info *info = GC_BLOCK_INFO(the_obj);
    if ((POINTER_SIZE_INT)the_obj < (POINTER_SIZE_INT)GC_BLOCK_ALLOC_START(info)) {
        return false; // We have a bogus object so return false.
    }// We have a bogus object
    if (p_global_gc->obj_belongs_in_single_object_blocks(the_obj)) {
        if ((POINTER_SIZE_INT) the_obj == (POINTER_SIZE_INT)GC_BLOCK_ALLOC_START(info)) {
        // Actually we don't know but we will return true here for now.
            return true;
        } else {
            return false;
        }
    }
  
    // It passed all the reasonable tests.
    return true;
}


// has this object survived a GC cycle or not? If we can't tell we return false, ie it is short lived.

GCEXPORT(Boolean, gc_is_object_long_lived)(void *p_obj) {    
    Partial_Reveal_Object *the_obj = (Partial_Reveal_Object *)p_obj;
    if (p_global_gc->obj_belongs_in_single_object_blocks(the_obj)) {
        // Actually we don't know but we will return true here for now.
        return true;
        
    }
    block_info *info = GC_BLOCK_INFO(the_obj);

    if ((POINTER_SIZE_INT)the_obj < (POINTER_SIZE_INT)GC_BLOCK_ALLOC_START(info)) {
        return false; // We have a bogus object so return false.
    } // We have a bogus object that can be ignored.

// FIX FIX FIX....upgrade to this at some point
//    return info->age != 0;

    if (info->block_has_been_swept) {
        if (info->in_nursery_p) {
            if (info->num_free_areas_in_block == 0) {
                // There were no free areas to allocate in. 
                return true;
            }
            unsigned int i = 0;
            free_area *area = &(info->block_free_areas[i]);
            while (area->area_base) {
                if ( ((POINTER_SIZE_INT)the_obj >= (POINTER_SIZE_INT)area->area_base) && 
                     ((POINTER_SIZE_INT)the_obj < (POINTER_SIZE_INT)area->area_ceiling) ) {
                    return false;
                }
                i++;
                area = &(info->block_free_areas[i]);
                if (i >= info->size_block_free_areas) {
                    // We are in some bogus part of the heap or we would have hit a NULL entry for area.
                    return false;
                }
            }
        }
    }
    return true;
}

// Time in milliseconds since last GC. TBD.
// ************************************************************************************
//
 
GCEXPORT(unsigned int, gc_time_since_last_gc)() {
    return 3737;
}


/****
*
*  Routines to support the functionality required by Jini to see if an object is pinned.
* 
*****/


GCEXPORT(Boolean, gc_is_object_pinned) (Managed_Object_Handle obj) {
    //
    // NOTE --->>>>>>>>> THIS API IS NOT FOR INTERIOR POINTERS...
    //
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *) obj;
    
    if (fullheapcompact_at_forcegc || incremental_compaction) {
        // Moving GC
        if (GC_BLOCK_INFO(p_obj)->in_los_p || GC_BLOCK_INFO(p_obj)->is_single_object_block) {
            return TRUE;
        } else {
            assert(GC_BLOCK_INFO(p_obj)->in_nursery_p);
            // No guarantee can be offered that it will not be moved...
            return FALSE;
        } 
    } else {
        // FIXED GC -- no object moves
        return TRUE;
    } 
}

/****
*
*  Routines to support the the class loader and to allow the GC to seperate out and 
*  control the format of the data structures that it uses.
* 
*****/

// Allocate GC-specific vtable data from a contiguous section of memory.
static char *vtalloc(size_t size) {
    static char *pool_start = NULL;
    static char *pool_end = NULL;
    const size_t default_pool_size = 64*1024;

    size_t bytes_available = pool_end - pool_start;
    if (bytes_available < size)
    {
        size_t size_needed = (size > default_pool_size ? size : default_pool_size);
        pool_start = (char *) malloc(size_needed);
        assert(pool_start);
        pool_end = pool_start + size_needed;
    }

    char *result = pool_start;
    pool_start += size;
    return result;
}


Field_Handle get_field_handle_from_offset(Partial_Reveal_VTable *vt, unsigned int offset) {
     unsigned num_fields = class_num_instance_fields_recursive(vt->get_gcvt()->gc_clss);
     for(unsigned int idx = 0; idx < num_fields; idx++) {
         Field_Handle fh = class_get_instance_field_recursive(vt->get_gcvt()->gc_clss, idx);
         if(field_is_reference(fh)) {
             unsigned int off = field_get_offset(fh);
             if (off == offset) {
                 return fh;
             }
         }
     }
     assert(0);
     return 0;
}


// A comparison function for qsort().
static int intcompare(const void *vi, const void *vj) {
    const int *i = (const int *) vi;
    const int *j = (const int *) vj;
    if (*i > *j)
        return 1;
    if (*i < *j)
        return -1;
    return 0;
}


//
// Given a v table build an array of slot offsets used by instantiations of objects
// using this vtable.
// Input
//     ch - the class handle for this object type.
//     vt - vtable for this object type.
//     for_compressed_fields - whether the result should correspond to raw fields or 32-bit compressed fields
// Returns
//    a 0 delimited array of the slots in an object of this type.
//
// Uses
//    num_instance_fields - the number of fields in an object of this type.
//    get_instance_field  - takes an index and returns the associated field.
//    field_is_reference  - True if the field is a slot (ie holds a reference)
//    field_get_offset    - given a field returns its offset from the start of the object.
//

static unsigned int *build_slot_offset_array(Class_Handle ch, Partial_Reveal_VTable *vt) {
    unsigned int *result = NULL;

    unsigned num_ref_fields = 0;
    //
    // Careful this doesn't give you the number of instance fields.
    // Calculate the size needed for the offset table.
    //
    unsigned num_fields = class_num_instance_fields_recursive(ch);

    unsigned idx;
    for(idx = 0; idx < num_fields; idx++) {
        Field_Handle fh = class_get_instance_field_recursive(ch, idx);
        if(field_is_reference(fh)) {
            num_ref_fields++;
        }
    }

    // We need room for the terminating 0 so add 1.
    unsigned int size = (num_ref_fields+1) * sizeof (unsigned int);

    // malloc up the array if we need one.
    unsigned int *new_ref_array = (unsigned int *)vtalloc (size);

    result = new_ref_array;
    for(idx = 0; idx < num_fields; idx++) {
        Field_Handle fh = class_get_instance_field_recursive(ch, idx);
        if(field_is_reference(fh)) {
            *new_ref_array = field_get_offset(fh);
            new_ref_array++;
        }
    }
    *new_ref_array = 0;
    // Update the number of slots.
    vt->get_gcvt()->gc_number_of_slots += num_ref_fields;
    // It is 0 delimited.

    // The VM doesn't necessarily report the reference fields in
    // memory order, so we sort the slot offset array.  The sorting
    // is required by the verify_live_heap code.
    qsort(result, num_ref_fields, sizeof(*result), intcompare);
    
    return result;
}

static unsigned int *build_weak_slot_offset_array(Class_Handle ch, Partial_Reveal_VTable *vt) {
    unsigned int *result = NULL;

    unsigned num_weak_ref_fields = 0;
    //
    // Careful this doesn't give you the number of instance fields.
    // Calculate the size needed for the offset table.
    //
    unsigned num_fields = class_num_instance_fields_recursive(ch);

    unsigned idx;
    for(idx = 0; idx < num_fields; idx++) {
        Field_Handle fh = class_get_instance_field_recursive(ch, idx);
        if(field_is_weak_reference(fh)) {
            num_weak_ref_fields++;
        }
    }

#ifdef CONCURRENT
    if(num_weak_ref_fields) {
        printf("Weak reference fields not supported in concurrent mode.\n");
        exit(-1);
    }
#endif // CONCURRENT

    // We need room for the terminating 0 so add 1.
    unsigned int weak_size = (num_weak_ref_fields+1) * sizeof (unsigned int);

    // malloc up the array if we need one.
    unsigned int *new_weak_ref_array = (unsigned int *)vtalloc (weak_size);

    result = new_weak_ref_array;
    for(idx = 0; idx < num_fields; idx++) {
        Field_Handle fh = class_get_instance_field_recursive(ch, idx);
        if(field_is_weak_reference(fh)) {
            *new_weak_ref_array = field_get_offset(fh);
            new_weak_ref_array++;
        }
    }
    *new_weak_ref_array = 0;
    // Update the number of slots.
    vt->get_gcvt()->gc_number_of_slots += num_weak_ref_fields;
    // It is 0 delimited.

    // The VM doesn't necessarily report the reference fields in
    // memory order, so we sort the slot offset array.  The sorting
    // is required by the verify_live_heap code.
    qsort(result, num_weak_ref_fields, sizeof(*result), intcompare);
    
    return result;
}

// Setter functions for the gc class property field.
void gc_set_prop_alignment_mask (Partial_Reveal_VTable *vt, unsigned int the_mask) {
    vt->get_gcvt()->gc_class_properties |= the_mask;
}
void gc_set_prop_non_ref_array (Partial_Reveal_VTable *vt) {
    vt->get_gcvt()->gc_class_properties |= CL_PROP_NON_REF_ARRAY_MASK;
}
void gc_set_prop_array (Partial_Reveal_VTable *vt) {
    vt->get_gcvt()->gc_class_properties |= CL_PROP_ARRAY_MASK;
}
void gc_set_prop_pinned (Partial_Reveal_VTable *vt) {
    vt->get_gcvt()->gc_class_properties |= CL_PROP_PINNED_MASK;
}
void gc_set_prop_finalizable (Partial_Reveal_VTable *vt) {
    vt->get_gcvt()->gc_class_properties |= CL_PROP_FINALIZABLE_MASK;
}

//
// gc_class_prepared is broken code for the following reasons.
// 1. It forces the GC to know about classes, superclasses and how inheritence works.
// 2. It assumes this routine is called for the super classes before it is
//    called for the subclass and such temporal dependencies are hard to understand.
//
// A better sequence would be the following.

GCEXPORT(void, gc_class_prepared) (Class_Handle ch, VTable_Handle vth) {
    assert(ch);
    assert(vth);
    Partial_Reveal_VTable *vt = (Partial_Reveal_VTable *)vth;
    g_tgc_vtable_list.push_back(vt);
    vt->set_gcvt((GC_VTable_Info *) malloc(sizeof(GC_VTable_Info)));
    memset((void *) vt->get_gcvt(), 0, sizeof(GC_VTable_Info));
    vt->get_gcvt()->gc_clss = ch;
    vt->get_gcvt()->gc_class_properties = 0; // Clear the properties.
    vt->get_gcvt()->gc_object_has_slots = false;
    // Set the properties.
    gc_set_prop_alignment_mask(vt, class_get_alignment(ch));

    // Remember the VTable (vt) in a hash table so that delta_dynopt can check if it has a legal
    // vtable. (see object_address_seems_valid for an example of its use.)
    if (!p_loaded_vtable_directory) {
        p_loaded_vtable_directory = new Hash_Table();
    }
    p_loaded_vtable_directory->add_entry(vt);

    if(class_is_array(ch)) {
        Class_Handle array_element_class = class_get_array_element_class(ch);
        // We have an array so not it.
        gc_set_prop_array(vt);
        // Get the size of an element.
        vt->get_gcvt()->gc_array_element_size = class_element_size(ch);
        
        // Place the byte offset to the first element of the array in the gc private part of the vtable.
        //unsigned int the_offset = array_first_element_offset_unboxed(array_element_class);
        unsigned int the_offset = vector_first_element_offset_unboxed(array_element_class);
#ifdef NUM_EXTRA_OBJ_HEADER_WORDS
        the_offset = sizeof(ORP_Vector);
#else
#ifdef EIGHT_BYTE_ALIGN_ARRAY
        the_offset = ((vt->get_gcvt()->gc_array_element_size == 8) ? 16 : 12);
#else
        the_offset = 12;
#endif
        // REVIEW vsm 03-Aug-2002 -- The following assertion fails on IPF. 
        // Reviewed by RLH 14-Aug-02 I am waiting for MC to provide interface to size information
        // so that this assert no longer is needed. But for now removing it is OK.
        //
        //assert(the_offset == sizeof(ORP_Vector));
#endif //NUM_EXTRA_OBJ_HEADER_WORDS
        
#ifdef ORP_POSIX
        // If Linux does not align 2 word items then this is the offset.
        //the_offset = 12; BUGBUG
#endif
        
#ifndef POINTER64
        // I shouldn't actually depend on this fact but I would like to know if it happens.
        // assert ((vt->gc_array_element_size == 8)? (the_offset == 16) : (the_offset == 12)); BUGBUG
#endif
        vt->get_gcvt()->gc_array_first_element_offset = the_offset;
        
        if (!class_is_non_ref_array (ch)) {
            vt->get_gcvt()->gc_object_has_slots = true;
        }
    }
    if(class_is_non_ref_array(ch)) {
        assert(class_is_array(ch));
        gc_set_prop_non_ref_array(vt);
    }
    if (class_is_pinned(ch)) {
        gc_set_prop_pinned(vt);
    }
    if (class_is_finalizable(ch)) {
        gc_set_prop_finalizable(vt);
    }
    //**
    // Mark for deletion.
    // Check that they are correct.    
    //assert (vt->gc_class_properties == vt->class_properties);
    //**
    unsigned int size = class_get_boxed_data_size(ch);
    vt->get_gcvt()->gc_allocated_size = size;
   
    // Build the offset array.
    vt->get_gcvt()->gc_number_of_slots = 0;
    vt->get_gcvt()->gc_ref_offset_array = build_slot_offset_array(ch, vt);
    vt->get_gcvt()->gc_weak_ref_offset_array = build_weak_slot_offset_array(ch, vt);
    if (vt->get_gcvt()->gc_number_of_slots) {
        vt->get_gcvt()->gc_object_has_slots = true;
    }
    // FIX FIX FIX....class_get_name() return a pointer to a static and will change on next invocation
    vt->get_gcvt()->gc_class_name = class_get_name(ch);
    assert (vt->get_gcvt()->gc_class_name);
    
    // We always fuse strings... If this aborts at some point it is most likely because
    // the string type has been changed not to include a "value"field. So look at the
    // string type and figure out if this optimization is needs to be eliminated.
    
    vt->get_gcvt()->gc_fuse_info = NULL;

} //gc_class_prepared

// If we are mangling pointers then no inlining, 
// instead force all allocation through the gc allocation routines.
GCEXPORT(Boolean, gc_supports_frontier_allocation) (unsigned *offset_of_current, unsigned *offset_of_limit) {
#ifdef MANGLEPOINTERS
    // Code to check ensure inlining is really off.
    *offset_of_current = NULL;
    *offset_of_limit = NULL;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
    return FALSE;
#else
    GC_Nursery_Info *dummy = NULL;
    *offset_of_current = (unsigned)((Byte *)&dummy->tls_current_free - (Byte *)dummy);
    *offset_of_limit = (unsigned)((Byte *)&dummy->tls_current_ceiling - (Byte *)dummy);
    return TRUE;
#endif // MANGLEPOINTERS
}

GCEXPORT(void, gc_suppress_finalizer)(Managed_Object_Handle obj) {
    p_global_gc->remove_finalize_object((Partial_Reveal_Object *)obj);
}

GCEXPORT(void, gc_register_finalizer)(Managed_Object_Handle obj) {
    p_global_gc->add_finalize_object((Partial_Reveal_Object *)obj,true);
}

unsigned int get_object_size_with_vtable(Partial_Reveal_VTable *vtable,Partial_Reveal_Object *p_obj) {
	bool arrayp;
	if(vtable->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK) {
        arrayp = true;
    } else {
        arrayp = false;
    }
    if (arrayp) {
        unsigned int sz;
        sz = orp_vector_size(vtable->get_gcvt()->gc_clss, vector_get_length_with_vt((Vector_Handle)p_obj,vtable));
        return sz; 
    } else {
        return vtable->get_gcvt()->gc_allocated_size;
    }
}

GCEXPORT(Boolean, gc_update_vtable)(Managed_Object_Handle object, Allocation_Handle new_vt) {
    Partial_Reveal_VTable *new_vtable = (Partial_Reveal_VTable*)new_vt;
	Partial_Reveal_Object *obj        = (Partial_Reveal_Object*)object;
	int size_diff = get_object_size_with_vtable(obj->vt(),obj) - get_object_size_with_vtable(new_vtable,obj);
	if(size_diff == 0) {
		obj->set_vtable(new_vt);
		return 1;
	}
	if(size_diff >= sizeof(Partial_Reveal_VTable*)) {
		// compute the offset from the start of the object to install the filler vtable (end of current object after new vtable is installed)
		int new_fake_object_offset = get_object_size_with_vtable(obj->vt(),obj) - size_diff;
		if(new_fake_object_offset % sizeof(Partial_Reveal_VTable*) != 0) return 0; // must be aligned
		if(size_diff % sizeof(Partial_Reveal_VTable*) != 0) return 0; // must be aligned
		// write the main object's new vtable
		obj->set_vtable(new_vt);
		// Write the fake vtable for the pseudo-object we are creating to keep the heap
		// tightly packed.  We use values less than 1<<16 for vtables whose object sizes
		// match the vtable ID.
		heapSetG4((ManagedObject*)obj,new_fake_object_offset,size_diff);
		return 1;
	}
	return 0;
}

GCEXPORT(unsigned, gc_get_tenure_offset)(void) {
	return (unsigned)(((GC_Thread_Info*)0)->get_nursery());
}

#ifdef __X86_64__

typedef struct {
    char prt_reserved[PILLAR_VSE_SIZE];
    void (*realM2uUnwinder)(void*,void*);
    void *latest_pseudo_frame;
    void *rip_estimate;
} _tgc_m2u_vse;

extern "C" unsigned get_m2u_vse_size(void) {
    unsigned ret = sizeof(_tgc_m2u_vse);
    if(ret % 8 != 0) {
        printf("tgc m2u vse size not a multiple of 8\n");
        exit(-1);
    }
    if(ret % 16 != 0) {
        // after the vse is added to the stack and the call made we need stack alignment
        ret += 8;
    }
    return ret;
}

typedef struct _tgc_pillar2c_pseudo {
    struct _tgc_pillar2c_pseudo * prev;
    void * fake_eip_target;
    void * ref_mask_ptr;
} tgc_pillar2c_pseudo;

void tgc_m2u_unwinder(struct PrtStackIterator *si, void *lvse) {
    _tgc_m2u_vse *prv = (_tgc_m2u_vse*)lvse;

    tgc_pillar2c_pseudo *prev_pseudo;

    // Note: the callee-save register values are incorrect, but they will be fixed after the M2U unwind.
    prev_pseudo = (tgc_pillar2c_pseudo*)prv->latest_pseudo_frame;

    // The problem is that we need the rip of the current call site, not the one from the pseudo-frame.
    prtSetStackIteratorFields(si, 
                              prv->rip_estimate,
                              /*esp*/    (PrtRegister)prv->latest_pseudo_frame,
                              /*rbxPtr*/ si->rbxPtr,
                              /*rbpPtr*/ si->rbpPtr,
                              /*r12Ptr*/ si->r12Ptr,
                              /*r13Ptr*/ si->r13Ptr,
                              /*r14Ptr*/ si->r14Ptr,
                              /*r15Ptr*/ si->r15Ptr,
                              /*vsh*/    (PrtVseHandle)lvse,
                              /*virtualFrameNumber*/ 0);
}

extern "C" void tgc_enter_unmanaged(_tgc_m2u_vse *vse, void *pseudo, void *task) {
    vse->realM2uUnwinder = tgc_m2u_unwinder;
    vse->latest_pseudo_frame = pseudo;
    vse->rip_estimate = ((void**)vse)-1;
    enterUnmanagedCode(task,(PrtVseHandle)vse);
}

extern "C" void tgc_reenter_managed(void *task) {
    reenterManagedCode(task);
}


#endif // __X86_64__


