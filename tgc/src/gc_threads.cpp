/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>

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
#include "gc_thread.h"
#include "gcv4_synch.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern void insert_scanned_compaction_objects_into_compaction_blocks(GC_Thread *);
extern void allocate_forwarding_pointers_for_compaction_live_objects(GC_Thread *);
#ifndef GC_NO_FUSE
// extern void allocate_forwarding_pointers_for_colocated_live_objects(GC_Thread *);
#endif

extern void fix_slots_to_compaction_live_objects(GC_Thread *);
extern void	slide_cross_compact_live_objects_in_compaction_blocks(GC_Thread *);
extern void move_colocated_objects_into_place(GC_Thread *gc_thread);

// extern void slide_compact_live_objects_in_same_compaction_blocks(GC_Thread *);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern bool mark_scan_load_balanced;
extern bool sweeps_during_gc;
extern bool g_gen;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1 //def USE_BEGINTHREADEX
unsigned int PRT_STDCALL gc_thread_func (void *);
#else
DWORD WINAPI gc_thread_func(LPVOID);
#endif
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GC_Thread::GC_Thread(Garbage_Collector *p_gc, unsigned int gc_thread_id) {
	assert(p_gc);
	_p_gc = p_gc;

	_id = gc_thread_id;

	// Create a mark stack per thread and zero it out.
#ifdef ONE_MARK_STACK
	_mark_stack = (MARK_STACK *) malloc(sizeof(MARK_STACK));
#else
#ifdef STL_MARK_STACK
	_mark_stack = new std::stack<Partial_Reveal_Object*,std::vector<Partial_Reveal_Object*> >;
#else
	_mark_stack = new MARK_STACK;
#endif
#endif
	zero_out_mark_stack(_mark_stack);
	
	if (g_gen || sweeps_during_gc) {
		_sweep_start_index = -1;
		_num_chunks_to_sweep = -1;
		zero_out_sweep_stats((chunk_sweep_stats *) &(_sweep_stats));
	}
		
	//////////////////
     _gc_thread_start_work_event = orp_synch_create_event(FALSE);  // flag for manual-reset event  -- auto reset mode 
										
	assert(_gc_thread_start_work_event);
	Boolean rstat = orp_synch_reset_event(_gc_thread_start_work_event);
	assert(rstat);


	//////////////////
    _gc_thread_work_done_event = orp_synch_create_event(FALSE);  // flag for manual-reset event  -- auto reset mode 
	assert(_gc_thread_work_done_event);

	rstat = orp_synch_reset_event(_gc_thread_work_done_event);
	assert(rstat);

    started = false;

    _thread_handle = orp_thread_create(  (pgc_thread_func)gc_thread_func,
                                    (void*)this,
                                    (unsigned int *) &(_thread_id));

    while(!started) {
#ifdef USE_PTHREADS
        sched_yield();
#else  // USE_PTHREADS
        mcrtThreadYield();
#endif // USE_PTHREADS
    }

	if (_thread_handle == NULL) { 
		orp_cout << "GC_Thread::GC_Thread(..): CreateThread() failed...exiting...\n";
		orp_exit(17010);
	}

#ifdef _DEBUG
	_marked_objects = new std::vector<Partial_Reveal_Object*>;
#else  // _DEBUG
    _marked_objects = NULL;
#endif // _DEBUG
    _weak_slots = new std::vector<Partial_Reveal_Object**>;

	_num_marked_objects = 0;
	_marked_object_size = 0;
	
	// Reset every GC
	_num_bytes_recovered_by_sweep = 0;

	_input_packet = NULL;

	_output_packet = NULL;

///////////////////////////////////////////////////	
	_compaction_turned_on_during_this_gc = false;
	_num_slots_collected_for_later_fixing = 0;
}

GC_Thread::~GC_Thread() {
    assert (0); // Stop and debug the system is going down if we are deleting gc threads.....
}


void GC_Thread::reset(bool compact_this_gc) {
	// There is no need to zero the mark stack between GCs since it is cleared each time a POP is done
#ifdef _DEBUG
	check_mark_stack_is_all_null(_mark_stack);
#endif

	if (g_gen || sweeps_during_gc) {
		_sweep_start_index = -1;
		_num_chunks_to_sweep = -1;
		zero_out_sweep_stats((chunk_sweep_stats *) &(_sweep_stats));
	}

	Boolean rstat = orp_synch_reset_event(_gc_thread_start_work_event);
	assert(rstat);
	rstat = orp_synch_reset_event(_gc_thread_work_done_event);
	assert(rstat);
	// Need to zero out only as much as occupied from last GC
#ifdef _DEBUG
	_marked_objects->clear();
#endif // _DEBUG
	_num_marked_objects = 0;
    _marked_object_size = 0;
	_num_bytes_recovered_by_sweep = 0;

////////////////////////////////
	_compaction_turned_on_during_this_gc = compact_this_gc;
	_num_slots_collected_for_later_fixing = 0;
} // GC_Thread::reset



void GC_Thread::wait_for_work() {
	unsigned int wstat = orp_synch_wait_for_event(_gc_thread_start_work_event, INFINITE);		
	assert(wstat != EVENT_WAIT_FAILED);
} // GC_Thread::wait_for_work



