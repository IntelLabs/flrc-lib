/*
 * COPYRIGHT_NOTICE_1
 */

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
#include "tgc/gc_thread.h"
#include "tgc/mark.h"
#include "tgc/descendents.h"
#include "tgc/micro_nursery.h"
#include "pgc/pgc.h"
#include <fstream>
#include "tgc/gcv4_synch.h"
#include <strstream>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Garbage_Collector *p_global_gc = NULL;
// The performance frequency of the high-resolution performance counter.
#ifdef _WINDOWS
LARGE_INTEGER performance_frequency;
#endif // _WINDOWS
// Aug. 1, 2003. Changed by RLH to true since that seems the more reasonable default even if it does
// slow done javac and some of the specjvm numbers a bit.
bool fullheapcompact_at_forcegc = true;
bool incremental_compaction = true;
unsigned ephemeral_collection = 0;
#ifdef _DEBUG
bool verify_gc = false;
//bool verify_gc = true;
#else
bool verify_gc = false;
#endif
bool machine_is_hyperthreaded = false;
bool mark_scan_load_balanced = false;
bool verify_live_heap = false;
bool single_threaded_gc = false;
bool use_large_pages = false;
bool sweeps_during_gc = false;
bool randomize_roots = false;
bool parallel_clear = false;
extern bool use_pillar_watermarks;
extern bool separate_immutable;
#ifdef PUB_PRIV
extern "C" unsigned g_use_pub_priv;
#endif // PUB_PRIV
extern bool pn_history;
bool g_show_all_pncollect = false;
bool g_dump_rs_vtables = false;
bool g_treat_wpo_as_normal = false;
#ifdef CONCURRENT
bool g_cheney = false;
#else
bool g_cheney = true;
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern void init_verify_live_heap_data_structures();
extern void take_snapshot_of_lives_before_gc(unsigned int, Object_List *);
extern void verify_live_heap_before_and_after_gc(unsigned int, Object_List *);
void process_object(Partial_Reveal_Object *p_obj, GC_Thread *gc_thread, MARK_STACK *ms);
void process_mark_stack(GC_Thread *gc_thread, MARK_STACK *ms);
void scan_one_slot (Slot p_slot, GC_Thread *gc_thread, bool is_weak, Partial_Reveal_Object *p_slot_obj);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef CONCURRENT_DEBUG_2
extern FILE *cgcdump;
#endif
//bool is_object_pointer(Partial_Reveal_Object *p_obj);
bool g_gen = false;
float g_full_gc_trigger_in_gen = 0.2;
bool g_gen_all = false; // false = young gen collected, true = young+old collected
extern unsigned g_remove_indirections;
#ifdef _DEBUG
extern bool g_profile;
extern std::ofstream * profile_out;
extern std::ofstream * live_dump;
#endif

unsigned int g_num_cpus = 0;
unsigned int g_root_index_hint;
float g_regular_block_ratio = 0.0;

#ifdef IGNORE_SOME_ROOTS
extern unsigned num_roots_ignored;
extern unsigned roots_to_ignore;
#endif // IGNORE_SOME_ROOTS

extern unsigned local_nursery_size;
bool adaptive_nursery_size = false;
bool g_two_space_pn = false;
bool g_pure_two_space = false;
extern bool g_zero_dead;
bool g_prevent_rs = false;

bool concurrent_torture = true; // other mode doesn't work right now

#ifdef CONCURRENT
unsigned int PRT_STDCALL gc_reclamation_func(void *arg);
SynchCriticalSectionHandle concurrent_gc_state_cs;
enum CONCURRENT_GC_STATE g_concurrent_gc_state = CONCURRENT_IDLE;
std::deque<Partial_Reveal_Object*> g_concurrent_gray_list;
volatile void *g_sweep_ptr=NULL;
volatile unsigned concurrent_gc_thread_id;
volatile bool stop_concurrent = false;
extern unsigned active_thread_count;
unsigned g_concurrent_transition_wait_time = 0;

#ifdef _DEBUG
std::set<Partial_Reveal_Object*> g_remembered_lives;
#endif // _DEBUG

SynchCriticalSectionHandle moved_objects_cs;
bimap<Partial_Reveal_Object*,ImmutableMoveInfo> g_moved_objects;

bool concurrent_mode = true;
extern unsigned max_pause_us;
unsigned g_num_blocks_available;
extern unsigned start_concurrent_gc;
unsigned g_concurrent_gc_max = 0;
#else  // CONCURRENT
bool concurrent_mode = false;
#endif // CONCURRENT

bool g_num_threads_specified = false;
int  g_num_threads = 0;
bool g_keep_mutable = false;
bool g_keep_mutable_closure = false;
bool g_keep_all = false;
bool g_determine_dead_rs = false;

Partial_Reveal_VTable *wpo_vtable      = NULL;
void (*wpo_finalizer_callback)(void *) = NULL;

#ifdef _DEBUG
unsigned indirections_removed = 0;
#endif

#ifdef _WINDOWS
typedef LARGE_INTEGER TIME_STRUCT;
#elif defined LINUX
typedef struct timeval TIME_STRUCT;
#endif

GCEXPORT(void, gc_set_wpo_vtable)(void *vt) {
    wpo_vtable = (Partial_Reveal_VTable*)vt;
}

GCEXPORT(void, gc_set_wpo_finalizer)(void (*finalizer)(void *)) {
    wpo_finalizer_callback = finalizer;
}

unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt);

Garbage_Collector::Garbage_Collector(POINTER_SIZE_INT initial_heap_size, POINTER_SIZE_INT final_heap_size, unsigned int block_size_bytes) {
    ///////////////////////////////////
    _free_chunks_end_index = -1;
    _los_lock = 0;
    _wpo_lock = 0;
    _los_blocks = NULL;
    _single_object_blocks = NULL;

    _gc_threads = NULL;
    _gc_num = 0;
    _total_gc_time = 0;
    _gc_num_time = 0;
    _p_block_store = NULL;
    _gc_thread_work_finished_event_handles = NULL;

    memset(_gc_chunks, 0, sizeof(chunk_info) * GC_MAX_CHUNKS);

    num_root_limit = 100000; // initial array size
    _array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * num_root_limit);
    _save_array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * num_root_limit);
    _verify_array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * num_root_limit);

    memset(_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * num_root_limit);
    dup_removal_enum_hash_table = new Hash_Table();
    assert (dup_removal_enum_hash_table->is_empty());

    _num_roots = 0;

    _p_block_store  = new Block_Store(initial_heap_size, final_heap_size, block_size_bytes);

#ifdef CONCURRENT
    g_num_blocks_available = _p_block_store->get_num_total_blocks_in_block_store();
#endif // CONCURRENT

    _gc_thread_work_finished_event_handles = (SynchEventHandle *) malloc(sizeof(SynchEventHandle) * g_num_cpus);

    if (mark_scan_load_balanced) {
        _mark_scan_pool = new Work_Packet_Manager();
    } else {
        _mark_scan_pool= NULL;
    }

    last_young_gen_percentage = 1.0; // first generational collect is always just the young generation.

    //// V E R I F Y /////////////
    _num_live_objects_found_by_first_trace_heap = 0;
    _live_objects_found_by_first_trace_heap = NULL;
    _num_live_objects_found_by_second_trace_heap = 0;
    _live_objects_found_by_second_trace_heap = NULL;

    memset(_verify_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * num_root_limit);

    protect_roots = orp_synch_create_critical_section();
    assert(protect_roots);
    protect_short_weak_roots = orp_synch_create_critical_section();
    assert(protect_short_weak_roots);
    protect_long_weak_roots = orp_synch_create_critical_section();
    assert(protect_long_weak_roots);
    protect_pinned_blocks = orp_synch_create_critical_section();
    assert(protect_pinned_blocks);

#ifdef CONCURRENT
    concurrent_gc_state_cs = orp_synch_create_critical_section();
    assert(concurrent_gc_state_cs);

    moved_objects_cs = orp_synch_create_critical_section();
    assert(moved_objects_cs);
#endif // CONCURRENT
}


Garbage_Collector::~Garbage_Collector() {
    assert(0);
    delete dup_removal_enum_hash_table;
    orp_synch_delete_critical_section(protect_roots);
    orp_synch_delete_critical_section(protect_short_weak_roots);
    orp_synch_delete_critical_section(protect_long_weak_roots);
    orp_synch_delete_critical_section(protect_pinned_blocks);
}

/*
 * If an exception is thrown between the time we acquire the lock and we set it to 0 then
 * the lock will never be freed.  An exception should not occur here so we should be OK
 * but is it ok to use try{}catch{} to make sure?
 */
void Garbage_Collector::add_finalize_object(Partial_Reveal_Object *p_object,bool check_for_duplicates) {
    std::list<Partial_Reveal_Object*>::iterator finalizer_iter;

    // take the finalize list lock
    while ((LONG) InterlockedCompareExchange( (LONG *)(&m_finalize_list_lock), (LONG) 1, (LONG) 0) == (LONG) 1);

    // Sometimes we know that the object can't already be in this list (for example,
    // if we have just allocated it) so we only have to check for duplicates if there is
    // the possibility of a duplication.  Such duplication is possible if the user has
    // triggered this function through the CLI re-register for finalization call.
    if ( check_for_duplicates ) {
        // iterate through the list of finalizer objects
        for(finalizer_iter  = m_listFinalize->begin();
            finalizer_iter != m_listFinalize->end();
            ++finalizer_iter) {
            // if we found the object identical to the one they are trying to add
            // then the object cannot be added to the list again.  This maintains
            // the invariant that an object can only appear in this list once.
            if (*finalizer_iter == p_object) {
                // release the finalize list lock
                m_finalize_list_lock = 0;
                return;
            }
        }
    }

    // It must have not been in the list yet so we can safely add it here.
    m_listFinalize->push_back(p_object);
    // release the finalize list lock
    m_finalize_list_lock = 0;
}

/*
 * If an exception is thrown between the time we acquire the lock and we set it to 0 then
 * the lock will never be freed.  An exception should not occur here so we should be OK
 * but is it ok to use try{}catch{} to make sure?
 */
void Garbage_Collector::remove_finalize_object(Partial_Reveal_Object *p_object) {
    std::list<Partial_Reveal_Object*>::iterator finalizer_iter;

    // take the finalize list lock
    while ((LONG) InterlockedCompareExchange( (LONG *)(&m_finalize_list_lock), (LONG) 1, (LONG) 0) == (LONG) 1);

    // iterate through the list of finalizer objects
    for(finalizer_iter  = m_listFinalize->begin();
        finalizer_iter != m_listFinalize->end();
        ++finalizer_iter) {
        // if we found the object that needs to be removed
        if (*finalizer_iter == p_object) {
            // remove the object from the list
            m_listFinalize->erase(finalizer_iter);
            // INVARIANT: add_finalize_object() checks to make sure only one
            // of each object can be on the finalizer list at a time so if
            // we find one here we can be guaranteed there are no more and it
            // is safe to return.
            // release the finalize list lock
            m_finalize_list_lock = 0;
            return;
        }
    }

    // release the finalize list lock
    m_finalize_list_lock = 0;
}

void Garbage_Collector::gc_v4_init() {
    assert(_p_block_store);
    unsigned int total_blocks = _p_block_store->get_num_total_blocks_in_block_store();

    // Lets allocated 80% of blocks into chunks for starts
    unsigned int num_chunks_to_allocated = (((unsigned int)(0.8 * total_blocks)) / GC_NUM_BLOCKS_PER_CHUNK);


    for (int i = 0; i < GC_MAX_CHUNKS; i++) {
        memset(&(_gc_chunks[i]), 0, sizeof(chunk_info));
    }

    if(!concurrent_mode) {
        init_gc_threads();
    } else {
//        SetPriorityClass(GetCurrentProcess(),0x100);
    }

    roots_init();

    gc_add_fresh_chunks(num_chunks_to_allocated);

#ifdef _WINDOWS
    QueryPerformanceFrequency(&performance_frequency);
#endif // _WINDOWS

    if (incremental_compaction && verbose_gc) {
        orp_cout << "GC will incrementally compact the heap every collection" << std::endl;
    }

    m_listFinalize = new std::list<Partial_Reveal_Object*>;
    m_finalize_list_lock = 0;


#ifdef CONCURRENT
    if (orp_thread_create(gc_reclamation_func,
        (void *)this,
        (unsigned int *)&concurrent_gc_thread_id) == NULL) {
        orp_cout << "Could not create the GC concurrent reclamation thread...exiting...\n";
        orp_exit(17015);
    }
    if(!concurrent_gc_thread_id) {
        orp_cout << "Concurrent GC thread was created but returned a zero thread id...exiting...\n";
        orp_exit(17015);
    }
#endif // CONCURRENT
}

//
// Retrieves a fresh chunk from the block store. If if is for object placement then
// stay_above_water_line will be false, if it is for a chunk to be allocated
// stay_above_water_line will the true.
//

// GC_FUSE added the stay_above_water_line that is passed to p_get_new_block without
// wrapping it in an #ifdef GC_FUSE.

void Garbage_Collector::uncleared_to_free_block(block_info *block) {
    //assert (block->is_free_block == false);        // // RLH Aug 04

    gc_trace_block (block, " get_fresh_chunk_from_block_store gets this block");
    block->in_nursery_p = true;
    assert(block->age == 0);
    block->in_los_p = false;
    block->is_single_object_block = false;

    block->set_nursery_status(block->get_nursery_status(),free_nursery);
    block->thread_owner = NULL;

    // Allocate free areas per block if not already allocated
    if (!block->block_free_areas)  {
        block->size_block_free_areas = GC_MAX_FREE_AREAS_PER_BLOCK(GC_MIN_FREE_AREA_SIZE);
        block->block_free_areas = (free_area *)malloc(sizeof(free_area) * block->size_block_free_areas);
        assert(block->block_free_areas);
    }

    memset(block->mark_bit_vector, 0, GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);

    // It is fully allocatable
    // RLH Removed Aug 04 block->is_free_block = true; ???
    //assert (block->is_free_block == false); // RLH Aug 04
    // Initialize free areas for block....
    gc_trace_block (block, " calling clear_block_free_areas in get_fresh_chunk_from_block_store.");
    clear_block_free_areas(block);

    // JUST ONE LARGE FREE AREA initially
    free_area *area = &(block->block_free_areas[0]);
    area->area_base = GC_BLOCK_ALLOC_START(block);
    area->area_ceiling = (void *)((uintptr_t)GC_BLOCK_ALLOC_START(block) + (POINTER_SIZE_INT)GC_BLOCK_ALLOC_SIZE - 1);
    area->area_size = GC_BLOCK_ALLOC_SIZE;

    area->has_been_zeroed = false;

    block->num_free_areas_in_block = 1;

#ifndef GC_SLOW_ALLOC
    block->current_alloc_area = -1;
#else
    block->current_alloc_area = 0;
#endif

    //        if (!sweeps_during_gc) {
    // Needed....This block is fresh from the block store...its ONLY allocation area has
    // been determined above. Allocator should not try to sweep it since it is meaningless...
    // (since it was not collectable during the previous GC cycle
    block->block_has_been_swept = true;
    //        }

    // Start allocation in this block at the base of the first and only area
    block->curr_free = area->area_base;
    block->curr_ceiling = area->area_ceiling;
}

block_info * Garbage_Collector::get_fresh_chunk_from_block_store(bool stay_above_water_line) {
    block_info *chunk_start = NULL;

    for (int j = 0; j < GC_NUM_BLOCKS_PER_CHUNK; j++) {

        block_info *block = _p_block_store->p_get_new_block(stay_above_water_line);
        if (block == NULL) { // Block store couldnt satisfy request.

            if (chunk_start) {
                // Return all previously acquired blocks of this partial chunk to the block store.
                block = chunk_start;
                while (block) {
                    block_info *block_next = block->next_free_block;
                    assert(block->number_of_blocks == 1);
                    _p_block_store->link_free_blocks(block, block->number_of_blocks);
                    block = block_next;
                }
                return NULL;
            }
            return chunk_start; // NULL
        }

        uncleared_to_free_block(block);

        // Insert new block into chunk
        block->next_free_block = chunk_start;
        chunk_start = block;
    }

    return chunk_start;
}


// used as a hint and shared by all threads...
static volatile unsigned int chunk_index_hint = 0;


block_info * Garbage_Collector::get_free_chunk_from_global_gc_chunks(void *new_thread_owner, bool desired_chunk_mutability) {
    block_info *fresh_chunk = NULL;
    bool search_from_hint_failed = false;
    int chunk_index = 0;

retry_get_free_chunk_label:
    if (!search_from_hint_failed) {
        chunk_index = chunk_index_hint;
    } else {
        chunk_index = 0;
    }
    // Search available chunks and grab one atomically
    while (chunk_index <= _free_chunks_end_index) {
        chunk_info *curr_chunk = &(_gc_chunks[chunk_index]);
        if (curr_chunk) {
            volatile void *free_val = curr_chunk->free_chunk;
            fresh_chunk = NULL;
            if (free_val != NULL) {
                assert(curr_chunk->chunk->block_free_areas);
                if(g_gen) {
                    if(curr_chunk->chunk->is_empty()) {
                        // Duplicate pointers to the same free chunk
                        assert(free_val == curr_chunk->chunk);
                        // attempt to grab free chunk
                        fresh_chunk = (block_info *)InterlockedCompareExchangePointer((PVOID *)&(curr_chunk->free_chunk), (PVOID) NULL, (PVOID)free_val);
                        if (fresh_chunk == (block_info *)free_val) {
                            // success.. I own chunk at chunk_index.
                            // update my hint for my search next time...
                            fresh_chunk->generation = 0;
                            chunk_index_hint = chunk_index;
                            break;
                        }
                    }
                } else {
                    if((curr_chunk->chunk->block_contains_only_immutables == desired_chunk_mutability &&
                        curr_chunk->chunk->thread_owner == new_thread_owner) ||
                        curr_chunk->chunk->block_free_areas[0].area_size == GC_BLOCK_ALLOC_SIZE) {
                        // Duplicate pointers to the same free chunk
                        assert(free_val == curr_chunk->chunk);
                        // attempt to grab free chunk
                        fresh_chunk = (block_info *)InterlockedCompareExchangePointer((PVOID *)&(curr_chunk->free_chunk), (PVOID) NULL, (PVOID)free_val);
                        if (fresh_chunk == (block_info *)free_val) {
#ifdef CONCURRENT
                            // We can only ask for an immutable nursery in the context of a concurrent GC.
                            // This check will prevent the GC from trying to use the current block being swept
                            // as the new copy_to immutable nursery.
                            if(desired_chunk_mutability == true && fresh_chunk->get_nursery_status() == concurrent_sweeper_nursery) {
                                curr_chunk->free_chunk = NULL;
                            } else {
                                if(fresh_chunk->set_nursery_status(free_nursery,active_nursery) == true) {
                                    fresh_chunk->block_contains_only_immutables = desired_chunk_mutability;
                                    --g_num_blocks_available;
                                    // success.. I own chunk at chunk_index.
                                    // update my hint for my search next time...
                                    chunk_index_hint = chunk_index;
                                    break;
                                } else {
                                    curr_chunk->free_chunk = NULL;
                                }
                            }
#else // !CONCURRENT
                            // success.. I own chunk at chunk_index.
                            // update my hint for my search next time...
                            chunk_index_hint = chunk_index;
                            break;
#endif // !CONCURRENT
                        }
                    } else {
    //                    printf("============ Mutability problem!!!! ==============\n");
    //                    printf("%d %d\n",curr_chunk->chunk->block_free_areas[0].area_size,GC_BLOCK_ALLOC_SIZE);
                        assert(1); // something to set a breakpoint on is all
                    }
                }
            }
        }
        chunk_index++;
    }

    if ((fresh_chunk == NULL) && (search_from_hint_failed == false)) {
        // Try again...exactly once more....this time from index 0;
        search_from_hint_failed = true;
        goto retry_get_free_chunk_label;
    }

    // Better have got some chunk...otherwise may need to cause GC
    if (fresh_chunk) {
        assert(chunk_index <= _free_chunks_end_index);
        // I OWN chunk at "chunk_index"
        assert(fresh_chunk == _gc_chunks[chunk_index].chunk);
#ifndef CONCURRENT
        // It better be free
        if(fresh_chunk->get_nursery_status() != free_nursery) {
            assert(0);
        }
#endif // CONCURRENT
    } else {
        assert(chunk_index == _free_chunks_end_index + 1);
    }
    return fresh_chunk;
}



//
// THIS FUNCTION is called from only inside a critical section or at gc_init()
// so it is thread safe. It gets new blocks from the block store and creates
// chunks out of them and adds them to "global_gc_chunks"
//
// Secondarily it can be called from p_cycle_chunk
// Then the gc lock has been taken before this is called.
// So it is thread safe again. Multiple threads cant try to add fresh chunks from
// the block store at the same time. The first one that fails would cause GC,
// and the rest of the threads that block on the lock would retry to get a
// chunk before doing anything else....
//
void Garbage_Collector::gc_add_fresh_chunks(unsigned int num_chunks_to_add) {
    unsigned int empty_chunk_index = 0;
    unsigned int num_chunks_added = 0;

    while (num_chunks_added < num_chunks_to_add) {

        while (_gc_chunks[empty_chunk_index].chunk != NULL) {
            empty_chunk_index++;
        }
        assert(empty_chunk_index < GC_MAX_CHUNKS);

        block_info *chunk = get_fresh_chunk_from_block_store(true); // Stay above the waterline.

        if (chunk != NULL) {
            assert(_gc_chunks[empty_chunk_index].chunk == NULL);
            assert(_gc_chunks[empty_chunk_index].free_chunk == NULL);
            _gc_chunks[empty_chunk_index].chunk = _gc_chunks[empty_chunk_index].free_chunk = chunk;
            num_chunks_added++;
            empty_chunk_index++;
            continue;
        }

        if (num_chunks_added > 0) {
            // I could successfully add some chunks....lets postpone GC
            break;
        }

        assert(num_chunks_added == 0);

        // All chunks that were cleaned up during the last GC have been exhausted.
        if (stats_gc) {
            // get_fresh_chunk_from_block_store() failed
            orp_cout << "get_fresh_chunk_from_block_store() failed...calling reclaim_full_heap(..)" << std::endl;
            fflush(stdout);
        }

        reclaim_full_heap(0, false, false);
        // GC should have made many chunks available...lets not do anythin now.
        // No need to adjust the chunk limits.

        return;
    }
    while (_gc_chunks[empty_chunk_index].chunk != NULL) {
        empty_chunk_index++;
    }
    if ((int) empty_chunk_index > _free_chunks_end_index) {
        _free_chunks_end_index = empty_chunk_index;
        if (stats_gc) {
            orp_cout << "#allocatable chunks = " << _free_chunks_end_index + 1 << std::endl;
        }
        //        printf("$\n");
    }
}

#ifdef CONCURRENT

#define WMA_FACTOR 0.97

class CircularTimeBuffer {
protected:
    double   m_wma;
    TIME_STRUCT last_timestamp;
    unsigned first_time;
public:
    CircularTimeBuffer() :
        m_wma(0.0),
        first_time(0) {
    }

    ~CircularTimeBuffer(void) {}

    // returns the number of blocks allocated per millisecond or 0 if min_size has not been reached.
    double add(void) {
        TIME_STRUCT cur_time;
        gc_time_start_hook(&cur_time);

        if(first_time < 10) {
            ++first_time;
        } else {
            if(first_time < 20) {
                ++first_time;
            }
            m_wma = (m_wma * WMA_FACTOR) + ((1.0 - WMA_FACTOR) * get_time_in_microseconds(last_timestamp,cur_time));
        }
        last_timestamp = cur_time;

        if(first_time < 20) return 0.0;

        return 1000.0 / m_wma; // returns blocks allocated per millisecond
    }
};

CircularTimeBuffer get_nursery_times;

#endif // CONCURRENT

PrtTaskHandle active_gc_thread = 0;

void get_chunk_lock(struct GC_Thread_Info *gc_info) {
    if(gc_info) {
//        gc_info->m_entering_gc_lock = true;
        if(local_nursery_size) {
            pn_info *local_collector = gc_info->get_private_nursery()->local_gc_info;
            // we got here via a task split callback

#if 0
            // BTL 20090213 Debug
            printf(">>>> get_chunk_lock: gc_info=%p, loc_coll=%p, loc_coll->m_original_task=%p, act_gc_th=%p\n",
                   gc_info, local_collector,
                   (local_collector? local_collector->m_original_task : (struct PrtTaskStruct *)0xDEADBEEF),
                   active_gc_thread);
            fflush(stdout);
#endif //0

#ifdef HAVE_PTHREAD_H
            orp_synch_enter_critical_section(g_chunk_lock);
            return;
#else // HAVE_PTHREAD_H
            if(local_collector && local_collector->m_original_task) {
                while(1) {
                    if(active_gc_thread) {
                        prtHandoffStackLock(local_collector->m_original_task, active_gc_thread);
//                        prtHandoffStackLock(local_collector->m_new_task, active_gc_thread);
                        prtHandoffReacquireStackLock(local_collector->m_original_task);
//                        prtHandoffReacquireStackLock(local_collector->m_new_task);
                    }
                    McrtSyncResult res;
                    res = mcrtMonitorTryEnter((McrtMonitor*)g_chunk_lock);
                    if(res == Success) return;
                }
            }
#endif // HAVE_PTHREAD_H
        }
    }
    orp_synch_enter_critical_section(g_chunk_lock);
}

void release_chunk_lock(struct GC_Thread_Info *gc_info) {
    orp_synch_leave_critical_section(g_chunk_lock);
#if 0
    if(gc_info) {
        gc_info->m_entering_gc_lock = false;
    }
#endif
}

//
// The Thread just ran out of another chunk. Give it a fresh one.
//
block_info * Garbage_Collector::p_cycle_chunk(block_info *p_used_chunk,
                                 bool returnNullOnFail,
                                 bool immutable,
                                 void *new_thread_owner,
                                 GC_Thread_Info *current_thread) {
    if (p_used_chunk) {
        block_info *spent_block = p_used_chunk;

        while (spent_block) {
            assert(spent_block);
            assert(spent_block->get_nursery_status() == active_nursery);
            spent_block->set_nursery_status(active_nursery,spent_nursery);
            spent_block = spent_block->next_free_block;
        }
    }

    block_info *free_chunk = NULL;

#ifdef CONCURRENT
    // All threads rushing to try to add fresh chunks will stop here.  This takes
    // pressure off the GC lock and makes plugging the GC into other runtimes easier.
    get_chunk_lock(current_thread);
    orp_gc_lock_enum();

    double allocate_rate = get_nursery_times.add();
    if(allocate_rate != 0) {
        if((g_num_blocks_available - 100) / allocate_rate < (float)(g_concurrent_gc_max == 0 ? 5000 : g_concurrent_gc_max)) {
            start_concurrent_gc = 1;
        }
    }

    // Check to see if someone has beaten me to this and
    // added free chunks to the global chunk store

    free_chunk = get_free_chunk_from_global_gc_chunks(new_thread_owner,immutable);
    unsigned loop_max = 10;

    if(returnNullOnFail && free_chunk == NULL) {
        orp_gc_unlock_enum();
        release_chunk_lock(current_thread);
        return NULL;
    }

    while (free_chunk == NULL) {
        // Add fresh chunks from the global block_store.USE ALL OF THEM???
        unsigned int free_blocks_in_bs = _p_block_store->get_num_free_blocks_in_block_store();
        // Add at least one...otherwise no forward progress is made
        unsigned int chunks_to_add = (free_blocks_in_bs > GC_NUM_BLOCKS_PER_CHUNK) ? (free_blocks_in_bs / GC_NUM_BLOCKS_PER_CHUNK) : 1;

		if(g_regular_block_ratio != 0.0) {
			chunks_to_add = (unsigned int)(chunks_to_add * g_regular_block_ratio);
			if(chunks_to_add == 0) {
				chunks_to_add = 1;
			}
		}
        gc_add_fresh_chunks(chunks_to_add);

        // Above function may have caused GC if block store was empty.
        // So, we can try again with a very high probability of success.
        free_chunk = get_free_chunk_from_global_gc_chunks(new_thread_owner,immutable);

        loop_max--;
        if(loop_max == 0) {
            printf("10 successive GCs in p_cycle_chunk without freeing room to allocate memory.\n");
            exit(17014);
        }
    }

    orp_gc_unlock_enum();
    release_chunk_lock(current_thread);
#else  // CONCURRENT

    free_chunk = get_free_chunk_from_global_gc_chunks(new_thread_owner);

    if (free_chunk == NULL) {
        if(returnNullOnFail) {
            return NULL;
        }

        // All threads rushing to try to add fresh chunks will stop here.  This takes
        // pressure off the GC lock and makes plugging the GC into other runtimes easier.
        get_chunk_lock(current_thread);
        orp_gc_lock_enum();

        // Check to see if someone has beaten me to this and
        // added free chunks to the global chunk store

        free_chunk = get_free_chunk_from_global_gc_chunks(new_thread_owner);
        unsigned loop_max = 10;

        while (free_chunk == NULL) {
            // Add fresh chunks from the global block_store.USE ALL OF THEM???
            unsigned int free_blocks_in_bs = _p_block_store->get_num_free_blocks_in_block_store();
            // Add at least one...otherwise no forward progress is made
            unsigned int chunks_to_add = (free_blocks_in_bs > GC_NUM_BLOCKS_PER_CHUNK) ? (free_blocks_in_bs / GC_NUM_BLOCKS_PER_CHUNK) : 1;

			if(g_regular_block_ratio != 0.0) {
				chunks_to_add = (unsigned int)(chunks_to_add * g_regular_block_ratio);
				if(chunks_to_add == 0) {
					chunks_to_add = 1;
				}
			}
            gc_add_fresh_chunks(chunks_to_add);

            // Above function may have caused GC if block store was empty.
            // So, we can try again with a very high probability of success.
            free_chunk = get_free_chunk_from_global_gc_chunks(new_thread_owner);

            loop_max--;
            if(loop_max == 0) {
                printf("10 successive GCs in p_cycle_chunk without freeing room to allocate memory.\n");
                exit(17014);
            }
        }

        orp_gc_unlock_enum();
        release_chunk_lock(current_thread);
    }

#endif // CONCURRENT

    assert(free_chunk != NULL);

    // Better have got some free chunk...
    if (free_chunk) {
        // Make each block in the chunk "active" before returning
        block_info *block = free_chunk;
        while (block) {
            assert(block);
            if(block->get_nursery_status() == free_uncleared_nursery) {
                printf("Forcing unclear to free\n");
                exit(17069);
            }

#ifdef CONCURRENT
            assert(block->get_nursery_status() == active_nursery);
#else
            block->set_nursery_status(free_nursery,active_nursery);
#endif

            block->thread_owner = new_thread_owner;
            block = block->next_free_block;
        }
    }

    return free_chunk;
}



unsigned int Garbage_Collector::return_free_blocks_to_block_store(int blocks_to_return, bool to_coalesce_block_store, bool compaction_has_been_done_this_gc, bool has_block_store_lock) {
    unsigned int num_blocks_returned = 0;
    unsigned int num_empty_chunks = 0;

    sweep_stats stats;
    for (int chunk_index = 0; chunk_index <= _free_chunks_end_index; chunk_index++) {
        chunk_info *this_chunk = &_gc_chunks[chunk_index];
        assert(this_chunk);

        if ((this_chunk->chunk) && (this_chunk->chunk->get_nursery_status() != active_nursery)) {
            // Will return only free blocks (no nursery is spent after a GC..either it is active or it is free
            if(this_chunk->chunk->get_nursery_status() != free_nursery) {
                continue;
            }

            block_info *block = this_chunk->chunk;
            assert(block);

            block_info *new_chunk_start = NULL;

            while (block) {
                gc_trace_block (block, "in return_free_blocks_to_block_store looking for free blocks");
                // If any compaction has happened in this GC
                if (compaction_has_been_done_this_gc && is_compaction_block(block)) {
                    // block is then automatically swept
                    assert(block->block_has_been_swept == true);
                } else {
                    if (g_gen || sweeps_during_gc) {
                        assert(block->block_has_been_swept == true);
                    } else {
                        if (block->block_has_been_swept == false) {
                            // First sweep it and check if it is fully free...we need to do this since as of now all blocks are setup for sweeps during allocation
                            sweep_one_block(block,stats);
                            block->block_has_been_swept = true;
                        }
                    }
                }

                if (!block->is_empty()) {
                    assert (block->block_free_areas[0].area_size < GC_BLOCK_ALLOC_SIZE);

                    block_info *next_block = block->next_free_block;
                    // Relink onto the new chunk
                    block->next_free_block = new_chunk_start;
                    new_chunk_start = block;
                    block = next_block;
                } else {
                    gc_trace_block (block, "in return_free_blocks_to_block_store returing free block to block store.");
                    // Fully free block means.....NO live data...can go back to the block store
                    // Return it to the block store
                    block_info *next_block = block->next_free_block;
                    assert(block->number_of_blocks == 1);
                    assert(block->get_nursery_status() == free_nursery);
                    _p_block_store->link_free_blocks (block, block->number_of_blocks, has_block_store_lock);
                    num_blocks_returned++;
                    block = next_block;
                }
            }
            if (new_chunk_start == NULL) {
                num_empty_chunks++;
            }
            this_chunk->chunk = this_chunk->free_chunk = new_chunk_start;
        }

        // check if we are done...
        if ((blocks_to_return != -1) && ((int) num_blocks_returned >= blocks_to_return)) {
            // Stop with this chunk
            break;
        }
    }

    assert(to_coalesce_block_store == false);

    if (stats_gc) {
        orp_cout << "return_free_blocks_to_block_store() returned " << num_blocks_returned << " to the block store\n";
        orp_cout << "amount of usable space found by sweeping = " << stats.amount_recovered << "\n";
        orp_cout << "amount of fragmented space found by sweeping = " << stats.amount_in_fragments << "\n";
        if(stats.amount_in_fragments) {
            if(!stats.num_fragments) {
                orp_cout << "Error: fragmented data found but num_fragments = 0\n";
            } else {
                orp_cout << "average fragment size = " << (float)stats.amount_in_fragments / stats.num_fragments << "\n";
            }
        }
    }

    return num_blocks_returned;
} //return_free_blocks_to_block_store

unsigned int Garbage_Collector::return_free_blocks_to_block_store_prob(float prob, bool to_coalesce_block_store, bool compaction_has_been_done_this_gc, bool has_block_store_lock) {
    unsigned int num_blocks_returned = 0;
    unsigned int num_empty_chunks = 0;
	unsigned int num_blocks_could_returned = 0;

    sweep_stats stats;
    for (int chunk_index = 0; chunk_index <= _free_chunks_end_index; chunk_index++) {
        chunk_info *this_chunk = &_gc_chunks[chunk_index];
        assert(this_chunk);

        if ((this_chunk->chunk) && (this_chunk->chunk->get_nursery_status() != active_nursery)) {
            // Will return only free blocks (no nursery is spent after a GC..either it is active or it is free
            if(this_chunk->chunk->get_nursery_status() != free_nursery) {
                continue;
            }

            block_info *block = this_chunk->chunk;
            assert(block);

            block_info *new_chunk_start = NULL;

			unsigned total_blocks_so_far = num_blocks_returned + num_blocks_could_returned;
			bool do_return;
			if(num_blocks_returned <= total_blocks_so_far * prob) {
				do_return = true;
			} else {
				do_return = false;
			}

			if(do_return) {
	            while (block) {
					gc_trace_block (block, "in return_free_blocks_to_block_store looking for free blocks");
					// If any compaction has happened in this GC
					if (compaction_has_been_done_this_gc && is_compaction_block(block)) {
						// block is then automatically swept
						assert(block->block_has_been_swept == true);
					} else {
						if (g_gen || sweeps_during_gc) {
							assert(block->block_has_been_swept == true);
						} else {
							if (block->block_has_been_swept == false) {
								// First sweep it and check if it is fully free...we need to do this since as of now all blocks are setup for sweeps during allocation
								sweep_one_block(block,stats);
								block->block_has_been_swept = true;
							}
						}
					}

					if (!block->is_empty()) {
						assert (block->block_free_areas[0].area_size < GC_BLOCK_ALLOC_SIZE);

						block_info *next_block = block->next_free_block;
						// Relink onto the new chunk
						block->next_free_block = new_chunk_start;
						new_chunk_start = block;
						block = next_block;
					} else {
						gc_trace_block (block, "in return_free_blocks_to_block_store returing free block to block store.");
						// Fully free block means.....NO live data...can go back to the block store
						// Return it to the block store
						block_info *next_block = block->next_free_block;
						assert(block->number_of_blocks == 1);
						assert(block->get_nursery_status() == free_nursery);
						_p_block_store->link_free_blocks (block, block->number_of_blocks, has_block_store_lock);
						num_blocks_returned++;
						block = next_block;
					}
	            }
				if (new_chunk_start == NULL) {
					num_empty_chunks++;
				}
				this_chunk->chunk = this_chunk->free_chunk = new_chunk_start;
    		} else {
	            while (block) {
					if(block->is_empty()) {
						num_blocks_could_returned++;
					}
					block = block->next_free_block;
				}
			}
	    }
    }

    assert(to_coalesce_block_store == false);

    if (stats_gc) {
        orp_cout << "return_free_blocks_to_block_store() returned " << num_blocks_returned << " to the block store\n";
        orp_cout << "amount of usable space found by sweeping = " << stats.amount_recovered << "\n";
        orp_cout << "amount of fragmented space found by sweeping = " << stats.amount_in_fragments << "\n";
        if(stats.amount_in_fragments) {
            if(!stats.num_fragments) {
                orp_cout << "Error: fragmented data found but num_fragments = 0\n";
            } else {
                orp_cout << "average fragment size = " << (float)stats.amount_in_fragments / stats.num_fragments << "\n";
            }
        }
    }

    return num_blocks_returned;
} //return_free_blocks_to_block_store


///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////// C O L L E C T I O N  ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

void Garbage_Collector::enumerate_thread(volatile GC_Thread_Info *stopped_thread) {
    orp_enumerate_thread ((PrtTaskHandle)stopped_thread->thread_handle);
    if(g_gen && !g_gen_all) {
        if(stopped_thread->elder_roots_to_younger) {
            ExpandInPlaceArray<Managed_Object_Handle *>::iterator iter;
            for(iter  = stopped_thread->elder_roots_to_younger->begin();
                iter != stopped_thread->elder_roots_to_younger->end();
                ++iter) {
                gc_add_root_set_entry((void**)iter.get_current(),FALSE);
            }
        }
    }
} // Garbage_Collector::enumerate_Thread

// This version stops the thread, enumerates it and then stops the next thread until all are stopped and enumerated. */
void Garbage_Collector::gc_enumerate_root_set_all_threads() {
#ifdef OLD_MULTI_LOCK
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Is this needed or can it be placed lower down...
#endif // OLD_MULTI_LOCK

    bool success = false;
    bool all_stopped = false;
    volatile GC_Thread_Info *temp_active_thread_gc_info_list = active_thread_gc_info_list;
    GC_Thread_Info *this_thread = (struct GC_Thread_Info *)get_gc_thread_local();

    //    volatile GC_Thread_Info *previous_active_thread_gc_info_list = active_thread_gc_info_list;
    assert (active_thread_gc_info_list);

    while (temp_active_thread_gc_info_list) {
        if (temp_active_thread_gc_info_list != this_thread) {
            temp_active_thread_gc_info_list->enumeration_state = gc_enumeration_state_running;
        }
        temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
    }

    /* Enumerate this thread first before all the others are stopped, it can actually be enumerated anywhere.
       Adam: Consulted with Rick moving it here - need to access the thread initiating GC for irrevocability */
    if(this_thread) {
        enumerate_thread (this_thread);
    }
    while (!all_stopped) {
        temp_active_thread_gc_info_list = active_thread_gc_info_list;
        all_stopped = true;
        while (temp_active_thread_gc_info_list) {
            if (temp_active_thread_gc_info_list != this_thread) {
                if (temp_active_thread_gc_info_list->enumeration_state == gc_enumeration_state_running) {
                    /* No need to stop this thread */
                    success = orp_suspend_thread_for_enumeration ((PrtTaskHandle)temp_active_thread_gc_info_list->thread_handle);
                    if (success) {
                        temp_active_thread_gc_info_list->enumeration_state = gc_enumeration_state_stopped;
                        enumerate_thread(temp_active_thread_gc_info_list);
                    } else {
//                        all_stopped = false;
                    }
                }
            }
            temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
        }
    }

//    printf("Num roots from threads = %d\n",_num_roots);
    orp_enumerate_global_refs();
//    printf("Num roots from threads + globals = %d\n",_num_roots);

#ifdef OLD_MULTI_LOCK
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#endif // OLD_MULTI_LOCK
} // Garbage_Collector::gc_enumerate_root_set_all_threads

void Garbage_Collector::stop_the_world() {
    bool success = false;
    bool all_stopped = false;
    volatile GC_Thread_Info *temp_active_thread_gc_info_list = active_thread_gc_info_list;
    GC_Thread_Info *this_thread = (struct GC_Thread_Info *)get_gc_thread_local();

    //    volatile GC_Thread_Info *previous_active_thread_gc_info_list = active_thread_gc_info_list;
    assert (active_thread_gc_info_list);

    while (temp_active_thread_gc_info_list) {
        if (temp_active_thread_gc_info_list != this_thread) {
            temp_active_thread_gc_info_list->enumeration_state = gc_enumeration_state_running;
        }
        temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
    }

    while (!all_stopped) {
        temp_active_thread_gc_info_list = active_thread_gc_info_list;
        all_stopped = true;
        while (temp_active_thread_gc_info_list) {
            if (temp_active_thread_gc_info_list != this_thread) {
                if (temp_active_thread_gc_info_list->enumeration_state == gc_enumeration_state_running) {
                    /* No need to stop this thread */
                    success = orp_suspend_thread_for_enumeration ((PrtTaskHandle)temp_active_thread_gc_info_list->thread_handle);
                    if (success) {
                        temp_active_thread_gc_info_list->enumeration_state = gc_enumeration_state_stopped;
                    } else {
//                        all_stopped = false;
                    }
                }
            }
            temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
        }
    }
    /* do in resume_the_world - release_active_thread_gc_info_list_lock(); */
} // Garbage_Collector::stop_the_world

void Garbage_Collector::enumerate_the_world(void) {
    volatile GC_Thread_Info *temp_active_thread_gc_info_list = active_thread_gc_info_list;
    //    volatile GC_Thread_Info *previous_active_thread_gc_info_list = active_thread_gc_info_list;
    assert (active_thread_gc_info_list);

    while (temp_active_thread_gc_info_list) {
        orp_enumerate_thread ((PrtTaskHandle)temp_active_thread_gc_info_list->thread_handle);
        temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
    }
//    orp_enumerate_this_thread();
    orp_enumerate_global_refs();
} // Garbage_Collector::enumerate_the_world

void Garbage_Collector::resume_the_world(void) {
    volatile GC_Thread_Info *temp_active_thread_gc_info_list = active_thread_gc_info_list;
    //    volatile GC_Thread_Info *previous_active_thread_gc_info_list = active_thread_gc_info_list;
    assert (active_thread_gc_info_list);

    GC_Thread_Info *this_thread = (struct GC_Thread_Info *)get_gc_thread_local();
    while (temp_active_thread_gc_info_list) {
        if(g_gen) {
            if(this_thread->elder_roots_to_younger) {
                this_thread->elder_roots_to_younger->reset();
            }
        }
        if (temp_active_thread_gc_info_list != this_thread) {
            orp_resume_thread_after_enumeration ((PrtTaskHandle)temp_active_thread_gc_info_list->thread_handle);
        }
        temp_active_thread_gc_info_list = temp_active_thread_gc_info_list->p_active_gc_thread_info;
    }

    orp_gc_cycle_end_notification();
} // Garbage_Collector::resume_the_world


/* RLH is rolling this back USE_GC_STW is being used as the #ifdef for the rollback so it needs to
 * be defined in more than this file to reenable GC_STW.
 */


void Garbage_Collector::_get_orp_live_references() {
    num_roots_added = 0;
#ifdef USE_ORP_STW
    orp_enumerate_root_set_all_threads();
#else
    gc_enumerate_root_set_all_threads();
#endif
}


void Garbage_Collector::resume_orp() {
#ifdef USE_ORP_STW
    orp_resume_threads_after();
#else
    resume_the_world();
#endif
}

#ifdef CONCURRENT

#define CONCURRENT_TIME_INSURANCE 0.6

void Garbage_Collector::fake_func(void) {
    while(1) {
        if(stop_concurrent) {
            return;
        }
    }
}

PrtBool concurrentPred(volatile void *location, void *data) {
    return (PrtBool)(start_concurrent_gc || stop_concurrent);
}

void Garbage_Collector::reclamation_func(void) {
    GC_Nursery_Info copy_to;
    if(separate_immutable && incremental_compaction) {
        copy_to.chunk = NULL;
        copy_to.curr_alloc_block = NULL;
        copy_to.tls_current_ceiling = NULL;
        copy_to.tls_current_free = NULL;
    }

    if(concurrent_torture) {
        while(1) {
            if(stop_concurrent) {
                concurrent_gc_thread_id = 0;
                return;
            }
            if(orp_initialized == 0) {
#ifndef HAVE_PTHREAD_H
                mcrtThreadYieldUntil(mcrtPredicateNotEqualUint32,&orp_initialized,0,InfiniteWaitCycles64);
#endif // HAVE_PTHREAD_H
                continue;
            }

            reclaim_full_heap_concurrent(&copy_to);
#ifndef HAVE_PTHREAD_H
            mcrtThreadYield();
#endif // HAVE_PTHREAD_H
        }
    } else {
        TIME_STRUCT concurrent_start_time, concurrent_end_time;
        unsigned num_gcs = 0;
        unsigned total_gc_time = 0;
        float next_multiplier = 1.2;
#if 1
        while(1) {
            if(stop_concurrent) {
                concurrent_gc_thread_id = 0;
                return;
            }
#if 1
#ifdef HAVE_PTHREAD_H
            while(!concurrentPred(NULL, NULL));
#else  // HAVE_PTHREAD_H
            mcrtThreadYieldUntil((McrtPredicate)concurrentPred, NULL, NULL, InfiniteWaitCycles64);
#endif // HAVE_PTHREAD_H
#else // 1
#ifdef HAVE_PTHREAD_H
            prtYieldUntil((PrtPredicate)concurrentPred, NULL, NULL, PrtInfiniteWait64);
#else  // HAVE_PTHREAD_H
            prtYieldUntil((PrtPredicate)concurrentPred, NULL, NULL, InfiniteWaitCycles64);
#endif // HAVE_PTHREAD_H
#endif

#if 1
            TIME_STRUCT ll;
            gc_time_start_hook(&ll);
            printf("GC_start_time %d %d %I64u\n",start_concurrent_gc,stop_concurrent,ll);
#endif // 0/1
            if(stop_concurrent) {
                concurrent_gc_thread_id = 0;
                return;
            }

            unsigned starting_num_blocks_available = g_num_blocks_available;
            float percent_full_at_start = 1.0 - ((float)starting_num_blocks_available / _p_block_store->get_num_total_blocks_in_block_store());

            gc_time_start_hook(&concurrent_start_time);
            reclaim_full_heap_concurrent(&copy_to);
            unsigned concurrent_gc_time = gc_time_end_hook("GC time", &concurrent_start_time, &concurrent_end_time, false);
            total_gc_time += concurrent_gc_time;
            num_gcs++;

            // estimate how long it would take to collect a full heap
            unsigned full_heap_concurrent_gc_time = (unsigned)(concurrent_gc_time / percent_full_at_start);

#if 0
            printf("TODD: start blocks = %d, percent full = %f, gc time = %d, full heap estimate = %d, gc max = %d\n",
                starting_num_blocks_available,
                percent_full_at_start,
                concurrent_gc_time,
                full_heap_concurrent_gc_time,
                g_concurrent_gc_max);
#endif

            if(num_gcs <= 1) {
                g_concurrent_gc_max = concurrent_gc_time;
            } else {
                if(concurrent_gc_time * next_multiplier > g_concurrent_gc_max) {
                    g_concurrent_gc_max = concurrent_gc_time * next_multiplier;
                    next_multiplier = 1.0 + ((next_multiplier-1.0) * 0.8);
                }
            }

#if 1
            gc_time_start_hook(&ll);
            printf("GC_end_time %I64u\n",ll);
#endif // 0/1

            start_concurrent_gc = 0;
        }
#else // 0/1
        unsigned prev_heap_free = initial_heap_size_bytes;
        unsigned max_allocation_rate = 0;

        orp_thread_sleep(1000);
        gc_time_start_hook(&concurrent_start_time);
        reclaim_full_heap_concurrent(&copy_to);
        unsigned concurrent_gc_time = gc_time_end_hook("GC time", &concurrent_start_time, &concurrent_end_time, stats_gc);
        total_gc_time += concurrent_gc_time;
        num_gcs++;

//        orp_thread_sleep(1000000);

        while(1) {
            if(stop_concurrent) {
                concurrent_gc_thread_id = 0;
                return;
            }

            orp_thread_sleep(1000);

            unsigned start_free = estimate_free_heap_space();
            unsigned allocate_rate = 0, end_free, allocate_rate_time;

            while (!allocate_rate) {
                if(stop_concurrent) {
                    concurrent_gc_thread_id = 0;
                    return;
                }

                gc_time_start_hook(&concurrent_start_time);
                orp_thread_sleep(1000);
                end_free = estimate_free_heap_space();
                allocate_rate_time = gc_time_end_hook("GC time", &concurrent_start_time, &concurrent_end_time, false);
                allocate_rate = (start_free - end_free) / allocate_rate_time;
            }

            if(allocate_rate > max_allocation_rate) {
                max_allocation_rate = allocate_rate;
            }

            int msecs_until_exhausted = 0;
            if(max_allocation_rate) {
                msecs_until_exhausted = end_free / max_allocation_rate;
            }

            msecs_until_exhausted = (int)(msecs_until_exhausted * CONCURRENT_TIME_INSURANCE);
            msecs_until_exhausted -= (total_gc_time / num_gcs);

            if(stats_gc) {
                printf("Start free = %d, End free = %d, Allocate Rate = %d/ms, Time until GC = %dms\n",start_free,end_free,allocate_rate,msecs_until_exhausted);
            }

#if 0
            if(msecs_until_exhausted > 0) {
                McrtTimeCycles64 end_time = mcrtGetTimeStampCounterFast();
                McrtTimeMsecs64 end_time_ms = mcrtConvertCyclesToMsecs(end_time);
                end_time_ms += msecs_until_exhausted;
                end_time = mcrtConvertMsecsToCycles(end_time_ms);
                McrtSyncResult res = Yielded;

                while (res == Yielded) {
                    res = mcrtThreadYieldUntil(mcrtPredicateNotEqualUint32,&stop_concurrent,0,mcrtTimeToCycles64(end_time));
                    switch(res) {
                    case Success:
                        concurrent_gc_thread_id = 0;
                        return;
                    case Timeout:
                        // Will terminate the while loop.
                        break;
                    case Yielded:
                        // Also intentionally do nothing here which will repeat the while loop.
                        break;
                    default:
                        assert(0);
                    }
                }
            }
#else

            if(msecs_until_exhausted > 2000) continue;
#endif

            gc_time_start_hook(&concurrent_start_time);
            reclaim_full_heap_concurrent(&copy_to);
            unsigned concurrent_gc_time = gc_time_end_hook("GC time", &concurrent_start_time, &concurrent_end_time, false);
            total_gc_time += concurrent_gc_time;
            num_gcs++;
        }
#endif // 0/1
    }
}

unsigned int PRT_STDCALL gc_reclamation_func(void *arg) {
    Garbage_Collector *gc = (Garbage_Collector*)arg;
    gc->reclamation_func();
    return 0;
}
#endif // CONCURRENT

void Garbage_Collector::init_gc_threads() {
    assert(_gc_threads == NULL);

    _gc_threads = (GC_Thread **) malloc(sizeof(GC_Thread *) * g_num_cpus);
    assert(_gc_threads);

    unsigned i;
    for (i = 0; i < g_num_cpus; i++) {
        // Point the GC thread to the GC object
        _gc_threads[i] = new GC_Thread(this, i);
        assert(_gc_threads[i]);
    }

    // Grab the work done handles of the GC worker threads so that we know when the work is done by each thread
    for (i = 0; i < g_num_cpus; i++) {
        _gc_thread_work_finished_event_handles[i] = _gc_threads[i]->get_gc_thread_work_done_event_handle();
    }
}


void Garbage_Collector::reset_gc_threads(bool compaction_gc) {
    for (unsigned int i = 0; i < g_num_cpus ; i++) {
        assert(_gc_threads[i]);
        _gc_threads[i]->reset(compaction_gc);
    }
}

void Garbage_Collector::get_gc_threads_to_begin_task(gc_thread_action task) {
    for (unsigned int y = 0; y < g_num_cpus; y++) {

        _gc_threads[y]->set_task_to_do(task);
        Boolean sstat = orp_synch_set_event(_gc_threads[y]->get_gc_thread_start_work_event_handle());
        assert(sstat);
    }
}

void Garbage_Collector::wait_for_gc_threads_to_complete_assigned_task() {
    unsigned int ret = orp_synch_wait_for_multiple_events( g_num_cpus, _gc_thread_work_finished_event_handles, INFINITE);
    assert((ret != EVENT_WAIT_TIMEOUT) && (ret != EVENT_WAIT_FAILED));
}


// Moved unreachable objects to the finalizable queue.
void Garbage_Collector::add_objects_to_finalizable_queue() {
    std::list<Partial_Reveal_Object*>::iterator finalize_iter = m_unmarked_objects.begin();

    // Iterate through each object in the finalize list.
    for(; finalize_iter != m_unmarked_objects.end(); ++finalize_iter) {
        orp_finalize_object((Managed_Object_Handle)*finalize_iter);
    }
    // We do not need the objects in m_unmarked_objects anymore.
    m_unmarked_objects.clear();
}

// Identify unreachable objects and recursively marks roots from those objects being moved.
void Garbage_Collector::identify_and_mark_objects_moving_to_finalizable_queue(bool compaction_this_gc) {
    // take the finalize list lock
    while ((LONG) InterlockedCompareExchange( (LONG *)(&m_finalize_list_lock), (LONG) 1, (LONG) 0) == (LONG) 1);

    std::list<Partial_Reveal_Object*>::iterator finalize_iter = m_listFinalize->begin();

    // Make sure the set of objects added to the finalizable queue in the
    // previous GC has been emptied.
    m_unmarked_objects.clear();

    // loop through all of the objects in the finalizer set
    while( finalize_iter != m_listFinalize->end() ) {
        // get the object * from the iterator
        Partial_Reveal_Object *p_obj = *finalize_iter;
        assert(p_obj);

        std::list<Partial_Reveal_Object*>::iterator insert_location;
        bool object_was_finalizable = false;
        if(!is_object_marked(p_obj)) {
            // Add the object to those that will be moved to the finalizable queue.
            m_unmarked_objects.push_back(p_obj);
            insert_location = m_unmarked_objects.end();
            --insert_location;
            // remove this object from the finalize set
            m_listFinalize->erase(finalize_iter++);
            // remove and use !m_unmarked_objects.empty()
            object_was_finalizable   = true;
        }

        if (_num_roots >= num_root_limit) {
            expand_root_arrays();
        }

        // The eumeration needs to provide a set which means no duplicates. Remove duplicates here.

        Partial_Reveal_Object **slot = NULL;
        if (object_was_finalizable) {
            // Add a root that could be modified later from m_unmarked_objects list.
            slot = &(*insert_location);
        } else {
            // Add a root from the m_listFinalize list.
            slot = &(*finalize_iter);

            ++finalize_iter;
        }

        assert (dup_removal_enum_hash_table->size() == _num_roots);
        if (dup_removal_enum_hash_table->add_entry_if_required(slot)) {
            assert(*slot); //RLH
            _array_of_roots[_num_roots] = slot;
            // save away this root as well so that it can be updated
            if(compaction_this_gc) {
                _save_array_of_roots[_num_roots] = slot;
            }
            // Because the post-GC trace heap uses _num_roots to know how many roots to scan.
            _verify_array_of_roots[_num_roots] = slot;
            ++_num_roots;
        }
    }

    // release the finalize list lock
    m_finalize_list_lock = 0;

    if (!m_unmarked_objects.empty()) {
        g_root_index_hint = 0;
        // One or more objects are being revived and will be placed onto the finalizable queue.
        // Initiate a mark/scan phase for those new objects.
        get_gc_threads_to_begin_task(GC_MARK_SCAN_TASK);
        wait_for_gc_threads_to_complete_assigned_task();
    }
} // Garbage_Collector::identify_and_mark_objects_moving_to_finalizable_queue

void Garbage_Collector::expand_root_arrays() {
//    printf("expand_root_arrays, new size = %d\n",num_root_limit + 100000);
    unsigned new_num_root_limit = num_root_limit + 100000;
    Partial_Reveal_Object*** new_array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * new_num_root_limit);
    Partial_Reveal_Object*** new_save_array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * new_num_root_limit);
    Partial_Reveal_Object*** new_verify_array_of_roots = (Partial_Reveal_Object***)malloc(sizeof(Partial_Reveal_Object**) * new_num_root_limit);

    memset(new_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * new_num_root_limit);
    memset(new_save_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * new_num_root_limit);
    memset(new_verify_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * new_num_root_limit);

    memcpy(new_array_of_roots,_array_of_roots,sizeof(Partial_Reveal_Object**) * num_root_limit);
    memcpy(new_save_array_of_roots,_save_array_of_roots,sizeof(Partial_Reveal_Object**) * num_root_limit);
    memcpy(new_verify_array_of_roots,_verify_array_of_roots,sizeof(Partial_Reveal_Object**) * num_root_limit);

    Partial_Reveal_Object*** temp;

    temp = _array_of_roots;
    _array_of_roots = new_array_of_roots;
    free(temp);

    temp = _save_array_of_roots;
    _save_array_of_roots = new_save_array_of_roots;
    free(temp);

    temp = _verify_array_of_roots;
    _verify_array_of_roots = new_verify_array_of_roots;
    free(temp);

    num_root_limit = new_num_root_limit;
}

// Scan the short or long weak root list and set each pointer
// to NULL if it is not reachable and otherwise adds it to
// the list of roots that will get repointed later.
//
// Our current approach is to enumerate the weak roots, store them in a list and then
// scan them later.  This function does the scanning.  An alternate but more complicated
// approach would be to enumerate and process them simultaneously.  If the extra storage
// cost of long weak roots or in the effect their second scanning is too slow then the
// latter approach may be employed later.
void Garbage_Collector::process_weak_roots(bool process_short_roots,bool compaction_this_gc) {
    std::list<MyPair> *cur_list = NULL;
    if (process_short_roots) {
        cur_list = &m_short_weak_roots;
    } else {
        cur_list = &m_long_weak_roots;
    }

    // Process all the roots in the given list.
    while (!cur_list->empty()) {
        // Get the first item in the list into root_slot.
        MyPair pair = cur_list->front();
        //Partial_Reveal_Object **root_slot  = (Partial_Reveal_Object **)((char *)pair.rootAddr - pair.offset);
        //Partial_Reveal_Object *root_object = *root_slot;
        Partial_Reveal_Object *root_object = (Partial_Reveal_Object *)((char *)*pair.rootAddr - pair.offset);

        // Remove the first item from the list.
        cur_list->pop_front();

        // No need to do anything if the root is already a NULL pointer.
        if (!root_object) {
            continue;
        }

        // The object is marked so from now on we treat it like any other root so
        // that it will get updated if necessary.
        if (is_object_marked(root_object)) {
            Partial_Reveal_Object **root_slot;
            if (pair.offset) {
                interior_pointer_table->add_entry (slot_offset_entry(pair.rootAddr, root_object, pair.offset));
                // pass the slot from the interior pointer table to the gc.
                slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                root_slot = &addr->base;
                //p_global_gc->gc_internal_add_root_set_entry(&(addr->base));
            } else {
                root_slot = (Partial_Reveal_Object **)pair.rootAddr;
            }
            if (_num_roots >= num_root_limit) {
                expand_root_arrays();
            }

            assert (dup_removal_enum_hash_table->size() == _num_roots);
            if (dup_removal_enum_hash_table->add_entry_if_required(root_slot)) {
                assert(*root_slot);
                _array_of_roots[_num_roots] = root_slot;
            } else  {
                assert(0);
            }

            // save away this root as well so that it can be updated
            if(compaction_this_gc) {
                _save_array_of_roots[_num_roots] = root_slot;
            }
            // Because the post-GC trace heap uses _num_roots to know how many roots to scan.
            _verify_array_of_roots[_num_roots] = root_slot;
            ++_num_roots;
        } else {
            // The object is not marked so by the definition of a weak root we set the root to NULL.
            //*root_slot = NULL;
            *(pair.rootAddr) = (void *)pair.offset;  // I think this is fine for 64-bit.
        }
    }
}

#ifdef CONCURRENT

void Garbage_Collector::process_weak_roots_concurrent(bool process_short_roots) {
    std::list<MyPair> *cur_list = NULL;
    if (process_short_roots) {
        cur_list = &m_short_weak_roots;
    } else {
        cur_list = &m_long_weak_roots;
    }

    // Process all the roots in the given list.
    while (!cur_list->empty()) {
        // Get the first item in the list into root_slot.
        MyPair pair = cur_list->front();
        //Partial_Reveal_Object **root_slot  = (Partial_Reveal_Object **)((char *)pair.rootAddr - pair.offset);
        //Partial_Reveal_Object *root_object = *root_slot;
        Partial_Reveal_Object *root_object = (Partial_Reveal_Object *)((char *)*pair.rootAddr - pair.offset);

        // Remove the first item from the list.
        cur_list->pop_front();

        if(*(pair.rootAddr) == NULL) {
            continue;
        }

        // No need to do anything if the root is already a NULL pointer.
        if (!root_object) {
            continue;
        }

        // The concurrent collector is non-moving so if the object is marked we're done.
        if (!is_object_marked(root_object)) {
            *(pair.rootAddr) = (void *)pair.offset;
        }
    }
}

#endif // CONCURRENT

void Garbage_Collector::repoint_all_roots_into_compacted_areas() {
    unsigned int roots_repointed = 0;
    for (unsigned int k = 0; k < _num_roots; k++) {
        Partial_Reveal_Object **root_slot = _save_array_of_roots[k];
        assert(root_slot);
        // We now allow low bits to be set on roots and we ignore them but propogate them to the new pointer value.
        // Save current low bits on the root.
        unsigned low_bits = (*(unsigned*)root_slot) & 0x3;
        // Get a pointer to the original object minus the low bits.
        Partial_Reveal_Object *root_object = (Partial_Reveal_Object*)((uintptr_t)(*root_slot) & ~(0x3));
        if (root_object->isForwarded()) { // has been forwarded
            // only objects in compaction blocks get forwarded
            assert(is_compaction_block(GC_BLOCK_INFO(root_object)));

            Partial_Reveal_Object *new_root_object = root_object->get_forwarding_pointer();
            // Update root slot and add back in any low bits set in the original root.
            *root_slot = (Partial_Reveal_Object*)((uintptr_t)new_root_object | low_bits);
            roots_repointed++;
        }
    }
    if (stats_gc) {
        orp_cout << "roots_repointed = " << roots_repointed << std::endl;
    }
}


void Garbage_Collector::repoint_all_roots_with_offset_into_compacted_areas() {
    unsigned int roots_with_offset_repointed = 0;

    ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;
    for(interior_pointer_iter  = interior_pointer_table->begin();
        interior_pointer_iter != interior_pointer_table->end();
        ++interior_pointer_iter) {

        slot_offset_entry entry = interior_pointer_iter.get_current();
        void **root_slot = entry.slot;
        Partial_Reveal_Object *root_base = entry.base;
        POINTER_SIZE_INT root_offset = entry.offset;

        void *new_slot_contents = (void *)((Byte*)root_base + root_offset);
        if (new_slot_contents != *root_slot) {
            *root_slot = new_slot_contents;
            roots_with_offset_repointed ++;
        }
    }

    ExpandInPlaceArray<slot_offset_entry>::iterator compressed_pointer_iter;
    for(compressed_pointer_iter  = compressed_pointer_table->begin();
        compressed_pointer_iter != compressed_pointer_table->end();
        ++compressed_pointer_iter) {

        slot_offset_entry entry = compressed_pointer_iter.get_current();
        uint32 *root_slot = (uint32 *)entry.slot;
        Partial_Reveal_Object *root_base = entry.base;
        POINTER_SIZE_INT root_offset = entry.offset;

        uint32 new_slot_contents = (uint32)(uintptr_t)((Byte*)root_base - root_offset);
        if (new_slot_contents != *root_slot) {
            *root_slot = new_slot_contents;
            roots_with_offset_repointed ++;
        }
    }

    if (stats_gc) {
        orp_cout << "roots_with_offset_repointed = " << roots_with_offset_repointed << std::endl;
    }

    interior_pointer_table->reset();
    compressed_pointer_table->reset();
} // Garbage_Collector::repoint_all_roots_with_offset_into_compacted_areas



///////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef GC_VERIFY_VM
extern bool running_gc;
#endif

void Garbage_Collector::reclaim_full_heap(unsigned int size_failed, bool force_gc, bool for_los) {
    reclaim_full_heap_from_gc_thread(size_failed, force_gc, for_los);
}

// Garbage Collection::reclaim_full_heap

#ifdef MCRT
extern int GetRandom(int min, int max);
#else
#define GetRandom( min, max ) ((rand() % (int)(((max) + 1) - (min))) + (min))
#endif

#ifdef IGNORE_SOME_ROOTS
std::set<Partial_Reveal_Object**> g_barrier_roots;
#endif // IGNORE_SOME_ROOTS

Arena *init_arena(void *space,Arena *next_arena,/*unsigned*/ size_t size) {
    // memory should be aligned
    Arena *arena = (Arena *)space;
    arena->next_arena = next_arena;
    arena->next_byte = arena->bytes;
    arena->last_byte = arena->bytes + size;
    return arena;
}

Arena *alloc_arena(Arena *next,/*unsigned*/ size_t size) {
    // malloc a chunk of memory for a new arena
    // we add space for 3 pointers - the arena's next and end fields and next arena
    // make sure it is rounded up or else space will be wasted
    // and unneccesssary reallocs will occur
    unsigned header_size = (sizeof(struct Arena *)+sizeof(char *)+sizeof(char *));
    return init_arena(malloc(size + header_size),next,size);
}

Arena *free_arena(Arena *a) {
    Arena *next = a->next_arena;
    free(a);
    return next;
}

void free_all_arenas(Arena *a) {
    while(a) {
        a = free_arena(a);
    }
}

Arena *free_all_but_one_arena(Arena *a) {
    while(1) {
        if(a->next_arena != NULL) {
            a = free_arena(a);
        } else {
            return a;
        }
    }
}

void *arena_alloc_space(Arena *arena,/*unsigned*/ size_t size) {
    if (size == 0)
        return NULL;

    if (arena->next_byte + size > arena->last_byte) {
        // not enough space
        return NULL;
    }
    // return the space and bump next_byte pointer
    char *mem = arena->next_byte;
    arena->next_byte += size;
    assert (arena->next_byte <= arena->last_byte);
    return (void*)mem;
}


//===========================================================================================================

extern "C" void local_root_callback(void *env, void **rootAddr, PrtGcTag tag, void *parameter) {
    if(!rootAddr) return;

    Partial_Reveal_Object *p_obj;
    POINTER_SIZE_INT offset;
    slot_offset_entry *addr;

    // Collect roots as inter-slots.
    pn_info *the_roots = (pn_info*)env;
    switch(get_object_location(*rootAddr,the_roots)) {
    case GLOBAL:
    case PUBLIC_HEAP:
#ifdef PUB_PRIV
	case PRIVATE_HEAP:
#endif // PUB_PRIV
        switch(tag) {
        case PrtGcTagDefault:
            p_obj = (Partial_Reveal_Object *)(*rootAddr);
            // Don't need to record roots that don't point into the local nursery.
            the_roots->add_inter_slot((Partial_Reveal_Object**)rootAddr, NULL /* unknown base */);
            break;
        case PrtGcTagOffset:
            offset = (uintptr_t)parameter;
            assert(offset > 0);

            p_obj = (Partial_Reveal_Object *)((Byte*)*rootAddr - offset);
            assert (p_obj->vt());

            the_roots->interior_pointer_table_public.add_entry (slot_offset_entry(rootAddr, p_obj, offset));
            // pass the slot from the interior pointer table to the gc.
//            addr = the_roots->interior_pointer_table_public.get_last_addr();
//            the_roots->add_inter_slot((Partial_Reveal_Object**)&(addr->base));
            break;
        case PrtGcTagBase:
            p_obj = (Partial_Reveal_Object *)parameter;
            assert (p_obj->vt());

            offset = (uintptr_t)*rootAddr - (uintptr_t)p_obj;
            the_roots->interior_pointer_table_public.add_entry (slot_offset_entry(rootAddr, p_obj, offset));
            // pass the slot from the interior pointer table to the gc.
//            addr = the_roots->interior_pointer_table_public.get_last_addr();
//            the_roots->add_inter_slot((Partial_Reveal_Object**)&(addr->base));
            break;
        default:
            assert(0);
            break;
        }
        break;
    case PRIVATE_NURSERY:
    switch(tag) {
    case PrtGcTagDefault:
        the_roots->m_roots.push_back((Partial_Reveal_Object**)rootAddr);
        break;
    case PrtGcTagOffset:
        offset = (uintptr_t)parameter;
        assert(offset > 0);

        p_obj = (Partial_Reveal_Object *)((Byte*)*rootAddr - offset);
        assert (p_obj->vt());
        the_roots->interior_pointer_table.add_entry (slot_offset_entry(rootAddr, p_obj, offset));
        // pass the slot from the interior pointer table to the gc.
        addr = the_roots->interior_pointer_table.get_last_addr();
        the_roots->m_roots.push_back(&(addr->base));

        break;
    case PrtGcTagBase:
        p_obj = (Partial_Reveal_Object *)parameter;
        assert (p_obj->vt());
        offset = (uintptr_t)*rootAddr - (uintptr_t)p_obj;
        the_roots->interior_pointer_table.add_entry (slot_offset_entry(rootAddr, p_obj, offset));
        // pass the slot from the interior pointer table to the gc.
        addr = the_roots->interior_pointer_table.get_last_addr();
        the_roots->m_roots.push_back(&(addr->base));
        break;
    default:
        assert(0);
        break;
    }
        break;
    default:
        assert(0);
    }
} // local_root_callback

//===========================================================================================================

#ifdef NEW_APPROACH

inline static void local_scan_one_slot (Slot p_slot,
                                        pn_info &collector,
                                        INTER_SLOT_MODE collect_inter_slots,
                                        bool is_weak=false) {
#if defined _DEBUG
    collector.num_slots++;
#endif

    assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return;
    }
    Partial_Reveal_Object *p_obj = p_slot.dereference();

    if(remove_indirections(p_obj, p_slot) == RI_REPLACE_NON_OBJ) {
        // new value is not a pointer so return
        return;
    }

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,&collector)) {
    case PUBLIC_HEAP:
    case GLOBAL:
        if(collect_inter_slots == YES_INTER_SLOT ||
            collect_inter_slots == ONLY_INTER_SLOT) {
            if(is_weak) {
                assert(0 && "Weak reference from local to public object while collecting inter slots.\n");
            } else {
                collector.add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
		if(collect_inter_slots == PRIVATE_HEAP_SLOT || collect_inter_slots == PRIVATE_HEAP_SLOT_NON_ESCAPING) {
			if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(p_obj, &(collector.mark_stack));
			}
		}
        if(collect_inter_slots == PRIVATE_HEAP_SLOT_NON_ESCAPING) {
            if(p_obj->isLowFlagSet()) {
                collector.add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        } else if(collect_inter_slots != NO_INTER_SLOT) {
            collector.add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
		}
		return;
#endif // PUB_PRIV
    case PRIVATE_NURSERY:
        // A pointer into the private nursery.
		if(collect_inter_slots != ONLY_INTER_SLOT
#ifdef PUB_PRIV
		 && collect_inter_slots != PRIVATE_HEAP_SLOT
		 && collect_inter_slots != PRIVATE_HEAP_SLOT_NON_ESCAPING
#endif // PUB_PRIV
		 ) {
            // If this object isn't already marked then add it to the mark stack.
            // The main loop will mark it later.
            if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(p_obj, &(collector.mark_stack));
            }

            if(is_weak) {
                collector.add_intra_weak_slot((Partial_Reveal_Object**)p_slot.get_value());
            } else {
                collector.add_intra_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
    default:
        assert(0);
        return;
    }
} // local_scan_one_slot

//===========================================================================================================

// Returns true if this array has an entry that points into the private heap.
static inline void local_scan_one_array_object(Partial_Reveal_Object *p_object,
                                               Partial_Reveal_VTable *obj_vt,
                                               pn_info &collector,
                                               INTER_SLOT_MODE collect_inter_slots)
{
    Type_Info_Handle tih = class_get_element_type_info(obj_vt->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        unsigned start_index = 0;

        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        for (int32 i=start_index; i < array_length; i++) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, obj_vt));
            p_element.base = p_object;
            local_scan_one_slot(p_element, collector, collect_inter_slots);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(obj_vt->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
        unsigned int *offset_scanner = NULL;

        unsigned start_index = 0;

        // Offsets assume that the object starts with vtable pointer.  We'll set our
        // fake object to scan 4 bytes before the start of the value type to adjust.
        void *cur_value_type_entry_as_object = (Byte*)p_object + first_elem_offset - base_offset + (start_index * elem_size);

        int i;
        for(i = start_index; i < array_length; i++) {
            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
            Slot pp_target_object(NULL);
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                local_scan_one_slot (pp_target_object, collector, collect_inter_slots);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // ========================== WEAK REFERENCES ===========================================
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                local_scan_one_slot (pp_target_object, collector, collect_inter_slots, true);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
} // local_scan_one_array_object

//===========================================================================================================

// Returns true if this object has an entry that points into the private heap.
static void local_scan_one_object(Partial_Reveal_Object *p_obj,
                                  Partial_Reveal_VTable *obj_vt,
                                  pn_info &collector,
                                  INTER_SLOT_MODE collect_inter_slots) {
    if(obj_vt == wpo_vtable && !g_treat_wpo_as_normal) {
        weak_pointer_object *wpo = (weak_pointer_object*)p_obj;
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        pp_target_object.set(&(wpo->m_key));
        local_scan_one_slot (pp_target_object, collector, collect_inter_slots);
        pp_target_object.set(&(wpo->m_value));
        local_scan_one_slot (pp_target_object, collector, collect_inter_slots);
        pp_target_object.set(&(wpo->m_finalizer));
        local_scan_one_slot (pp_target_object, collector, collect_inter_slots);
        // other extra fields in the wpo type will be scanned below
    }

#ifdef WRITE_BUFFERING
    /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
       but potentially only reachable from the transaction_info field of the object.
       check if the transaction_info contains a forwarding pointer, and scan the slot
       if appropriate
     */
    local_scan_object_transaction_info(p_obj,collector);
#endif // WRITE_BUFFERING

    struct GC_VTable_Info *obj_gcvt = obj_vt->get_gcvt();

    if (obj_gcvt->gc_object_has_slots) {
        if (is_vt_array(obj_vt)) {
            local_scan_one_array_object(p_obj, obj_vt, collector, collect_inter_slots);
        }

        unsigned int *offset_scanner = init_object_scanner_from_vt (obj_vt);
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            local_scan_one_slot (pp_target_object, collector, collect_inter_slots);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }

        // ========================= HANDLE WEAK REFERENCES ========================================
        offset_scanner = init_object_scanner_weak_from_vt (obj_vt);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            local_scan_one_slot (pp_target_object, collector, collect_inter_slots, true);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }
} // local_scan_one_object

#else

inline static void local_scan_one_slot (PncLiveObject *from_pnc,
                                 PncLiveObject *to_pnc,
                                 Slot p_slot,
                                 pn_info &collector,
                                 INTER_SLOT_MODE collect_inter_slots,
                                 bool is_weak=false)
{
#if defined _DEBUG
    collector.num_slots++;
#endif

    assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return;
    }
    Partial_Reveal_Object *p_obj = p_slot.dereference();

    if(collect_inter_slots != ONLY_INTER_SLOT) {
		REMOVE_INDIR_RES rir;
		while((rir = remove_one_indirection(p_obj, p_slot, 5)) == RI_REPLACE_OBJ);
		if(rir == RI_REPLACE_NON_OBJ) {
			// New value is not a pointer so return.
			return;
		}
    }

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,&collector)) {
    case PUBLIC_HEAP:
    case GLOBAL:
        if(collect_inter_slots == YES_INTER_SLOT ||
            collect_inter_slots == ONLY_INTER_SLOT) {
            if(is_weak) {
                assert(0 && "Weak reference from local to public object while collecting inter slots.\n");
            } else {
                collector.add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
		if(collect_inter_slots == PRIVATE_HEAP_SLOT) {
#ifdef _DEBUG
            if(collector.ph_ofstream) {
                std::string to_prefix;
                std::string dest_prefix;
                if(pgc_is_vtable_immutable((VTable*)to_pnc->vt)) {
                    if(to_pnc->vt->get_gcvt()->gc_object_has_slots) {
                        to_prefix = "IR";
                    } else {
                        to_prefix = "IL";
                    }
                } else {
                    if(to_pnc->vt->get_gcvt()->gc_object_has_slots) {
                        to_prefix = "MR";
                    } else {
                        to_prefix = "ML";
                    }
                }
                if(pgc_is_vtable_immutable((VTable*)p_obj->vt())) {
                    if(p_obj->vt()->get_gcvt()->gc_object_has_slots) {
                        dest_prefix = "IR";
                    } else {
                        dest_prefix = "IL";
                    }
                } else {
                    if(p_obj->vt()->get_gcvt()->gc_object_has_slots) {
                        dest_prefix = "MR";
                    } else {
                        dest_prefix = "ML";
                    }
                }
                (*collector.ph_ofstream) << to_prefix.c_str() << to_pnc->old_location << " -> " <<
                                          dest_prefix.c_str() << p_obj << " ;\n";
            }
#endif
			if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(to_pnc, p_obj, &(collector.mark_stack));
			}
		}
		if(collect_inter_slots != NO_INTER_SLOT) {
            collector.add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
		}
		return;
#endif // PUB_PRIV
    case PRIVATE_NURSERY:
        // A pointer into the private nursery.
		if(collect_inter_slots != ONLY_INTER_SLOT
#ifdef PUB_PRIV
		 && collect_inter_slots != PRIVATE_HEAP_SLOT
#endif // PUB_PRIV
		 ) {
            // If this object isn't already marked then add it to the mark stack.
            // The main loop will mark it later.
            if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(to_pnc, p_obj, &(collector.mark_stack));
            }

            if(is_weak) {
                collector.add_intra_weak_slot((Partial_Reveal_Object**)p_slot.get_value());
            } else {
                collector.add_intra_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
    default:
        assert(0);
        return;
    }
} // local_scan_one_slot

//===========================================================================================================

// Returns true if this array has an entry that points into the private heap.
static inline void local_scan_one_array_object(Partial_Reveal_Object *p_object,
                                               Partial_Reveal_VTable *obj_vt,
                                               PncLiveObject *from_pnc,
                                               PncLiveObject *to_pnc,
                                               pn_info &collector,
                                               INTER_SLOT_MODE collect_inter_slots)
{
    Type_Info_Handle tih = class_get_element_type_info(obj_vt->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        unsigned start_index = 0;

        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        for (int32 i=start_index; i < array_length; i++) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, obj_vt));
            p_element.base = p_object;
            local_scan_one_slot(from_pnc, to_pnc, p_element, collector, collect_inter_slots);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(obj_vt->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
        unsigned int *offset_scanner = NULL;

        unsigned start_index = 0;

        // Offsets assume that the object starts with vtable pointer.  We'll set our
        // fake object to scan 4 bytes before the start of the value type to adjust.
        void *cur_value_type_entry_as_object = (Byte*)p_object + first_elem_offset - base_offset + (start_index * elem_size);

        int i;
        for(i = start_index; i < array_length; i++) {
            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
            Slot pp_target_object(NULL);
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // ========================== WEAK REFERENCES ===========================================
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots, true);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
} // local_scan_one_array_object

//===========================================================================================================

// Returns true if this object has an entry that points into the private heap.
static void local_scan_one_object(Partial_Reveal_Object *p_obj,
                                  Partial_Reveal_VTable *obj_vt,
                                  PncLiveObject *from_pnc,
                                  PncLiveObject *to_pnc,
                                  pn_info &collector,
                                  INTER_SLOT_MODE collect_inter_slots) {
    if(obj_vt == wpo_vtable && !g_treat_wpo_as_normal) {
        weak_pointer_object *wpo = (weak_pointer_object*)p_obj;
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        pp_target_object.set(&(wpo->m_key));
        local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots);
        pp_target_object.set(&(wpo->m_value));
        local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots);
        pp_target_object.set(&(wpo->m_finalizer));
        local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots);
        // other extra fields in the wpo type will be scanned below
    }

#ifdef WRITE_BUFFERING
    /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
       but potentially only reachable from the transaction_info field of the object.
       check if the transaction_info contains a forwarding pointer, and scan the slot
       if appropriate
     */
    local_scan_object_transaction_info(p_obj,collector);
#endif // WRITE_BUFFERING

    struct GC_VTable_Info *obj_gcvt = obj_vt->get_gcvt();

    if (obj_gcvt->gc_object_has_slots) {
        if (is_vt_array(obj_vt)) {
            local_scan_one_array_object(p_obj, obj_vt, from_pnc, to_pnc, collector, collect_inter_slots);
        }

        unsigned int *offset_scanner = init_object_scanner_from_vt (obj_vt);
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }

        // ========================= HANDLE WEAK REFERENCES ========================================
        offset_scanner = init_object_scanner_weak_from_vt (obj_vt);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            local_scan_one_slot (from_pnc, to_pnc, pp_target_object, collector, collect_inter_slots, true);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }
} // local_scan_one_object

#endif // NEW_APPROACH

void generic_scan_debug_slot_start_default(void *env) {}
void generic_scan_process_obj_slot_default(Slot p_slot, Partial_Reveal_Object *obj, void *env, bool is_weak) {}

class GenericScanControl {
public:
    void *env;
    void (*debug_slot_start)(void *env);
    void (*process_obj_slot)(Slot p_slot, Partial_Reveal_Object *obj, void *env, bool is_weak);

    GenericScanControl(void) {
        debug_slot_start = generic_scan_debug_slot_start_default;
        process_obj_slot = generic_scan_process_obj_slot_default;
    }
};

class PN_collection_info {
public:
    pn_info *collector;
    PncLiveObject *from_pnc;
    PncLiveObject *to_pnc;
    INTER_SLOT_MODE collect_inter_slots;
};

void pn_collection_debug_slot_start(void *env) {
#if defined _DEBUG
    PN_collection_info *info = (PN_collection_info*)env;
    info->collector->num_slots++;
#endif
}

void pn_collection_process_obj_slot(Slot p_slot, Partial_Reveal_Object *p_obj, void *env, bool is_weak) {
    PN_collection_info *info = (PN_collection_info*)env;

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,info->collector)) {
    case PUBLIC_HEAP:
    case GLOBAL:
        if(info->collect_inter_slots == YES_INTER_SLOT ||
           info->collect_inter_slots == ONLY_INTER_SLOT) {
            if(is_weak) {
                assert(0 && "Weak reference from local to public object while collecting inter slots.\n");
            } else {
                info->collector->add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
		if(info->collect_inter_slots == PRIVATE_HEAP_SLOT) {
#ifdef _DEBUG
            if(info->collector->ph_ofstream) {
                std::string to_prefix;
                std::string dest_prefix;
                if(pgc_is_vtable_immutable((VTable*)info->to_pnc->vt)) {
                    if(info->to_pnc->vt->get_gcvt()->gc_object_has_slots) {
                        to_prefix = "IR";
                    } else {
                        to_prefix = "IL";
                    }
                } else {
                    if(info->to_pnc->vt->get_gcvt()->gc_object_has_slots) {
                        to_prefix = "MR";
                    } else {
                        to_prefix = "ML";
                    }
                }
                if(pgc_is_vtable_immutable((VTable*)p_obj->vt())) {
                    if(p_obj->vt()->get_gcvt()->gc_object_has_slots) {
                        dest_prefix = "IR";
                    } else {
                        dest_prefix = "IL";
                    }
                } else {
                    if(p_obj->vt()->get_gcvt()->gc_object_has_slots) {
                        dest_prefix = "MR";
                    } else {
                        dest_prefix = "ML";
                    }
                }
                (*info->collector->ph_ofstream) << to_prefix.c_str() << info->to_pnc->old_location << " -> " <<
                                          dest_prefix.c_str() << p_obj << " ;\n";
            }
#endif
			if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(info->to_pnc, p_obj, &(info->collector->mark_stack));
			}
		}
		if(info->collect_inter_slots != NO_INTER_SLOT) {
            info->collector->add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
		}
		return;
#endif // PUB_PRIV
    case PRIVATE_NURSERY:
        // A pointer into the private nursery.
		if(info->collect_inter_slots != ONLY_INTER_SLOT
#ifdef PUB_PRIV
		 && info->collect_inter_slots != PRIVATE_HEAP_SLOT
#endif // PUB_PRIV
		 ) {
            // If this object isn't already marked then add it to the mark stack.
            // The main loop will mark it later.
            if(!p_obj->isMarked()) {
                push_bottom_on_local_mark_stack(info->to_pnc, p_obj, &(info->collector->mark_stack));
            }

            if(is_weak) {
                info->collector->add_intra_weak_slot((Partial_Reveal_Object**)p_slot.get_value());
            } else {
                info->collector->add_intra_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
            }
        }
        return;
    default:
        assert(0);
        return;
    }
}

inline void generic_scan_one_slot(Slot p_slot,
                                  GenericScanControl *control,
                                  bool is_weak=false) {
#if defined _DEBUG
    control->debug_slot_start(control->env);
#endif

    assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return;
    }
    Partial_Reveal_Object *p_obj = p_slot.dereference();

    if(is_object_pointer(p_obj)) {
        control->process_obj_slot(p_slot, p_obj, control->env, is_weak);
    }
} // generic_scan_one_slot

// Returns true if this array has an entry that points into the private heap.
inline void generic_scan_one_array_object(Partial_Reveal_Object *p_object,
                                          Partial_Reveal_VTable *obj_vt,
                                          GenericScanControl *control) {
    Type_Info_Handle tih = class_get_element_type_info(obj_vt->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        unsigned start_index = 0;

        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        for (int32 i=start_index; i < array_length; i++) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, obj_vt));
            p_element.base = p_object;
            generic_scan_one_slot(p_element, control);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(obj_vt->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
        unsigned int *offset_scanner = NULL;

        unsigned start_index = 0;

        // Offsets assume that the object starts with vtable pointer.  We'll set our
        // fake object to scan 4 bytes before the start of the value type to adjust.
        void *cur_value_type_entry_as_object = (Byte*)p_object + first_elem_offset - base_offset + (start_index * elem_size);

        int i;
        for(i = start_index; i < array_length; i++) {
            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
            Slot pp_target_object(NULL);
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                generic_scan_one_slot (pp_target_object, control);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // ========================== WEAK REFERENCES ===========================================
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
            pp_target_object.base = p_object;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                generic_scan_one_slot (pp_target_object, control, true);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
} // generic_scan_one_array_object

void generic_scan_one_object(Partial_Reveal_Object *p_obj,
                             Partial_Reveal_VTable *obj_vt,
                             GenericScanControl *control) {
    if(obj_vt == wpo_vtable && !g_treat_wpo_as_normal) {
        weak_pointer_object *wpo = (weak_pointer_object*)p_obj;
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        pp_target_object.set(&(wpo->m_key));
        generic_scan_one_slot (pp_target_object, control);
        pp_target_object.set(&(wpo->m_value));
        generic_scan_one_slot (pp_target_object, control);
        pp_target_object.set(&(wpo->m_finalizer));
        generic_scan_one_slot (pp_target_object, control);
        // other extra fields in the wpo type will be scanned below
    }

#ifdef WRITE_BUFFERING
    /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
       but potentially only reachable from the transaction_info field of the object.
       check if the transaction_info contains a forwarding pointer, and scan the slot
       if appropriate
     */
    local_scan_object_transaction_info(p_obj,collector);
#endif // WRITE_BUFFERING

    struct GC_VTable_Info *obj_gcvt = obj_vt->get_gcvt();

    if (obj_gcvt->gc_object_has_slots) {
        if (is_vt_array(obj_vt)) {
            generic_scan_one_array_object(p_obj, obj_vt, control);
        }

        unsigned int *offset_scanner = init_object_scanner_from_vt (obj_vt);
        Slot pp_target_object(NULL);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            generic_scan_one_slot (pp_target_object, control);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }

        // ========================= HANDLE WEAK REFERENCES ========================================
        offset_scanner = init_object_scanner_weak_from_vt (obj_vt);
        pp_target_object.base = p_obj;

        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            generic_scan_one_slot (pp_target_object, control, true);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }
} // generic_scan_one_object

//===========================================================================================================

#ifdef RECORD_IPS
class ip_info {
public:
    unsigned count;
    std::string frame_name;

    ip_info(const std::string &name) : frame_name(name), count(1) {}
};
std::map<void *,ip_info> g_record_ips;
#endif // RECORD_IPS

Partial_Reveal_Object *gc_malloc_from_thread_chunk_or_null(unsigned size, Allocation_Handle ah, GC_Thread_Info *tls_for_gc);

#ifdef TRACK_IPS
std::set<void *> previous_ips, this_ips;
#endif // TRACK_IPS

//===========================================================================================================

extern std::map<void*,EscapeIpInfo> g_escape_ips;

#if defined _DEBUG
void debug_record_pn_object_stats(pn_info *local_collector, struct Partial_Reveal_VTable *obj_trace_vt,unsigned size) {
    local_collector->num_marks++;
    if(pgc_is_vtable_immutable((struct VTable*)obj_trace_vt)) {
        if(obj_trace_vt->get_gcvt()->gc_object_has_slots) {
            local_collector->num_immutable_ref++;
        } else {
            local_collector->num_immutable_refless++;
        }
    } else {
        if(obj_trace_vt->get_gcvt()->gc_object_has_slots) {
            local_collector->num_mutable_ref++;
        } else {
            local_collector->num_mutable_refless++;
        }
    }
#if 0
#ifdef TYPE_SURVIVAL
    std::pair<std::map<struct Partial_Reveal_VTable *,TypeSurvivalInfo>::iterator, bool> res;
    res = local_collector->m_type_survival.insert(std::pair<struct Partial_Reveal_VTable*,TypeSurvivalInfo>(obj_trace_vt,TypeSurvivalInfo(1,size)));
    if(res.second == false) {
        (res.first)->second.m_num_objects++;
        (res.first)->second.m_num_bytes+=size;
    }
#endif // TYPE_SURVIVAL
#endif
}
#endif

bool lc_finalize_object_not_marked(Partial_Reveal_Object *obj) {
    return !obj->isMarked();
}

//===========================================================================================================

void local_mark_loop(pn_info *local_collector,
                     unsigned &size_survivors,
                     NEW_LOCATION_CLASSIFIER default_new_loc,
                     INTER_SLOT_MODE collect_inter_slots,
                     uint64_t *count,
                     void (*end_loop)(Partial_Reveal_Object*)) {
    Partial_Reveal_Object *obj_to_trace;

    while(1) {
#ifdef NEW_APPROACH
        obj_to_trace = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
#else
        ObjectPath op = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
        obj_to_trace = op.to;
#endif
        if(!obj_to_trace) break;  // no more objects to mark
        if(obj_to_trace->isMarked()) {
            continue;  // already marked
        }

        Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

#ifdef NEW_APPROACH
        local_collector->stay_mark(obj_to_trace);
#else
        PncLiveObject *cur_pnc;

        NEW_LOCATION_CLASSIFIER class_to_use = default_new_loc;

#ifdef PUB_PRIV
        if(g_keep_mutable && g_keep_mutable_closure && !pgc_is_vtable_immutable((struct VTable*)obj_trace_vt)) {
            class_to_use = POINTS_TO_MUTABLE;

            PncLiveObject *pnc_temp = op.from;
            while(pnc_temp) {
                if(pnc_temp->new_loc_class == CAN_STAY) {
                    pnc_temp->new_loc_class = POINTS_TO_MUTABLE;
                } else {
                    break;
                }
                pnc_temp = pnc_temp->previous_object;
            }
        }
#endif // PUB_PRIV

        local_collector->live_objects->push_back(PncLiveObject(obj_to_trace,class_to_use,obj_trace_vt,op.from));
        cur_pnc = local_collector->live_objects->get_last_addr();
#endif

        size_survivors += obj_size;

        if(count) {
            ++(*count);
        }

#if defined _DEBUG
        debug_record_pn_object_stats(local_collector,obj_trace_vt,obj_size);
#endif

        obj_to_trace->mark(); // mark the object as being visited
#ifdef NEW_APPROACH
        local_scan_one_object(obj_to_trace, obj_trace_vt, *local_collector, collect_inter_slots);
#else
        local_scan_one_object(obj_to_trace, obj_trace_vt, op.from, cur_pnc, *local_collector, collect_inter_slots);
#endif
        if(end_loop) {
            end_loop(obj_to_trace);
        }
    }
} // local_mark_loop

void setLowBit(Partial_Reveal_Object *p_obj) {
    p_obj->setLowFlag();
}

void cheney_roots(GC_Small_Nursery_Info *private_nursery,
                  struct PrtStackIterator *additional_si_to_walk,
                  pn_info *local_collector,
                  void *thread_handle,
                  bool use_watermarks) {
    unsigned root_index;

#ifdef DETAILED_PN_TIMES
    TIME_STRUCT _detail_start_time, _detail_end_time;
    gc_time_start_hook(&_detail_start_time);
#endif

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_prepare_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    // Enumerate Stack Roots
    PrtRseInfo rse;
    rse.callback = &local_root_callback;
    rse.env = local_collector;

#if defined _DEBUG
    unsigned frame_count = 0;
#endif // DEBUG

    // First enumerate each frame on the task's stack.
    PrtStackIterator _si;
    PrtStackIterator *si = &_si;
#ifdef HAVE_PTHREAD_H
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)thread_handle);
#else  // HAVE_PTHREAD_H
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, PrtTrue);
#endif // HAVE_PTHREAD_H
    while (!prtIsActivationPastEnd(si)) {

        if (use_pillar_watermarks /* global version */ && use_watermarks /* local version */) {
            if (prtHasFrameBeenVisited(si)) {
                // No need to walk the stack beyond the point of the first frame we saw in a previous GC
                // since any roots there will already point into the public heap.  We still have to walk
                // the whole stack though if we are trying to collect inter slots.
                break;
            } else {
                // Mark the frame as visited so that the next time through we don't have to walk this frame.
                prtMarkFrameAsVisited(si);
            }
        }

        local_collector->current_si = si;
        prtEnumerateRootsOfActivation(si, &rse);
        prtNextActivation(si);
#if defined _DEBUG
        ++frame_count;
#endif // DEBUG
    }

    if(additional_si_to_walk) {
        while (!prtIsActivationPastEnd(additional_si_to_walk)) {
            local_collector->current_si = additional_si_to_walk;
            prtEnumerateRootsOfActivation(additional_si_to_walk, &rse);
            prtNextActivation(additional_si_to_walk);
        }
    }

#if defined _DEBUG
    private_nursery->frame_count += frame_count;
#endif // DEBUG

    local_collector->current_si = NULL;
    // Now enumerate the task's VSE roots.
    prtEnumerateVseRootsOfTask((PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, &rse);
    prtEnumerateTlsRootSet((PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, &rse);

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_stackwalk_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

#if 0
#ifdef PUB_PRIV
    std::ofstream *rs_dump;
    if(g_dump_rs_vtables) {
        char buf[30];
        sprintf(buf,"rs_vtables%d",local_collector->num_micro_collections);
        rs_dump = new std::ofstream(buf);
    }

    external_pointer *temp = (external_pointer*)private_nursery->tls_current_ceiling;
    while(temp < local_collector->currently_used_nursery_end) {
        if(temp->base) {
            Partial_Reveal_Object **root = (Partial_Reveal_Object**)temp->slot.get_value();
		    if(is_object_pointer(*root)) {
	            local_collector->m_roots.push_back(root);
			}
        }
        temp++;
    }


    if(g_dump_rs_vtables) {
        delete rs_dump;
    }

#endif // PUB_PRIV
#endif

    // ====weak pointer object stuff ==============
    // Process the WPO list as a list of roots.
    for(root_index = 0;
        root_index < local_collector->m_wpos.size();
        ++root_index) {
        Partial_Reveal_Object **root = (Partial_Reveal_Object**)&(local_collector->m_wpos[root_index]);
        local_collector->m_roots.push_back(root);
    }

    // ====weak pointer stuff end==============
} // cheney_roots

//===========================================================================================================

void local_nursery_roots_and_mark(GC_Small_Nursery_Info *private_nursery,
                                  struct PrtStackIterator *additional_si_to_walk,
                                  pn_info *local_collector,
                                  void *thread_handle,
                                  INTER_SLOT_MODE collect_inter_slots,
                                  Partial_Reveal_Object *escaping_object,
                                  unsigned &size_survivors,
                                  bool is_future,
                                  bool use_watermarks,
                                  bool for_local_collection,
                                  bool only_roots = false) {
    unsigned root_index;

#ifdef DETAILED_PN_TIMES
    TIME_STRUCT _detail_start_time, _detail_end_time;
    gc_time_start_hook(&_detail_start_time);
#endif

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_prepare_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    size_survivors = 0;

    Partial_Reveal_VTable *escaping_vtable = NULL;

    // If we were given an object that is escaping then do this section to mark the escaping object and
    // its transitive closure as "must be moved out of the private nursery."
    //
    // A value of 1 for escaping_object means that everything should be moved out of the private
    // nursery.
    if(escaping_object > (Partial_Reveal_Object*)1) {
        // Later we may want to move other objects optimististically that have the same vtable so remember it.
        escaping_vtable = escaping_object->vt();

#ifdef NEW_APPROACH
        push_bottom_on_local_mark_stack(escaping_object,&(local_collector->mark_stack));
        local_mark_loop(local_collector,size_survivors,MUST_LEAVE,collect_inter_slots,NULL,setLowBit);
#else
        push_bottom_on_local_mark_stack(NULL, escaping_object,&(local_collector->mark_stack));
        local_mark_loop(local_collector,size_survivors,MUST_LEAVE,collect_inter_slots,NULL,NULL);
#endif

    }

    // Enumerate Stack Roots
    PrtRseInfo rse;
    rse.callback = &local_root_callback;
    rse.env = local_collector;

#if defined _DEBUG
    unsigned frame_count = 0;
#endif // DEBUG

    // First enumerate each frame on the task's stack.
    PrtStackIterator _si;
    PrtStackIterator *si = &_si;
#ifdef HAVE_PTHREAD_H
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)thread_handle);
#else  // HAVE_PTHREAD_H
    prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, PrtTrue);
#endif // HAVE_PTHREAD_H
    while (!prtIsActivationPastEnd(si)) {

#if defined _DEBUG
#ifdef TRACK_ESCAPING_IPS
        // The only way escaping_object is set to a real object is by the write barriers.
        // If we get here via the write barrier then the first frame will be the M2U, the next
        // frame will be the write barrier, and the next frame will be for the callsite of the
        // write barrier which we wish to capture.
        if(escaping_object > (Partial_Reveal_Object*)1) {
            if(frame_count == 2) {
                std::pair<std::map<void*,EscapeIpInfo>::iterator, bool> res;
                res = g_escape_ips.insert(std::pair<void*,EscapeIpInfo>(*(si->eipPtr),1));
                if(res.second == false) {
                    (res.first)->second.m_count++;
                } else {
                    prtGetActivationString(&_si, (res.first)->second.m_description, sizeof((res.first)->second.m_description));
                }
            }
        }
#endif // TRACK_ESCAPING_IPS
#endif

#ifdef TRACK_IPS
        if(local_collector->gc_state == LOCAL_MARK_GC) {
            this_ips.insert(*(si->eipPtr));
        }
#endif // TRACK_IPS
#ifdef RECORD_IPS
        if(collect_inter_slots) {
            std::map<void*,ip_info>::iterator rip_iter = g_record_ips.find(*(si->eipPtr));
            if(rip_iter != g_record_ips.end()) {
                rip_iter->second.count++;
            } else {
                char buffer[200];
                char *act_string = prtGetActivationString(si, buffer, sizeof(buffer));
                g_record_ips.insert(std::pair<void*,ip_info>(*(si->eipPtr),ip_info(std::string(act_string))));
            }
        }
#endif // RECORD_IPS

#if CONCURRENT_DEBUG_2
        if(collect_inter_slots == YES_INTER_SLOT) {
            char buffer[200];
            char *act_string = prtGetActivationString(si, buffer, sizeof(buffer));
            fprintf(cdump,"%s\n",act_string);
        }
#endif

        if (use_pillar_watermarks /* global version */ && use_watermarks /* local version */) {
            if (prtHasFrameBeenVisited(si)) {
                // No need to walk the stack beyond the point of the first frame we saw in a previous GC
                // since any roots there will already point into the public heap.  We still have to walk
                // the whole stack though if we are trying to collect inter slots.
                if(collect_inter_slots == NO_INTER_SLOT) {
                    break;
                }
            } else {
                // Mark the frame as visited so that the next time through we don't have to walk this frame.
                prtMarkFrameAsVisited(si);
            }
        }

        local_collector->current_si = si;
        prtEnumerateRootsOfActivation(si, &rse);
        prtNextActivation(si);
#if defined _DEBUG
        ++frame_count;
#endif // DEBUG
    }

    if(additional_si_to_walk) {
        while (!prtIsActivationPastEnd(additional_si_to_walk)) {
            local_collector->current_si = additional_si_to_walk;
            prtEnumerateRootsOfActivation(additional_si_to_walk, &rse);
            prtNextActivation(additional_si_to_walk);
        }
    }

#if defined _DEBUG
    private_nursery->frame_count += frame_count;
#endif // DEBUG

    local_collector->current_si = NULL;
    // Now enumerate the task's VSE roots.
    prtEnumerateVseRootsOfTask((PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, &rse);
    prtEnumerateTlsRootSet((PrtTaskHandle)(struct PrtTaskStruct*)thread_handle, &rse);

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_stackwalk_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    if(only_roots) {
        return;
    }

    bool futures_escaped = false;

    // If we are doing this collection because a future is escaping then we check if
    // any other futures with the same vtable are in the private nursery and if so we
    // move them as well.  We know that futures will be enumerated as roots from the
    // futures package so we just scan the roots to find those futures.
    for(root_index = 0;
        root_index < local_collector->m_roots.size();
        ++root_index) {
        Partial_Reveal_Object **root = local_collector->m_roots[root_index];

		if(!is_object_pointer(*root)) continue;

        // If the root's vtable matches the future vtable that is escaping...
        if(is_future && (*root)->vt() == escaping_vtable) {
            if(get_object_location(*root,local_collector) != PRIVATE_NURSERY) {
                assert(0); // don't think this should ever happen so assert for a while until we're sure.
                printf("Future escaping vtable object enumerated but not in private nursery.\n");
                exit(-1);
            } else {
                if(!((*root)->isMarked())) {
#ifdef NEW_APPROACH
                    push_bottom_on_local_mark_stack(*root,&(local_collector->mark_stack));
#else
                    push_bottom_on_local_mark_stack(NULL,*root,&(local_collector->mark_stack));
#endif
                }
            }
        }
    }

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_push_roots_escaping_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    // Get the transitive closure of all the future objects we want to move out of the private nursery.
    local_mark_loop(local_collector,size_survivors,MUST_LEAVE,collect_inter_slots,NULL,NULL);

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_all_escaping_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    futures_escaped = true;

    // Process all the other roots that weren't processed by the above "future moving" code.
    for(root_index = 0;
        root_index < local_collector->m_roots.size();
        ++root_index) {
        Partial_Reveal_Object **root = local_collector->m_roots[root_index];

		if(!is_object_pointer(*root)) continue;

        if(!futures_escaped || (!(is_future && (*root)->vt() == escaping_vtable))) {
            if(get_object_location(*root,local_collector) != PRIVATE_NURSERY) {
                if(collect_inter_slots != NO_INTER_SLOT) {
                    local_collector->add_inter_slot(root,NULL /* no object base for roots */);
                }
            }

            if(!(*root)->isMarked()) {
#ifdef NEW_APPROACH
                push_bottom_on_local_mark_stack(*root,&(local_collector->mark_stack));
#else
                push_bottom_on_local_mark_stack(NULL,*root,&(local_collector->mark_stack));
#endif
            }
        }
    }

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_push_roots_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

#if defined _DEBUG
    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,&(local_collector->num_stack_live),NULL);
#else
    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,NULL,NULL);
#endif

#ifdef PUB_PRIV
    std::ofstream *rs_dump;
    if(g_dump_rs_vtables) {
        char buf[30];
        sprintf(buf,"rs_vtables%d",local_collector->num_micro_collections);
        rs_dump = new std::ofstream(buf);
    }

    external_pointer *temp = (external_pointer*)private_nursery->tls_current_ceiling;
    while(temp < local_collector->currently_used_nursery_end) {
        if(temp->base) {
            Partial_Reveal_Object *root = temp->slot.dereference();
		    if(!is_object_pointer(root)) continue;

            // If a slot in the private heap is written to more than once then a subsequent
            // write may put a value there that is not in the private nursery so we just ignore
            // such slots and only do anything for ones that still have points into the public
            // nursery.
            if(get_object_location(root,local_collector) == PRIVATE_NURSERY) {
                if(g_dump_rs_vtables) {
                    *rs_dump << std::hex << temp->base->vt() << std::endl;
                }
                if(!root->isMarked()) {
#ifdef NEW_APPROACH
                    push_bottom_on_local_mark_stack(root,&(local_collector->mark_stack));
#else
                    push_bottom_on_local_mark_stack(NULL,root,&(local_collector->mark_stack));
#endif
                }
            }
        }
        temp++;
    }


    if(g_dump_rs_vtables) {
        delete rs_dump;
    }

#endif // PUB_PRIV

    // Process all the other roots from the remembered set.
    for(/* root_index will be the last root index before adding the remembered set to it*/ ;
        root_index < local_collector->m_roots.size();
        ++root_index) {
        Partial_Reveal_Object **root = local_collector->m_roots[root_index];

		if(!is_object_pointer(*root)) continue;

        if(get_object_location(*root,local_collector) != PRIVATE_NURSERY) {
            assert(0);
        }

#ifdef NEW_APPROACH
        push_bottom_on_local_mark_stack(*root,&(local_collector->mark_stack));
#else
        push_bottom_on_local_mark_stack(NULL,*root,&(local_collector->mark_stack));
#endif
    }

#if defined _DEBUG
    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,&(local_collector->num_rs_live),NULL);
#else
    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,NULL,NULL);
#endif

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    local_collector->mark_all_time.QuadPart += get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif

    // ====weak pointer object stuff ==============
    // Process the WPO list as a list of roots.
    for(root_index = 0;
        root_index < local_collector->m_wpos.size();
        ++root_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_wpos[root_index];

		assert(is_object_pointer(root));
        assert(get_object_location(root,local_collector) == PRIVATE_NURSERY);

#ifdef NEW_APPROACH
        push_bottom_on_local_mark_stack(root,&(local_collector->mark_stack));
#else
        push_bottom_on_local_mark_stack(NULL,root,&(local_collector->mark_stack));
#endif
    }

    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,NULL,NULL);

    // ====weak pointer stuff end==============

    // ====finalize stuff ==============
    // Process the finalize list as a list of roots.
    for(root_index = 0;
        root_index < local_collector->m_finalize.size();
        ++root_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_finalize[root_index];

		assert(is_object_pointer(root));
        assert(get_object_location(root,local_collector) == PRIVATE_NURSERY);

        if(!root->isMarked()) {
            if(for_local_collection) {
                local_collector->m_to_be_finalize.push_back(root);
            }
#ifdef NEW_APPROACH
            push_bottom_on_local_mark_stack(root,&(local_collector->mark_stack));
#else
            push_bottom_on_local_mark_stack(NULL,root,&(local_collector->mark_stack));
#endif
        }
    }
    if(for_local_collection) {
        std::vector<Partial_Reveal_Object*>::iterator new_end =
            std::remove_if(local_collector->m_finalize.begin(), local_collector->m_finalize.end(), lc_finalize_object_not_marked);
        local_collector->m_finalize.erase(new_end, local_collector->m_finalize.end());
    }

    // don't know whether CAN_STAY or MUST_LEAVE is better here.
    local_mark_loop(local_collector,size_survivors,CAN_STAY,collect_inter_slots,NULL,NULL);
    // ====finalize stuff end==============
} // local_nursery_roots_and_mark

//===========================================================================================================

void local_undo_marks(pn_info *local_collector) {
    Partial_Reveal_Object *p_obj;
    assert(local_collector);

#ifdef NEW_APPROACH
#if 1
    pn_info::mb_iter live_iter;
    for(live_iter = local_collector->start_iterator();
        live_iter.valid();
        ++live_iter) {
        (*live_iter)->unmark();
    }
#else
    Partial_Reveal_Object *this_obj = NULL;
    this_obj = (Partial_Reveal_Object*)local_collector->start_iterator();
    while(this_obj) {
        this_obj->unmark();
        this_obj = (Partial_Reveal_Object*)local_collector->next_iterator();
    }
#endif
#else
    ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

    for(live_object_iter  = local_collector->live_objects->begin();
        live_object_iter != local_collector->live_objects->end();
        ++live_object_iter) {

        PncLiveObject *pnc = live_object_iter.get_addr();
        p_obj = pnc->old_location;
        p_obj->unmark();
    }
#endif
} // local_undo_marks

//===========================================================================================================

#ifdef CONCURRENT

void concurrent_scan_one_object(Partial_Reveal_Object *p_obj,
                                std::deque<Partial_Reveal_Object*> &gray_list,
                                CONCURRENT_SCAN_MODE csm);

void add_to_grays_local(pn_info *local_collector,Partial_Reveal_Object *obj) {
#ifdef _DEBUG
#ifdef NO_GRAY_SWEEPING
    unsigned *orp_local = (unsigned*)orp_get_gc_thread_local();
    if(orp_local) {
        GC_Thread_Info *thread = orp_local_to_gc_local(orp_local);
        GC_Small_Nursery_Info *private_nursery = thread->get_private_nursery();
        if(private_nursery->concurrent_state_copy != CONCURRENT_MARKING) {
            printf("Making an object gray when not in marking mode.\n");
        }
    } else {
        if(g_concurrent_gc_state != CONCURRENT_MARKING) {
            printf("Concurrent thread making an object gray when not in marking mode.\n");
        }
    }
#endif // NO_GRAY_SWEEPING
#endif // _DEBUG

    orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
    local_collector->m_concurrent_gray_objects.push_back(obj);
    orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
}

void add_to_grays_local(pn_info *local_collector,std::vector<Partial_Reveal_Object*> &gray_list) {
    if(gray_list.empty()) return;

#ifdef _DEBUG
#ifdef NO_GRAY_SWEEPING
    unsigned *orp_local = (unsigned*)orp_get_gc_thread_local();
    if(orp_local) {
        GC_Thread_Info *thread = orp_local_to_gc_local(orp_local);
        GC_Small_Nursery_Info *private_nursery = thread->get_private_nursery();
        if(private_nursery->concurrent_state_copy != CONCURRENT_MARKING) {
            printf("Making an object gray when not in marking mode.\n");
        }
    } else {
        if(g_concurrent_gc_state != CONCURRENT_MARKING) {
            printf("Concurrent thread making an object gray when not in marking mode.\n");
        }
    }
#endif // NO_GRAY_SWEEPING
#endif

    orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
    for(unsigned i=0;i<gray_list.size();++i) {
        local_collector->m_concurrent_gray_objects.push_back(gray_list[i]);
    }
    orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
    gray_list.clear();
}

void make_object_gray_local(pn_info *local_collector,Partial_Reveal_Object *obj) {
#ifdef _DEBUG
//    g_remembered_lives.insert(obj);
#endif // _DEBUG

    if(!is_object_pointer(obj)) {
        return;
    }

#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(obj) ) {
        if(obj->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(obj) == 4);
            add_to_grays_local(local_collector,obj);
        }
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP

    if(mark_header_and_block_atomic(obj)) {
        if(obj->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(obj) == 4);
            add_to_grays_local(local_collector,obj);
        }
    }
} // make_object_gray

void make_object_gray_in_concurrent_thread(Partial_Reveal_Object *obj,
                                           std::deque<Partial_Reveal_Object *> &gray_list) {
#ifdef _DEBUG
//    g_remembered_lives.insert(obj);
#endif // _DEBUG

#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(obj) ) {
        if(obj->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(obj) == 4);
            gray_list.push_back(obj);
        }
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP

    if(mark_header_and_block_atomic(obj)) {
        if(obj->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(obj) == 4);
            gray_list.push_back(obj);
        }
    }
} // make_object_gray

//===========================================================================================================

void report_one_slot_to_concurrent_gc(Partial_Reveal_Object *pro,
                                      pn_info *local_collector,
                                      std::vector<Partial_Reveal_Object*> &gray_list) {
    OBJECT_LOCATION location = get_object_location(pro,local_collector);
    if(location == PRIVATE_NURSERY) return;

#ifdef FILTER_NON_HEAP
    if(location == GLOBAL) {
        if(pro->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(pro) == 4);
            gray_list.push_back(pro);
        }
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP

    if(mark_header_and_block_atomic(pro)) {
        if(pro->vt()->get_gcvt()->gc_object_has_slots) {
            assert(sizeof(pro) == 4);
            gray_list.push_back(pro);
        }
    }
} // report_one_slot_to_concurrent_gc

void report_inter_slots_to_concurrent_gc_src_dest(pn_info *local_collector,pn_info *destination_collector) {
    pn_info::inter_iterator inter_iter;
    Slot one_slot(NULL);

    for(inter_iter  = local_collector->inter_slots->begin();
        inter_iter != local_collector->inter_slots->end();
        ++inter_iter) {
        one_slot.set(inter_iter->slot);
        Partial_Reveal_Object *pro = one_slot.dereference();
        if(!pro) continue;

#ifndef NO_IMMUTABLE_UPDATES
        if(separate_immutable && incremental_compaction) {
            MovedObjectIterator moved_iter;
            moved_iter = g_moved_objects.find(pro);
            if(moved_iter != g_moved_objects.end()) {
                pro = moved_iter->second.m_new_location;
                one_slot.unchecked_update(pro);
            }
        }
#endif // NO_IMMUTABLE_UPDATES

        report_one_slot_to_concurrent_gc(pro, local_collector, local_collector->new_moved_grays);
    } // while (one_slot)

    ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;

    for(interior_pointer_iter  = local_collector->interior_pointer_table_public.begin();
        interior_pointer_iter != local_collector->interior_pointer_table_public.end();
        ++interior_pointer_iter) {

        slot_offset_entry entry = interior_pointer_iter.get_current();
        report_one_slot_to_concurrent_gc(entry.base, local_collector, local_collector->new_moved_grays);
    }

    add_to_grays_local(destination_collector,local_collector->new_moved_grays);
}

void report_inter_slots_to_concurrent_gc(pn_info *local_collector) {
    report_inter_slots_to_concurrent_gc_src_dest(local_collector,local_collector);
}

#endif // CONCURRENT

//===========================================================================================================
#ifndef NEW_APPROACH
#if 1
inline bool PncLiveObjectPtrCmp(const PncLiveObject *a, const PncLiveObject *b) {
    return a->old_location < b->old_location;
}
#endif
#endif

//#define ALLOC_TIMES

unsigned ADAPTIVE_DECREASING_SAMPLE_SIZE = 1;
unsigned ADAPTIVE_SEARCHING_SAMPLE_SIZE  = 2;
unsigned ADAPTIVE_PHASE_SAMPLE_SIZE      = 100000;
unsigned ADAPTIVE_HIGH_LOW_SAMPLE_SIZE   = 100000;
float    ADAPTIVE_HIGH_LOW_FACTOR        = 0.05;
float    ADAPTIVE_PHASE_THRESHOLD        = 2.0;

//#define ADAPTIVE_DEBUG

void do_nursery_size_adaptation(
    const TIME_STRUCT &_start_time,
    const TIME_STRUCT &_end_time,
    pn_info *local_collector,
    GC_Small_Nursery_Info *private_nursery) {
    unsigned int time = get_time_in_microseconds(_start_time, _end_time);
#if 0
    if(time > 1000) {
        printf("Time over 1ms. %d\n",time);
    }
#endif // 0

    unsigned space_used_this_time = ((uintptr_t)private_nursery->tls_current_free - (uintptr_t)private_nursery->start);
    if(space_used_this_time > 10000) {
        local_collector->adaptive_cumul_time += time;
        local_collector->adaptive_cumul_size += space_used_this_time;
        local_collector->adaptive_sample++;

        if(local_collector->adaptive_mode == ADAPTIVE_DECREASING) {
            if(local_collector->adaptive_sample == (local_collector->adaptive_main_thread ? 15 : 1) * ADAPTIVE_DECREASING_SAMPLE_SIZE) {
                local_collector->adaptive_bottom_res = (double)local_collector->adaptive_cumul_time / local_collector->adaptive_cumul_size;
#ifdef ADAPTIVE_DEBUG
                printf("Adaptive decreasing: %f %f %f %d %d %d %I64u %I64u\n",
                    local_collector->adaptive_top_res,
                    local_collector->adaptive_middle_res,
                    local_collector->adaptive_bottom_res,
                    local_collector->adaptive_top,
                    local_collector->adaptive_middle,
                    local_collector->adaptive_bottom,
                    local_collector->adaptive_cumul_time,
                    local_collector->adaptive_cumul_size);
#endif // ADAPTIVE_DEBUG

                if(local_collector->adaptive_bottom_res <= local_collector->adaptive_middle_res) {
                    local_collector->adaptive_top = local_collector->adaptive_middle;
                    local_collector->adaptive_middle = local_collector->adaptive_bottom;
                    local_collector->adaptive_bottom = local_collector->adaptive_bottom / 2;

                    local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                    local_collector->adaptive_middle_res = local_collector->adaptive_bottom_res;
                    local_collector->adaptive_bottom_res = 0;

                    local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_bottom;
                } else {
                    if(local_collector->adaptive_top > local_nursery_size) {
                        local_collector->adaptive_mode = ADAPTIVE_PHASE_DETECT;
                        local_collector->adaptive_middle_res = local_collector->adaptive_bottom_res;
                        local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                        local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_bottom;
                        local_collector->adaptive_num_successive_excessive = 0;
                    } else {
                        local_collector->adaptive_searching_current_size = (local_collector->adaptive_middle + local_collector->adaptive_top) / 2;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                        local_collector->adaptive_mode = ADAPTIVE_SEARCHING;
                    }
                }

                local_collector->adaptive_sample     = 0;
                local_collector->adaptive_cumul_time = 0;
                local_collector->adaptive_cumul_size = 0;
            }
        } else if (local_collector->adaptive_mode == ADAPTIVE_SEARCHING) {
            if(local_collector->adaptive_sample == (local_collector->adaptive_main_thread ? 15 : 1) * ADAPTIVE_SEARCHING_SAMPLE_SIZE) {
                double current_res = (double)local_collector->adaptive_cumul_time / local_collector->adaptive_cumul_size;
#ifdef ADAPTIVE_DEBUG
                printf("Adaptive searching: %f %f %f %d %d %d %d %I64u %I64u %f\n",
                    local_collector->adaptive_top_res,
                    local_collector->adaptive_middle_res,
                    local_collector->adaptive_bottom_res,
                    local_collector->adaptive_top,
                    local_collector->adaptive_middle,
                    local_collector->adaptive_bottom,
                    local_collector->adaptive_cumul_time,
                    local_collector->adaptive_cumul_size,
                    local_collector->adaptive_searching_current_size,
                    current_res);
#endif // ADAPTIVE_DEBUG

                if(local_collector->adaptive_searching_current_size == (local_collector->adaptive_middle + local_collector->adaptive_top) / 2) {
                    local_collector->adaptive_high_middle_res = current_res;
                    local_collector->adaptive_searching_current_size = (local_collector->adaptive_middle + local_collector->adaptive_bottom) / 2;
                    local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                } else {
                    if(current_res <= local_collector->adaptive_middle_res) {
#ifdef ADAPTIVE_DEBUG
                        if(local_collector->adaptive_high_middle_res < local_collector->adaptive_middle_res) {
                            printf("ADAPTIVE_SEARCHING both high and low middle less than middle.\n");
                        }
#endif // ADAPTIVE_DEBUG
                        local_collector->adaptive_top = local_collector->adaptive_middle;
                        local_collector->adaptive_top_res = local_collector->adaptive_middle_res;
                        local_collector->adaptive_middle = local_collector->adaptive_searching_current_size;
                        local_collector->adaptive_middle_res = current_res;
                        local_collector->adaptive_searching_current_size = (local_collector->adaptive_middle + local_collector->adaptive_top) / 2;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                    } else {
                        if(local_collector->adaptive_high_middle_res < local_collector->adaptive_middle_res) {
                            local_collector->adaptive_bottom = local_collector->adaptive_middle;
                            local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                            local_collector->adaptive_middle = (local_collector->adaptive_bottom + local_collector->adaptive_top) / 2;
                            local_collector->adaptive_middle_res = current_res;
                            local_collector->adaptive_searching_current_size = (local_collector->adaptive_middle + local_collector->adaptive_top) / 2;
                            local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                        } else {
                            local_collector->adaptive_searching_current_size = local_collector->adaptive_middle;
                            local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_middle;
                            local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                            local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                            local_collector->adaptive_mode = ADAPTIVE_PHASE_DETECT;
                            local_collector->adaptive_num_successive_excessive = 0;
                        }
                    }
                }

                local_collector->adaptive_sample     = 0;
                local_collector->adaptive_cumul_time = 0;
                local_collector->adaptive_cumul_size = 0;
            }
        } else if (local_collector->adaptive_mode == ADAPTIVE_PHASE_DETECT) {
            if(local_collector->adaptive_sample == (local_collector->adaptive_main_thread ? (40 * ADAPTIVE_DECREASING_SAMPLE_SIZE) : ADAPTIVE_PHASE_SAMPLE_SIZE)) {
                double current_res = (double)local_collector->adaptive_cumul_time / local_collector->adaptive_cumul_size;
                local_collector->adaptive_sample     = 0;
                local_collector->adaptive_cumul_time = 0;
                local_collector->adaptive_cumul_size = 0;

#ifdef ADAPTIVE_DEBUG
                printf("Adaptive phase: %u %f %f\n",
                    local_collector->adaptive_searching_current_size,
                    local_collector->adaptive_middle_res,
                    current_res);
#endif // ADAPTIVE_DEBUG

                if(current_res < (local_collector->adaptive_middle_res * (1.0 / (ADAPTIVE_PHASE_THRESHOLD*1.4)))) {
                    local_collector->adaptive_mode   = ADAPTIVE_HIGH_LOW;

                    local_collector->adaptive_middle = local_collector->adaptive_searching_current_size;
                    local_collector->adaptive_top    = (unsigned)(local_collector->adaptive_middle * (1.0 + ADAPTIVE_HIGH_LOW_FACTOR));
                    local_collector->adaptive_bottom = (unsigned)(local_collector->adaptive_middle * (1.0 - ADAPTIVE_HIGH_LOW_FACTOR));

                    local_collector->adaptive_top_res    = 0.0;
                    local_collector->adaptive_middle_res = (current_res + local_collector->adaptive_middle_res) / 2;
                    local_collector->adaptive_bottom_res = 0.0;

                    local_collector->adaptive_searching_current_size = local_collector->adaptive_bottom;
                    local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                } else if(current_res > (local_collector->adaptive_middle_res * ADAPTIVE_PHASE_THRESHOLD)) {
                    if(++local_collector->adaptive_num_successive_excessive >= 2) {
                        local_collector->adaptive_mode   = ADAPTIVE_HIGH_LOW;

                        local_collector->adaptive_middle = local_collector->adaptive_searching_current_size;
                        local_collector->adaptive_top    = (unsigned)(local_collector->adaptive_middle * (1.0 + ADAPTIVE_HIGH_LOW_FACTOR));
                        local_collector->adaptive_bottom = (unsigned)(local_collector->adaptive_middle * (1.0 - ADAPTIVE_HIGH_LOW_FACTOR));

                        local_collector->adaptive_top_res    = 0.0;
                        local_collector->adaptive_middle_res = current_res;
                        local_collector->adaptive_bottom_res = 0.0;

                        local_collector->adaptive_searching_current_size = local_collector->adaptive_bottom;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                    }
                } else {
                    local_collector->adaptive_num_successive_excessive = 0;
                    if(current_res > local_collector->adaptive_top_res) {
                        local_collector->adaptive_top_res = current_res;
                    }
                    if(current_res < local_collector->adaptive_bottom_res) {
                        local_collector->adaptive_bottom_res = current_res;
                    }
                }

                if(local_collector->adaptive_main_thread) {
                    local_collector->adaptive_main_thread = false;
                }
            }
        } else if (local_collector->adaptive_mode == ADAPTIVE_HIGH_LOW) {
            assert(!local_collector->adaptive_main_thread);
            if(local_collector->adaptive_sample == ADAPTIVE_HIGH_LOW_SAMPLE_SIZE) {
                double current_res = (double)local_collector->adaptive_cumul_time / local_collector->adaptive_cumul_size;
#ifdef ADAPTIVE_DEBUG
                printf("Adaptive high-low: %f %d %d %d %I64u %I64u %d %f\n",
                    local_collector->adaptive_middle_res,
                    local_collector->adaptive_top,
                    local_collector->adaptive_middle,
                    local_collector->adaptive_bottom,
                    local_collector->adaptive_cumul_time,
                    local_collector->adaptive_cumul_size,
                    local_collector->adaptive_searching_current_size,
                    current_res);
#endif // ADAPTIVE_DEBUG

                // We are testing lower than the previous nursery size.
                if(local_collector->adaptive_searching_current_size == local_collector->adaptive_bottom) {
                    // Was less better?
                    if(current_res <= local_collector->adaptive_middle_res) {
                        // Repeat the process getting less until you find something that is worse.
                        local_collector->adaptive_top    = local_collector->adaptive_middle;
                        local_collector->adaptive_middle = local_collector->adaptive_bottom;
                        local_collector->adaptive_bottom = (unsigned)(local_collector->adaptive_middle * (1.0 - ADAPTIVE_HIGH_LOW_FACTOR));

                        local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                        local_collector->adaptive_middle_res = current_res;

                        local_collector->adaptive_searching_current_size = local_collector->adaptive_bottom;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                    } else {
                        // If top_res == 0.0 then the initial attempt at a lower size was worse so now we explore larger sizes.
                        if(local_collector->adaptive_top_res == 0.0) {
                            // Try something bigger.
                            local_collector->adaptive_bottom_res = current_res;
                            local_collector->adaptive_searching_current_size = local_collector->adaptive_top;
                            local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                        } else {
                            // We have been in the searching progressively lower mode and have now found where middle_res is better than top or bottom.
                            local_collector->adaptive_searching_current_size = local_collector->adaptive_middle;
                            local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_middle;
                            local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                            local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                            local_collector->adaptive_mode = ADAPTIVE_PHASE_DETECT;
                            local_collector->adaptive_num_successive_excessive = 0;
                        }
                    }
                } else {
                    // We were testing greater.  Was it any better?
                    if(current_res < local_collector->adaptive_middle_res) {
                        // Repeat the process getting greater until you find something that is worse.
                        local_collector->adaptive_bottom = local_collector->adaptive_middle;
                        local_collector->adaptive_middle = local_collector->adaptive_top;
                        local_collector->adaptive_top    = (unsigned)(local_collector->adaptive_middle * (1.0 + ADAPTIVE_HIGH_LOW_FACTOR));

                        local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                        local_collector->adaptive_middle_res = current_res;

                        local_collector->adaptive_searching_current_size = local_collector->adaptive_top;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_searching_current_size;
                    } else {
                        // We got here either because the initial lower and higher attempt were both worse than middle
                        // or because we searched progressively higher and eventually got to a point where it got worse.
                        // In either case, the action is the same.  Set the size to the current middle var and steady state.
                        local_collector->adaptive_searching_current_size = local_collector->adaptive_middle;
                        local_collector->currently_used_nursery_end = private_nursery->tls_current_ceiling = (char*)local_collector->local_nursery_start + local_collector->adaptive_middle;
                        local_collector->adaptive_top_res    = local_collector->adaptive_middle_res;
                        local_collector->adaptive_bottom_res = local_collector->adaptive_middle_res;
                        local_collector->adaptive_mode = ADAPTIVE_PHASE_DETECT;
                        local_collector->adaptive_num_successive_excessive = 0;
                    }
                }

                local_collector->adaptive_sample     = 0;
                local_collector->adaptive_cumul_time = 0;
                local_collector->adaptive_cumul_size = 0;
            }
        } else {
            printf("Unknown adaptive nursery mode.\n");
            assert(0);
        }
    }
} // do_nursery_size_adaptation

void cache_nurseries(GC_Thread_Info *tls_for_gc, GC_Nursery_Info *&public_nursery,GC_Nursery_Info *&immutable_nursery) {
    public_nursery = tls_for_gc->get_nursery();
    if(local_nursery_size) {
        immutable_nursery = tls_for_gc->get_immutable_nursery();
    }
} // cache_nurseries

#ifdef CONCURRENT_DEBUG_2
FILE *cdump = NULL;
#endif

uint32 tgc_lockedXAddUint32(volatile uint32 * loc, uint32 addend) {
#ifdef _WIN32
#ifdef INTEL64
    register uint32 val;
    __asm {
        mov rcx, loc
        mov eax, addend
        lock xadd dword ptr[rcx], eax
        mov val, eax
    }
    return val;
#else // !INTEL64
    register uint32 val;
    __asm {
        mov ecx, loc
        mov eax, addend
        lock xadd dword ptr[ecx], eax
        mov val, eax
    }
    return val;
#endif // INTEL64
#else                           /* Linux gcc */
    register uint32 val;
    __asm__ __volatile__(
        "lock; xaddl %0,(%1)"
        : "=r"(val)
        : "r"(loc), "0"(addend)
        : "memory");

    return val;
#endif                          /* _WIN32 */
}

void alloc_and_init_pn(GC_Small_Nursery_Info *private_nursery);
GCEXPORT(void, gc_force_gc)() ;

void pn_cheney_debug_slot_start(void *env) {
#if defined _DEBUG
    PN_collection_info *info = (PN_collection_info*)env;
    info->collector->num_slots++;
#endif
}

// This is like the NO_INTER_SLOT case.
void pn_cheney_process_obj_slot(Slot p_slot, Partial_Reveal_Object *p_obj, void *env, bool is_weak) {
    PN_cheney_info *info = (PN_cheney_info*)env;

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,info->collector)) {
    case PUBLIC_HEAP:
    case GLOBAL:
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
#endif
        return;
    case PRIVATE_NURSERY:
        // If this object isn't already marked then add it to the mark stack.
        // The main loop will mark it later.
		cheney_process_slot(p_slot.base, (Partial_Reveal_Object**)p_slot.get_value(), info);
        return;
    default:
        assert(0);
        return;
    }
}

void add_spaces_for_nurseries(cheney_spaces &cspaces, GC_Nursery_Info *public_nursery, GC_Nursery_Info *immutable_nursery, cheney_space_pair *& cheney_pair) {
    if(!public_nursery) {
        printf("public nursery should not be NULL in add_space_for_nurseries\n");
        exit(-1);
    }
    cspaces.add_entry(cheney_space_pair(public_nursery->tls_current_free, immutable_nursery ? immutable_nursery->tls_current_free : NULL));
    cheney_pair = cspaces.get_last_addr();
}

void cheney_spaces::process(PN_cheney_info *pci) {
    cheney_spaces::iterator cspace_iter;
    for(cspace_iter  = begin();
        cspace_iter != end();
        ++cspace_iter) {
		if(g_two_space_pn) {
			if(!target_two_space) {
				printf("target_two_space not specified in two space mode.\n");
				exit(-1);
			}
			// Process the public heap to space and the pn target two space until nothing new is added
			// to either one.
			while(cspace_iter.get_addr()->process(pci) || target_two_space->process(pci));
		} else {
		cspace_iter.get_addr()->process(pci);
    }
}
}

void pn_space::copy_rs_entry(external_pointer *entry) {
	ceiling = (char*)ceiling - sizeof(external_pointer);
	external_pointer *new_entry = (external_pointer*)ceiling;
	new_entry->base = entry->base;
	new_entry->slot = entry->slot;
}

void pn_space::add_rs_entry(Partial_Reveal_Object *base, Partial_Reveal_Object **slot) {
	external_pointer *new_external_pointer = (external_pointer*)((char*)ceiling - sizeof(external_pointer));
	if(new_external_pointer <= free) {
		printf("Copied objects plus remembered set entries exceed size of target two space.\n");
		exit(-1);
	}
	ceiling = new_external_pointer;
	new_external_pointer->base = base;
	new_external_pointer->slot = slot;
}

bool cheney_space_pair::process(void *env) {
	bool processed_something = false;

    GenericScanControl gsc;
    gsc.process_obj_slot = pn_cheney_process_obj_slot;
#if defined _DEBUG
    gsc.debug_slot_start = pn_cheney_debug_slot_start;
#endif
    gsc.env = env;

    while(pub_cheney_ptr < pub_space.end || im_cheney_ptr < im_space.end) {
        while(pub_cheney_ptr < pub_space.end) {
			processed_something = true;
            Partial_Reveal_VTable *vt = pub_cheney_ptr->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(pub_cheney_ptr,vt);
            if(!alignment_vt(vt)) {
                generic_scan_one_object(pub_cheney_ptr,vt,&gsc);
            }
            pub_cheney_ptr = (Partial_Reveal_Object*)(((char*)pub_cheney_ptr) + obj_size);
        }
        while(im_cheney_ptr < im_space.end) {
			processed_something = true;
            Partial_Reveal_VTable *vt = im_cheney_ptr->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(im_cheney_ptr,vt);
            if(!alignment_vt(vt)) {
                generic_scan_one_object(im_cheney_ptr,vt,&gsc);
            }
            im_cheney_ptr = (Partial_Reveal_Object*)(((char*)im_cheney_ptr) + obj_size);
        }
    }
	return processed_something;
}

bool pn_space::process(void *env) {
	bool processed_something = false;

    GenericScanControl gsc;
    gsc.process_obj_slot = pn_cheney_process_obj_slot;
#if defined _DEBUG
    gsc.debug_slot_start = pn_cheney_debug_slot_start;
#endif
    gsc.env = env;

	while(cheney_ptr < free) {
		processed_something = true;
		Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)cheney_ptr;
        Partial_Reveal_VTable *vt = p_obj->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
        if(!alignment_vt(vt)) {
            generic_scan_one_object(p_obj,vt,&gsc);
        }
        cheney_ptr = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
	}
	return processed_something;
}

void global_gc_cheney_process_obj_slot(Slot p_slot, Partial_Reveal_Object *p_obj, void *env, bool is_weak) {
    pn_info *just_for_non_pn_roots = (pn_info*)env;

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,just_for_non_pn_roots)) {
    case PUBLIC_HEAP:
    case GLOBAL:
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
#endif
//        gc_add_root_set_entry((Managed_Object_Handle*)p_slot.get_value(),FALSE);
        return;
    case PRIVATE_NURSERY:
        if(p_obj->isForwarded()) {
//            printf("Found forwarded object during global_gc_cheney_process_obj_slot\n");
        } else {
            if(!(p_obj->isMarked())) {
                p_obj->mark();
                push_bottom_on_local_mark_stack(NULL,p_obj,&(just_for_non_pn_roots->mark_stack));
            }
        }
        return;
    default:
        assert(0);
        return;
    }
}

void cheney_space_pair::global_gc_get_pn_lives(void *env) {
    Partial_Reveal_Object *p_obj     = (Partial_Reveal_Object*)pub_space.begin;
    Partial_Reveal_Object *p_imm_obj = (Partial_Reveal_Object*) im_space.begin;

    GenericScanControl gsc;
    gsc.process_obj_slot = global_gc_cheney_process_obj_slot;
    gsc.env = env;

    while(p_obj < pub_space.end || p_imm_obj < im_space.end) {
        while(p_obj < pub_space.end) {
            Partial_Reveal_VTable *vt = p_obj->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
            if(!alignment_vt(vt)) {
                generic_scan_one_object(p_obj,vt,&gsc);
            }
            p_obj = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
        }
        while(p_imm_obj < im_space.end) {
            Partial_Reveal_VTable *vt = p_imm_obj->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(p_imm_obj,vt);
            if(!alignment_vt(vt)) {
                generic_scan_one_object(p_imm_obj,vt,&gsc);
            }
            p_imm_obj = (Partial_Reveal_Object*)(((char*)p_imm_obj) + obj_size);
        }
    }
}

void pn_space::global_gc_get_pn_lives(void *env) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)start;

    GenericScanControl gsc;
    gsc.process_obj_slot = global_gc_cheney_process_obj_slot;
    gsc.env = env;

    while(p_obj < free) {
        Partial_Reveal_VTable *vt = p_obj->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
        if(!alignment_vt(vt)) {
            generic_scan_one_object(p_obj,vt,&gsc);
        }
        p_obj = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
    }
}

CPS_RESULT cheney_process_slot(Partial_Reveal_Object *base, Partial_Reveal_Object **slot, PN_cheney_info *info) {
//    if(!is_object_pointer(*slot)) return;
    Partial_Reveal_Object *obj = *slot;
	if(!info->collector->address_in_pn(obj)) {
		return CPS_NOT_IN_PN;
	}

	CPS_RESULT res = CPS_COPY;

retry:
    Partial_Reveal_Object *new_location;
    if(obj->isForwarded()) {
        // The object pointed to by the slot has already been seen so just update the slot by removing the forwarding bit from the vtable location which should hold the new location.
        new_location = (Partial_Reveal_Object*)obj->get_vt_no_low_bits();
#ifdef PUB_PRIV
		if(g_use_pub_priv &&
		   g_two_space_pn &&
		   info->collector->two_spaces[!info->collector->two_space_in_use].contains(new_location)) {
		    if(get_object_location(slot,info->collector) == PRIVATE_HEAP) {
#ifdef _DEBUG
    			info->rs_created++;
#endif // _DEBUG
	    	    info->collector->two_spaces[!info->collector->two_space_in_use].add_rs_entry(base, slot);
#ifdef _DEBUG
				if(live_dump) {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 1 " << base << " " << obj << "\n";
				}
#endif // _DEBUG
			} else {
#ifdef _DEBUG
				if(live_dump) {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 2 " << base << " " << obj << "\n";
				}
#endif // _DEBUG
			}
		} else {
#ifdef _DEBUG
			if(live_dump) {
				if(g_use_pub_priv &&
				   g_two_space_pn &&
				   get_object_location(slot,info->collector) != PRIVATE_HEAP) {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 3 " << base << " " << obj << "\n";
				} else {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 4 " << base << " " << obj << "\n";
				}
			}
#endif // _DEBUG
		}
#else // PUB_PRIV
#ifdef _DEBUG
		if(live_dump) {
			*live_dump << "CHENEY " << info->collector->num_micro_collections << " 4 " << base << " " << obj << "\n";
		}
#endif
#endif // PUB_PRIV
		res = CPS_FORWARDED;
    } else {
		// If a slot in the private heap gets written to more than once where each write is to a
		// private nursery object then multiple entries will be added to the remembered set.  The
		// first such remembered set entry to be processed will (possibly) update the slot to point
		// to a new object location in the other side of the pn two space.  Here, we detect that such
		// a previous rs entry for this slot has been processed and already updated so there is no
		// work we need to do here.
		if(g_use_pub_priv &&
		   g_two_space_pn &&
		   info->collector->two_spaces[!info->collector->two_space_in_use].contains(obj)) {
		   return CPS_ALREADY_TARGET_TWO_SPACE;
//		   printf("Object is not set as forwarded but found cheney slot object in target two space.\n");
		}

        REMOVE_INDIR_RES rir;
        if((rir = remove_one_indirection(obj, slot, 6)) == RI_REPLACE_NON_OBJ) {
            // New value is not a pointer so return.
            return CPS_AFTER_INDIRECTION_NON_OBJ;
        }
        if(rir == RI_REPLACE_OBJ) {
            switch(get_object_location(obj,info->collector)) {
            case PUBLIC_HEAP:
            case GLOBAL:
#ifdef PUB_PRIV
            case PRIVATE_HEAP:
#endif
                return CPS_AFTER_INDIRECTION_NOT_IN_PN;
            case PRIVATE_NURSERY:
                goto retry;
            default:
                assert(0);
				exit(-1);
                //return;
            }
        }

        Partial_Reveal_Object *frontier;

        // Determine new object location
        Partial_Reveal_VTable *obj_vtable = obj->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(obj,obj_vtable);
        bool obj_is_immutable = pgc_is_vtable_immutable((struct VTable*)obj_vtable);

#if defined _DEBUG
        if(obj_is_immutable) {
            info->immutable_copied += obj_size;
            if(obj_vtable->get_gcvt()->gc_object_has_slots) {
                info->collector->num_immutable_ref++;
            } else {
                info->collector->num_immutable_refless++;
            }
        } else {
            info->mutable_copied   += obj_size;
            if(obj_vtable->get_gcvt()->gc_object_has_slots) {
                info->collector->num_mutable_ref++;
            } else {
                info->collector->num_mutable_refless++;
            }
        }
        info->collector->num_marks++;
#endif // DEBUG

#ifdef PUB_PRIV
		if(g_use_pub_priv  &&
		   g_two_space_pn  &&
		   info->two_space &&
		  (g_pure_two_space || (
		   obj >= info->collector->two_spaces[info->collector->two_space_in_use].get_fill() &&
		   (!g_prevent_rs || get_object_location(slot,info->collector) != PRIVATE_HEAP)))) {
			new_location = info->collector->two_spaces[!info->collector->two_space_in_use].allocate(obj_vtable, obj_size);
			// If the slot wasn't also moved to the other side of the two-space then it must be in the private heap
			// which means we need a remembered set entry here.

#ifdef _DEBUG
			info->two_space_copy += obj_size;
#endif

			if(get_object_location(slot,info->collector) == PRIVATE_HEAP) {
#ifdef _DEBUG
				info->rs_created++;
#endif
				info->collector->two_spaces[!info->collector->two_space_in_use].add_rs_entry(base, slot);
#if 0
				// FIX FIX FIX
				printf("If slot is not in target two space then we need to create a remembered set entry.\n");
				assert(0);
				exit(-1);
#endif
#ifdef _DEBUG
				if(live_dump) {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 5 " << base << " " << obj << "\n";
				}
#endif // _DEBUG
			} else {
#ifdef _DEBUG
				if(live_dump) {
					*live_dump << "CHENEY " << info->collector->num_micro_collections << " 6 " << base << " " << obj << "\n";
				}
#endif // _DEBUG
			}
			res = CPS_TWO_SPACE_COPY;
			goto copy_obj;
		}
#endif

#ifdef _DEBUG
		if(live_dump) {
#ifdef PUB_PRIV
			if(get_object_location(slot,info->collector) != PRIVATE_HEAP) {
				*live_dump << "CHENEY " << info->collector->num_micro_collections << " 7 " << base << " " << obj << "\n";
			} else {
				*live_dump << "CHENEY " << info->collector->num_micro_collections << " 8 " << base << " " << obj << "\n";
			}
#else  // PUB_PRIV
			*live_dump << "CHENEY " << info->collector->num_micro_collections << " 8 " << base << " " << obj << "\n";
#endif // PUB_PRIV
		}
#endif // _DEBUG

		GC_Nursery_Info *nursery_to_use = *(info->public_nursery);
        cheney_space *used_space = &((*info->cur_cheney_pair)->pub_space);
#ifdef PUB_PRIV
        bool private_heap_block = false;
        if(g_use_pub_priv) {
            private_heap_block = true;
        }
        if((g_use_pub_priv || separate_immutable) && obj_is_immutable) {
            nursery_to_use = *(info->immutable_nursery);
            used_space = &((*info->cur_cheney_pair)->im_space);
//            private_heap_block = false; // for use when you put immutable in the public space but there are serious unresolved questions with this approach!
        }
#else
        if(separate_immutable && obj_is_immutable) {
            nursery_to_use = *(info->immutable_nursery);
            used_space = &((*info->cur_cheney_pair)->im_space);
        }
#endif // PUB_PRIV

        frontier = (Partial_Reveal_Object *)nursery_to_use->tls_current_free;
        unsigned skip_size = adjust_frontier_to_alignment(frontier, obj_vtable);

        POINTER_SIZE_INT new_free = (obj_size + (uintptr_t)frontier);
        if (new_free <= (uintptr_t) nursery_to_use->tls_current_ceiling) {
            if(skip_size) {
                *((POINTER_SIZE_INT*)((char*)frontier - skip_size)) = skip_size;
            }
            frontier->set_vtable((uintptr_t)obj_vtable);
            // increment free ptr and return object
            nursery_to_use->tls_current_free = (void *) new_free;
            new_location = frontier;
            used_space->end = (void*)new_free;
        } else {
#ifdef DETAILED_PN_TIMES
            num_slow_allocs++;
#endif // DETAILED_PN_TIMES
            info->lock_myself->resume();
            // GC may happen here!!!
            new_location = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
                obj_size,
                (uintptr_t)obj_vtable,
                info->tls_for_gc,
                nursery_to_use
#ifdef PUB_PRIV
                ,private_heap_block
#endif // PUB_PRIV
                );
            cache_nurseries(info->tls_for_gc, *(info->public_nursery), *(info->immutable_nursery));
            cheney_space_pair temp_space(new_location,NULL,(char*)new_location + obj_size);
            info->cspaces->add_entry(temp_space);
            add_spaces_for_nurseries(*(info->cspaces), *(info->public_nursery), *(info->immutable_nursery), *(info->cur_cheney_pair));
            info->lock_myself->suspend();
        }

	copy_obj:
#ifdef _DEBUG
        info->num_surviving++;
        info->size_surviving += obj_size;
#endif
        memcpy(new_location,obj,obj_size);
        obj->set_forwarding_pointer(new_location);
    }

    *slot = new_location;
	return res;
} // cheney_process_slot

void cheney_space_pair::forbid_compaction_here(Block_Store *block_store) {
    pub_space.forbid_compaction_here(block_store);
    im_space.forbid_compaction_here(block_store);
}

void cheney_space::forbid_compaction_here(Block_Store *block_store) {
    if(global_is_in_heap((Partial_Reveal_Object*)begin)) {
        block_info *bi = GC_BLOCK_INFO(begin);
        bi->is_compaction_block = false;
        block_store->disable_compaction(bi->block_store_info_index);
    }
}

void cheney_space::mark_objects_in_block(void) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)begin;

    while(p_obj < end) {
        while(p_obj < end) {
            Partial_Reveal_VTable *vt = p_obj->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
            if(!alignment_vt(vt)) {
                mark_object_in_block(p_obj);
            }
            p_obj = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
        }
    }
}

void local_nursery_collection(GC_Thread_Info *tls_for_gc,
                              struct PrtStackIterator *si,
                              Partial_Reveal_Object *escaping_object,
                              bool is_future) {
    if(g_cheney) {
        TIME_STRUCT _start_time, _end_time;
        if(verbose_gc) {
            gc_time_start_hook(&_start_time);
        }

        PrtSuspendSelf cheney_lock_myself;
        GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();
        pn_info *local_collector = private_nursery->local_gc_info;
        assert(local_collector);
        if (local_collector->gc_state != LOCAL_MARK_IDLE) {
            printf("local_nursery_collection: local_collector->gc_state=%u, not LOCAL_MARK_IDLE\n", local_collector->gc_state);  fflush(stdout);
        }
        assert(local_collector->gc_state == LOCAL_MARK_IDLE);

        local_collector->gc_state = LOCAL_MARK_ACTIVE;

        cheney_roots(private_nursery,NULL,local_collector,tls_for_gc->thread_handle,true);
        unsigned root_index;

        GC_Nursery_Info *public_nursery    = NULL;
        GC_Nursery_Info *immutable_nursery = NULL;
        cache_nurseries(tls_for_gc,public_nursery,immutable_nursery);
        cheney_space_pair *cur_cheney_space = NULL;

        add_spaces_for_nurseries(local_collector->cspaces, public_nursery, immutable_nursery, cur_cheney_space);
		local_collector->cspaces.set_target_two_space(&local_collector->two_spaces[!local_collector->two_space_in_use]);
        PN_cheney_info pci;
        pci.collector = local_collector;
        pci.tls_for_gc = tls_for_gc;
        pci.public_nursery = &public_nursery;
        pci.immutable_nursery = &immutable_nursery;
        pci.lock_myself = &cheney_lock_myself;
        pci.cur_cheney_pair = &cur_cheney_space;
        pci.cspaces = &(local_collector->cspaces);
		// We can't use the two-space approach (yet) if there is an escaping object.
		if(escaping_object) {
			pci.two_space = false;
		} else {
			pci.two_space = true;
		}

		unsigned cps_reses[CPS_END];
		memset(&cps_reses,0,sizeof(cps_reses));

		external_pointer *temp = (external_pointer*)private_nursery->tls_current_ceiling;
		while(temp < local_collector->currently_used_nursery_end) {
			if(temp->base) {
				Partial_Reveal_Object **root = (Partial_Reveal_Object**)temp->slot.get_value();
				if(is_object_pointer(*root)) {
#ifdef _DEBUG
					pci.num_rs++;
#endif
					++cps_reses[cheney_process_slot(temp->base, root , &pci)];
				}
			}
			temp++;
		}

        for(root_index = 0;
            root_index < local_collector->m_roots.size();
            ++root_index) {
            Partial_Reveal_Object **root = local_collector->m_roots[root_index];

		    if(!is_object_pointer(*root)) continue;
            if(get_object_location(*root,local_collector) == PRIVATE_NURSERY) {
                cheney_process_slot(NULL, root, &pci);
            } else {
                assert(1);
            }
        }

		local_collector->cspaces.process(&pci);

        unsigned final_index;
        unsigned used_index = 0;
        for(final_index = 0;
            final_index < local_collector->m_finalize.size();
          ++final_index) {
            Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_finalize[final_index];

		    assert(is_object_pointer(root));
            assert(get_object_location(root,local_collector) == PRIVATE_NURSERY);

            if(!root->isForwarded()) {
                local_collector->m_to_be_finalize.push_back(root);
                Partial_Reveal_Object **final_slot = &(local_collector->m_to_be_finalize[local_collector->m_to_be_finalize.size()-1]);
                cheney_process_slot(NULL, final_slot, &pci);
            } else {
                if(final_index != used_index) {
                    local_collector->m_finalize[used_index++] = root;
                }
            }
        }
        local_collector->m_finalize.resize(used_index);

		local_collector->cspaces.process(&pci);

#if defined _DEBUG
        private_nursery->num_survivors  += pci.num_surviving;
        private_nursery->size_survivors += pci.size_surviving;
        unsigned saved_space_used        = ((POINTER_SIZE_INT)private_nursery->tls_current_free - (POINTER_SIZE_INT)private_nursery->start);
        private_nursery->space_used     += saved_space_used;
        private_nursery->size_objects_escaping += pci.size_surviving;
#endif // _DEBUG

        // Update interior pointer table entries.
        ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;

        for(interior_pointer_iter  = local_collector->interior_pointer_table.begin();
            interior_pointer_iter != local_collector->interior_pointer_table.end();
            ++interior_pointer_iter) {
            slot_offset_entry entry = interior_pointer_iter.get_current();
            void **root_slot = entry.slot;
            Partial_Reveal_Object *root_base = entry.base;
            POINTER_SIZE_INT root_offset = entry.offset;
            void *new_slot_contents = (void *)((Byte*)root_base + root_offset);
            if (new_slot_contents != *root_slot) {
                *root_slot = new_slot_contents;
            }
        }

        if(!local_collector->m_wpos.empty()) {
            while ((LONG) InterlockedCompareExchange( (LONG *)(&p_global_gc->_wpo_lock), (LONG) 1, (LONG) 0) == (LONG) 1);

            unsigned num_staying = 0;

            for(unsigned wpo_index = 0;
                wpo_index < local_collector->m_wpos.size();
                ++wpo_index) {

                Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_wpos[wpo_index];

                if(get_object_location(root,local_collector) == PRIVATE_NURSERY) {
                    if(wpo_index != num_staying) {
                        local_collector->m_wpos[num_staying++] = (weak_pointer_object*)root;
                    }
                } else {
                    p_global_gc->m_wpos.push_back((weak_pointer_object*)root);
                }
            }

            local_collector->m_wpos.resize(num_staying);
            p_global_gc->_wpo_lock = 0;
        }

        if(!local_collector->m_finalize.empty()) {
            unsigned final_index;
            unsigned num_staying = 0;

            p_global_gc->lock_finalize_list();

            for(final_index = 0;
                final_index < local_collector->m_finalize.size();
                ++final_index) {
                Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_finalize[final_index];

                if(get_object_location(root,local_collector) == PRIVATE_NURSERY) {
                    if(final_index != num_staying) {
                        local_collector->m_finalize[num_staying++] = root;
                    }
                } else {
                    p_global_gc->add_finalize_object_prelocked(root,false);
                }
            }

            p_global_gc->unlock_finalize_list();
            local_collector->m_finalize.resize(num_staying);
        }

        for(unsigned wpo_index = 0;
            wpo_index < local_collector->m_to_be_finalize.size();
            ++wpo_index) {
            Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_to_be_finalize[wpo_index];
            orp_finalize_object(root);
        }
        local_collector->m_to_be_finalize.clear();

        local_collector->num_micro_collections++;

#if 0
        char *next_compact_spot_in_pn = (char*)private_nursery->start;

#ifdef PUB_PRIV
        external_pointer *ep_temp = ((external_pointer*)local_collector->currently_used_nursery_end) - 1;
        external_pointer *next_copy_ep = ep_temp;
        ++next_copy_ep;
        // We can do allocations from the end of the private nursery backwards.
        // The ceiling in use by threads is private_nursery->tls_current_ceiling which
        // gets decreased every time such an object is allocated where as
        // local_collector->currently_used_nursery_end holds the real ceiling and
        // enables us to restore the active ceiling to the real value at this point
        // where those temporarily allocated objects are no longer needed.

        // The ceiling for the next private nursery is where the last external pointer was
        // created or maintained.  This will be equal to currently_used_nursery_end
        // if there are no external pointers.
        if(private_nursery->tls_current_ceiling < next_copy_ep) {
            memset(private_nursery->tls_current_ceiling,0,((POINTER_SIZE_INT)next_copy_ep - (POINTER_SIZE_INT)private_nursery->tls_current_ceiling));
        }
        private_nursery->tls_current_ceiling = next_copy_ep;
        // tls_current_free can point into the middle of remembered set if more
        // external pointers are created than existed on the way in.  If this is the
        // case then adjust tls_current_free to just after the last byte that needs to
        // be zeroed.
        if( private_nursery->tls_current_free > private_nursery->tls_current_ceiling) {
            private_nursery->tls_current_free = private_nursery->tls_current_ceiling;
        }
#endif // PUB_PRIV

        // tls_current_free is still set to the end of the last object allocated in the private nursery
        memset(next_compact_spot_in_pn,0,((POINTER_SIZE_INT)private_nursery->tls_current_free - (POINTER_SIZE_INT)next_compact_spot_in_pn));

        local_collector->num_micro_collections++;

//        private_nursery->tls_current_free = (char*)private_nursery->start;
        private_nursery->tls_current_free = next_compact_spot_in_pn;
#else
		char *zero_start = (char*)private_nursery->start;
		char *zero_end   = (char*)private_nursery->tls_current_free;

#ifdef PUB_PRIV
        external_pointer *ep_temp = ((external_pointer*)local_collector->currently_used_nursery_end) - 1;
        external_pointer *next_copy_ep = ep_temp;
        ++next_copy_ep;
        // We can do allocations from the end of the private nursery backwards.
        // The ceiling in use by threads is private_nursery->tls_current_ceiling which
        // gets decreased every time such an object is allocated where as
        // local_collector->currently_used_nursery_end holds the real ceiling and
        // enables us to restore the active ceiling to the real value at this point
        // where those temporarily allocated objects are no longer needed.

        // The ceiling for the next private nursery is where the last external pointer was
        // created or maintained.  This will be equal to currently_used_nursery_end
        // if there are no external pointers.
        if(private_nursery->tls_current_ceiling < next_copy_ep) {
            memset(private_nursery->tls_current_ceiling,0,((POINTER_SIZE_INT)next_copy_ep - (POINTER_SIZE_INT)private_nursery->tls_current_ceiling));
        }
        private_nursery->tls_current_ceiling = next_copy_ep;
        // tls_current_free can point into the middle of remembered set if more
        // external pointers are created than existed on the way in.  If this is the
        // case then adjust tls_current_free to just after the last byte that needs to
        // be zeroed.
        if( private_nursery->tls_current_free > private_nursery->tls_current_ceiling) {
			zero_end = (char*)private_nursery->tls_current_ceiling;
        }
#endif // PUB_PRIV

		if(g_two_space_pn) {
			local_collector->two_spaces[local_collector->two_space_in_use].reset();

			if(!escaping_object) {
				local_collector->two_space_in_use = !local_collector->two_space_in_use;
			}

			private_nursery->tls_current_free    = local_collector->two_spaces[local_collector->two_space_in_use].get_free();
			private_nursery->tls_current_ceiling = local_collector->two_spaces[local_collector->two_space_in_use].get_ceiling();
			local_collector->two_spaces[local_collector->two_space_in_use].set_fill_to_free();
			local_collector->currently_used_nursery_end = local_collector->two_spaces[local_collector->two_space_in_use].get_end();
#ifdef _DEBUG
			if(g_maximum_debug) {
				POINTER_SIZE_INT size =
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_end() -
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_start();
				POINTER_SIZE_INT used =
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_free() -
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_start();
				POINTER_SIZE_INT eps =
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_end() -
					(POINTER_SIZE_INT)local_collector->two_spaces[local_collector->two_space_in_use].get_ceiling();
				printf("%d live: %d %f, rs: %d, promoted: %d, copy: %d, new_rs: %d, ",local_collector->num_micro_collections,
					pci.size_surviving,
					pci.size_surviving / (float)size,
					pci.num_rs,
					pci.size_surviving - pci.two_space_copy,
					pci.two_space_copy,
					pci.rs_created);
				for(unsigned cps_res_index = 0; cps_res_index < CPS_END; ++cps_res_index) {
					printf("%d ",cps_reses[cps_res_index]);
				}
				printf("\n");
				fflush(stdout);
			}
#endif
		} else {
	        private_nursery->tls_current_free    = (char*)private_nursery->start;
			private_nursery->tls_current_ceiling = local_collector->currently_used_nursery_end;
#ifdef _DEBUG
			if(g_maximum_debug) {
				printf("%d live: %d %f, rs: %d\n",local_collector->num_micro_collections,
					pci.num_surviving,
					pci.num_surviving / (float)local_nursery_size,
					pci.num_rs);
			}
#endif
		}
		memset(zero_start,0,((uintptr_t)zero_end - (uintptr_t)zero_start));
#endif

        local_collector->clear();
        local_collector->gc_state = LOCAL_MARK_IDLE;

        if(verbose_gc) {
            gc_time_start_hook(&_end_time);
            unsigned int time = get_time_in_microseconds(_start_time, _end_time);

#ifdef _WINDOWS
            local_collector->sum_micro_time.QuadPart += time;
#elif defined LINUX
            add_microseconds(local_collector->sum_micro_time,time);
#endif
            if(time > local_collector->max_collection_time) {
                local_collector->max_collection_time = time;
            }
        }

#ifdef _DEBUG
	    if(g_show_all_pncollect) {
			printf("u-collection: #Survive = %d , SizeSurvive = %d %f Time = %d ms\n",
				pci.num_surviving,
				pci.size_surviving,
				100.0 * ((float)pci.size_surviving/saved_space_used),
				get_time_in_microseconds(_start_time, _end_time));
			fflush(stdout);
		}
#endif

        // Indicate the end of a private nursery GC
        // needs to be moved to a better spot
        pgc_local_nursery_collection_finish();

        return;
    }

    unsigned size_surviving = 0;
    TIME_STRUCT _start_time, _end_time;
    if(verbose_gc || concurrent_mode) {
        gc_time_start_hook(&_start_time);
    }
#ifdef DETAILED_PN_TIMES
    TIME_STRUCT _detail_start_time, _detail_end_time;
#ifdef ALLOC_TIMES
    std::vector<TIME_STRUCT> alloc_times;
#endif // ALLOC_TIMES
#endif // DETAILED_PN_TIMES

    // Indicate the start of a private nursery GC
    // needs to be moved to a better spot
    pgc_local_nursery_collection_start();

    PrtSuspendSelf lock_myself;

    GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();
    pn_info *local_collector = private_nursery->local_gc_info;
    assert(local_collector);

#if 0
    printf("%d\n",local_collector->num_micro_collections);
    p_global_gc->reclaim_full_heap(0,true);
#endif

    // BTL 20090204 Two local nursery collections going on at once
    if (local_collector->gc_state != LOCAL_MARK_IDLE) {
        printf("local_nursery_collection: local_collector->gc_state=%u, not LOCAL_MARK_IDLE\n", local_collector->gc_state);  fflush(stdout);
    }
    assert(local_collector->gc_state == LOCAL_MARK_IDLE);

    local_collector->gc_state = LOCAL_MARK_ACTIVE;

#ifdef DETAILED_PN_TIMES
    unsigned num_slow_allocs = 0;
    gc_time_start_hook(&_detail_start_time);
#ifdef _DEBUG
    unsigned cur_num_survivors  = private_nursery->num_survivors;
    unsigned cur_size_survivors = private_nursery->size_survivors;
    __uint64 cur_space_used     = private_nursery->space_used;
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

#ifdef _DEBUG
    unsigned amount_old_live_data = 0, amount_new_live_data = 0;
#endif

    INTER_SLOT_MODE ism = NO_INTER_SLOT;

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                 STACK WALK AND MARK LIVE OBJECTS                                  *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
#ifdef CONCURRENT
    bool phase_switch = false;
    if(g_concurrent_gc_state != private_nursery->concurrent_state_copy) {
#ifdef _DEBUG
        printf("Thread going from concurrent state %d to %d during collection %d.\n",private_nursery->concurrent_state_copy,g_concurrent_gc_state,local_collector->num_micro_collections);
#endif
        private_nursery->concurrent_state_copy = g_concurrent_gc_state;
        ism = YES_INTER_SLOT;
        phase_switch = true;
//    local_collector->sweep_ptr_copy = g_sweep_ptr;
    }
#endif // CONCURRENT

    // Get the roots and record all live objects.
    local_nursery_roots_and_mark(private_nursery,
                                si,
                                local_collector,
                                tls_for_gc->thread_handle,
                                ism,
                                escaping_object,
                                size_surviving,
                                is_future,
                                true,  // use watermarks
                                true);

#ifdef CONCURRENT
    if(phase_switch) {
        switch(g_concurrent_gc_state) {
        case CONCURRENT_IDLE:
            tgc_lockedXAddUint32((volatile uint32 *)&p_global_gc->num_threads_remaining_until_next_phase,-1);
            private_nursery->current_state = g_concurrent_gc_state;
            break;
        case CONCURRENT_MARKING:
            report_inter_slots_to_concurrent_gc(local_collector);
            tgc_lockedXAddUint32((volatile uint32 *)&p_global_gc->num_threads_remaining_until_next_phase,-1);
            private_nursery->current_state = g_concurrent_gc_state;
            break;
        case CONCURRENT_SWEEPING:
            tgc_lockedXAddUint32((volatile uint32 *)&p_global_gc->num_threads_remaining_until_next_phase,-1);
            private_nursery->current_state = g_concurrent_gc_state;
            break;
        default:
            assert(0);
        }
    }
#endif // CONCURRENT

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned roots_and_mark_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->roots_and_mark_time.QuadPart += roots_and_mark_micro;
#endif

    unsigned i;
    Partial_Reveal_Object *p_obj, *p_dest_obj;

#if defined _DEBUG || defined DETAILED_PN_TIMES
    unsigned num_surviving = 0;
    unsigned mutable_copied = 0, immutable_copied = 0;
#endif // _DEBUG

    local_collector->gc_state = LOCAL_MARK_LIVE_IDENTIFIED;
    char *next_compact_spot_in_pn = (char*)private_nursery->start;

    GC_Nursery_Info *public_nursery    = NULL;
    GC_Nursery_Info *immutable_nursery = NULL;
    cache_nurseries(tls_for_gc,public_nursery,immutable_nursery);


    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                 DETERMINE NEW OBJECT LOCATIONS                                    *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    // determine the new location of each object
#ifdef NEW_APPROACH
    local_collector->m_forwards.reset();
    local_collector->cur_pn_live_obj_num = 0;
#if 1
    pn_info::mb_iter live_iter;
    for(live_iter = local_collector->start_iterator();
        live_iter.valid();
        ++live_iter, ++local_collector->cur_pn_live_obj_num) {
        p_obj = (*live_iter);
#else // 1
    }
    for(p_obj = (Partial_Reveal_Object*)local_collector->start_iterator();
        p_obj;
        p_obj = (Partial_Reveal_Object*)local_collector->next_iterator()) {

#endif
            Partial_Reveal_VTable *obj_vtable = (Partial_Reveal_VTable*)p_obj->get_vt_no_low_bits();
#else // NEW_APPROACH
    ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

    for(live_object_iter  = local_collector->live_objects->begin();
        live_object_iter != local_collector->live_objects->end();
        ++live_object_iter) {
        PncLiveObject *pnc = live_object_iter.get_addr();

        // Get the vtable of the object.
        Partial_Reveal_VTable *obj_vtable = pnc->vt;

        p_obj = pnc->old_location;
#endif // NEW_APPROACH

#if defined _DEBUG || defined DETAILED_PN_TIMES
        num_surviving++;
#endif // _DEBUG

        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,obj_vtable);

        Partial_Reveal_Object *new_location;
        bool obj_is_immutable = pgc_is_vtable_immutable((struct VTable*)obj_vtable);

#ifdef PUB_PRIV
#ifdef NEW_APPROACH
        if(g_keep_all || (g_keep_mutable && !obj_is_immutable) && !p_obj->isLowFlagSet()) {
            local_collector->m_forwards.add_entry(ForwardedObject(obj_vtable,(Partial_Reveal_Object*)next_compact_spot_in_pn));
            p_obj->set_forwarding_pointer(local_collector->m_forwards.get_last_addr());
            p_obj->mark();
            next_compact_spot_in_pn += obj_size;
            continue;
        }
        // For escaping objects that are mutable we have to figure out a solution here.
#else
        if(g_keep_all) {
            if(pnc->new_loc_class != MUST_LEAVE) {
                pnc->old_location->set_vtable((POINTER_SIZE_INT)pnc);
                local_collector->stay_mark(pnc->old_location);
                continue;
            }
        } else if(g_keep_mutable) {
            if(pnc->new_loc_class != MUST_LEAVE && (!obj_is_immutable || (g_keep_mutable_closure && pnc->new_loc_class == POINTS_TO_MUTABLE))) {
                pnc->old_location->set_vtable((POINTER_SIZE_INT)pnc);
                local_collector->stay_mark(pnc->old_location);
                continue;
            }
        }
#endif // NEW_APPROACH
#endif // PUB_PRIV

#ifdef _DEBUG
        if(obj_is_immutable) {
            immutable_copied += obj_size;
        } else {
            mutable_copied   += obj_size;
        }
#endif // DEBUG

        Partial_Reveal_Object *frontier;

        GC_Nursery_Info *nursery_to_use = public_nursery;
#ifdef PUB_PRIV
        bool private_heap_block = false;
        if(g_use_pub_priv) {
            private_heap_block = true;
        }
        if((g_use_pub_priv || separate_immutable) && obj_is_immutable) {
            nursery_to_use = immutable_nursery;
        }
#else
        if(separate_immutable && obj_is_immutable) {
            nursery_to_use = immutable_nursery;
        }
#endif // PUB_PRIV

        frontier = (Partial_Reveal_Object *)nursery_to_use->tls_current_free;
        adjust_frontier_to_alignment(frontier, obj_vtable);

        POINTER_SIZE_INT new_free = (obj_size + (uintptr_t)frontier);
        if (new_free <= (uintptr_t) nursery_to_use->tls_current_ceiling) {
            frontier->set_vtable((uintptr_t)obj_vtable);
            // increment free ptr and return object
            nursery_to_use->tls_current_free = (void *) new_free;
            new_location = frontier;
        } else {
#ifdef DETAILED_PN_TIMES
            num_slow_allocs++;
#endif // DETAILED_PN_TIMES
            lock_myself.resume();
            // GC may happen here!!!
            new_location = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
                obj_size,
                (uintptr_t)obj_vtable,
                tls_for_gc,
                nursery_to_use
#ifdef PUB_PRIV
                ,private_heap_block
#endif // PUB_PRIV
                );
            cache_nurseries(tls_for_gc,public_nursery,immutable_nursery);
            lock_myself.suspend();
        }

#ifdef NEW_APPROACH
        p_obj->set_forwarding_pointer(new_location);
#else
        pnc->new_location = new_location;
        pnc->old_location->set_vtable((uintptr_t)pnc);
#endif

        // We have to put the array size in the new object location because at the next object allocation some
        // GC might happen and then we need to know how to walk the new object.
        if (is_vt_array(obj_vtable) && get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
            unsigned array_length_offset = pgc_array_length_offset(obj_vtable);
            unsigned *p_array_length = (unsigned*)((char*)new_location + array_length_offset);
            *p_array_length = vector_get_length_with_vt((Vector_Handle)p_obj,obj_vtable);
        }

#ifdef CONCURRENT
        switch(GetLocalConcurrentGcState(private_nursery)) {
        case CONCURRENT_IDLE:
#ifdef CONCURRENT_DEBUG_2
            fprintf(cdump,"Idle %p %d\n",new_location,p_global_gc->get_gc_num());
#endif
            // intentionally do nothing
            break;
        case CONCURRENT_MARKING:
#ifdef CONCURRENT_DEBUG_2
            fprintf(cdump,"Marking %p %d\n",new_location,p_global_gc->get_gc_num());
#endif
            // Make object black
            mark_header_and_block_atomic(new_location);
            gc_trace (new_location, "Marking object during marking phase when transferring to public heap.");
            break;
        case CONCURRENT_SWEEPING:
            if(new_location >= g_sweep_ptr) {
//            if(new_location >= local_collector->sweep_ptr_copy) {
#ifdef CONCURRENT_DEBUG_2
                fprintf(cdump,"Sweeping_greater %p %d\n",new_location,p_global_gc->get_gc_num());
#endif
                bool has_slots = obj_vtable->get_gcvt()->gc_object_has_slots ? true : false;
                bool mark_res = mark_header_and_block_atomic(new_location);
//                        if(has_slots) {
                    // make object gray
#ifndef NO_GRAY_SWEEPING
                if(mark_res) {
                    assert(sizeof(new_location) == 4);
                    local_collector->new_moved_grays.push_back(new_location);
                    if(local_collector->new_moved_grays.size() == local_collector->new_moved_grays.capacity()) {
//                                printf("new_moved_grays full\n");
                        // This will empty new_moved_grays.
                        add_to_grays_local(local_collector,local_collector->new_moved_grays);
                        // Should be able to add it now.
//                                local_collector->new_moved_grays.push_back(new_location);
//                                assert(local_collector->new_moved_grays.size() != local_collector->new_moved_grays.capacity());
                    }
                }
#endif // NO_GRAY_SWEEPING
//                    }
            } else {
#ifdef CONCURRENT_DEBUG_2
                fprintf(cdump,"Sweeping_less %p %d\n",new_location,p_global_gc->get_gc_num());
#endif
            }
            break;
        default:
            assert(0);
        }
#endif // CONCURRENT
#ifdef ALLOC_TIMES
        if(local_collector->live_objects->size() < 10) {
            gc_time_start_hook(&_detail_end_time);
            alloc_times.push_back(_detail_end_time);
        }
#endif // ALLOC_TIMES
    }

    // We can't naively assign new private nursery address above when we encounter
    // new objects because doing so will assign them in random order.  If you had
    // an object at the beginning of the private nursery moved farther down and
    // another object moved to the beginning then the that move would overwrite
    // the object at the beginning that hasn't been moved yet.  The solution is to
    // sort the addresses of objects that are staying and move them in this order
    // so that we are always moving objects lower and therefore can't overwrite an
    // object that hasn't been moved yet.
#ifdef PUB_PRIV
#ifdef NEW_APPROACH
#if 0
    Partial_Reveal_Object *this_obj = NULL;
    this_obj = (Partial_Reveal_Object*)local_collector->start_iterator();
    while(this_obj) {
        // if alive but not yet forwarded then must stay in the pn
        if(!this_obj->isForwarded()) {
            assert(0);
            // FIX FIX FIX
        }
        this_obj = (Partial_Reveal_Object*)local_collector->next_iterator();
    }
#endif
#else
    PncLiveObject **walk_pnc = NULL;
    if(g_keep_mutable || g_keep_all) {
        walk_pnc = (PncLiveObject**)local_collector->start_iterator();
        while(walk_pnc) {
            PncLiveObject *this_pnc = *walk_pnc;
#ifdef _DEBUG
            if(get_object_location(this_pnc->old_location,local_collector) != PRIVATE_NURSERY) {
                printf("big problem\n");
                exit(-1);
            }
#endif
            this_pnc->new_location = (Partial_Reveal_Object*)next_compact_spot_in_pn;
#ifdef _DEBUG
            if(get_object_location(this_pnc->new_location,local_collector) != PRIVATE_NURSERY) {
                printf("big problem\n");
                exit(-1);
            }
#endif
            next_compact_spot_in_pn += get_object_size_bytes_with_vt(this_pnc->old_location,this_pnc->vt);
            walk_pnc = (PncLiveObject**)local_collector->next_iterator();
        }
    }
#endif
#endif // PUB_PRIV


#ifdef CONCURRENT
#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned before_gray_add_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
#endif // DETAILED_PN_TIMES

#ifndef NO_GRAY_SWEEPING
    add_to_grays_local(local_collector,local_collector->new_moved_grays);
#endif
#endif // CONCURRENT

#if 0
    printf("%d\n",local_collector->num_micro_collections);
    p_global_gc->reclaim_full_heap(0,true);
#endif

#if defined _DEBUG
    private_nursery->num_survivors  += num_surviving;
    private_nursery->size_survivors += size_surviving;
    unsigned saved_space_used        = ((POINTER_SIZE_INT)private_nursery->tls_current_free - (POINTER_SIZE_INT)private_nursery->start);
    private_nursery->space_used     += saved_space_used;
    private_nursery->size_objects_escaping += size_surviving;
#endif // _DEBUG

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned allocate_time_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->allocate_time.QuadPart += allocate_time_micro;
//    printf("NumLives=%d ",local_collector->live_objects->size());
#endif // DETAILED_PN_TIMES

    pn_info::intra_iterator intra_iter;
    Slot one_slot(NULL);

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                 UPDATE WEAK POINTER OBJECTS                                       *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    unsigned wpo_index;
    for(wpo_index = 0;
        wpo_index < local_collector->m_wpos.size();
        ++wpo_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_wpos[wpo_index];

		assert(is_object_pointer(root));

#ifdef NEW_APPROACH
        Partial_Reveal_Object *p_new_obj = NULL;
        if(root->isMarked()) {
            p_new_obj = root->get_forwarding_pointer();
        } else {
            p_new_obj = (Partial_Reveal_Object*)root->get_raw_forwarding_pointer();
        }
#else
        PncLiveObject *plo = (PncLiveObject*)root->vt();
        Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

        local_collector->m_wpos[wpo_index] = (weak_pointer_object*)p_new_obj;
    }

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                     UPDATE FINALIZE OBJECTS                                       *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    for(wpo_index = 0;
        wpo_index < local_collector->m_finalize.size();
        ++wpo_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_finalize[wpo_index];

		assert(is_object_pointer(root));

#ifdef NEW_APPROACH
        Partial_Reveal_Object *p_new_obj = NULL;
        if(root->isMarked()) {
            p_new_obj = root->get_forwarding_pointer();
        } else {
            p_new_obj = (Partial_Reveal_Object*)root->get_raw_forwarding_pointer();
        }
#else
        PncLiveObject *plo = (PncLiveObject*)root->vt();
        Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

        local_collector->m_finalize[wpo_index] = p_new_obj;
    }
    for(wpo_index = 0;
        wpo_index < local_collector->m_to_be_finalize.size();
        ++wpo_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_to_be_finalize[wpo_index];

		assert(is_object_pointer(root));

#ifdef NEW_APPROACH
        Partial_Reveal_Object *p_new_obj = NULL;
        if(root->isMarked()) {
            p_new_obj = root->get_forwarding_pointer();
        } else {
            p_new_obj = (Partial_Reveal_Object*)root->get_raw_forwarding_pointer();
        }
#else
        PncLiveObject *plo = (PncLiveObject*)root->vt();
        Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

        local_collector->m_to_be_finalize[wpo_index] = p_new_obj;
    }

#ifdef PUB_PRIV
    external_pointer *ep_temp = ((external_pointer*)local_collector->currently_used_nursery_end) - 1;
    external_pointer *next_copy_ep = ep_temp;
    while(ep_temp >= private_nursery->tls_current_ceiling) {
        if(ep_temp->base) {
            Partial_Reveal_Object *p_obj = ep_temp->slot.dereference();

            if(get_object_location(p_obj,local_collector) == PRIVATE_NURSERY) {
		        if(!is_object_pointer(p_obj)) continue;

#ifdef NEW_APPROACH
                Partial_Reveal_Object *p_new_obj = NULL;
                if(p_obj->isMarked()) {
                    p_new_obj = p_obj->get_forwarding_pointer();
                } else {
                    p_new_obj = (Partial_Reveal_Object*)p_obj->get_raw_forwarding_pointer();
                }
#else
                PncLiveObject *plo = (PncLiveObject*)p_obj->vt();
                Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

                ep_temp->slot.unchecked_update(p_new_obj);

                // if something in the remembered set is staying in the private nursery
                if(get_object_location(p_new_obj,local_collector) == PRIVATE_NURSERY) {
                    // we don't need to copy if the src and dest are the same
                    if(next_copy_ep != ep_temp) {
                        memcpy(next_copy_ep,ep_temp,sizeof(external_pointer));
                    }
                    // set the next ep slot we can fill
                    --next_copy_ep;
                }
            }
        }

        --ep_temp;
    }
#endif // PUB_PRIV


    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                 UPDATE SLOTS IN OBJECTS TO BE MOVED                               *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    // update slots
    for(intra_iter  = local_collector->intra_slots->begin();
        intra_iter != local_collector->intra_slots->end();
        ++intra_iter) {
        one_slot.set((*intra_iter).slot);
        Partial_Reveal_Object *p_obj = one_slot.dereference();

#ifdef NEW_APPROACH
        Partial_Reveal_Object *p_new_obj = NULL;
        if(p_obj->isMarked()) {
            p_new_obj = p_obj->get_forwarding_pointer();
        } else {
            p_new_obj = (Partial_Reveal_Object*)p_obj->get_raw_forwarding_pointer();
        }
#else
        PncLiveObject *plo = (PncLiveObject*)p_obj->vt();
        Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

        // update slot
        one_slot.unchecked_update(p_new_obj);

#ifdef PUB_PRIV
        // if something in the remembered set is staying in the private nursery
        if(get_object_location(p_new_obj,local_collector) == PRIVATE_NURSERY) {
            Partial_Reveal_Object *slot_base = (*intra_iter).base;
            assert(slot_base);
            assert(get_object_location(slot_base,local_collector) == PRIVATE_NURSERY);
#ifdef NEW_APPROACH
            Partial_Reveal_Object *new_slot_base = NULL;
            if(slot_base->isMarked()) {
                new_slot_base = slot_base->get_forwarding_pointer();
            } else {
                new_slot_base = (Partial_Reveal_Object*)slot_base->get_raw_forwarding_pointer();
            }
#else
            PncLiveObject *base_plo = (PncLiveObject*)slot_base->vt();
            Partial_Reveal_Object *new_slot_base = base_plo->new_location;
#endif

            if(get_object_location(new_slot_base,local_collector) != PRIVATE_NURSERY) {
                (*intra_iter).slot = (Partial_Reveal_Object**)((char*)new_slot_base + (*intra_iter).get_offset());
                (*intra_iter).base = new_slot_base;
            } else {
                (*intra_iter).slot = NULL;
                (*intra_iter).base = NULL;
            }
        } else {
            (*intra_iter).slot = NULL;
            (*intra_iter).base = NULL;
        }
#endif
    }

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                 UPDATE WEAK SLOTS IN OBJECTS TO BE MOVED                          *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    pn_info::intra_weak_iterator intra_weak_iter;
    // update slots
    for(intra_weak_iter  = local_collector->intra_weak_slots->begin();
        intra_weak_iter != local_collector->intra_weak_slots->end();
        ++intra_iter) {
        one_slot.set((*intra_iter).slot);
        Partial_Reveal_Object *p_obj = one_slot.dereference();

        if(!p_obj->isMarked()) {
            one_slot.update(NULL);
        } else {
#ifdef NEW_APPROACH
            Partial_Reveal_Object *p_new_obj = NULL;
            if(p_obj->isMarked()) {
                p_new_obj = p_obj->get_forwarding_pointer();
            } else {
                p_new_obj = (Partial_Reveal_Object*)p_obj->get_raw_forwarding_pointer();
            }
#else
            PncLiveObject *plo = (PncLiveObject*)p_obj->vt();
            Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

            // update slot
            one_slot.unchecked_update(p_new_obj);

#ifdef PUB_PRIV
            // if something in the remembered set is staying in the private nursery
            if(get_object_location(p_new_obj,local_collector) == PRIVATE_NURSERY) {
                Partial_Reveal_Object *slot_base = (*intra_iter).base;
                assert(slot_base);
                assert(get_object_location(slot_base,local_collector) == PRIVATE_NURSERY);
#ifdef NEW_APPROACH
                Partial_Reveal_Object *new_slot_base = NULL;
                if(slot_base->isMarked()) {
                    new_slot_base = slot_base->get_forwarding_pointer();
                } else {
                    new_slot_base = (Partial_Reveal_Object*)slot_base->get_raw_forwarding_pointer();
                }
#else
                PncLiveObject *base_plo = (PncLiveObject*)slot_base->vt();
                Partial_Reveal_Object *new_slot_base = base_plo->new_location;
#endif

                if(get_object_location(new_slot_base,local_collector) != PRIVATE_NURSERY) {
                    (*intra_iter).slot = (Partial_Reveal_Object**)((char*)new_slot_base + (*intra_iter).get_offset());
                    (*intra_iter).base = new_slot_base;
                } else {
                    (*intra_iter).slot = NULL;
                    (*intra_iter).base = NULL;
                }
            } else {
                (*intra_iter).slot = NULL;
                (*intra_iter).base = NULL;
            }
#endif
        }
    }

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                            UPDATE ROOTS                                           *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    // Update roots
    for(i = 0;
        i < local_collector->m_roots.size();
        ++i) {
        Partial_Reveal_Object **root = local_collector->m_roots[i];
        Partial_Reveal_Object *p_obj = *root;

		if(!is_object_pointer(p_obj)) continue;

#ifdef NEW_APPROACH
        Partial_Reveal_Object *p_new_obj = NULL;
        if(p_obj->isMarked()) {
            p_new_obj = p_obj->get_forwarding_pointer();
        } else {
            p_new_obj = (Partial_Reveal_Object*)p_obj->get_raw_forwarding_pointer();
        }
#else
        PncLiveObject *plo = (PncLiveObject*)p_obj->vt();
        Partial_Reveal_Object *p_new_obj = plo->new_location;
#endif

        *root = p_new_obj;
    }

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                            UPDATE INTERIOR POINTERS                               *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    // Update interior pointer table entries.
    ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;

    for(interior_pointer_iter  = local_collector->interior_pointer_table.begin();
        interior_pointer_iter != local_collector->interior_pointer_table.end();
        ++interior_pointer_iter) {

        slot_offset_entry entry = interior_pointer_iter.get_current();
        void **root_slot = entry.slot;
        Partial_Reveal_Object *root_base = entry.base;
        POINTER_SIZE_INT root_offset = entry.offset;
        void *new_slot_contents = (void *)((Byte*)root_base + root_offset);
        if (new_slot_contents != *root_slot) {
            *root_slot = new_slot_contents;
        }
    }

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned update_root_time_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->update_root_time.QuadPart += update_root_time_micro;
//    printf(" Slots/Roots/Interior=%d/%d/%d\n",local_collector->intra_slots->size(),local_collector->m_roots.size(),local_collector->interior_pointer_table.size());
#endif

#ifdef _DEBUG
    char buf[100];
#endif

    /*****************************************************************************************************
     *                                                                                                   *
     *                                                                                                   *
     *                                            MOVE OBJECTS                                           *
     *                                                                                                   *
     *                                                                                                   *
     *****************************************************************************************************/
    // move objects
#ifdef NEW_APPROACH
    for(p_obj = (Partial_Reveal_Object*)local_collector->start_zero_iterator();
        p_obj;
        p_obj = (Partial_Reveal_Object*)local_collector->next_zero_iterator()) {

        assert(p_obj->isLowFlagSet());

        // We use the mark bit to indicate something staying in the PN.
        // In which case we use normal ForwardedObject in the vtable slot.
        if(p_obj->isMarked()) {
            p_obj->unmark();
            p_dest_obj = p_obj->get_forwarding_pointer();
            Partial_Reveal_VTable *p_obj_vt = p_obj->vt();
            unsigned obj_size = get_object_size_bytes_with_vt(p_obj,p_obj_vt);
            assert(get_object_location(p_dest_obj,local_collector) == PRIVATE_NURSERY);

            if(p_obj != p_dest_obj) {
                memmove(p_dest_obj,p_obj,obj_size);
            }
            p_dest_obj->set_vtable((Allocation_Handle)p_obj_vt);
            continue;
        }

        p_dest_obj = (Partial_Reveal_Object*)p_obj->get_raw_forwarding_pointer();
        assert(get_object_location(p_dest_obj,local_collector) != PRIVATE_NURSERY);
        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,p_dest_obj->vt());
        assert(p_obj != p_dest_obj);

#else
    for(live_object_iter  = local_collector->live_objects->begin();
        live_object_iter != local_collector->live_objects->end();
        ++live_object_iter) {
        PncLiveObject *pnc = live_object_iter.get_addr();

        p_obj      = pnc->old_location;
        p_dest_obj = pnc->new_location;

        if(get_object_location(p_dest_obj,local_collector) == PRIVATE_NURSERY) {
            continue;
        }
        p_obj->set_vtable((uintptr_t)pnc->vt);

        assert(p_obj != p_dest_obj);

        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,pnc->vt);
#endif // NEW_APPROACH

        // No need to recopy the vtable of each object.
        // Moreover, the source objects have their mark bits set which we don't want.
#ifdef _DEBUG
#ifdef TYPE_SURVIVAL
        std::pair<std::map<struct Partial_Reveal_VTable *,TypeSurvivalInfo>::iterator, bool> res;
        res = local_collector->m_type_survival.insert(std::pair<struct Partial_Reveal_VTable*,TypeSurvivalInfo>(pnc->vt,TypeSurvivalInfo(1,obj_size)));
        if(res.second == false) {
            (res.first)->second.m_num_objects++;
            (res.first)->second.m_num_bytes+=obj_size;
        }
#endif // TYPE_SURVIVAL
        gc_trace (p_obj, buf);
        gc_trace (p_dest_obj, buf);
#endif // _DEBUG
        memcpy((char*)p_dest_obj + 4,(char*)p_obj + 4, obj_size - 4);
    }

#ifdef PUB_PRIV
#ifndef NEW_APPROACH
    if(g_keep_mutable || g_keep_all) {
        walk_pnc = (PncLiveObject**)local_collector->start_zero_iterator();
        while(walk_pnc) {
            PncLiveObject *pnc = *walk_pnc;

            p_obj      = pnc->old_location;
            p_dest_obj = pnc->new_location;
            p_obj->set_vtable((unsigned)pnc->vt);

#ifdef _DEBUG
            if(get_object_location(p_obj,local_collector) != PRIVATE_NURSERY) {
                printf("big problem %p\n",pnc);
                exit(-1);
            }
            if(get_object_location(p_dest_obj,local_collector) != PRIVATE_NURSERY) {
                printf("big problem %p\n",pnc);
                exit(-1);
            }
#endif
            if(p_obj == p_dest_obj) {
                p_obj->unmark();
                walk_pnc = (PncLiveObject**)local_collector->next_zero_iterator();
                continue;
            }

            // No need to recopy the vtable of each object.
            // Moreover, the source objects have their mark bits set which we don't want.
#ifdef _DEBUG
            sprintf(buf,"Transfering %p in local to %p in local.",p_obj,p_dest_obj);
            gc_trace (p_obj, buf);
            gc_trace (p_dest_obj, buf);
#endif // _DEBUG
//          printf("%p %p\n",p_dest_obj,p_obj);
            memmove(p_dest_obj,p_obj,get_object_size_bytes_with_vt(p_obj,pnc->vt));
            p_dest_obj->unmark();
            assert(!p_dest_obj->isLowFlagSet());

            walk_pnc = (PncLiveObject**)local_collector->next_zero_iterator();
        }
    }
#endif // NEW_APPROACH
#endif // PUB_PRIV

    if(!local_collector->m_wpos.empty()) {
        while ((LONG) InterlockedCompareExchange( (LONG *)(&p_global_gc->_wpo_lock), (LONG) 1, (LONG) 0) == (LONG) 1);

        unsigned num_staying = 0;

        for(unsigned wpo_index = 0;
            wpo_index < local_collector->m_wpos.size();
            ++wpo_index) {

            Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_wpos[wpo_index];

            if(get_object_location(root,local_collector) == PRIVATE_NURSERY) {
                if(wpo_index != num_staying) {
                    local_collector->m_wpos[num_staying++] = (weak_pointer_object*)root;
                }
            } else {
                p_global_gc->m_wpos.push_back((weak_pointer_object*)root);
            }
        }

        local_collector->m_wpos.resize(num_staying);
        p_global_gc->_wpo_lock = 0;
    }

    if(!local_collector->m_finalize.empty()) {
        unsigned final_index;
        unsigned num_staying = 0;

        p_global_gc->lock_finalize_list();

        for(final_index = 0;
            final_index < local_collector->m_finalize.size();
            ++final_index) {
            Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_finalize[final_index];

            if(get_object_location(root,local_collector) == PRIVATE_NURSERY) {
                if(final_index != num_staying) {
                    local_collector->m_finalize[num_staying++] = root;
                }
            } else {
                p_global_gc->add_finalize_object_prelocked(root,false);
            }
        }

        p_global_gc->unlock_finalize_list();
        local_collector->m_finalize.resize(num_staying);
    }

    for(wpo_index = 0;
        wpo_index < local_collector->m_to_be_finalize.size();
        ++wpo_index) {
        Partial_Reveal_Object *root = (Partial_Reveal_Object*)local_collector->m_to_be_finalize[wpo_index];
        orp_finalize_object(root);
    }
    local_collector->m_to_be_finalize.clear();


#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned move_time_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->move_time.QuadPart += move_time_micro;
#endif // DETAILED_PN_TIMES

#ifdef PUB_PRIV
    if(g_keep_mutable || g_keep_all) {
        for(intra_iter  = local_collector->intra_slots->begin();
            intra_iter != local_collector->intra_slots->end();
            ++intra_iter) {
            if((*intra_iter).base) {
                next_copy_ep->base = (*intra_iter).base;
                next_copy_ep->slot = (*intra_iter).slot;
                --next_copy_ep;
            }
        }
        for(intra_weak_iter  = local_collector->intra_weak_slots->begin();
            intra_weak_iter != local_collector->intra_weak_slots->end();
            ++intra_iter) {
            if((*intra_iter).base) {
                next_copy_ep->base = (*intra_iter).base;
                next_copy_ep->slot = (*intra_iter).slot;
                --next_copy_ep;
            }
        }
    }
    // Up until now, next_copy_ep has pointed to the next remembered set entry to create but not
    // that there aren't any more, make it point to the last one we created.
    ++next_copy_ep;
#endif

    local_collector->gc_state = LOCAL_MARK_IDLE;
    local_collector->clear();

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned clear_time_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->clear_time.QuadPart += clear_time_micro;
#endif // DETAILED_PN_TIMES

#ifdef PUB_PRIV
    // We can do allocations from the end of the private nursery backwards.
    // The ceiling in use by threads is private_nursery->tls_current_ceiling which
    // gets decreased every time such an object is allocated where as
    // local_collector->currently_used_nursery_end holds the real ceiling and
    // enables us to restore the active ceiling to the real value at this point
    // where those temporarily allocated objects are no longer needed.

    // The ceiling for the next private nursery is where the last external pointer was
    // created or maintained.  This will be equal to currently_used_nursery_end
    // if there are no external pointers.
    if(private_nursery->tls_current_ceiling < next_copy_ep) {
        memset(private_nursery->tls_current_ceiling,0,((POINTER_SIZE_INT)next_copy_ep - (POINTER_SIZE_INT)private_nursery->tls_current_ceiling));
    }
    private_nursery->tls_current_ceiling = next_copy_ep;
    // tls_current_free can point into the middle of remembered set if more
    // external pointers are created than existed on the way in.  If this is the
    // case then adjust tls_current_free to just after the last byte that needs to
    // be zeroed.
    if( private_nursery->tls_current_free > private_nursery->tls_current_ceiling) {
        private_nursery->tls_current_free = private_nursery->tls_current_ceiling;
    }

    static unsigned num_revert = 0; // modify this in debugger for something different

    if(num_revert && local_collector->num_micro_collections+1 >= num_revert) {
        g_keep_mutable = false;
        g_keep_all     = false;
    }
#endif // PUB_PRIV

    // tls_current_free is still set to the end of the last object allocated in the private nursery
    memset(next_compact_spot_in_pn,0,((uintptr_t)private_nursery->tls_current_free - (uintptr_t)next_compact_spot_in_pn));

#ifdef _DEBUG
    gc_time_start_hook(&_end_time);
#else
    if(verbose_gc || concurrent_mode) {
        gc_time_start_hook(&_end_time);
    }
#endif // _DEBUG

#ifdef DETAILED_PN_TIMES
    gc_time_start_hook(&_detail_end_time);
    unsigned memset_time_micro = get_time_in_microseconds(_detail_start_time, _detail_end_time);
    local_collector->memset_time.QuadPart += memset_time_micro;

#ifdef _DEBUG
    if(pn_history) {
        local_collector->pn_stats->push_back(pn_collection_stats(
                        _start_time,
                        _end_time,
                        private_nursery->space_used     - cur_space_used,
                        private_nursery->num_survivors  - cur_num_survivors,
                        private_nursery->size_survivors - cur_size_survivors));
    }
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

#ifdef SURVIVE_WHERE
    local_collector->last_survive_percent = (double)size_surviving / local_nursery_size;
#endif

#ifdef PUB_PRIV
    if(g_determine_dead_rs && !g_keep_mutable && !g_keep_all) {
        gc_force_gc();
        ep_temp = (external_pointer*)private_nursery->tls_current_ceiling;
        unsigned ep_count=0, ep_count_dead = 0;
        while(ep_temp < local_collector->currently_used_nursery_end) {
            if(ep_temp->base == NULL) {
                ep_count_dead++;
            }
            ep_count++;

            ++ep_temp;
        }
        if(ep_count_dead) {
            printf("Found %d out of %d remembered set roots dead.\n",ep_count_dead,ep_count);
        }
    }
#endif // PUB_PRIV

    if(adaptive_nursery_size) {
        do_nursery_size_adaptation(_start_time, _end_time, local_collector, private_nursery);
    }

    local_collector->num_micro_collections++;

    if(verbose_gc) {
        unsigned int time = get_time_in_microseconds(_start_time, _end_time);

#ifdef _WINDOWS
        local_collector->sum_micro_time.QuadPart += time;
#elif defined LINUX
        add_microseconds(local_collector->sum_micro_time,time);
#endif
        if(time > local_collector->max_collection_time) {
            local_collector->max_collection_time = time;
        }

#if defined _DEBUG
        if(g_show_all_pncollect) {
            printf("pn-collect: %d, Time=%d, Size=%d, PerByteSurvive=%0.2f, NumSurvive=%d, TotalFromStack: %d\n",
                local_collector->num_micro_collections,
                time,
                saved_space_used,
                (float)size_surviving/saved_space_used,
                num_surviving,
                local_collector->num_stack_live);
            printf("TotalFromRS: %d\n",local_collector->num_rs_live);
            fflush(stdout);
        }
#if 0
        unsigned average_nursery_used = private_nursery->space_used / local_collector->num_micro_collections;
        if(local_collector->num_micro_collections % (4000000000/average_nursery_used) == 0) {
            unsigned average = local_collector->sum_micro_time.QuadPart / local_collector->num_micro_collections;
            printf("u-collection=%d,Time=%dms",
                local_collector->num_micro_collections,
                local_collector->sum_micro_time.QuadPart / 1000);
            printf(",Average=%dus,AvgSpace=%d.\n",average,average_nursery_used);
            if(private_nursery->num_write_barriers) {
                printf("Barriers = %ld, Useful = %ld/%f, Good slot = %ld/%f, Good value = %ld/%f\n",
                    private_nursery->num_write_barriers,
                    private_nursery->useful, (float)private_nursery->useful / private_nursery->num_write_barriers,
                    private_nursery->slot_outside_nursery, (float)private_nursery->slot_outside_nursery / private_nursery->num_write_barriers,
                    private_nursery->value_inside_nursery, (float)private_nursery->value_inside_nursery / private_nursery->num_write_barriers);
            }
            printf("Survivors/GC = %f, Survivor sizes/GC/nursery size = %f\n",
                (float)private_nursery->num_survivors / local_collector->num_micro_collections,
                ((float)private_nursery->size_survivors / local_collector->num_micro_collections) / average_nursery_used);
        }
#endif
#endif // _DEBUG
    }

#ifdef CONCURRENT
    unsigned int time_in_us = get_time_in_microseconds(_start_time, _end_time);
    if(time_in_us > max_pause_us) {
        printf("Local nursery collection pause of %d exceeded maximum pause time of %d.\n",time_in_us,max_pause_us);
#ifdef DETAILED_PN_TIMES
        memset_time_micro      -= clear_time_micro;
        clear_time_micro       -= move_time_micro;
        move_time_micro        -= update_root_time_micro;
        update_root_time_micro -= allocate_time_micro;
        allocate_time_micro    -= before_gray_add_micro;
        before_gray_add_micro  -= roots_and_mark_micro;

        printf("Mark = %d, Allocate = %d, Add Grays = %d,Update = %d, Move = %d, Clear = %d, memset = %d, NumSurviving = %d, thread = %p, Num = %d, SlowAllocs = %d, Mode = %d\n",
            roots_and_mark_micro,
            before_gray_add_micro,
            allocate_time_micro,
            update_root_time_micro,
            move_time_micro,
            clear_time_micro,
            memset_time_micro,
            num_surviving,
            local_collector,
            local_collector->num_micro_collections,
            num_slow_allocs,
            private_nursery->concurrent_state_copy);
#ifdef ALLOC_TIMES
        printf("%d ",alloc_times.size());
        for(unsigned j=0;j<alloc_times.size();++j) {
            printf("%d ", get_time_in_microseconds(_detail_start_time, alloc_times[j]));
        }
        printf ("\n");
#endif // ALLOC_TIMES
#endif // DETAILED_PN_TIMES
    }
#endif // CONCURRENT

//    private_nursery->tls_current_free = (char*)private_nursery->start;
    private_nursery->tls_current_free = next_compact_spot_in_pn;

    // Indicate the end of a private nursery GC
    // needs to be moved to a better spot
    pgc_local_nursery_collection_finish();

    if(local_collector->new_pn) {
        local_collector->new_pn = false;
#ifndef ORP_POSIX
        DWORD oldprot;
        int res = VirtualProtect(private_nursery->start,local_nursery_size,PAGE_NOACCESS,&oldprot);
        if(!res) {
            printf("VirtualProtect of old private nursery failed.\n");
        }
        alloc_and_init_pn(private_nursery);
#endif
    }
} // local_nursery_collection

//===========================================================================================================

#ifdef CONCURRENT
Remembered_Set *g_concurrent_remembered_set;
unsigned g_start_concurrent_compact_block;
unsigned g_end_concurrent_compact_block;

bool is_block_concurrent_compaction(block_info *block) {
    return separate_immutable &&
           incremental_compaction &&
           block->block_store_info_index >= g_start_concurrent_compact_block &&
           block->block_store_info_index <  g_end_concurrent_compact_block &&
           block->block_contains_only_immutables &&
           !block->block_immutable_copied_to_this_gc &&
#ifdef PUB_PRIV
		   && block->thread_owner == NULL // FIX...for now, we can't compact a private heap block since we can't update the private heap to private nursery roots yet.
#endif // PUB_PRIV
           !block->in_los_p;
}

static void concurrent_scan_one_slot (Partial_Reveal_Object *base,
                                      Slot p_slot,
                                      std::deque<Partial_Reveal_Object*> &gray_list,
                                      CONCURRENT_SCAN_MODE csm) {
    assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return;
    }
    // p_obj is the current value in the slot.
    Partial_Reveal_Object *p_obj = p_slot.dereference();

    REMOVE_INDIR_RES rir;
	while((rir = remove_one_indirection(p_obj, p_slot, 8)) == RI_REPLACE_OBJ);
    if(rir == RI_REPLACE_NON_OBJ) {
        // New value is not a pointer so return.
        return;
    }

    bool slot_in_heap  = p_global_gc->is_in_heap((Partial_Reveal_Object*)p_slot.get_value());
    bool value_in_heap = p_global_gc->is_in_heap(p_obj);
    block_info *p_slot_block_info = slot_in_heap  ? GC_BLOCK_INFO(p_slot.get_value()) : NULL;
    block_info *p_obj_block_info  = value_in_heap ? GC_BLOCK_INFO(p_obj) : NULL;

    // In concurrent mode, the slot may be updated after the "is_null()" check is made.
    // The write barrier will take care of references not being lost so we just need to
    // prevent a NULL object was attempting to become gray.
    if(p_obj) {
        if(separate_immutable && incremental_compaction && p_slot_block_info) {
#ifndef NO_IMMUTABLE_UPDATES
//            orp_synch_enter_critical_section(moved_objects_cs);
            MovedObjectIterator moved_iter;
            moved_iter = g_moved_objects.find(p_obj);
            if(moved_iter != g_moved_objects.end()) {
#ifdef _DEBUG
                char buf[100];
                sprintf(buf,"Updating relocated pointer csos %p to %p.",p_obj,moved_iter->second);
                gc_trace (p_obj, buf);
                gc_trace (moved_iter->second.m_new_location, buf);
#endif // _DEBUG
				Partial_Reveal_VTable *base_vt = p_obj->vt();
				if(p_slot_block_info->block_contains_only_immutables || pgc_is_vtable_immutable((struct VTable*)base_vt)) {
                    if(csm != UPDATE_MOVED_SLOTS) {
                        make_object_gray_in_concurrent_thread(p_obj,gray_list);
                    }
                    p_obj = moved_iter->second.m_new_location;
                    p_slot.update(p_obj);
                } else {
#ifdef _DEBUG
					printf("");
#endif
//                    printf("Didn't update slot in concurrent_scan_one_slot because the slot resides in a mutable block.\n");
                }
            }
//            orp_synch_leave_critical_section(moved_objects_cs);
#endif // NO_IMMUTABLE_UPDATES
        }

        // If the mode is update_moved_slots then only the above code is required.
        if(csm == UPDATE_MOVED_SLOTS) return;

        // If this object isn't already marked then add it to the mark stack.
        // The main loop will mark it later.
        make_object_gray_in_concurrent_thread(p_obj,gray_list);

        if(p_obj_block_info && p_slot_block_info) {
	        if(is_block_concurrent_compaction(p_obj_block_info) &&
	           p_slot_block_info->block_contains_only_immutables &&
	           !is_block_concurrent_compaction(p_slot_block_info)) {
#if 0
	            printf("%p => %p, %d-%d, %d, %d, %d\n",
	                p_slot.get_value(),
	                *((Partial_Reveal_Object**)p_slot.get_value()),
	                g_start_concurrent_compact_block,
	                g_end_concurrent_compact_block,
	                p_obj_block_info->block_store_info_index,
	                p_slot_block_info->block_store_info_index,
	                p_slot_block_info->block_immutable_copied_to_this_gc);
#endif // 0
	            g_concurrent_remembered_set->add_entry(p_slot);
	        }
	    }
    }
} // concurrent_scan_one_slot

static inline void concurrent_scan_one_array_object(Partial_Reveal_Object *p_object,
                                                    std::deque<Partial_Reveal_Object*> &gray_list,
                                                    CONCURRENT_SCAN_MODE csm) {
    Type_Info_Handle tih = class_get_element_type_info(p_object->vt()->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,p_object->vt());
        for (int32 i=array_length-1; i>=0; i--) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, p_object->vt()));
            concurrent_scan_one_slot(p_object,p_element,gray_list,csm);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(p_object->vt()->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,p_object->vt());
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
                concurrent_scan_one_slot (p_object,pp_target_object,gray_list,csm);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
} // concurrent_scan_one_array_object

std::set<Partial_Reveal_Object *> g_concurrent_global_marks;

#define CONCURRENT_GLOBAL_MARKS

void concurrent_scan_one_object(Partial_Reveal_Object *p_obj,
                                std::deque<Partial_Reveal_Object*> &gray_list,
                                CONCURRENT_SCAN_MODE csm) {
#ifdef CONCURRENT_GLOBAL_MARKS
    bool in_heap = true;
	if ( !p_global_gc->is_in_heap(p_obj) ) {
        in_heap = false;
        std::pair<std::set<Partial_Reveal_Object*>::iterator,bool> res = g_concurrent_global_marks.insert(p_obj);
        if(!res.second) {
            // object was already marked so just return
            return;
        }
    }

    if(in_heap) {
#endif // CONCURRENT_GLOBAL_MARKS
#ifdef WRITE_BUFFERING
    /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
       but potentially only reachable from the transaction_info field of the object.
       check if the transaction_info contains a forwarding pointer, and scan the slot
       if appropriate
     */
    concurrent_scan_object_transaction_info(p_obj);
#endif // WRITE_BUFFERING
#ifdef CONCURRENT_GLOBAL_MARKS
    }
#endif // CONCURRENT_GLOBAL_MARKS

    if (p_obj->vt()->get_gcvt()->gc_object_has_slots) {
        if (is_array(p_obj)) {
            concurrent_scan_one_array_object(p_obj,gray_list,csm);
        }

        unsigned int *offset_scanner = init_object_scanner (p_obj);
        Slot pp_target_object(NULL);
        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            concurrent_scan_one_slot (p_obj,pp_target_object,gray_list,csm);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    } else {
//        assert(0); // shouldn't get here if the object has no slots
    }
} // concurrent_scan_one_object


void Garbage_Collector::wait_for_marks_concurrent(void) {
    if(g_concurrent_transition_wait_time > 0) {
#ifdef HAVE_PTHREAD_H
        assert(0);
        printf("No implementation for non-zero wait time for pthreads in wait_for_marks_concurrent.\n");
        exit(-1);
#else  // HAVE_PTHREAD_H
        McrtTimeCycles64 end_time = mcrtGetTimeStampCounterFast();
        McrtTimeMsecs64 end_time_ms = mcrtConvertCyclesToMsecs(end_time);
        end_time_ms += g_concurrent_transition_wait_time;
        end_time = mcrtConvertMsecsToCycles(end_time_ms);
        McrtSyncResult res = Yielded;

        while (res == Yielded) {
            res = mcrtThreadYieldUntil(mcrtPredicateEqualUint32,&num_threads_remaining_until_next_phase,0, mcrtTimeToCycles64(end_time));
            switch(res) {
            case Success:
            case Timeout:
                break;
            case Yielded:
                // Also intentionally do nothing here which will repeat the while loop.
                break;
            default:
                assert(0);
            }
        }

#endif // HAVE_PTHREAD_H
    }

    // This is super-complicated.  num_threads_remaining_until_next_phase is only an approximation
    // because threads can come and go.  So, we always check each thread here to make sure it is
    // in the right state and has completely entered that mode.

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    GC_Thread_Info *cur_thread_node = NULL;

    while(1) {
        if(stop_concurrent) {
            release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            return;
        }

        bool all_in_right_mode = true;

        cur_thread_node = active_thread_gc_info_list;
        while (cur_thread_node) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

            if(private_nursery->concurrent_state_copy != CONCURRENT_MARKING) {
                bool success = orp_suspend_thread_for_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
                if(!success) {
                    if(stop_concurrent) {
                        release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                        return;
                    }

                    cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
                    continue;
                }

#ifdef DETAILED_PN_TIMES
#ifdef _DEBUG
                TIME_STRUCT _start_time, _end_time;
                if(pn_history) {
                    gc_time_start_hook(&_start_time);
                }
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

                // There is some chance the concurrent state got changed while we were waiting for a
                // suspension so check it again here.
                if(private_nursery->concurrent_state_copy != CONCURRENT_MARKING) {
                    pn_info *local_collector = private_nursery->local_gc_info;
                    assert(local_collector);
                    assert(local_collector->gc_state != LOCAL_MARK_GC);
                    if(local_collector->gc_state == LOCAL_MARK_IDLE) {
                        local_collector->gc_state = LOCAL_MARK_GC;
                        unsigned unused;
                        local_nursery_roots_and_mark(private_nursery,
                                                     NULL,
                                                     local_collector,
                                                     cur_thread_node->thread_handle,
                                                     YES_INTER_SLOT,
                                                     NULL,
                                                     unused,
                                                     false,
                                                     false, // don't use watermarks
                                                     false);

                        report_inter_slots_to_concurrent_gc(local_collector);

                        tgc_lockedXAddUint32((volatile uint32 *)&num_threads_remaining_until_next_phase,-1);

                        local_undo_marks(local_collector);
                        local_collector->clear();
                        local_collector->gc_state = LOCAL_MARK_IDLE;

                        private_nursery->concurrent_state_copy = CONCURRENT_MARKING;
                        private_nursery->current_state = CONCURRENT_MARKING;
                    } else if (local_collector->gc_state == LOCAL_PAUSED_DURING_MOVE) {
                        ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

                        for(live_object_iter  = local_collector->live_objects->begin();
                            live_object_iter != local_collector->live_objects->end();
                            ++live_object_iter) {
                            PncLiveObject *pnc = live_object_iter.get_addr();

                            // We don't collect inter-slots in every local nursery collection
                            // since it is too expensive.  So, if we interrupt a local nursery collection
                            // in the allocating new object phase then we walk the live objects and
                            // collect inter-slots here and let the code below add them normally.
                            Partial_Reveal_Object *old_location, *new_location;

                            old_location = pnc->old_location;
                            new_location = pnc->new_location;

                            local_scan_one_object(old_location, pnc->vt, NULL, NULL, *local_collector, ONLY_INTER_SLOT);

                            gc_add_root_set_entry((void**)&(pnc->new_location),FALSE);
                        }

                        report_inter_slots_to_concurrent_gc(local_collector);

                        pn_info just_for_inter_stack_roots(NULL);
                        just_for_inter_stack_roots.local_nursery_start = local_collector->local_nursery_start;
                        just_for_inter_stack_roots.local_nursery_end   = local_collector->local_nursery_end;

                        // Enumerate Stack Roots
                        PrtRseInfo rse;
                        rse.callback = &local_root_callback;
                        rse.env = &just_for_inter_stack_roots;

                        // First enumerate each frame on the task's stack.
                        PrtStackIterator _si;
                        PrtStackIterator *si = &_si;
#ifdef HAVE_PTHREAD_H
                        prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle);
#else  // HAVE_PTHREAD_H
                        prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, PrtTrue);
#endif // HAVE_PTHREAD_H
                        while (!prtIsActivationPastEnd(si)) {
                            prtEnumerateRootsOfActivation(si, &rse);
                            prtNextActivation(si);
                        }

                        // Now enumerate the task's VSE roots.
                        prtEnumerateVseRootsOfTask((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

                        // Enumerate roots in the TLS area.
                        prtEnumerateTlsRootSet((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

                        unsigned num_survivors = 0;
                        unsigned size_survivors = 0;

#if 0
                        unsigned root_index;
                        for(root_index = 0;
                            root_index < just_for_inter_stack_roots.m_roots.size();
                            ++root_index) {
                            Partial_Reveal_Object **root = just_for_inter_stack_roots.m_roots[root_index];
                            object_location loc = get_object_location(*root,just_for_inter_stack_roots);
                            if(loc == PUBLIC_HEAP || loc == GLOBAL) {
                                Partial_Reveal_Object *p_obj = *root;
                                    just_for_inter_stack_roots.add_inter_slot(root);
                                }
                            }
#endif // 0/1

                        report_inter_slots_to_concurrent_gc_src_dest(&just_for_inter_stack_roots,local_collector);

                        private_nursery->concurrent_state_copy = CONCURRENT_MARKING;
                        private_nursery->current_state = CONCURRENT_MARKING;
                    } else {
                        // If the state changes before and after the suspension then it obviously a
                        // private nursery collection is or has taken place.
                        if(private_nursery->current_state != CONCURRENT_MARKING) {
                            all_in_right_mode = false;
                        }
                    }
                } else {
                    if(private_nursery->current_state != CONCURRENT_MARKING) {
                        all_in_right_mode = false;
                    }
                }

#ifdef DETAILED_PN_TIMES
#ifdef _DEBUG
                if(pn_history) {
                    gc_time_start_hook(&_end_time);
                    private_nursery->local_gc_info->pn_stats->push_back(pn_collection_stats(
                                    _start_time,
                                    _end_time,
                                    0, 0, 0,
                                    CONCURRENT_GC_MARK_CROSS_SUSPEND));
                }
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

                orp_resume_thread_after_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
            } else {
                if(private_nursery->current_state != CONCURRENT_MARKING) {
                    all_in_right_mode = false;
                }
            }

            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }

        if(all_in_right_mode) break;
#ifndef HAVE_PTHREAD_H
        mcrtThreadYield();
#endif // !HAVE_PTHREAD_H
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
} // Garbage_Collector::wait_for_marks_concurrent

void Garbage_Collector::wait_for_sweep_or_idle_concurrent(CONCURRENT_GC_STATE new_gc_state) {
    if(g_concurrent_transition_wait_time > 0) {
#ifdef HAVE_PTHREAD_H
        assert(0);
        printf("No implementation for non-zero wait time for pthreads in wait_for_sweep_or_idle_concurrent.\n");
        exit(-1);
#else  // HAVE_PTHREAD_H
        McrtTimeCycles64 end_time = mcrtGetTimeStampCounterFast();
        McrtTimeMsecs64 end_time_ms = mcrtConvertCyclesToMsecs(end_time);
        end_time_ms += g_concurrent_transition_wait_time;
        end_time = mcrtConvertMsecsToCycles(end_time_ms);
        McrtSyncResult res = Yielded;

        gc_time_start_hook(&_start_time);

        while (res == Yielded) {
            res = mcrtThreadYieldUntil(mcrtPredicateEqualUint32,&num_threads_remaining_until_next_phase,0, mcrtTimeToCycles64(end_time));
            switch(res) {
            case Success:
            case Timeout:
                break;
            case Yielded:
                // Also intentionally do nothing here which will repeat the while loop.
                break;
            default:
                assert(0);
            }
        }
#endif // HAVE_PTHREAD_H
    }

    gc_time_end_hook("wait_for_sweep_or_idle_concurrent first while loop...", &_start_time, &_end_time, stats_gc ? true : false);

    // This is super-complicated.  num_threads_remaining_until_next_phase is only an approximation
    // because threads can come and go.  So, we always check each thread here to make sure it is
    // in the right state and has completely entered that mode.

    gc_time_start_hook(&_start_time);
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    gc_time_end_hook("Getting thread list lock...", &_start_time, &_end_time, stats_gc ? true : false);

    GC_Thread_Info *cur_thread_node = NULL;

    gc_time_start_hook(&_start_time);

    while(1) {
        if(stop_concurrent) {
            release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            return;
        }

        bool all_in_right_mode = true;

        cur_thread_node = active_thread_gc_info_list;
        while (cur_thread_node) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

            if(private_nursery->concurrent_state_copy != new_gc_state) {
                bool success = orp_suspend_thread_for_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
                if(!success) {
                    if(stop_concurrent) {
                        release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                        return;
                    }

                    cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
                    continue;
                }

#ifdef DETAILED_PN_TIMES
#ifdef _DEBUG
                TIME_STRUCT _start_time, _end_time;
                if(pn_history) {
                    gc_time_start_hook(&_start_time);
                }
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

                // There is some chance the concurrent state got changed while we were waiting for a
                // suspension so check it again here.
                if(private_nursery->concurrent_state_copy != new_gc_state) {
                    pn_info *local_collector = private_nursery->local_gc_info;
                    assert(local_collector);
                    assert(local_collector->gc_state != LOCAL_MARK_GC);
                    if(local_collector->gc_state == LOCAL_MARK_IDLE) {
                        tgc_lockedXAddUint32((volatile uint32 *)&num_threads_remaining_until_next_phase,-1);
                        private_nursery->concurrent_state_copy = new_gc_state;
                        private_nursery->current_state = new_gc_state;
                    } else if (local_collector->gc_state == LOCAL_PAUSED_DURING_MOVE) {
                        private_nursery->concurrent_state_copy = new_gc_state;
                        private_nursery->current_state = new_gc_state;
                    } else {
                        if(private_nursery->current_state != new_gc_state) {
                            all_in_right_mode = false;
                        }
                    }
                } else {
                    if(private_nursery->current_state != new_gc_state) {
                        all_in_right_mode = false;
                    }
                }

#ifdef DETAILED_PN_TIMES
#ifdef _DEBUG
                if(pn_history) {
                    gc_time_start_hook(&_end_time);
                    private_nursery->local_gc_info->pn_stats->push_back(pn_collection_stats(
                                    _start_time,
                                    _end_time,
                                    0, 0, 0,
                                    CONCURRENT_GC_SWEEP_IDLE_CROSS_SUSPEND));
                }
#endif // _DEBUG
#endif // DETAILED_PN_TIMES

                orp_resume_thread_after_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
            }

            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }

        if(all_in_right_mode) break;
#ifndef HAVE_PTHREAD_H
        mcrtThreadYield();
#endif // !HAVE_PTHREAD_H
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    gc_time_end_hook("wait_for_sweep_or_idle_concurrent second while loop...", &_start_time, &_end_time, stats_gc ? true : false);
} // Garbage_Collector::wait_for_sweep_or_idle_concurrent

//#define CSTW_TEST

unsigned path0 = 0;
unsigned path1 = 0;
unsigned path2 = 0;
unsigned path3 = 0;

#ifdef TRACK_IPS
#include <fstream>
void diff_ips(void) {
    std::ofstream bad_ips("bad_ips.txt");

    std::set<void *>::iterator iter;
    for(iter  = this_ips.begin();
        iter != this_ips.end();
        ++iter) {
        bad_ips << *iter << std::endl;
#if 0
        if(previous_ips.find(*iter) == previous_ips.end()) {
            printf("Different IP = %p\n",*iter);
        }
#endif
    }
}
#endif // TRACK_IPS

#if 1



void verify_found_obj(Partial_Reveal_Object *obj,
                      std::set<Partial_Reveal_Object*> &lives,
                      std::set<Partial_Reveal_Object*> &to_be_scanned) {
    if(obj->vt()->get_gcvt()->gc_object_has_slots) {
        if(obj->isMarked()) {
            printf("Verify_found_obj found that %p was marked.\n",obj);
        }
        if ( p_global_gc->is_in_heap(obj) ) {
            if(is_object_marked(obj)) {
//                printf("Verify_found_obj found that %p was marked in block.\n",obj);
            }
        }
    }

    if(lives.find(obj) == lives.end()) {
        lives.insert(obj);
        to_be_scanned.insert(obj);
    }
}

static void verify_scan_one_slot (Slot p_slot,
                      std::set<Partial_Reveal_Object*> &lives,
                      std::set<Partial_Reveal_Object*> &to_be_scanned)
{
    assert(p_slot.get_value());
    if (p_slot.is_null()) {
        return;
    }
    Partial_Reveal_Object *p_obj = p_slot.dereference();

    // This will be called from local_nursery_collection to fix up pointers to relocated immutables so
    // filter out non-heap addresses that we can't update or mark.
    if ( !p_global_gc->is_in_heap(p_obj) ) {
        return;
    }

    // In concurrent mode, the slot may be updated after the "is_null()" check is made.
    // The write barrier will take care of references not being lost so we just need to
    // prevent a NULL object was attempting to become gray.
    if(p_obj) {
        verify_found_obj(p_obj,lives,to_be_scanned);
    }
}

static inline void verify_scan_one_array_object(Partial_Reveal_Object *p_object,
                      std::set<Partial_Reveal_Object*> &lives,
                      std::set<Partial_Reveal_Object*> &to_be_scanned)
{
    Type_Info_Handle tih = class_get_element_type_info(p_object->vt()->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,p_object->vt());
        for (int32 i=array_length-1; i>=0; i--) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, p_object->vt()));
            verify_scan_one_slot(p_element,lives,to_be_scanned);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(p_object->vt()->get_gcvt()->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,p_object->vt());
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
                verify_scan_one_slot (pp_target_object,lives,to_be_scanned);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
} // verify_scan_one_array_object

static void verify_scan_one_object(Partial_Reveal_Object *p_obj,
                      std::set<Partial_Reveal_Object*> &lives,
                      std::set<Partial_Reveal_Object*> &to_be_scanned)
{
#ifdef WRITE_BUFFERING
    /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
       but potentially only reachable from the transaction_info field of the object.
       check if the transaction_info contains a forwarding pointer, and scan the slot
       if appropriate
     */
    verify_scan_object_transaction_info(p_obj);
#endif // WRITE_BUFFERING

    if (p_obj->vt()->get_gcvt()->gc_object_has_slots) {
        if (is_array(p_obj)) {
            verify_scan_one_array_object(p_obj,lives,to_be_scanned);
        }

        unsigned int *offset_scanner = init_object_scanner (p_obj);
        Slot pp_target_object(NULL);
        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            verify_scan_one_slot (pp_target_object,lives,to_be_scanned);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }
}

void verify_no_objects_marked(void) {
    pn_info verify_stuff(0);
    pn_info::inter_iterator inter_iter;
    Slot one_slot(NULL);
    std::set<Partial_Reveal_Object*> lives, to_be_scanned;

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    GC_Thread_Info *cur_thread_node = NULL;

    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        while(1) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
            bool success = orp_suspend_thread_for_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
            assert(success);
            pn_info *local_collector = private_nursery->local_gc_info;
            assert(local_collector);
            assert(local_collector->gc_state != LOCAL_MARK_GC);
            if(local_collector->gc_state == LOCAL_MARK_IDLE) {
                local_collector->gc_state = LOCAL_MARK_GC;
                unsigned unused;
                local_nursery_roots_and_mark(private_nursery,
                                             NULL,
                                             &verify_stuff,
                                             cur_thread_node->thread_handle,
                                             YES_INTER_SLOT,
                                             NULL,
                                             unused,
                                             false,
                                             false, // don't use watermarks
                                             false);

                for(inter_iter  = verify_stuff.inter_slots->begin();
                    inter_iter != verify_stuff.inter_slots->end();
                    ++inter_iter) {
                    one_slot.set(inter_iter->slot);
                    Partial_Reveal_Object *obj = one_slot.dereference();
                    verify_found_obj(obj,lives,to_be_scanned);
                }

                local_undo_marks(&verify_stuff);
                verify_stuff.clear();
                local_collector->gc_state = LOCAL_MARK_IDLE;
                break;
            } else if (local_collector->gc_state == LOCAL_PAUSED_DURING_MOVE) {

                orp_resume_thread_after_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
                assert(0);

                ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

                for(live_object_iter  = local_collector->live_objects->begin();
                    live_object_iter != local_collector->live_objects->end();
                    ++live_object_iter) {

                    PncLiveObject *pnc = live_object_iter.get_addr();

                    // We don't collect inter-slots in every local nursery collection
                    // since it is too expensive.  So, if we interrupt a local nursery collection
                    // in the allocating new object phase then we walk the live objects and
                    // collect inter-slots here and let the code below add them normally.
                    Partial_Reveal_Object *old_location, *new_location;

                    old_location = pnc->old_location;
                    new_location = pnc->new_location;

//                    if(new_location > (Partial_Reveal_Object*)1) {
//                        pnc->old_location->set_vtable((unsigned)pnc->vt);
//                    }
                    local_scan_one_object(old_location,pnc->vt,NULL,NULL,*local_collector,ONLY_INTER_SLOT);
//                    if(new_location > (Partial_Reveal_Object*)1) {
//                        pnc->old_location->set_vtable((unsigned)pnc);
//                    }

                    gc_add_root_set_entry((void**)&(pnc->new_location),FALSE);
                }

                pn_info just_for_inter_stack_roots(NULL);
                just_for_inter_stack_roots.local_nursery_start = local_collector->local_nursery_start;
                just_for_inter_stack_roots.local_nursery_end   = local_collector->local_nursery_end;
//                just_for_inter_stack_roots.prepare();

                // Enumerate Stack Roots
                PrtRseInfo rse;
                rse.callback = &local_root_callback;
                rse.env = &just_for_inter_stack_roots;

                // First enumerate each frame on the task's stack.
                PrtStackIterator _si;
                PrtStackIterator *si = &_si;
#ifdef HAVE_PTHREAD_H
                prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle);
#else  // HAVE_PTHREAD_H
                prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, PrtTrue);
#endif // HAVE_PTHREAD_H
                while (!prtIsActivationPastEnd(si)) {
                    prtEnumerateRootsOfActivation(si, &rse);
                    prtNextActivation(si);
                }

                // Now enumerate the task's VSE roots.
                prtEnumerateVseRootsOfTask((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

                // Enumerate roots in the TLS area.
                prtEnumerateTlsRootSet((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

                unsigned root_index;
                unsigned num_survivors = 0;
                unsigned size_survivors = 0;

                for(root_index = 0;
                    root_index < just_for_inter_stack_roots.m_roots.size();
                    ++root_index) {
                    Partial_Reveal_Object **root = just_for_inter_stack_roots.m_roots[root_index];
					if(!is_object_pointer(*root)) continue;

                    if(*root < local_collector->local_nursery_start || *root >= local_collector->local_nursery_end) {
                        Partial_Reveal_Object *p_obj = *root;
                        just_for_inter_stack_roots.add_inter_slot(root,NULL);
                    }
                }

                report_inter_slots_to_concurrent_gc_src_dest(&just_for_inter_stack_roots,local_collector);

                private_nursery->concurrent_state_copy = CONCURRENT_MARKING;
                private_nursery->current_state = CONCURRENT_MARKING;
            } else {
                orp_resume_thread_after_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);
            }
        }


        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }

//    orp_enumerate_global_refs();

    while(!to_be_scanned.empty()) {
        Partial_Reveal_Object *scan = *(to_be_scanned.begin());
        to_be_scanned.erase(to_be_scanned.begin());

        verify_scan_one_object(scan,lives,to_be_scanned);
    }

    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        orp_resume_thread_after_enumeration((PrtTaskHandle)cur_thread_node->thread_handle);

        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#ifdef _DEBUG
    while(!g_remembered_lives.empty()) {
        Partial_Reveal_Object *scan = *(g_remembered_lives.begin());
        g_remembered_lives.erase(g_remembered_lives.begin());

        if(scan->vt()->get_gcvt()->gc_object_has_slots) {
            if(scan->isMarked()) {
                printf("remembered lives found that %p was marked.\n",scan);
            }
            // can't call is_object_marked on non-heap object
            if ( !p_global_gc->is_in_heap(scan) ) {
                continue;
            }

            if(is_object_marked(scan)) {
                printf("remembered lives found that %p was marked in block.\n",scan);
            }
        }
    }
#endif // _DEBUG
} // verify no objects marked





//#define NO_LINK_TEST
//#define ONLY_EMPTY

unsigned int Garbage_Collector::return_free_blocks_concurrent(void)
{
    unsigned int num_blocks_returned = 0;
    unsigned int num_empty_chunks = 0;

    get_chunk_lock(NULL);
    orp_gc_lock_enum();

    for (int chunk_index = 0; chunk_index <= _free_chunks_end_index; chunk_index++) {

        chunk_info *this_chunk = &_gc_chunks[chunk_index];
        assert(this_chunk);

        if ((this_chunk->chunk) && (this_chunk->chunk->get_nursery_status() == free_nursery)) {
            block_info *block = this_chunk->chunk;
            assert(block);

            block_info *new_chunk_start = NULL;

#if 1
            while (block) {
                if(!block->set_nursery_status(free_nursery,concurrent_sweeper_nursery)) {
                    block = block->next_free_block;
                    continue;
                }
                gc_trace_block (block, "in return_free_blocks_concurrent looking for free blocks");
#ifdef ONLY_EMPTY
                if(block->block_free_areas[0].area_size == GC_BLOCK_ALLOC_SIZE) {
                    gc_trace_block (block, "in return_free_blocks_concurrent returing free block to block store.");
                    // Fully free block means.....NO live data...can go back to the block store
                    // Return it to the block store
                    block_info *next_block = block->next_free_block;
                    assert(block->number_of_blocks == 1);
                    if(block->get_nursery_status() != concurrent_sweeper_nursery) {
                        assert(0);
                    }
                    _p_block_store->link_free_blocks (block, block->number_of_blocks);
                    num_blocks_returned++;
                    block = next_block;
                } else {
                    block->set_nursery_status(concurrent_sweeper_nursery,free_nursery);
                    block = block->next_free_block;
                }
#else // ONLY_EMPTY
#ifdef NO_LINK_TEST
                if (block->block_free_areas[0].area_size != GC_BLOCK_ALLOC_SIZE) {            // RLH Aug 04
                    assert (block->block_free_areas[0].area_size < GC_BLOCK_ALLOC_SIZE);      // RLH Aug 04
#endif
                    //assert (block->is_free_block == false);                                   // RLH Aug 04

                    block_info *next_block = block->next_free_block;
                    // Relink onto the new chunk
                    block->next_free_block = new_chunk_start;
                    new_chunk_start = block;
//                    ++g_num_blocks_available;
                    block->set_nursery_status(concurrent_sweeper_nursery,free_nursery);
                    block = next_block;
#ifdef NO_LINK_TEST
                } else {
                    gc_trace_block (block, "in return_free_blocks_concurrent returing free block to block store.");
                    // Fully free block means.....NO live data...can go back to the block store
                    // Return it to the block store
                    block_info *next_block = block->next_free_block;
                    assert(block->number_of_blocks == 1);
                    if(block->get_nursery_status() != concurrent_sweeper_nursery) {
                        assert(0);
                    }
                    _p_block_store->link_free_blocks (block, block->number_of_blocks);
                    num_blocks_returned++;
                    block = next_block;
                }
#endif
#endif // ONLY_EMPTY
            }


            if (new_chunk_start == NULL) {
                num_empty_chunks++;
            }
            this_chunk->chunk = this_chunk->free_chunk = new_chunk_start;
#endif

        }
    }

    orp_gc_unlock_enum();
    release_chunk_lock(NULL);

    if (stats_gc) {
        orp_cout << "return_free_blocks_concurrent() returned " << num_blocks_returned << " to the block store\n";
    }

    return num_blocks_returned;
} //return_free_blocks_concurrent

#endif

unsigned remove_unmarked_moved_objects(unsigned cur_gc_num);

//#define STOP_SWEEP_PHASE

void Garbage_Collector::reclaim_full_heap_concurrent(GC_Nursery_Info *copy_to) {
start:
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    unsigned first_thread_count = num_threads_remaining_until_next_phase = active_thread_count;
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    if(num_threads_remaining_until_next_phase == 0) {
        goto start;
    }

    g_concurrent_remembered_set = new Remembered_Set;

//    incremental_compaction = false;
//    reclaim_full_heap_from_gc_thread(0, false);
//    return 0;

    static unsigned start_compact_block = 0;
    unsigned num_blocks = _p_block_store->get_num_total_blocks_in_block_store();
    unsigned max_blocks_to_compact = num_blocks / _p_block_store->get_heap_compaction_ratio();

    gc_time_start_hook(&_gc_start_time);

    if (verbose_gc || stats_gc) {
        printf("==============================GC[%d]======================================\n", _gc_num);
    }

    _gc_num_time = 0;

    if(separate_immutable && incremental_compaction && !copy_to->chunk) {
        copy_to->chunk = (GC_Nursery_Info*)p_global_gc->p_cycle_chunk(NULL, true, true, NULL, (struct GC_Thread_Info*)get_gc_thread_local());
        copy_to->curr_alloc_block = copy_to->chunk;
        copy_to->tls_current_ceiling = NULL;
        copy_to->tls_current_free = NULL;
    }

    g_start_concurrent_compact_block = start_compact_block;
    g_end_concurrent_compact_block   = start_compact_block+max_blocks_to_compact;

    assert(g_concurrent_gc_state == CONCURRENT_IDLE);

    prepare_root_containers();

    /********************************************************************************
     *                                                                              *
     *                              MARKING PHASE                                   *
     *                                                                              *
     ********************************************************************************/
    g_concurrent_gc_state = CONCURRENT_MARKING;

    gc_time_end_hook("Preparing root containers...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    wait_for_marks_concurrent();
    if(stop_concurrent) {
        return;
    }

#ifdef CGC_PHASE_DELAY
    unsigned delay;
    for(delay=0;delay < 10000000; ++delay) {}
#endif // CGC_PHASE_DELAY

    gc_time_end_hook("Waiting for marks...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    MovedObjectIterator cur, next;

#if 0
    // This code seems to cause a nasty bug and is not needed anymore.
    for(cur  = g_moved_objects.begin(); cur != g_moved_objects.end(); ++cur) {
        make_object_gray_in_concurrent_thread(cur->second.m_new_location,concurrent_gray_list);
    }
    gc_time_end_hook("Graying moved objects...", &_gc_start_time, &_end_time, stats_gc ? true : false);
#endif

    // Get the global roots here.
    orp_enumerate_global_refs();
    gc_time_end_hook("Enumerating global refs...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    if(_num_roots != 0) {
        printf("In concurrent mode, _num_roots was not equal to 0!!!\n");
        assert(0);
    }

    unsigned num_scanned = 0;

    while(1) {
        Partial_Reveal_Object *obj_to_trace;

        get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
        GC_Thread_Info *cur_thread_node = NULL;
        cur_thread_node = active_thread_gc_info_list;
        while (cur_thread_node) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
            pn_info *local_collector = private_nursery->local_gc_info;
            orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
            while(!local_collector->m_concurrent_gray_objects.empty()) {
                g_concurrent_gray_list.push_back(local_collector->m_concurrent_gray_objects.front());
                local_collector->m_concurrent_gray_objects.pop_front();
            }
            orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }

        release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

        if(g_concurrent_gray_list.empty()) break;

        while(!g_concurrent_gray_list.empty()) {
            obj_to_trace = g_concurrent_gray_list.front();
            g_concurrent_gray_list.pop_front();

#ifdef CONCURRENT_DEBUG_2
#ifdef _DEBUG
            fprintf(cgcdump,"Main %p %d offset %d eip %p\n",obj_to_trace,_gc_num,coi.offset,coi.eip);
#else
            fprintf(cgcdump,"Main %p %d\n",obj_to_trace,_gc_num);
#endif
#endif

            gc_trace (obj_to_trace, "Tracing object concurrently in marking mode.");
            ++num_scanned;
            concurrent_scan_one_object(obj_to_trace,g_concurrent_gray_list,NORMAL);
        } // while(!local_gray_list.empty())
    }

    gc_time_end_hook("Marking live objects...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    /********************************************************************************
     *                                                                              *
     *                              SWEEPING PHASE                                  *
     *                                                                              *
     ********************************************************************************/
    g_sweep_ptr = NULL;

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    num_threads_remaining_until_next_phase = active_thread_count;
//    assert(first_thread_count == num_threads_remaining_until_next_phase);
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    g_concurrent_gc_state = CONCURRENT_SWEEPING;
    wait_for_sweep_or_idle_concurrent(CONCURRENT_SWEEPING);
    if(stop_concurrent) {
        return;
    }

#ifdef CGC_PHASE_DELAY
    for(delay=0;delay < 10000000; ++delay) {}
#endif // CGC_PHASE_DELAY

    // While threads are getting into sweeping mode they may be adding grays to be traced.
    // So, once they've all acknowledged sweeping mode, go through and trace grays again.
    bool first = true;
    while(1) {
        Partial_Reveal_Object *obj_to_trace;

        get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
        GC_Thread_Info *cur_thread_node = NULL;
        cur_thread_node = active_thread_gc_info_list;
        while (cur_thread_node) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
            pn_info *local_collector = private_nursery->local_gc_info;
            orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
            while(!local_collector->m_concurrent_gray_objects.empty()) {
                g_concurrent_gray_list.push_back(local_collector->m_concurrent_gray_objects.front());
                local_collector->m_concurrent_gray_objects.pop_front();
            }
            orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }

        release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

        if(g_concurrent_gray_list.empty()) break;

        if(!first) {
#ifdef NO_GRAY_SWEEPING
            printf("Unexpected condition in concurrent GC.\n");
            printf("concurrent_gray_list non-empty the second time through after entering sweeping mode.\n");
#endif // NO_GRAY_SWEEPING
        }
        first = false;

        while(!g_concurrent_gray_list.empty()) {
            obj_to_trace = g_concurrent_gray_list.front();
            g_concurrent_gray_list.pop_front();

#ifdef CONCURRENT_DEBUG_2
#ifdef _DEBUG
            fprintf(cgcdump,"AfterMark %p %d offset %d eip %p\n",obj_to_trace,_gc_num,coi.offset,coi.eip);
#else
            fprintf(cgcdump,"AfterMark %p %d\n",obj_to_trace,_gc_num);
#endif
#endif

            gc_trace (obj_to_trace, "Tracing object concurrently in sweeping.");
            ++num_scanned;
            concurrent_scan_one_object(obj_to_trace,g_concurrent_gray_list,NORMAL);
        } // while(!local_gray_list.empty())
    }

    if(stats_gc) {
        printf("Number of object scanned concurrently is %d\n",num_scanned);
    }

    gc_time_end_hook("Entering sweep mode...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    // All of the previous GC's moved objects should have had their slots updated
    // to their new homes now so we can forget the previous moved set.
    orp_synch_enter_critical_section(moved_objects_cs);
//#ifdef NO_IMMUTABLE_UPDATES
//    g_moved_objects.clear();
//#else
//    assert(g_moved_objects.size() == 0);
      remove_unmarked_moved_objects(_gc_num);
//#endif // NO_IMMUTABLE_UPDATES
    gc_time_end_hook("Removing unmarked moved objects...", &_gc_start_time, &_end_time, stats_gc ? true : false);
    orp_synch_leave_critical_section(moved_objects_cs);

    // Don't know if this is needed.
#if 1
    for(cur = g_moved_objects.begin(); cur != g_moved_objects.end(); ++cur) {
        if(cur->first->isMarked() && !cur->second.m_new_location->isMarked()) {
            mark_header_and_block_atomic(cur->second.m_new_location);
        }
    }
#endif

#ifdef STOP_SWEEP_PHASE
    volatile GC_Thread_Info *cur_thread_node = NULL;

    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

        printf("Stopping %p\n",cur_thread_node->thread_handle);
        bool success = orp_suspend_thread_for_enumeration(cur_thread_node->thread_handle);
//        assert(success);

        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }
#endif

    unsigned blocks_to_return_to_heap = 0;

#if 1
//#define BLOCK_FRAG
#ifdef BLOCK_FRAG
    char buf[20];
    sprintf(buf,"block_frag_%d.txt",_gc_num);
    std::ofstream block_frag(buf);
#endif // BLOCK_FRAG
    unsigned num_total_holes = 0;

    unsigned block_index;
    for (block_index = 0;
         block_index < num_blocks;
         ++block_index) {
        block_info *bi = _p_block_store->get_block_info(block_index);
        if(!bi) continue;

        if(bi->is_single_object_block) {
            Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)GC_BLOCK_ALLOC_START(bi);

            if (is_object_marked(p_obj) == false) {
                if(p_obj->isMarked()) {
                    p_obj->unmark();
                }
                //printf("single_object_block return = %p, obj_start = %p\n",bi,GC_BLOCK_ALLOC_START(bi));

//                assert(!p_obj->isMarked());
                // It is free, relink the blocks onto the free list.
                assert (bi->number_of_blocks);
                blocks_to_return_to_heap += bi->number_of_blocks;
                _p_block_store->link_free_blocks (bi, bi->number_of_blocks);
            } else {
                // Used block
                if(p_obj->isMarked()) {
                    p_obj->unmark();
                }

                GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
            }
        } else if(bi->in_los_p) {
            unsigned no_compact_here = 0;
            blocks_to_return_to_heap += sweep_one_block_concurrent(bi,copy_to); // disable compaction with the 1,0 parameters
        } else {
#if 1
            switch(bi->get_nursery_status()) {
            case free_uncleared_nursery:
                // INTENTIONALLY DO NOTHING IN THIS CASE
                break;
            // Sweeping removes marks which are necessary for the next GC to function correctly so
            // we have to sweep even if the nursery is active or free.
            case active_nursery:
            case free_nursery:
            case spent_nursery:
                blocks_to_return_to_heap += sweep_one_block_concurrent(bi,copy_to);
//                if(bi->block_contains_only_immutables) {
                    num_total_holes += bi->last_sweep_num_holes;
//                }
                break;
            case thread_clearing_nursery:
            case bogus_nursery:
            default:
                printf("Unexpected block nursery state %d in reclaim_full_heap_concurrent.\n",bi->get_nursery_status());
                assert(0);
                exit(17018);
            }
#endif
        }

        if(g_num_blocks_available < 300 && blocks_to_return_to_heap) {
#if 0
            printf("Returning block while sweeping, available = %d returning = %d.\n",g_num_blocks_available,blocks_to_return_to_heap);
#endif
            return_free_blocks_concurrent();
            g_num_blocks_available += blocks_to_return_to_heap;
            blocks_to_return_to_heap = 0;
         }

#ifdef BLOCK_FRAG
        block_frag << block_index << " " << bi->block_contains_only_immutables << " " << bi->last_sweep_num_holes << std::endl;
#endif // BLOCK_FRAG
    }

    if(!g_concurrent_gray_list.empty()) {
        printf("Shouldn't happen.\n");
    }

#ifdef BLOCK_FRAG
    block_frag.close();
#endif // BLOCK_FRAG

#if 1
    std::map<unsigned,unsigned> moved_object_gc_nums;
    for(cur  = g_moved_objects.begin();
        cur != g_moved_objects.end();
        ++cur) {
        if (cur->second.m_gc_move_number == _gc_num) {
            if(cur->second.m_new_location->vt()->get_gcvt()->gc_object_has_slots) {
                // Update the slots in the new object locations.
                concurrent_scan_one_object(cur->second.m_new_location,g_concurrent_gray_list,UPDATE_MOVED_SLOTS);
            }
        }
#ifdef MOVED_OBJ_STATS
        unsigned this_gc_num = cur->second.m_gc_move_number;
        std::pair<std::map<unsigned,unsigned>::iterator,bool> res = moved_object_gc_nums.insert(std::pair<unsigned,unsigned>(this_gc_num,1));
        if(!res.second) {
            res.first->second++;
        }
#endif // 0/1
    }

#ifdef MOVED_OBJ_STATS
    std::map<unsigned,unsigned>::iterator mogn_iter;
//    printf("Moved object statistics.\n");
    for(mogn_iter  = moved_object_gc_nums.begin();
        mogn_iter != moved_object_gc_nums.end();
        ++mogn_iter) {
        printf("GC num = %d, Number of objects = %d\n",mogn_iter->first,mogn_iter->second);
    }
#endif // MOVED_OBJ_STATS
#endif // 0/1

#if 1
#ifdef MOVED_OBJ_STATS
    unsigned rs_slots_updated = 0;
    printf("Remembered concurrent slots = %d\n",g_concurrent_remembered_set->size());
#endif // MOVED_OBJ_STATS

#ifdef REMEMBERED_SET_ARRAY
    Remembered_Set::iterator iter;
	Slot one_slot(NULL);

    for(iter  = g_concurrent_remembered_set->begin();
        iter != g_concurrent_remembered_set->end();
        ++iter) {
        one_slot.set(iter.get_current());
        Partial_Reveal_Object *p_obj = one_slot.dereference();
        cur = g_moved_objects.find(p_obj);
        if(cur != g_moved_objects.end()) {
            one_slot.update(cur->second.m_new_location);
#ifdef MOVED_OBJ_STATS
            ++rs_slots_updated;
#endif // MOVED_OBJ_STATS
        }
    }
#else
    // Update slots in object in immutable blocks to moved objects.
    g_concurrent_remembered_set->rewind();
    Slot one_slot(NULL);

    while (one_slot.set(g_concurrent_remembered_set->next().get_value())) {
        Partial_Reveal_Object *p_obj = one_slot.dereference();
        cur = g_moved_objects.find(p_obj);
        if(cur == g_moved_objects.end()) {
//            printf("Moved object, %p, not found while fixing pointers. %d\n",p_obj,rs_slots_updated);
//            exit(-1);
        } else {
//            printf("%p\n",p_obj);
            one_slot.update(cur->second.m_new_location);
#ifdef MOVED_OBJ_STATS
            ++rs_slots_updated;
#endif // MOVED_OBJ_STATS
        }
    }
#endif

#ifdef MOVED_OBJ_STATS
    printf("Moving slots updated = %d\n",rs_slots_updated);
#endif // MOVED_OBJ_STATS
#endif // 0/1

#ifdef _DEBUG
//    printf("Num holes = %d\n",num_total_holes);
#endif // _DEBUG

    // If this is still greater than zero then we went through all the blocks
    // and didn't find enough to compact so we'll start over at the first block
    // next GC.
    start_compact_block += max_blocks_to_compact;
    if(start_compact_block > _p_block_store->get_num_total_blocks_in_block_store()) {
        start_compact_block = 0;
    }

    gc_time_end_hook("Sweeping...", &_gc_start_time, &_end_time, stats_gc ? true : false);
#endif

#if 1
    // Refresh block store
    return_free_blocks_concurrent();
    g_num_blocks_available += blocks_to_return_to_heap;
    gc_time_end_hook("Return free blocks...", &_gc_start_time, &_end_time, stats_gc ? true : false);
#endif

    /********************************************************************************
     *                                                                              *
     *                                IDLE PHASE                                    *
     *                                                                              *
     ********************************************************************************/
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    num_threads_remaining_until_next_phase = active_thread_count;
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    g_concurrent_gc_state = CONCURRENT_IDLE;

    wait_for_sweep_or_idle_concurrent(CONCURRENT_IDLE);
    if(stop_concurrent) {
        return;
    }

#ifdef CGC_PHASE_DELAY
    for(delay=0;delay < 10000000; ++delay) {}
#endif // CGC_PHASE_DELAY

#if 0
    if(!mcrtNonBlockingQueueIsEmpty(concurrent_gray_objects)) {
        printf("Problem with concurrent GC.  Gray set not empty at end of GC.\n");
    }
#endif

    gc_time_end_hook("Waiting for idle...", &_gc_start_time, &_end_time, stats_gc ? true : false);

    // Here we know that no one is marking any objects so objects marked during the sweeping phase
    // that weren't cleared for some reason can be unmarked here.

    /*************************************************************************************
     *     Unmark objects marked by threads when they think it is sweeping mode but when *
     *     we really want to go into idle mode.                                          *
     *************************************************************************************/
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    GC_Thread_Info *cur_thread_node = NULL;
    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
        pn_info *local_collector = private_nursery->local_gc_info;
        orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
        while(!local_collector->m_concurrent_gray_objects.empty()) {
            g_concurrent_gray_list.push_back(local_collector->m_concurrent_gray_objects.front());
            local_collector->m_concurrent_gray_objects.pop_front();
        }
        orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#if 0
    while(!g_concurrent_gray_list.empty()) {
//        printf("This section of the concurrent GC code should be defunct.\n");

#ifdef _DEBUG
        ConcurrentObjInfo coi = g_concurrent_gray_list.front();
        obj_to_trace = coi.obj;
#else
        obj_to_trace = g_concurrent_gray_list.front();
#endif
        g_concurrent_gray_list.pop_front();

#ifdef CONCURRENT_DEBUG_2
#ifdef _DEBUG
        fprintf(cgcdump,"AfterSweep %p %d offset %d eip %p\n",obj_to_trace,_gc_num,coi.offset,coi.eip);
#else
        fprintf(cgcdump,"AfterSweep %p %d\n",obj_to_trace,_gc_num);
#endif
#endif

        if(obj_to_trace->isMarked()) {
            obj_to_trace->unmark();
        }
    } // while(!local_gray_list.empty())
#else
    g_concurrent_gray_list.clear();
#endif

    for (block_index = 0;
         block_index < num_blocks;
         ++block_index) {
        block_info *bi = _p_block_store->get_block_info(block_index);
        if(!bi) continue;

        if(bi->is_single_object_block) {
            Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)GC_BLOCK_ALLOC_START(bi);
            if(is_object_marked(p_obj)) {
                p_obj->unmark();
            }
            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
        } else if(bi->in_los_p) {
            clear_and_unmark(bi,false);
//            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
        } else {
//            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
            clear_and_unmark(bi,false);
        }
    }


    gc_time_end_hook("Unmarking gray set after idle mode...", &_gc_start_time, &_end_time, stats_gc ? true : false);

#ifdef _DEBUG
//    verify_no_objects_marked();
#endif // _DEBUG

#ifdef STOP_SWEEP_PHASE
    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
        pn_info *local_collector = private_nursery->local_gc_info;
        assert(local_collector);

        private_nursery->concurrent_state_copy = CONCURRENT_IDLE;
        private_nursery->current_state = CONCURRENT_IDLE;

        orp_resume_thread_after_enumeration(cur_thread_node->thread_handle);

        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#endif

    _gc_num_time = gc_time_end_hook("GC time", &_gc_start_time, &_gc_end_time, false);
    _total_gc_time += _gc_num_time;
    if (verbose_gc) {
        printf ("GC[%d]: %dms, Total = %dms\n", _gc_num , _gc_num_time, _total_gc_time);
        fflush(stdout);
    }

    interior_pointer_table->reset();
    compressed_pointer_table->reset();
    delete g_concurrent_remembered_set;

    _gc_num++;

    /*************************************************************************************
     *     Unmark objects marked by threads when they think it is sweeping mode but when *
     *     we really want to go into idle mode.                                          *
     *************************************************************************************/
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();
        pn_info *local_collector = private_nursery->local_gc_info;
        orp_synch_enter_critical_section(local_collector->m_concurrent_gray_lock_cs);
        if(!local_collector->m_concurrent_gray_objects.empty()) {
            printf("Error in concurrent GC algorithm.\n");
            printf("A mutator created a gray object while in idle mode.\n");
            assert(0);
            exit(-1);
        }
        orp_synch_leave_critical_section(local_collector->m_concurrent_gray_lock_cs);
        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }

    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    for (block_index = 0;
         block_index < num_blocks;
         ++block_index) {
        block_info *bi = _p_block_store->get_block_info(block_index);
        if(!bi) continue;

        if(bi->is_single_object_block) {
            Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)GC_BLOCK_ALLOC_START(bi);
            if(is_object_marked(p_obj)) {
                printf("Big problem!!!!!!!!!!!!\n");
            }
            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
        } else if(bi->in_los_p) {
            clear_and_unmark(bi,true);
//            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
        } else {
//            GC_CLEAR_BLOCK_MARK_BIT_VECTOR(bi);
            clear_and_unmark(bi,true);
        }
    }

} // Garbage_Collector::reclaim_full_heap_concurrent

unsigned Garbage_Collector::estimate_free_heap_space(void) {
    unsigned free_space = 0;

    unsigned num_blocks = _p_block_store->get_num_total_blocks_in_block_store();
    unsigned block_index;
    for (block_index = 0;
         block_index < num_blocks;
         ++block_index) {
        block_info *bi = _p_block_store->get_block_info(block_index);
        switch(bi->get_nursery_status()) {
        case free_nursery:
        case free_uncleared_nursery:
            free_space += bi->amount_free_space();
            break;
        case active_nursery:
        case spent_nursery:
            // Intentionally add nothing here
            break;
        case thread_clearing_nursery:
        case bogus_nursery:
        default:
            printf("Unexpected block nursery state %d in reclaim_full_heap_concurrent.\n",bi->get_nursery_status());
            assert(0);
            exit(17019);
        }
    }
    return free_space;
}
#endif // CONCURRENT

void reset_one_thread_nursery(GC_Nursery_Info *nursery) {
    nursery->tls_current_ceiling = NULL;     // Set to 0 so in allocation routine free (0) + size will be > ceiling (0);
    nursery->tls_current_free    = NULL;
    nursery->curr_alloc_block    = nursery->chunk; // Restart using all the blocks in the chunk.
} // Garbage_Collector::reset_one_thread_nursery

void Garbage_Collector::reset_thread_nurseries(void) {
    GC_Thread_Info *cur_thread_node = active_thread_gc_info_list;
    while (cur_thread_node) {
        cur_thread_node->reset_nurseries();
        if(local_nursery_size) {
            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

            pn_info *local_collector = private_nursery->local_gc_info;
            assert(local_collector);
            if(local_collector->gc_state == LOCAL_MARK_GC) {
                local_undo_marks(local_collector);
                local_collector->clear();
                local_collector->gc_state = LOCAL_MARK_IDLE;
            } else {
                if(g_cheney) {
                    if(local_collector->gc_state == LOCAL_MARK_ACTIVE) {
                        local_collector->gc_state = LOCAL_MARK_IDLE;
                        //printf("Resetting thread nursery in active cheney mode not yet implemented\n");
                    } else {
                        printf("Resetting thread nursery in cheney mode with thread gc state = %d\n",local_collector->gc_state);
                        exit(-1);
                    }
                } else {
                    assert(1);
                }
            }
        }

        cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
    }
} // Garbage_Collector::reset_thread_nurseries

#ifdef HAVE_PTHREAD_H
PrtBool PRT_CDECL tgcPredicateNotEqualUint32(volatile uint32 *location, uint32 data) {
    return (*location != data) ? PrtTrue : PrtFalse;
}
#endif // HAVE_PTHREAD_H

void global_gc_pn_mark_loop_process_obj_slot(Slot p_slot, Partial_Reveal_Object *p_obj, void *env, bool is_weak) {
    pn_info *local_collector = (pn_info*)env;

    // A pointer to an object outside the private nursery.
    switch(get_object_location(p_obj,local_collector)) {
    case PUBLIC_HEAP:
    case GLOBAL:
#ifdef PUB_PRIV
    case PRIVATE_HEAP:
#endif
        local_collector->add_inter_slot((Partial_Reveal_Object**)p_slot.get_value(),p_slot.base);
        return;
    case PRIVATE_NURSERY:
        if(p_obj->isForwarded()) {
//            printf("Found forwarded object during global_gc_pn_mark_loop_process_obj_slot\n");
        } else {
            if(!(p_obj->isMarked())) {
                p_obj->mark();
                push_bottom_on_local_mark_stack(NULL,p_obj,&(local_collector->mark_stack));
            }
        }

        return;
    default:
        assert(0);
        return;
    }
}

void global_gc_pn_mark_loop(pn_info *local_collector) {
    Partial_Reveal_Object *obj_to_trace;

    GenericScanControl gsc;
    gsc.process_obj_slot = global_gc_pn_mark_loop_process_obj_slot;
    gsc.env = local_collector;

    while(1) {
#ifdef NEW_APPROACH
        obj_to_trace = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
#else
        ObjectPath op = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
        obj_to_trace = op.to;
#endif
        if(!obj_to_trace) break;  // no more objects to mark
        if(!obj_to_trace->isMarked()) {
            //obj_to_trace->mark();
            printf("Object was not marked upon entering global_gc_pn_mark_loop.\n");
            exit(-1);
            //continue;  // already marked
        }

        Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();

#ifdef NEW_APPROACH
        local_collector->stay_mark(obj_to_trace);
#else
        local_collector->live_objects->push_back(PncLiveObject(obj_to_trace,CAN_STAY,obj_trace_vt,op.from));
#endif

#ifdef NEW_APPROACH
        generic_scan_one_object(obj_to_trace,obj_trace_vt,&gsc);
#else
        generic_scan_one_object(obj_to_trace,obj_trace_vt,&gsc);
#endif
    }
} // global_gc_pn_mark_loop

// main GC
void Garbage_Collector::reclaim_full_heap_from_gc_thread(unsigned int size_failed, bool force_gc, bool for_los) {
#ifdef CONCURRENT
    GC_Thread_Info *thread = get_gc_thread_local();

    start_concurrent_gc = 1;

    TIME_STRUCT _start_time, _end_time;
    gc_time_start_hook(&_start_time);

    orp_gc_unlock_enum();
    release_chunk_lock(thread);

#if 1
    TIME_STRUCT ll;
    gc_time_start_hook(&ll);
    printf("Concurrent_GC_late %I64u\n",ll);
#endif // 0/1

    GC_Small_Nursery_Info *private_nursery = thread->get_private_nursery();
    pn_info *local_collector = private_nursery->local_gc_info;
    assert(local_collector);
    LOCAL_MARK_STATE old_state = local_collector->gc_state;
    local_collector->gc_state = LOCAL_PAUSED_DURING_MOVE;

    unsigned temp_gc_num = _gc_num;
#ifdef HAVE_PTHREAD_H
    prtYieldUntil((PrtPredicate)tgcPredicateNotEqualUint32, &_gc_num, (void *)temp_gc_num, PrtInfiniteWait64);
#else  // HAVE_PTHREAD_H
    prtYieldUntil((PrtPredicate)mcrtPredicateNotEqualUint32, &_gc_num, (void *)temp_gc_num, InfiniteWaitCycles64);
#endif // HAVE_PTHREAD_H

    local_collector->gc_state = old_state;

    get_chunk_lock(thread);
    orp_gc_lock_enum();

    gc_time_start_hook(&_end_time);
    unsigned int time_in_us = get_time_in_microseconds(_start_time, _end_time);
    if(time_in_us > max_pause_us) {
        printf("reclaim_full_heap_from_gc_thread pause of %d exceeded maximum pause time of %d.\n",time_in_us,max_pause_us);
    }
    return;
#endif // CONCURRENT

	unsigned block_type_los, block_type_regular, block_type_single;
	if(g_return_free_block_policy == MAINTAIN_RATIO) {
		p_global_gc->_p_block_store->get_block_type_stats(block_type_regular, block_type_single, block_type_los);
		g_regular_block_ratio = (float)block_type_regular / (block_type_regular + block_type_single + block_type_los);
	}

    if(for_los && stats_gc) {
        p_global_gc->_p_block_store->print_block_stats();
    }

	//    pgc_local_nursery_collection_start();
#ifndef HAVE_PTHREAD_H
    mcrtThreadNotSuspendable(mcrtThreadGet());
#endif // !HAVE_PTHREAD_H

    active_gc_thread = prtGetTaskHandle();

    gc_time_start_hook(&_start_time);
    if (verbose_gc || stats_gc) {
        printf("==============================GC[%d]======================================\n", _gc_num);
		fflush(stdout);
    }
    // Tell the block store if any compaction will be done in this GC cycle

    if (verify_live_heap) {
        init_verify_live_heap_data_structures();
    }

    _gc_num++;
    _gc_num_time = 0;

    if(g_gen) {
        if(last_young_gen_percentage < g_full_gc_trigger_in_gen) {
            g_gen_all = true;
        } else {
            g_gen_all = false;
        }
    }

    // Are we doing compaction this GC?
    bool compaction_this_gc = (g_gen || incremental_compaction || (fullheapcompact_at_forcegc && force_gc));

#ifdef GC_VERIFY_VM
    assert(!running_gc); // If we are testing the gc we had better have the GC lock.
    running_gc = true; // We hold gc lock so we can change this without race conditions.
#endif // GC_VERIFY_VM
    if (fullheapcompact_at_forcegc && force_gc) {
        _p_block_store->set_compaction_type_for_this_gc(gc_full_heap_sliding_compaction);
    }
    else if (incremental_compaction) {
        _p_block_store->set_compaction_type_for_this_gc(gc_incremental_sliding_compaction);
    } else {
        _p_block_store->set_compaction_type_for_this_gc(gc_no_compaction);
    }

    gc_time_end_hook("GC type determination", &_start_time, &_end_time, stats_gc ? true : false);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    gc_time_start_hook(&_start_time);
    // Initialize all GC threads for this cycle...tell GC threads if compaction is happening in this GC
    reset_gc_threads(compaction_this_gc);
    gc_time_end_hook("Reset GC Threads", &_start_time, &_end_time, stats_gc ? true : false);
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    gc_time_start_hook(&_start_time);
    prepare_root_containers();
    if (compaction_this_gc) {
        // Save roots away for repointing if they point to compacted areas
        memset(_save_array_of_roots, 0, sizeof(void *) * num_root_limit);
//        memset(_save_array_of_roots, 0, sizeof(void *) * GC_MAX_ROOTS);
    }
    gc_time_end_hook("Reset root containers", &_start_time, &_end_time, stats_gc ? true : false);

    // Stop-the-world begins just now!!!
    gc_time_start_hook(&_gc_start_time);

    gc_time_start_hook(&_start_time);

    // Forget any pinned blocks that may have been identified during a previous GC.
    m_pinned_blocks.clear();
    // Forget any short weak roots from a previous GC...precautionary..should already be empty.
//    m_short_weak_roots.clear();
    // Forget any long weak roots from a previous GC...precautionary..should already be empty.
//    m_long_weak_roots.clear();

#ifndef OLD_MULTI_LOCK
    get_active_thread_gc_info_list_lock();     // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
#endif // OLD_MULTI_LOCK
    // Stop the threads and collect the roots.
    _get_orp_live_references();

    GC_Thread_Info *cur_thread_node = NULL;

#ifdef IGNORE_SOME_ROOTS
    g_barrier_roots.clear();
#endif // IGNORE_SOME_ROOTS

//    cheney_spaces this_gc_cheney_spaces;
	this_gc_cheney_spaces.clear();

    pn_ranges.clear();

    if(local_nursery_size) {
        TIME_STRUCT _ln_start_time, _ln_end_time;
        gc_time_start_hook(&_ln_start_time);

        cur_thread_node = active_thread_gc_info_list;
        while (cur_thread_node) {
			TIME_STRUCT _thread_start_time, _thread_end_time;
			gc_time_start_hook(&_thread_start_time);

            GC_Small_Nursery_Info *private_nursery = cur_thread_node->get_private_nursery();

            pn_info *local_collector = private_nursery->local_gc_info;
            assert(local_collector);

            pn_ranges.add(local_collector->local_nursery_start,local_collector->local_nursery_end);

            if(g_cheney && local_collector->gc_state == LOCAL_MARK_ACTIVE) {
				TIME_STRUCT _cheney_start_time, _cheney_end_time;
				gc_time_start_hook(&_cheney_start_time);

                // The thread is in the middle of a cheney scan copy into the heap.
                // Here is the basic idea of how to handle this situation.
                // 1. Get stack, VSE, and TLS roots.
                // 2. Scan the root list.  If it is a pn-root, add it to a set of pn roots
                //    and use those later to find live objects in the pn.
                // 3. Scan the cheney spaces for the cheney scan PN-collection in progress
                //    looking for slots into the PN.  Treat those as roots similar to #2.
                // 4. Do transitive closure in the PN and find live objects.
                // 5. Enumerate inter-slots into the heap as roots to the global GC.
                // 6. Find inter-slots in those live objects and enumerate as roots to the global GC.
                pn_info just_for_non_pn_roots(NULL);
                just_for_non_pn_roots.local_nursery_start = local_collector->local_nursery_start;
                just_for_non_pn_roots.local_nursery_end   = local_collector->local_nursery_end;

                // #1 -----------------------------------------------------------------------------------
                // Enumerate Stack Roots
                PrtRseInfo rse;
                rse.callback = &local_root_callback;
                rse.env = &just_for_non_pn_roots;

                // First enumerate each frame on the task's stack.
                PrtStackIterator _si;
                PrtStackIterator *si = &_si;
#ifdef HAVE_PTHREAD_H
                prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle);
#else  // HAVE_PTHREAD_H
                prtYoungestActivationFromUnmanagedInTask(si, (PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, PrtTrue);
#endif // HAVE_PTHREAD_H
                while (!prtIsActivationPastEnd(si)) {
                    prtEnumerateRootsOfActivation(si, &rse);
                    prtNextActivation(si);
                }

                // Now enumerate the task's VSE roots.
                prtEnumerateVseRootsOfTask((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

                // Enumerate roots in the TLS area.
                prtEnumerateTlsRootSet((PrtTaskHandle)(struct PrtTaskStruct*)cur_thread_node->thread_handle, &rse);

		        gc_time_end_hook("Cheney roots", &_cheney_start_time, &_cheney_end_time, stats_gc);

#ifdef PUB_PRIV
                external_pointer *ep_temp = (external_pointer*)private_nursery->tls_current_ceiling;
                while(ep_temp < local_collector->currently_used_nursery_end) {
                    if(ep_temp->base) {
                        p_global_gc->gc_internal_add_weak_root_set_entry((Partial_Reveal_Object**)&(ep_temp->base),0,true);
                        POINTER_SIZE_INT offset = (POINTER_SIZE_INT)ep_temp->slot.get_value() - (POINTER_SIZE_INT)ep_temp->base;
                        p_global_gc->gc_internal_add_weak_root_set_entry((Partial_Reveal_Object**)ep_temp->slot.get_address(),offset,true);

			            Partial_Reveal_Object **root = (Partial_Reveal_Object**)ep_temp->slot.get_value();
						if(is_object_pointer(*root)) {
		                    if(get_object_location(*root,local_collector) == PRIVATE_NURSERY) {
								just_for_non_pn_roots.m_roots.push_back(root);
							} else {
		                        gc_add_root_set_entry((Managed_Object_Handle*)root,FALSE);
							}
						}
					}

					ep_temp++;
                }
#endif // PUB_PRIV

		        gc_time_end_hook("Cheney remembered set", &_cheney_start_time, &_cheney_end_time, stats_gc);

				// FIX FIX FIX
				// Below there is a section annotated "Is there an equivalent...".
				// I'm not sure at this point if we need an equivalent section here or not yet.

                pn_info::inter_iterator inter_iter;
                Slot one_slot(NULL);

                // #2 -----------------------------------------------------------------------------------
                unsigned root_index;
                for(root_index = 0;
                    root_index < just_for_non_pn_roots.m_roots.size();
                    ++root_index) {
                    Partial_Reveal_Object **root = just_for_non_pn_roots.m_roots[root_index];
            		if(!is_object_pointer(*root)) continue;
                    if(get_object_location(*root,local_collector) == PRIVATE_NURSERY) {
                        if(!(*root)->isForwarded() && !((*root)->isMarked())) {
                            (*root)->mark();
                            push_bottom_on_local_mark_stack(NULL,*root,&(just_for_non_pn_roots.mark_stack));
                        }
                    } else {
                        gc_add_root_set_entry((Managed_Object_Handle*)root,FALSE);
                    }
                }

		        gc_time_end_hook("Cheney process roots", &_cheney_start_time, &_cheney_end_time, stats_gc);

                // #3 -----------------------------------------------------------------------------------
                cheney_spaces::iterator cspace_iter;
                for(cspace_iter  = local_collector->cspaces.begin();
                    cspace_iter != local_collector->cspaces.end();
                  ++cspace_iter) {
                    this_gc_cheney_spaces.add_entry(cspace_iter.get_current());
                    cspace_iter.get_addr()->global_gc_get_pn_lives(&just_for_non_pn_roots);
                }

				if(g_two_space_pn) {
					local_collector->cspaces.get_target_two_space()->global_gc_get_pn_lives(&just_for_non_pn_roots);
				}

		        gc_time_end_hook("Cheney cspace to live pn", &_cheney_start_time, &_cheney_end_time, stats_gc);

                // #4 -----------------------------------------------------------------------------------
                global_gc_pn_mark_loop(&just_for_non_pn_roots);

		        gc_time_end_hook("Cheney pn mark loop", &_cheney_start_time, &_cheney_end_time, stats_gc);

                // #5 -----------------------------------------------------------------------------------
                for(inter_iter  = just_for_non_pn_roots.inter_slots->begin();
                    inter_iter != just_for_non_pn_roots.inter_slots->end();
                    ++inter_iter) {
                    gc_add_root_set_entry((Managed_Object_Handle*)inter_iter->slot,FALSE);
                } // while (one_slot)

                ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;

                for(interior_pointer_iter  = just_for_non_pn_roots.interior_pointer_table_public.begin();
                    interior_pointer_iter != just_for_non_pn_roots.interior_pointer_table_public.end();
                    ++interior_pointer_iter) {

                    slot_offset_entry entry = interior_pointer_iter.get_current();
                    if(get_object_location(entry.base,&just_for_non_pn_roots) == PUBLIC_HEAP) {
                        interior_pointer_table->add_entry (entry);
                        // pass the slot from the interior pointer table to the gc.
                        slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                        p_global_gc->gc_internal_add_root_set_entry(&(addr->base));
                    }
#ifdef PUB_PRIV
                    if(g_use_pub_priv && get_object_location(entry.base,&just_for_non_pn_roots) == PRIVATE_HEAP) {
                        interior_pointer_table->add_entry (entry);
                        // pass the slot from the interior pointer table to the gc.
                        slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                        p_global_gc->gc_internal_add_root_set_entry(&(addr->base));
                    }
#endif
                }

                local_undo_marks(&just_for_non_pn_roots);
                just_for_non_pn_roots.clear();
		        gc_time_end_hook("One thread Cheney", &_cheney_start_time, &_cheney_end_time, stats_gc);
            } else {
                //local_collector->new_pn = true;
                if(local_collector->gc_state == LOCAL_MARK_IDLE) {
//                    printf("LOCAL_MARK_IDLE\n");
                    local_collector->gc_state = LOCAL_MARK_GC;
                    unsigned unused;
                    local_nursery_roots_and_mark(private_nursery,
                                                 NULL,
                                                 local_collector,
                                                 cur_thread_node->thread_handle,
                                                 YES_INTER_SLOT,
                                                 NULL,
                                                 unused,
                                                 false,
                                                 false, // don't use watermarks
                                                 false);
                } else {
                    assert(local_collector->gc_state == LOCAL_MARK_LIVE_IDENTIFIED);
                }

#ifdef NEW_APPROACH
                Partial_Reveal_Object *old_location, *new_location;

#if 1
                pn_info::mb_iter live_iter;
                unsigned pn_live_obj_num = 0;
                for(live_iter = local_collector->start_iterator();
                    live_iter.valid();
                    ++live_iter, ++pn_live_obj_num) {

                    old_location = (*live_iter);
#else
                for(old_location = (Partial_Reveal_Object*)local_collector->start_iterator();
                    old_location;
                    old_location = (Partial_Reveal_Object*)local_collector->next_iterator()) {
#endif

                    Partial_Reveal_VTable *oldvt = NULL;
                    if((local_collector->gc_state == LOCAL_MARK_LIVE_IDENTIFIED) && (pn_live_obj_num < local_collector->cur_pn_live_obj_num)) {
                        assert(old_location->isLowFlagSet());
                        if(old_location->isMarked()) {
                            oldvt = old_location->vt();
                        } else {
                            gc_add_root_set_entry_mid_pn_collect((void**)old_location);
                            new_location = (Partial_Reveal_Object*)old_location->get_raw_forwarding_pointer();
                            oldvt = new_location->vt();
                        }
                    } else {
                        oldvt = (Partial_Reveal_VTable*)((POINTER_SIZE_INT)old_location->vt_raw & (~(0x3)));
                    }

                    if(local_collector->gc_state == LOCAL_MARK_LIVE_IDENTIFIED) {
                        local_scan_one_object(old_location,oldvt,*local_collector,ONLY_INTER_SLOT);
                    }
                }
#else
                ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

                for(live_object_iter  = local_collector->live_objects->begin();
                    live_object_iter != local_collector->live_objects->end();
                    ++live_object_iter) {

                    PncLiveObject *pnc = live_object_iter.get_addr();

                    // We don't collect inter-slots in every local nursery collection
                    // since it is too expensive.  So, if we interrupt a local nursery collection
                    // in the allocating new object phase then we walk the live objects and
                    // collect inter-slots here and let the code below add them normally.
                    Partial_Reveal_Object *old_location, *new_location;

                    old_location = pnc->old_location;
                    new_location = pnc->new_location;

                    if(local_collector->gc_state == LOCAL_MARK_LIVE_IDENTIFIED) {
//                      if(new_location > (Partial_Reveal_Object*)1) {
//                            pnc->old_location->set_vtable((unsigned)pnc->vt);
//                        }
                        local_scan_one_object(old_location,pnc->vt,NULL,NULL,*local_collector,ONLY_INTER_SLOT);
//                        if(new_location > (Partial_Reveal_Object*)1) {
//                            pnc->old_location->set_vtable((unsigned)pnc);
//                      }
                    }

                    gc_add_root_set_entry((void**)&(pnc->new_location),FALSE);
                }
#endif

                pn_info::inter_iterator inter_iter;
                Slot one_slot(NULL);

#ifdef PUB_PRIV
                external_pointer *ep_temp = (external_pointer*)private_nursery->tls_current_ceiling;
                while(ep_temp < local_collector->currently_used_nursery_end) {
#if 1
                    if(ep_temp->base) {
                        p_global_gc->gc_internal_add_weak_root_set_entry((Partial_Reveal_Object**)&(ep_temp->base),0,true);
                        POINTER_SIZE_INT offset = (POINTER_SIZE_INT)ep_temp->slot.get_value() - (POINTER_SIZE_INT)ep_temp->base;
                        p_global_gc->gc_internal_add_weak_root_set_entry((Partial_Reveal_Object**)ep_temp->slot.get_address(),offset,true);
                    }
#else
                    gc_add_root_set_entry((void**)&(ep_temp->base),FALSE);

                    POINTER_SIZE_INT offset = (POINTER_SIZE_INT)ep_temp->slot.get_value() - (POINTER_SIZE_INT)ep_temp->base;
                    slot_offset_entry soe = slot_offset_entry((void**)ep_temp->slot.get_address(), ep_temp->base, offset);

                    interior_pointer_table->add_entry (soe);
                    // pass the slot from the interior pointer table to the gc.
                    slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                    p_global_gc->gc_internal_add_root_set_entry(&(addr->base));

#endif

                    ep_temp++;
                }
#endif // PUB_PRIV

				// Is there an equivalent for the cheney scan for these two parts?
                for(inter_iter  = local_collector->inter_slots->begin();
                    inter_iter != local_collector->inter_slots->end();
                    ++inter_iter) {
//                    one_slot.set(inter_iter->slot);
//                  gc_add_root_set_entry((void**)one_slot.get_value(),FALSE);
                    gc_add_root_set_entry((void**)inter_iter->slot,FALSE);
                } // while (one_slot)

                ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;

                for(interior_pointer_iter  = local_collector->interior_pointer_table_public.begin();
                    interior_pointer_iter != local_collector->interior_pointer_table_public.end();
                    ++interior_pointer_iter) {

                    slot_offset_entry entry = interior_pointer_iter.get_current();
                    if(get_object_location(entry.base,local_collector) == PUBLIC_HEAP) {
                        interior_pointer_table->add_entry (entry);
                        // pass the slot from the interior pointer table to the gc.
                        slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                        p_global_gc->gc_internal_add_root_set_entry(&(addr->base));
                    }
#ifdef PUB_PRIV
                    if(g_use_pub_priv && get_object_location(entry.base,local_collector) == PRIVATE_HEAP) {
                        interior_pointer_table->add_entry (entry);
                        // pass the slot from the interior pointer table to the gc.
                        slot_offset_entry *addr = interior_pointer_table->get_last_addr();
                        p_global_gc->gc_internal_add_root_set_entry(&(addr->base));
                    }
#endif
                }
            }

	        gc_time_end_hook("One thread local nursery scan", &_thread_start_time, &_thread_end_time, stats_gc);
            cur_thread_node = cur_thread_node->p_active_gc_thread_info; // Do it for all the threads.
        }

        gc_time_end_hook("Local nursery scan", &_ln_start_time, &_ln_end_time, stats_gc);
    }

    unsigned int total_LOB_bytes_recovered = 0;

    if (stats_gc) {
        gc_time_end_hook("Root Set Enum", &_start_time, &_end_time, true);
        orp_cout << "Number of roots enumerated by ORP = " << num_roots_added << std::endl;
        num_roots_added = 0; // zero out for the next GC cycle
    } else {
        gc_time_end_hook("Root Set Enum", &_start_time, &_end_time, false);
    }
    if (compaction_this_gc) {
        // Save roots away for repointing if they point to compacted areas
        memcpy(_save_array_of_roots, _array_of_roots, _num_roots * sizeof(void *));
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // All the roots have been obtained from ORP.
    if (verify_gc || verify_live_heap) {
        memset(_verify_array_of_roots, 0, sizeof(void *) * _num_roots);
        memcpy(_verify_array_of_roots, _array_of_roots, _num_roots * sizeof(void *));
        unsigned int lives_before_gc = trace_verify_heap(true);
        if (verbose_gc) {
            printf("%d  live objects found before starting GC[%d]\n", lives_before_gc, _gc_num - 1);
        }

        if (verify_live_heap) {
            take_snapshot_of_lives_before_gc(_num_live_objects_found_by_first_trace_heap, _live_objects_found_by_first_trace_heap);
        }
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    gc_time_start_hook(&_start_time);
    if(parallel_clear) {
        get_gc_threads_to_begin_task(GC_CLEAR_MARK_BIT_VECTORS);
        wait_for_gc_threads_to_complete_assigned_task();
    } else {
        clear_all_mark_bit_vectors_of_unswept_blocks();
    }
    gc_time_end_hook("clear_all_mark_bit_vectors_of_unswept_blocks", &_start_time, &_end_time, stats_gc ? true : false);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (compaction_this_gc) {
        gc_time_start_hook(&_start_time);
        // Pick blocks to compact during this GC excluding any in the m_pinned_blocks set.
        _p_block_store->determine_compaction_areas_for_this_gc(m_pinned_blocks);
        gc_time_end_hook("determine_compaction_areas_for_this_gc", &_start_time, &_end_time, stats_gc ? true : false);
    }

    cheney_spaces::iterator cspace_iter;
    for(cspace_iter  = this_gc_cheney_spaces.begin();
        cspace_iter != this_gc_cheney_spaces.end();
        ++cspace_iter) {
        cspace_iter.get_current().forbid_compaction_here(_p_block_store);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (mark_scan_load_balanced) {
        gc_time_start_hook(&_start_time);
        setup_mark_scan_pools();
        gc_time_end_hook("setup_mark_scan_pools", &_start_time, &_end_time, stats_gc ? true : false);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Lets begin the MARK SCAN PHASE

    gc_time_start_hook(&_start_time);

    // Mix up the roots if randomization is turned on.
    if(randomize_roots) {
        // Don't start at 0 because if you have only one root then you won't find
        // another index to swap with.
        for (unsigned int i = 1; i < _num_roots; i++) {
            unsigned swapper = i;
            while(swapper == i) {
                swapper = GetRandom(0,_num_roots-1);
            }

            Partial_Reveal_Object **temp = _array_of_roots[i];
            _array_of_roots[i] = _array_of_roots[swapper];
            _array_of_roots[swapper] = temp;
        }
    }

    g_root_index_hint = 0;
    // Wake up GC threads and get them to start doing marking and scanning work
    get_gc_threads_to_begin_task(GC_MARK_SCAN_TASK);
    wait_for_gc_threads_to_complete_assigned_task();
    // All marking of the heap should be done.
    gc_time_end_hook("MarkScan", &_start_time, &_end_time, stats_gc ? true : false);

#if 0
    for(cspace_iter  = this_gc_cheney_spaces.begin();
        cspace_iter != this_gc_cheney_spaces.end();
        ++cspace_iter) {
        // process objects here
        cspace_iter.get_current().mark_objects_in_block();
    }
#endif

#ifdef _DEBUG
    if(stats_gc) {
        orp_cout << "Total indirections removed = " << indirections_removed << std::endl;
    }
#endif

    bool found_wpo_change = true;
    std::vector<weak_pointer_object*> new_wpos;
    std::vector<weak_pointer_object*> wpos_left;
    unsigned wpo_index;

    if(!g_treat_wpo_as_normal) {
        if(is_young_gen_collection()) {
            for(wpo_index = 0;
                wpo_index < p_global_gc->m_wpos.size();
                ++wpo_index) {
                weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];

		        assert(is_object_pointer(root));

                bool previously_unmarked;

                previously_unmarked = mark_object_in_block(root);
                // make the wpo live
                if(previously_unmarked) {
		            _gc_threads[0]->add_to_marked_objects(root);
#ifdef _DEBUG
                    if(profile_out) {
                        *profile_out << p_global_gc->get_gc_num() << " 0 " << root->vt() << " " << root << " " << get_object_size_bytes(root) << "\n";
                    }
#endif
                }

                Slot pp_target_object(NULL);

                pp_target_object.set(&(root->m_key));
            	::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

                pp_target_object.set(&(root->m_value));
            	::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

        	    pp_target_object.set(&(root->m_finalizer));
    	        ::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

                process_mark_stack(_gc_threads[0],_gc_threads[0]->get_mark_stack());
            }
        } else {
            /****************************************************************************************
             *                         PROCESS WEAK POINTERS OBJECTS                                *
             ****************************************************************************************/
            while(found_wpo_change) {
                found_wpo_change = false;

                for(wpo_index = 0;
                    wpo_index < p_global_gc->m_wpos.size();
                    ++wpo_index) {
                    weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];

		            assert(is_object_pointer(root));

                    bool previously_unmarked;

                    previously_unmarked = mark_object_in_block(root);
                    // make the wpo live
                    if(previously_unmarked) {
		                _gc_threads[0]->add_to_marked_objects(root);
#ifdef _DEBUG
                        if(profile_out) {
                            *profile_out << p_global_gc->get_gc_num() << " 0 " << root->vt() << " " << root << " " << get_object_size_bytes(root) << "\n";
                        }
#endif
                    }

                    if(!is_in_heap(root->m_key) || is_object_marked(root->m_key)) {
                        found_wpo_change = true;
                        new_wpos.push_back(root);

                        Slot pp_target_object(NULL);

                        pp_target_object.set(&(root->m_key));
            	        ::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

                        pp_target_object.set(&(root->m_value));
            	        ::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

        	            pp_target_object.set(&(root->m_finalizer));
    	                ::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

                        process_mark_stack(_gc_threads[0],_gc_threads[0]->get_mark_stack());
                    } else {
                        wpos_left.push_back(root);
                    }
                }
                p_global_gc->m_wpos.swap(wpos_left);
                wpos_left.clear();
            }

            wpos_left.swap(p_global_gc->m_wpos); // wpos_left has the stuff to run finalizer on
            p_global_gc->m_wpos.swap(new_wpos);  // the continuing WPOs.
            new_wpos.clear();

            for(wpo_index = 0;
                wpo_index < wpos_left.size();
                ++wpo_index) {
                weak_pointer_object *root = wpos_left[wpo_index];

                Slot pp_target_object(NULL);

                pp_target_object.set(&(root->m_finalizer));
                ::scan_one_slot(pp_target_object, _gc_threads[0], false, root);

                process_mark_stack(_gc_threads[0],_gc_threads[0]->get_mark_stack());
            }
        }
    }

    /****************************************************************************************
     *                         PROCESS WEAK ROOTS AND FINALIZERS                            *
     ****************************************************************************************/
    // Since short weak roots don't track an object into or while on the finalizable
    // queue, we process short weak roots here.  Those roots will be NULLed (if not
    // marked) or added to the list of roots to potentially be updated later.
    // See comments before process_weak_roots();
    process_weak_roots(true,compaction_this_gc);
//    if(!is_young_gen_collection()) {
        // Identify the objects that need to be moved to the finalizable queue.
        // Mark and scan any such objects.
        identify_and_mark_objects_moving_to_finalizable_queue(compaction_this_gc);
//    }
    // Since long weak roots DO track an object into or while on the finalizable
    // queue, we process long weak roots here.  Those roots will be NULLed (if not
    // marked) or added to the list of roots to potentially be updated later.
    // See comments before process_weak_roots();
    process_weak_roots(false,compaction_this_gc);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef REMEMBERED_SET_ARRAY
    if (verify_gc) {
        // NEED to verify that no duplicate slots are collected by mark/scan phase....
        _p_block_store->verify_no_duplicate_slots_into_compaction_areas();
    }
#endif
    ////////////////////////// M A R K S   C H E C K   A F T E R    M A R K / S C A N ///////////////////////////////////////////

    verify_marks_for_all_lives();
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (mark_scan_load_balanced) {
        _mark_scan_pool->verify_after_gc();
        _verify_gc_threads_state();
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    if (compaction_this_gc) {
        gc_time_start_hook(&_start_time);
        get_gc_threads_to_begin_task(GC_ALLOCATE_FORWARDING_POINTERS_FOR_COMPACTION_LIVE_OBJECTS_TASK);
        wait_for_gc_threads_to_complete_assigned_task();
        gc_time_end_hook("GC_ALLOCATE_FORWARDING_POINTERS_FOR_COMPACTION_LIVE_OBJECTS_TASK", &_start_time, &_end_time, stats_gc ? true : false);

        gc_time_start_hook(&_start_time);
        get_gc_threads_to_begin_task(GC_FIX_SLOTS_TO_COMPACTION_LIVE_OBJECTS_TASK);
        wait_for_gc_threads_to_complete_assigned_task();
        gc_time_end_hook("GC_FIX_SLOTS_TO_COMPACTION_LIVE_OBJECTS_TASK", &_start_time, &_end_time, stats_gc ? true : false);

        gc_time_start_hook(&_start_time);
        // Now repoint all root slots if the root objects that they pointed to will be slide compacted.
        // this should be small and we should be able to do this as part of the main thread with little overhead
        repoint_all_roots_into_compacted_areas();
        repoint_all_roots_with_offset_into_compacted_areas();

        for(wpo_index = 0;
            wpo_index < wpos_left.size();
            ++wpo_index) {
            weak_pointer_object *root = wpos_left[wpo_index];

            if (root->isForwarded()) { // has been forwarded
                assert(is_compaction_block(GC_BLOCK_INFO(root)));

                Partial_Reveal_Object *new_root_object = root->get_forwarding_pointer();
                // Update root slot
                wpos_left[wpo_index] = (weak_pointer_object*)new_root_object;
            }
        }
        for(wpo_index = 0;
            wpo_index < p_global_gc->m_wpos.size();
            ++wpo_index) {
            weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];

            if (root->isForwarded()) { // has been forwarded
                assert(is_compaction_block(GC_BLOCK_INFO(root)));

                Partial_Reveal_Object *new_root_object = root->get_forwarding_pointer();
                // Update root slot
                p_global_gc->m_wpos[wpo_index] = (weak_pointer_object*)new_root_object;
            }
        }

        gc_time_end_hook("Repoint root slots during compaction", &_start_time, &_end_time, stats_gc ? true : false);

        gc_time_start_hook(&_start_time);
        get_gc_threads_to_begin_task(GC_SLIDE_COMPACT_LIVE_OBJECTS_IN_COMPACTION_BLOCKS);
        wait_for_gc_threads_to_complete_assigned_task();
        gc_time_end_hook("GC_SLIDE_COMPACT_LIVE_OBJECTS_IN_COMPACTION_BLOCKS", &_start_time, &_end_time, stats_gc ? true : false);

#if 0
        gc_time_start_hook(&_start_time);
        get_gc_threads_to_begin_task(GC_RESTORE_HIJACKED_HEADERS);
        wait_for_gc_threads_to_complete_assigned_task();
        gc_time_end_hook("GC_RESTORE_HIJACKED_HEADERS", &_start_time, &_end_time, stats_gc ? true : false);
#endif
    }

////////////////////////////////////////// NOW SWEEP THE ENTIRE HEAP/////////////////////////////////////////////////////////
    if (sweeps_during_gc || g_gen) {
        gc_time_start_hook(&_start_time);
        // Divide all managed chunks equally among all threads and get them to sweep all of them.
        prepare_to_sweep_heap();
        // Wake up GC threads and get them to start doing sweep work
        get_gc_threads_to_begin_task(GC_SWEEP_TASK);
        wait_for_gc_threads_to_complete_assigned_task();

        if (stats_gc) {
            unsigned int recovered = 0;
            unsigned int fragments = 0;
            unsigned int bswept    = 0;
            unsigned int num_fragments = 0;
            for (unsigned int n = 0; n < g_num_cpus; n++) {
                recovered += _gc_threads[n]->get_num_bytes_recovered_by_sweep();
                fragments += _gc_threads[n]->sweeping_stats.amount_in_fragments;
                bswept    += _gc_threads[n]->sweeping_stats.blocks_swept;
                num_fragments += _gc_threads[n]->sweeping_stats.num_fragments;
            }
            orp_cout << "Chunks Sweep recovered total bytes -- " << recovered << std::endl;
            orp_cout << "Chunks Sweep found fragmented bytes -- " << fragments << std::endl;
            orp_cout << "Chunks Sweep swept this many blocks -- " << bswept << std::endl;
            if(fragments) {
                if(!num_fragments) {
                    orp_cout << "Error: fragmented data found but num_fragments = 0\n";
                } else {
                    orp_cout << "Chunks Sweep average fragment size = " << (float)fragments / num_fragments << "\n";
                }
            }
            gc_time_end_hook("Chunks Sweep", &_start_time, &_end_time, true);
        } else {
            gc_time_end_hook("Chunks Sweep", &_start_time, &_end_time, false);
        }
    } else {
        gc_time_start_hook(&_start_time);
        prepare_chunks_for_sweeps_during_allocation(compaction_this_gc);
        gc_time_end_hook("Prepare chunks for Allocation Sweeps", &_start_time, &_end_time, stats_gc ? true : false);
    }
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    if(g_gen) {
        unsigned num_blocks = _p_block_store->get_num_total_blocks_in_block_store();
        unsigned num_cur_young = 0;
        unsigned block_index;
        for (block_index = 0;
             block_index < num_blocks;
             ++block_index) {
            block_info *bi = _p_block_store->get_block_info(block_index);
            if(!bi) continue;

            if(!bi->generation) {
                num_cur_young++;
            }

            bi->generation = 1;
        }
        if(!g_gen_all) {
            last_young_gen_percentage = (float)num_cur_young / num_blocks;
        } else {
            last_young_gen_percentage = 1.0;
        }
    }

    if(!is_young_gen_collection()) {
        for(wpo_index = 0;
            wpo_index < wpos_left.size();
            ++wpo_index) {
            weak_pointer_object *root = wpos_left[wpo_index];

            wpo_finalizer_callback(root);
        }
    }

/////////////////////////////////////// RESET the gc TLS state. /////////////////////////////////////////////////////////////

    // Reset the gc_information fields for each thread that is active. This includes setting the current
    // free and ceiling to NULL and reset alloc block to the start of the chuck.
    // The gc malloc code should be able to deal with seeing free set to 0 and know that it needs to just
    // go to the alloca block and get the next free alloc area.
    //
    reset_thread_nurseries();

    // Sweep the LOS
    sweep_stats los_stats;
    gc_time_start_hook(&_start_time);
    if (_los_blocks) {
        block_info *block = _los_blocks;
        // LOS blocks never get compacted
        assert(!is_compaction_block(block));
        while (block) {
            // XXX -- TO DO....change LOS allocation to use existing blocks...reorder them or something
            assert(block->in_los_p);
            total_LOB_bytes_recovered += sweep_one_block(block,los_stats);
            block = block->next_free_block;
        }
    }
    // Sweep the "single object blocks"
    if (_single_object_blocks) {
        total_LOB_bytes_recovered += sweep_single_object_blocks(_single_object_blocks,los_stats);
    }
    if (stats_gc) {
        gc_time_end_hook("LOB & SOB Sweep", &_start_time, &_end_time, true);
        orp_cout << "total_LOB_bytes_recovered = " << total_LOB_bytes_recovered << std::endl;
        orp_cout << "amount of usable space found by los sweeping = " << los_stats.amount_recovered << "\n";
        orp_cout << "amount of fragmented space found by los sweeping = " << los_stats.amount_in_fragments << "\n";
    } else {
        gc_time_end_hook("LOB & SOB Sweep", &_start_time, &_end_time, false);
    }

    gc_time_start_hook(&_start_time);

    // Refresh block store if needed
    if(g_gen) {
        // return all free blocks to block store if this GC was due to a LOS request.
        return_free_blocks_to_block_store(-1, false, incremental_compaction || (fullheapcompact_at_forcegc && force_gc));
	} else {
		switch(g_return_free_block_policy) {
		case MAINTAIN_RATIO:
			return_free_blocks_to_block_store(block_type_single + block_type_los, false, incremental_compaction || (fullheapcompact_at_forcegc && force_gc));
			break;
		case RETURN_ALL_IF_LOS:
			if(for_los) {
				// return all free blocks to block store if this GC was due to a LOS request.
				return_free_blocks_to_block_store(-1, false, incremental_compaction || (fullheapcompact_at_forcegc && force_gc));
				break;
			}
		case WATERMARK:
			if (_p_block_store->get_num_free_blocks_in_block_store() < _p_block_store->get_block_store_low_watermark_free_blocks()) {
				// return all free blocks to block store if this GC was due to a LOS request.
				return_free_blocks_to_block_store(_p_block_store->get_block_store_low_watermark_free_blocks(), false, incremental_compaction || (fullheapcompact_at_forcegc && force_gc));
			}
			break;
		default:
            printf("Unknown return free block policy.\n");
            exit(18000);
		}
	}

    if (stats_gc) {
#if 0
		if(for_los || g_gen) {
			orp_cout << "Returned all free blocks to the block store\n";
		} else {
			orp_cout << "Returned enough free blocks to satisfy low watermark\n";
		}
#endif
        orp_cout << "Block store has " << _p_block_store->get_num_free_blocks_in_block_store() << " free blocks now\n";
        orp_cout << "#allocatable chunks = " << _free_chunks_end_index + 1 << std::endl;
    }
    if (size_failed > GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
        // a single object block allocation failed...see if we can satisfy request based on what we have in the BS
        while (true) {
            // Keep doing below until we can satisfy the request that failed and thus caused GC
            _p_block_store->coalesce_free_blocks();
            unsigned int number_of_blocks_needed = (size_failed / _p_block_store->get_block_size_bytes()) + 1;
            if (_p_block_store->block_store_can_satisfy_request(number_of_blocks_needed) == false) {
                // return twice as many blocks to BS
                unsigned blocks_returned = return_free_blocks_to_block_store(number_of_blocks_needed * 2, false, incremental_compaction || (fullheapcompact_at_forcegc && force_gc));
				if(!blocks_returned) {
					orp_cout << "GC: out of memory.  Could not allocate memory for allocation of size " << size_failed << std::endl;
					orp_exit(18002);
				}
            } else {
                if (verbose_gc && stats_gc) {
                    orp_cout << "Block store has after recycling and coalescing as needed " << _p_block_store->get_num_free_blocks_in_block_store() << " free blocks now\n";
                    orp_cout << "#allocatable chunks = " << _free_chunks_end_index + 1 << std::endl;
                }
                break;
            }
        }
    }
    gc_time_end_hook("Block store management after GC", &_start_time, &_end_time, verbose_gc && stats_gc ? true : false);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    _gc_num_time = gc_time_end_hook("GC time", &_gc_start_time, &_gc_end_time, false);
    _total_gc_time += _gc_num_time;
    //    orp_cout << "GC[" << _gc_num << "]: " << _gc_num_time << "ms, Total = " << _total_gc_time << "ms" << std::endl;
    if (verbose_gc) {
        printf ("GC[%d]: %dms, Total = %dms\n", (_gc_num - 1), _gc_num_time, _total_gc_time);
        fflush(stdout);
    }
#ifdef GC_VERIFY_VM
    running_gc = false; // We hold gc lock so we can change this without race conditions.
#endif // GC_VERIFY_VM
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////                                                                       //

    if(stats_gc) {
        p_global_gc->_p_block_store->print_block_stats();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (verify_gc || verify_live_heap) {
        unsigned int lives_after_gc = trace_verify_heap(false);
        if (verbose_gc) {
            printf("%d  live objects found after GC[%d]\n", lives_after_gc, _gc_num - 1);
        }
        if (verify_live_heap) {
            verify_live_heap_before_and_after_gc(_num_live_objects_found_by_second_trace_heap, _live_objects_found_by_second_trace_heap);
        }
        //        assert(_num_live_objects_found_by_second_trace_heap == _num_live_objects_found_by_first_trace_heap);
    }

    if(!is_young_gen_collection()) {
        // Earlier, we may have added objects to a temporary list of objects that require finalization.
        // We did this so that we could take pointers to the interior of this list and use those as roots
        // that could be updated if compaction is enabled.
        // Now that we know we have (potentially) updated roots, we can added them to the finalizable queue.
        add_objects_to_finalizable_queue();
    }

    /* Used temporarily until full concurrent GC support is in place.
       Processing of the global McRT data structure (currently done at
       the end of the GC cycle) will then be done incrementally (and
       concurrently with mutator threads) in a way similar to
       processing McRT logs. */
    orp_post_gc_mcrt_cleanup();

    active_gc_thread = 0;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Restart mutators

    resume_orp();
#ifndef OLD_MULTI_LOCK
    release_active_thread_gc_info_list_lock(); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#endif // OLD_MULTI_LOCK

#ifdef _DEBUG
    if(profile_out) profile_out->flush();
    if(live_dump)   live_dump->flush();
#endif

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (compaction_this_gc) {
        // Zero out live bit lists and reverse compaction information in BS...this can be done concurrently since we hold the GC lock right now.
        gc_time_start_hook(&_start_time);
        _p_block_store->reset_compaction_areas_after_this_gc();
        gc_time_end_hook("Reset compaction areas after GC", &_start_time, &_end_time, stats_gc ? true : false);
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _IA64_
    // dynopt_pass_GC_time_to_PMU_driver only available on IA64
    if (delta_dynopt) {
        dynopt_pass_GC_time_to_PMU_driver();
    }
#endif

#ifndef HAVE_PTHREAD_H
    mcrtThreadSuspendable(mcrtThreadGet());
#endif // !HAVE_PTHREAD_H
//    pgc_local_nursery_collection_finish();
} // ::Garbage_Collector::reclaim_full_heap_from_gc_thread


external_pointer * GC_Small_Nursery_Info::allocate_external_pointer(void) {
	// Compute where the new one collection object would go.
	external_pointer *new_external_pointer = (external_pointer*)((char*)tls_current_ceiling - sizeof(external_pointer));
	if(new_external_pointer <= tls_current_free) return NULL; // no space for the new one collection object
	tls_current_ceiling = new_external_pointer;
	return new_external_pointer;
}

bool GC_Small_Nursery_Info::is_duplicate_last_external_pointer(Partial_Reveal_Object **slot) {
    if(tls_current_ceiling < this->local_gc_info->currently_used_nursery_end) {
        external_pointer *ep_temp = (external_pointer*)tls_current_ceiling;
        if(ep_temp->slot.get_value() == slot) {
            return true;
        }
    }
    return false;
}

// Centralize all mallocs to this routine...
static unsigned int total_memory_alloced = 0;

void *malloc_or_die(unsigned int size) {
    void *temp = malloc(size);
    if (!temp) {
        printf ("Internal MALLOC or DIE dies in GC.");
        assert(0);
    }
    total_memory_alloced += size;
    return temp;
}

// Malloc an areas or report the problems, then clear the area.
void *malloc_cleared_or_die (unsigned int size) {
    void *result = malloc_or_die(size);
    memset (result, 0, size);
    return result;
}

#ifdef MCRT
#ifdef _USRDLL
void main(int argc,char* argv[]) // Bogus entry point to satisfy mcrt.lib
{
    printf ("-- How did we end up here, this should only be in a dll and never called.\n");
    printf ("-- main is needed for bulding the GC .dll with McRT since some of the (unused)\n");
    printf ("-- kmpc code calls main so something needs to be available to satisfy the external.\n");
    assert (0);
}
#endif
#endif

#ifdef PUB_PRIV

struct cph_root {
	char anonymous[PILLAR_VSE_SIZE];
	void **        root;
    PrtGcTag       tag;
    void *         gcTagParameter;
    cph_root(void **r, PrtGcTag t=PrtGcTagDefault, void *g=NULL): root(r), tag(t), gcTagParameter(g) {}
};

extern "C" void cph_rse_callback(PrtVseHandle theHandle, struct PrtRseInfo *rootSetInfo) {
    struct cph_root *cph = (struct cph_root*)theHandle;
    if(cph->root) {
        rootSetInfo->callback((void*)rootSetInfo->env, cph->root, cph->tag, cph->gcTagParameter);
    }
} // cph_rse_callback

extern "C" void __stdcall cph_destructor(void);

void collect_private_heap(GC_Small_Nursery_Info *private_nursery,
						  GC_Thread_Info *thread,
						  Partial_Reveal_Object *escaping_object) {
	assert(escaping_object); // so far this param must have a value

	TIME_STRUCT _start_time, _end_time;
    gc_time_start_hook(&_start_time);

	pn_info *local_collector = private_nursery->local_gc_info;

	local_collector->num_private_heap_collections++;

	if(get_object_location(escaping_object,local_collector) == PRIVATE_NURSERY) {
        local_collector->ph_to_pn_escape = false;
		cph_root cph_vse((void**)&escaping_object);
		prtPushVse((PrtCodeAddress)&cph_destructor,(PrtVseHandle)&cph_vse);
		local_nursery_collection(thread, NULL, escaping_object, false);
        prtPopVse();
	} else {
	    // We move all the local nursery objects into the private heap.
		local_nursery_collection(thread, NULL, NULL, false);
	}

#ifdef NEW_APPROACH
    // Now, get all the roots (no slots left since PN is empty) into the private heap from the thread's stack.
	local_collector->gc_state = LOCAL_MARK_PRIVATE_HEAP_GC;

	// ----------------------------------------------------------------------------------------------

	// Mark the escaping objects and set the MUST_LEAVE flag so that we know to only allocate new spots for these objects.
    push_bottom_on_local_mark_stack(escaping_object,&(local_collector->mark_stack));

    std::vector<Partial_Reveal_Object*> ph_lives;
    std::map<Partial_Reveal_Object*,Partial_Reveal_Object*> ph_escaping;

    /* First we start with the object that is escaping.  All escaping objects must be in the private
       heap and this is guaranteed by the previous private nursery collection.  Put the escaping objects
       onto the ph_lives vector. */
	while(1) {
		Partial_Reveal_Object *obj_to_trace;

        obj_to_trace = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
		if(!obj_to_trace) break;  // no more objects to mark
		if(obj_to_trace->isMarked()) {
			continue;  // already marked
		}

        if(get_object_location(obj_to_trace,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}

		Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
		unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

		obj_to_trace->mark(); // mark the object as being visited

		// This should add private heap to private heap slots into the inter_slots list.
        ph_lives.push_back(obj_to_trace);
		local_scan_one_object(obj_to_trace, obj_trace_vt, *local_collector, PRIVATE_HEAP_SLOT);
	}

    local_collector->num_private_collection_escaping += ph_escaping.size();

	// ----------------------------------------------------------------------------------------------
    // Find new locations for the private_heap objects moving to the public heap.
	GC_Nursery_Info *public_nursery    = thread->get_public_nursery();
	GC_Nursery_Info *immutable_nursery = thread->get_public_immutable_nursery();

    // Determine the new locations for each of the escaping objects.
    unsigned ph_live_index;
    for(ph_live_index = 0;
        ph_live_index < ph_lives.size();
        ++ph_live_index) {
		Partial_Reveal_Object *p_obj = ph_lives[ph_live_index];
        Partial_Reveal_VTable *obj_vtable = p_obj->vt();

#ifdef _DEBUG
        if(get_object_location(p_obj,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}
#endif // _DEBUG

		unsigned obj_size = get_object_size_bytes_with_vt(p_obj,obj_vtable);

		Partial_Reveal_Object *new_location;
        bool obj_is_immutable = pgc_is_vtable_immutable((struct VTable*)obj_vtable);

        Partial_Reveal_Object *frontier;

        GC_Nursery_Info *nursery_to_use = public_nursery;
        if(separate_immutable && obj_is_immutable) {
            nursery_to_use = immutable_nursery;
        }

		frontier = (Partial_Reveal_Object *)nursery_to_use->tls_current_free;
        adjust_frontier_to_alignment(frontier, obj_vtable);

		POINTER_SIZE_INT new_free = (obj_size + (POINTER_SIZE_INT)frontier);
		if (new_free <= (POINTER_SIZE_INT) nursery_to_use->tls_current_ceiling) {
			frontier->set_vtable((Allocation_Handle)obj_vtable);
			// increment free ptr and return object
			nursery_to_use->tls_current_free = (void *) new_free;
		    new_location = frontier;
		} else {
			// GC may happen here!!!
			new_location = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
					obj_size,
					(Allocation_Handle)obj_vtable,
					thread,
					nursery_to_use,
					false);
			public_nursery    = thread->get_public_nursery();
			immutable_nursery = thread->get_public_immutable_nursery();
		}

	    if (obj_vtable->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK) {
			unsigned array_length_offset = pgc_array_length_offset(obj_vtable);
			unsigned *p_array_length = (unsigned*)((char*)new_location + array_length_offset);
			*p_array_length = vector_get_length_with_vt((Vector_Handle)p_obj,obj_vtable);
		}

        ph_escaping.insert(std::pair<Partial_Reveal_Object*,Partial_Reveal_Object*>(p_obj,new_location));

#ifdef CONCURRENT
		switch(private_nursery->concurrent_state_copy) {
		case CONCURRENT_IDLE:
			// intentionally do nothing
			break;
		case CONCURRENT_MARKING:
			// Make object black
			if(get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
    			new_location->mark(); // no need for atomic since no one can hold a reference except this thread
	    		mark_object(new_location);
		    	gc_trace (new_location, "Marking object during marking phase when transferring to public heap.");
			}
			break;
		case CONCURRENT_SWEEPING:
			if(get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
				if(new_location >= g_sweep_ptr) {
//				if(new_location >= local_collector->sweep_ptr_copy) {
					bool has_slots = obj_vtable->get_gcvt()->gc_object_has_slots ? true : false;
					new_location->mark(); // no need for atomic since no one can hold a reference except this thread
					mark_object(new_location);
					if(has_slots) {
    					// make object gray
						assert(sizeof(new_location) == 4);
						local_collector->new_moved_grays.push_back(new_location);
						if(local_collector->new_moved_grays.size() == local_collector->new_moved_grays.capacity()) {
							add_to_grays_local(local_collector,local_collector->new_moved_grays);
						}
					}
				}
			}
			break;
		default:
			assert(0);
		}
#endif // CONCURRENT
	}

#ifdef _DEBUG
    if(local_collector->ph_ofstream) {
        (*local_collector->ph_ofstream) << "node [color=black];\n";
        (*local_collector->ph_ofstream) << "edge [color=black];\n";
    }
#endif

	unsigned unused;
	local_nursery_roots_and_mark(private_nursery,
					             NULL,
								 local_collector,
								 thread->thread_handle,
								 PRIVATE_HEAP_SLOT,
								 NULL,
								 unused,
								 false,
								 false,
                                 false, // don't use watermarks
                                 false); // just get the roots

#if 0
    for(ph_live_index = 0;
        ph_live_index < ph_lives.size();
        ++ph_live_index) {
		Partial_Reveal_Object *p_obj = ph_lives[ph_live_index];
        p_obj->setLowFlag(); // indicates an escaping object
    }
#endif

#if 0
    unsigned root_index;
    for(root_index = 0;
        root_index < local_collector->m_roots.size();
        ++root_index) {
        Partial_Reveal_Object **root = local_collector->m_roots[root_index];

		if(!is_object_pointer(*root)) continue;

        if(!((*root)->isMarked())) {
            push_bottom_on_local_mark_stack(*root,&(local_collector->mark_stack));
        }
    }

    Partial_Reveal_Object * obj_to_trace;
    while(1) {
        obj_to_trace = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
        if(!obj_to_trace) break;  // no more objects to mark
        if(obj_to_trace->isMarked()) {
            continue;  // already marked
        }

        ph_lives.push_back(obj_to_trace);

        Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

        obj_to_trace->mark(); // mark the object as being visited
        local_scan_one_object(obj_to_trace, obj_trace_vt, *local_collector, PRIVATE_HEAP_SLOT_NON_ESCAPING);
    }
#endif

    // ----------------------------------------------------------------------------------------------
	// Iterate over the slots into the private heap and add them to the mark stack.
	pn_info::inter_iterator inter_iter;
	Slot one_slot(NULL);

	for(inter_iter  = local_collector->inter_slots->begin();
		inter_iter != local_collector->inter_slots->end();
		++inter_iter) {
        one_slot.set(inter_iter->slot);
		Partial_Reveal_Object *pro = one_slot.dereference();
        if(!pro) continue;

        if(pro->isMarked()) {
            continue;
        }

        if(get_object_location(pro,local_collector) == PRIVATE_HEAP) {
   		    push_bottom_on_local_mark_stack(pro,&(local_collector->mark_stack));
        }
	}

	// ----------------------------------------------------------------------------------------------
	// This will just collect slots from other objects in the private heap to objects that will be moved.
    Partial_Reveal_Object * obj_to_trace;
    while(1) {
        obj_to_trace = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
        if(!obj_to_trace) break;  // no more objects to mark
        if(obj_to_trace->isMarked()) {
            continue;  // already marked
        }

        ph_lives.push_back(obj_to_trace);

        if(get_object_location(obj_to_trace,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}

        Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

        obj_to_trace->mark(); // mark the object as being visited
        local_scan_one_object(obj_to_trace, obj_trace_vt, *local_collector, PRIVATE_HEAP_SLOT_NON_ESCAPING);
    }

	// ----------------------------------------------------------------------------------------------

	// Update the slots with the new locations of the moving objects.
	for(inter_iter  = local_collector->inter_slots->begin();
		inter_iter != local_collector->inter_slots->end();
		++inter_iter) {
        one_slot.set(inter_iter->slot);
		Partial_Reveal_Object *pro = one_slot.dereference();
        if(!pro) continue;
        assert(pro->isMarked());

        if(get_object_location(pro,local_collector) == PRIVATE_HEAP) {
            if(pro->isLowFlagSet()) {
                auto ph_iter = ph_escaping.find(pro);
                if(ph_iter == ph_escaping.end()) {
                    printf("Private heap collection: object has low flag set but isn't in escaping set.\n");
                    exit(-1);
                } else {
                    Partial_Reveal_Object *p_new_obj = ph_iter->second;
                    if(p_new_obj) {
                        if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
                            assert(0);
                            printf("Private heap collection slot found for new object location now in the public heap.\n");
                            exit(-1);
                        }
	                    one_slot.unchecked_update(p_new_obj);
                    } else {
                        printf("Private heap collection: object in escaping set has NULL destination.\n");
                        exit(-1);
                    }
                }
            }
        }
	}

	ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;
	for(interior_pointer_iter  = local_collector->interior_pointer_table_public.begin();
		interior_pointer_iter != local_collector->interior_pointer_table_public.end();
		++interior_pointer_iter) {

		slot_offset_entry entry = interior_pointer_iter.get_current();
        void **root_slot                 = entry.slot;
        Partial_Reveal_Object *root_base = entry.base;
        POINTER_SIZE_INT root_offset     = entry.offset;
        void *new_slot_contents = (void *)((Byte*)root_base + root_offset);
        if (new_slot_contents != *root_slot) {
            *root_slot = new_slot_contents;
        }
	}

	// ----------------------------------------------------------------------------------------------

    std::map<Partial_Reveal_Object*,Partial_Reveal_Object*>::iterator ph_iter;
	// Move the objects from the private to the public heap.
	for(ph_iter  = ph_escaping.begin();
		ph_iter != ph_escaping.end();
		++ph_iter) {

		Partial_Reveal_Object *p_obj      = ph_iter->first;
		Partial_Reveal_Object *p_dest_obj = ph_iter->second;

        if(p_dest_obj) {
            if(get_object_location(p_dest_obj,local_collector) != PUBLIC_HEAP) {
                assert(0);
                printf("Private heap collection slot found for new object location now in the public heap.\n");
                exit(-1);
            }
		    memcpy((char*)p_dest_obj + 4,(char*)p_obj + 4,get_object_size_bytes_with_vt(p_obj,(Partial_Reveal_VTable*)p_obj->get_vt_no_low_bits()) - 4);
        } else {
            printf("Private heap collection: object in escaping set has NULL destination.\n");
            exit(-1);
        }
	}

#ifdef _DEBUG
    if(local_collector->ph_ofstream) {
        (*local_collector->ph_ofstream) << "}\n";
        local_collector->ph_ofstream->close();
    }
#endif // _DEBUG
	// ----------------------------------------------------------------------------------------------

    for(ph_live_index = 0;
        ph_live_index < ph_lives.size();
        ++ph_live_index) {
	    Partial_Reveal_Object *p_obj = ph_lives[ph_live_index];
        p_obj->clearLowBits();
    }

#else  // NEW_APPROACH
    // Now, get all the roots (no slots left since PN is empty) into the private heap from the thread's stack.
	local_collector->gc_state = LOCAL_MARK_PRIVATE_HEAP_GC;
	unsigned unused;
	local_nursery_roots_and_mark(private_nursery,
					             NULL,
								 local_collector,
								 thread->thread_handle,
								 PRIVATE_HEAP_SLOT,
								 NULL,
								 unused,
								 false,
								 false,
                                 false); // don't use watermarks

	// ----------------------------------------------------------------------------------------------

#ifdef _DEBUG
#if 0
    if(local_collector->num_private_heap_collections == 4000) {
        local_collector->ph_ofstream = new std::ofstream("ph4000.dot");
        (*local_collector->ph_ofstream) << "digraph ph4000 {\n";
        (*local_collector->ph_ofstream) << "node [color=red];\n";
        (*local_collector->ph_ofstream) << "edge [color=red];\n";
    }
#endif
#endif

	// Mark the escaping objects and set the MUST_LEAVE flag so that we know to only allocate new spots for these objects.
    push_bottom_on_local_mark_stack(NULL,escaping_object,&(local_collector->mark_stack));

	while(1) {
		Partial_Reveal_Object *obj_to_trace;
        PncLiveObject *cur_pnc;

		ObjectPath op = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
		obj_to_trace = op.to;
		if(!obj_to_trace) break;  // no more objects to mark
		if(obj_to_trace->isMarked()) {
			continue;  // already marked
		}

        if(get_object_location(obj_to_trace,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}

		Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
		unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

		obj_to_trace->mark(); // mark the object as being visited

		// This should add private heap to private heap slots into the inter_slots list.
        local_collector->live_objects->push_back(PncLiveObject(obj_to_trace,MUST_LEAVE,obj_trace_vt,op.from));
		cur_pnc = local_collector->live_objects->get_last_addr();

		local_scan_one_object(obj_to_trace, obj_trace_vt, op.from, cur_pnc, *local_collector, PRIVATE_HEAP_SLOT);
	}

    local_collector->num_private_collection_escaping += local_collector->live_objects->size();

	// ----------------------------------------------------------------------------------------------
    // Find new locations for the private_heap objects moving to the public heap.
	GC_Nursery_Info *public_nursery    = thread->get_public_nursery();
	GC_Nursery_Info *immutable_nursery = thread->get_public_immutable_nursery();

	// determine the new location of each object
	ExpandInPlaceArray<PncLiveObject>::iterator live_object_iter;

	for(live_object_iter  = local_collector->live_objects->begin();
		live_object_iter != local_collector->live_objects->end();
		++live_object_iter) {
		Partial_Reveal_Object *p_obj;

		PncLiveObject *pnc = live_object_iter.get_addr();

		// Get the vtable of the object.
        Partial_Reveal_VTable *obj_vtable = pnc->old_location->vt();

		p_obj = pnc->old_location;

#ifdef _DEBUG
        if(get_object_location(p_obj,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}
#endif // _DEBUG

		unsigned obj_size = get_object_size_bytes_with_vt(p_obj,obj_vtable);
        bool obj_vtable_immutable = pgc_is_vtable_immutable((struct VTable*)obj_vtable);

		Partial_Reveal_Object *new_location;

        GC_Nursery_Info *nursery_to_use = public_nursery;
        if(separate_immutable && obj_vtable_immutable) {
            nursery_to_use = immutable_nursery;
        }

		Partial_Reveal_Object *frontier = (Partial_Reveal_Object *)nursery_to_use->tls_current_free;
        adjust_frontier_to_alignment(frontier, obj_vtable);

		POINTER_SIZE_INT new_free = (obj_size + (POINTER_SIZE_INT)frontier);
		if (new_free <= (POINTER_SIZE_INT) nursery_to_use->tls_current_ceiling) {
			frontier->set_vtable((Allocation_Handle)obj_vtable);
			// increment free ptr and return object
			nursery_to_use->tls_current_free = (void *) new_free;
			new_location = frontier;
		} else {
			// GC may happen here!!!
			new_location = (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(
				obj_size,
				(Allocation_Handle)obj_vtable,
				thread,
				nursery_to_use,
				false);
			public_nursery    = thread->get_public_nursery();
            immutable_nursery = thread->get_public_immutable_nursery();
		}
        pnc->new_location = new_location;

	    if (obj_vtable->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK && get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
			unsigned array_length_offset = pgc_array_length_offset(obj_vtable);
			unsigned *p_array_length = (unsigned*)((char*)new_location + array_length_offset);
			*p_array_length = vector_get_length_with_vt((Vector_Handle)p_obj,obj_vtable);
		}

#ifdef CONCURRENT
		switch(private_nursery->concurrent_state_copy) {
		case CONCURRENT_IDLE:
			// intentionally do nothing
			break;
		case CONCURRENT_MARKING:
			// Make object black
			if(get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
    			new_location->mark(); // no need for atomic since no one can hold a reference except this thread
	    		mark_object(new_location);
		    	gc_trace (new_location, "Marking object during marking phase when transferring to public heap.");
			}
			break;
		case CONCURRENT_SWEEPING:
			if(get_object_location(new_location,local_collector) != PRIVATE_NURSERY) {
				if(new_location >= g_sweep_ptr) {
//				if(new_location >= local_collector->sweep_ptr_copy) {
					bool has_slots = obj_vtable->get_gcvt()->gc_object_has_slots ? true : false;
					new_location->mark(); // no need for atomic since no one can hold a reference except this thread
					mark_object(new_location);
					if(has_slots) {
    					// make object gray
						assert(sizeof(new_location) == 4);
						local_collector->new_moved_grays.push_back(new_location);
						if(local_collector->new_moved_grays.size() == local_collector->new_moved_grays.capacity()) {
							add_to_grays_local(local_collector,local_collector->new_moved_grays);
						}
					}
				}
			}
			break;
		default:
			assert(0);
		}
#endif // CONCURRENT
	}

#ifdef _DEBUG
    if(local_collector->ph_ofstream) {
        (*local_collector->ph_ofstream) << "node [color=black];\n";
        (*local_collector->ph_ofstream) << "edge [color=black];\n";
    }
#endif

	// ----------------------------------------------------------------------------------------------
	// Iterate over the slots into the private heap and add them to the mark stack.
	pn_info::inter_iterator inter_iter;
	Slot one_slot(NULL);

	for(inter_iter  = local_collector->inter_slots->begin();
		inter_iter != local_collector->inter_slots->end();
		++inter_iter) {
        one_slot.set(inter_iter->slot);
		Partial_Reveal_Object *pro = one_slot.dereference();
        if(!pro) continue;

        if(get_object_location(pro,local_collector) == PRIVATE_HEAP) {
   		    push_bottom_on_local_mark_stack(NULL,pro,&(local_collector->mark_stack));
        }
	}

    for(unsigned wpo_index = 0;
        wpo_index < p_global_gc->m_wpos.size();
        ++wpo_index) {
        weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];
		if(GC_BLOCK_INFO(root)->thread_owner == thread) {
   		    push_bottom_on_local_mark_stack(NULL,root,&(local_collector->mark_stack));
		}
	}

	// ----------------------------------------------------------------------------------------------
	// This will just collect slots from other objects in the private heap to objects that will be moved.
	while(1) {
		Partial_Reveal_Object *obj_to_trace;

		ObjectPath op = pop_bottom_object_from_local_mark_stack(&(local_collector->mark_stack));
		obj_to_trace = op.to;
		if(!obj_to_trace) break;  // no more objects to mark
		if(obj_to_trace->isMarked()) {
			continue;  // already marked
		}

        if(get_object_location(obj_to_trace,local_collector) != PRIVATE_HEAP) {
			printf("collect_private_heap, live object not in private heap.\n");
			assert(0);
		}

		Partial_Reveal_VTable *obj_trace_vt = obj_to_trace->vt();
		unsigned obj_size = get_object_size_bytes_with_vt(obj_to_trace,obj_trace_vt);

		PncLiveObject *cur_pnc;

		local_collector->live_objects->push_back(PncLiveObject(obj_to_trace,CAN_STAY,obj_trace_vt,op.from));
		cur_pnc = local_collector->live_objects->get_last_addr();

		obj_to_trace->mark(); // mark the object as being visited
		local_scan_one_object(obj_to_trace, obj_trace_vt, op.from, cur_pnc, *local_collector, PRIVATE_HEAP_SLOT);
	}

	// ----------------------------------------------------------------------------------------------

    local_collector->num_private_collection_objects += local_collector->live_objects->size();

	for(live_object_iter  = local_collector->live_objects->begin();
		live_object_iter != local_collector->live_objects->end();
		++live_object_iter) {
		PncLiveObject *pnc = live_object_iter.get_addr();

		Partial_Reveal_Object *p_obj      = pnc->old_location;
        p_obj->set_vtable((unsigned)pnc);
    }

	// ----------------------------------------------------------------------------------------------

	// Update the slots with the new locations of the moving objects.
	for(inter_iter  = local_collector->inter_slots->begin();
		inter_iter != local_collector->inter_slots->end();
		++inter_iter) {
        one_slot.set(inter_iter->slot);
		Partial_Reveal_Object *pro = one_slot.dereference();
        if(!pro) continue;

        if(get_object_location(pro,local_collector) == PRIVATE_HEAP) {
		    PncLiveObject *plo = (PncLiveObject*)pro->vt();
            Partial_Reveal_Object *p_new_obj = plo->new_location;
            if(p_new_obj) {
                if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
                    assert(0);
                    printf("Private heap collection slot found for new object location now in the public heap.\n");
                    exit(-1);
                }
	            one_slot.unchecked_update(p_new_obj);
            }
        }
	}

	ExpandInPlaceArray<slot_offset_entry>::iterator interior_pointer_iter;
	for(interior_pointer_iter  = local_collector->interior_pointer_table_public.begin();
		interior_pointer_iter != local_collector->interior_pointer_table_public.end();
		++interior_pointer_iter) {

		slot_offset_entry entry = interior_pointer_iter.get_current();
        void **root_slot                 = entry.slot;
        Partial_Reveal_Object *root_base = entry.base;
        POINTER_SIZE_INT root_offset     = entry.offset;
        void *new_slot_contents = (void *)((Byte*)root_base + root_offset);
        if (new_slot_contents != *root_slot) {
            *root_slot = new_slot_contents;
        }
	}

    for(unsigned wpo_index = 0;
        wpo_index < p_global_gc->m_wpos.size();
        ++wpo_index) {
        weak_pointer_object *root = p_global_gc->m_wpos[wpo_index];
		if(GC_BLOCK_INFO(root)->thread_owner == thread) {
			PncLiveObject *plo = (PncLiveObject*)root->vt();
			Partial_Reveal_Object *p_new_obj = plo->new_location;
			if(p_new_obj) {
				if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
					assert(0);
					printf("Private heap collection slot found for new object location now in the public heap.\n");
					exit(-1);
				}
				p_global_gc->m_wpos[wpo_index] = (weak_pointer_object*)p_new_obj;
			}
#if 0
			if(get_object_location(root->m_key,local_collector) == PRIVATE_HEAP) {
				PncLiveObject *plo = (PncLiveObject*)root->m_key->vt();
				Partial_Reveal_Object *p_new_obj = plo->new_location;
				if(p_new_obj) {
					if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
						assert(0);
						printf("Private heap collection slot found for new object location now in the public heap.\n");
						exit(-1);
					}
					root->m_key = p_new_obj;
				}
			}
			if(get_object_location(root->m_value,local_collector) == PRIVATE_HEAP) {
				PncLiveObject *plo = (PncLiveObject*)root->m_value->vt();
				Partial_Reveal_Object *p_new_obj = plo->new_location;
				if(p_new_obj) {
					if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
						assert(0);
						printf("Private heap collection slot found for new object location now in the public heap.\n");
						exit(-1);
					}
					root->m_value = p_new_obj;
				}
			}
			if(get_object_location(root->m_finalizer,local_collector) == PRIVATE_HEAP) {
				PncLiveObject *plo = (PncLiveObject*)root->m_finalizer->vt();
				Partial_Reveal_Object *p_new_obj = plo->new_location;
				if(p_new_obj) {
					if(get_object_location(p_new_obj,local_collector) != PUBLIC_HEAP) {
						assert(0);
						printf("Private heap collection slot found for new object location now in the public heap.\n");
						exit(-1);
					}
					root->m_finalizer = p_new_obj;
				}
			}
#endif
		}
    }

	// ----------------------------------------------------------------------------------------------

	// Move the objects from the private to the public heap.
	for(live_object_iter  = local_collector->live_objects->begin();
		live_object_iter != local_collector->live_objects->end();
		++live_object_iter) {
		PncLiveObject *pnc = live_object_iter.get_addr();

		Partial_Reveal_Object *p_obj      = pnc->old_location;
		Partial_Reveal_Object *p_dest_obj = pnc->new_location;
		p_obj->set_vtable((unsigned)pnc->vt);

        if(p_dest_obj) {
            if(get_object_location(p_dest_obj,local_collector) != PUBLIC_HEAP) {
                assert(0);
                printf("Private heap collection slot found for new object location now in the public heap.\n");
                exit(-1);
            }
    		// No need to recopy the vtable of each object.
	    	// Moreover, the source objects have their mark bits set which we don't want.
#ifdef _DEBUG
			char buf[1000];
			sprintf(buf,"Transfering %p in private heap to %p in public heap.",p_obj,p_dest_obj);
			gc_trace (p_obj, buf);
			gc_trace (p_dest_obj, buf);
#endif // _DEBUG
			unsigned obj_size = get_object_size_bytes_with_vt(p_obj,pnc->vt);
		    memcpy((char*)p_dest_obj + 4, (char*)p_obj + 4, obj_size - 4);
#ifdef _DEBUG
			memset(p_obj, 0, obj_size);
#endif // _DEBUG
        }
	}

#ifdef _DEBUG
    if(local_collector->ph_ofstream) {
        (*local_collector->ph_ofstream) << "}\n";
        local_collector->ph_ofstream->close();
    }
#endif
	// ----------------------------------------------------------------------------------------------

#endif // NEW_APPROACH

	local_undo_marks(local_collector);
	local_collector->clear();
	local_collector->gc_state = LOCAL_MARK_IDLE;

	QueryPerformanceCounter(&_end_time);
	unsigned int time = get_time_in_microseconds(_start_time, _end_time);
	local_collector->sum_private_heap_time.QuadPart += time;


#ifdef _DEBUG
	p_global_gc->reclaim_full_heap(0, false, false);
#endif // _DEBUG
} // collect_private_heap


#endif // PUB_PRIV

bool block_info::set_nursery_status(const nursery_state &expected_state, const nursery_state &new_state) {
#ifdef CONCURRENT
	if(new_state == concurrent_sweeper_nursery) {
		// We are trying to sweep a block but another thread may simultaneously be making this an active nursery.
		if(expected_state == active_nursery) return false;

		if((nursery_state)(LONG)InterlockedCompareExchange((volatile LONG *)&nursery_status, new_state, expected_state) == expected_state) {
			// We won the race so the we can sweep the block.
			return true;
		} else {
			// We lost the race so we'll create free areas next time.
			return false;
		}
	}

	// New state is not concurrent_sweeper_nursery.
	while (1) {
		nursery_state old_nursery_state = (nursery_state)(LONG)InterlockedCompareExchange((volatile LONG *)&nursery_status, new_state, expected_state);
		if (old_nursery_state == expected_state) {
			return true;
		}
		// The only race there should be is to set the nursery to active or concurrent_sweeper_nursery.
		if (old_nursery_state != concurrent_sweeper_nursery) {
			printf("Nursery state %d not equal to the expected state %d.  New state desired = %d\n",old_nursery_state,expected_state,new_state);
			assert(0);
			exit(17067);
		}
	}
#else // CONCURRENT
	// When not in concurrent mode, the state should always be what we expect.
	if(nursery_status == expected_state) {
		nursery_status = new_state;
#if 0
        if(new_state == active_nursery) {
            // We will sweep blocks only right before we start using it. This seems to have good cache benefits

		    if (!sweeps_during_gc) {
			    // Sweep the block
			    if (block_has_been_swept == false) {
                    sweep_stats stats;
				    // Determine allocation areas in the block
				    p_global_gc->sweep_one_block(this,stats);
				    block_has_been_swept = true;
			    }
		    }
        }
#endif
		return true;
	} else {
		assert(0);
		printf("Nursery state not equal to the expected stated.\n");
		exit(17067);
	}
#endif // CONCURRENT
}

extern "C" unsigned global_is_in_heap(Partial_Reveal_Object *p_obj) {
    return p_global_gc->is_in_heap(p_obj) ? 1 : 0;
}

#if defined ORP_POSIX && defined __GNUC__
#ifdef __x86_64__
PVOID InterlockedCompareExchangePosix(IN OUT PVOID *Destination,
				 IN PVOID Exchange,
				 IN PVOID Comperand
				 ) {
    PVOID old;

    __asm__ __volatile__ ("lock; cmpxchgq %2, %0"
    : "=m" (*Destination), "=a" (old)
    : "r" (Exchange), "m" (*Destination), "a" (Comperand));
    return(old);
}
#else // __x86_64__
PVOID InterlockedCompareExchangePosix(IN OUT PVOID *Destination,
				 IN PVOID Exchange,
				 IN PVOID Comperand
				 ) {
    PVOID old;

    __asm__ __volatile__ ("lock; cmpxchgl %2, %0"
    : "=m" (*Destination), "=a" (old)
    : "r" (Exchange), "m" (*Destination), "a" (Comperand));
    return(old);
}
#endif // __x86_64__

#ifdef __x86_64__
LONG InterlockedExchangeAddPosix(
				IN OUT PVOID Addend,
				IN LONG Value
				) {
long ret;
__asm__ (
/* lock for SMP systems */
"lock\n\t"
"xaddq %0,(%1)"
:"=r" (ret)
:"r" (Addend), "0" (Value)
:"memory" );
return ret;
}
#else  // __x86_64__
LONG InterlockedExchangeAddPosix(
				IN OUT PVOID Addend,
				IN LONG Value
				) {
long ret;
__asm__ (
/* lock for SMP systems */
"lock\n\t"
"xaddl %0,(%1)"
:"=r" (ret)
:"r" (Addend), "0" (Value)
:"memory" );
return ret;
}
#endif // __x86_64__
#endif

bool is_young_gen(Partial_Reveal_Object *p_obj) {
    if(global_is_in_heap(p_obj)) {
        return GC_BLOCK_INFO(p_obj)->generation == 0;
    } else {
        return false;
    }
}