void GC_Thread::signal_work_is_done() {
	// I am done with my job. SIGNAL MASTER THREAD
	Boolean sstat = orp_synch_set_event(_gc_thread_work_done_event);
	assert(sstat);
} // GC_Thread::signal_work_is_done


volatile POINTER_SIZE_INT dummy_for_good_cache_performance = 0;				


#if 1 //def USE_BEGINTHREADEX
unsigned int PRT_STDCALL gc_thread_func (void *arg)
#else
DWORD WINAPI 
gc_thread_func(LPVOID arg)
#endif
{
    orp_set_affinity();

	GC_Thread *p_gc_thread = (GC_Thread *) arg; 
    p_gc_thread->started = 1;
	assert(p_gc_thread);
#ifdef _WINDOWS
	LARGE_INTEGER task_start_time, task_end_time;
#elif defined LINUX
	struct timeval task_start_time, task_end_time;
#endif

	while(true) {
		//I will go to sleep forever or until the next time I am woken up with work
		p_gc_thread->wait_for_work();
		// When thread is woken up, it has work to do.
		assert(p_gc_thread->get_task_to_do() != GC_BOGUS_TASK);

		if (p_gc_thread->get_task_to_do() == GC_MARK_SCAN_TASK) {
			gc_time_start_hook(&task_start_time);

            p_gc_thread->clear_weak_slots();

			if (mark_scan_load_balanced) {
				p_gc_thread->_p_gc->mark_scan_pools(p_gc_thread);
			} else {
				mark_scan_heap(p_gc_thread);
			}

            p_gc_thread->m_global_marks.clear(); // clear list of marks for global objects

			if (stats_gc) {
#if 1 // def USE_BEGINTHREADEX
                printf ("%d: %u objects %u size\n",  (POINTER_SIZE_INT)p_gc_thread->get_thread_handle(), p_gc_thread->get_num_marked_objects(), p_gc_thread->get_marked_object_size() );
#else
                printf ("%p: %u objects\n",  p_gc_thread->get_thread_handle(), p_gc_thread->get_num_marked_objects() );
#endif
                // orp_cout << p_gc_thread->get_thread_handle() << ": " << p_gc_thread->get_num_marked_objects() << " objects ";
			}
			gc_time_end_hook(": GC_MARK_SCAN_TASK ", &task_start_time, &task_end_time, stats_gc ? true: false);

		} else if (p_gc_thread->get_task_to_do() == GC_SWEEP_TASK) {

			if (g_gen || sweeps_during_gc) {
				// Sweep part of the heap.
				assert(p_gc_thread->get_num_bytes_recovered_by_sweep() == 0);
				p_gc_thread->set_num_bytes_recovered_by_sweep(0);
                p_gc_thread->sweeping_stats.reset();
				gc_time_start_hook(&task_start_time);
				unsigned int bytes_recovered = p_gc_thread->_p_gc->sweep_heap(p_gc_thread,p_gc_thread->sweeping_stats);
				p_gc_thread->set_num_bytes_recovered_by_sweep(bytes_recovered);
				if (stats_gc) {
					orp_cout << p_gc_thread->get_thread_handle() << " recovered " << bytes_recovered << "bytes";
				}
				gc_time_end_hook(": GC_SWEEP_TASK ", &task_start_time, &task_end_time, stats_gc ? true: false);
			} else {
				// BAD!!!!!
				assert(0);
				orp_cout << "SWEEP_TASK not expected during GC in the configuration in which you have built GC code" << std::endl;
				orp_exit(17011);
			}

		}  else if (p_gc_thread->get_task_to_do() == GC_OBJECT_HEADERS_CLEAR_TASK) {

  		    assert(0);
			orp_cout << "BAD GC configuration\n";
			orp_exit(17011);
			
		} else if (p_gc_thread->get_task_to_do() == GC_INSERT_COMPACTION_LIVE_OBJECTS_INTO_COMPACTION_BLOCKS_TASK) {

            assert(0);
			orp_cout << "BAD GC configuration\n";
			orp_exit(17012);
            
		} else if (p_gc_thread->get_task_to_do() == GC_ALLOCATE_FORWARDING_POINTERS_FOR_COMPACTION_LIVE_OBJECTS_TASK) {
            
			allocate_forwarding_pointers_for_compaction_live_objects(p_gc_thread);

		} else if (p_gc_thread->get_task_to_do() == GC_FIX_SLOTS_TO_COMPACTION_LIVE_OBJECTS_TASK) {

			fix_slots_to_compaction_live_objects(p_gc_thread);

		} else if (p_gc_thread->get_task_to_do() == GC_SLIDE_COMPACT_LIVE_OBJECTS_IN_COMPACTION_BLOCKS) {

        	slide_cross_compact_live_objects_in_compaction_blocks(p_gc_thread);

		} else if (p_gc_thread->get_task_to_do() == GC_CLEAR_MARK_BIT_VECTORS) {

			p_global_gc->parallel_clear_mark_bit_vectors(p_gc_thread->get_id());

		} else {
			assert(0);
		}
	
		// Work is done!!!
		p_gc_thread->signal_work_is_done();

	}

	assert(0);
	return 0;
}

