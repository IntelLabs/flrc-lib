/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _garbage_collector_H_
#define _garbage_collector_H_

#include "tgc/hash_table.h"
#include "tgc/object_list.h"
#include "tgc/remembered_set.h"
#include "tgc/block_store.h"
#include "tgc/work_packet_manager.h"
#include "tgc/gc_v4.h"
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <list>
#include <set>
#include <stack>
#include <vector>
#else
#include <..\stlport\list>
#include <..\stlport\set>
#include <..\stlport\stack>
#include <..\stlport\vector>
#endif
#include <stdint.h>

/* Taken from recent versions of stdint.h */
#ifndef INTPTR_MAX
# if __WORDSIZE == 64
#  define INTPTR_MAX        (9223372036854775807L)
# else
#  define INTPTR_MAX        (2147483647)
# endif
#endif

class GC_Thread;

struct MyPair {
    void **rootAddr;
    int offset;
    MyPair(void **a, int o) : rootAddr(a), offset(o) {}
};

extern unsigned int _machine_page_size_bytes;

class weak_pointer_object : public Partial_Reveal_Object {
public:
    // the vtable comes from Partial_Reveal_Object
    Partial_Reveal_Object *m_key;
    Partial_Reveal_Object *m_value;
    Partial_Reveal_Object *m_finalizer;
};

class sweep_stats {
public:
    unsigned amount_recovered;
    unsigned amount_in_fragments;
    unsigned blocks_swept;
    unsigned num_fragments;

    sweep_stats() : amount_recovered(0), amount_in_fragments(0), blocks_swept(0), num_fragments(0) {}

    void reset(void) {
        amount_recovered    = 0;
        amount_in_fragments = 0;
        blocks_swept        = 0;
        num_fragments       = 0;
    }
};

class PN_range {
public:
    void *start, *end;
    PN_range(void *s, void *e) : start(s), end(e) {}
    bool operator<(const PN_range &other) {
        return start < other.start;
    }
    bool in_range(void *addr) {
        if(addr >= start && addr < end) {
            return true;
        } else {
            return false;
        }
    }
};

class PN_ranges : std::vector<PN_range> {
protected:
    bool m_sorted;
    void *min_pn_addr, *max_pn_addr;
public:
    PN_ranges(void) : m_sorted(false) {
        min_pn_addr = (void*)INTPTR_MAX;
        max_pn_addr = 0;
    }

    void clear(void) {
        m_sorted    = false;
        min_pn_addr = (void*)INTPTR_MAX;
        max_pn_addr = 0;
    }

    void add(void *start, void *end) {
        m_sorted = false;
        if(start < min_pn_addr) {
            min_pn_addr = start;
        }
        if(end > max_pn_addr) {
            max_pn_addr = end;
        }
        push_back(PN_range(start,end));
    }

    bool is_in_range(void *addr) {
        if(addr < min_pn_addr || addr >= max_pn_addr) return false;

        unsigned cur_size = size();
        unsigned i;
        for(i = 0; i < cur_size; ++i) {
            if(this->operator[](i).in_range(addr)) {
                return true;
            }
        }
        return false;
    }
};

class cheney_space {
public:
    void *begin, *end;
    cheney_space(void *a) : begin(a), end(a) {}
    void forbid_compaction_here(Block_Store *block_store);
    void mark_objects_in_block(void);
	void mark_phase_process(GC_Thread *gc_thread);
};

class cheney_space_pair {
public:
	volatile POINTER_SIZE_INT marked;
    cheney_space pub_space;
    cheney_space im_space;
	Partial_Reveal_Object *pub_cheney_ptr, *im_cheney_ptr;

    cheney_space_pair(void *pub_start, void *imm_start, void *pub_end = NULL, void *imm_end = NULL) :
		marked(0),
        pub_space(pub_start),
        im_space(imm_start),
		pub_cheney_ptr((Partial_Reveal_Object*)pub_start),
		im_cheney_ptr((Partial_Reveal_Object*)imm_start) {
        if(pub_end) {
            pub_space.end = pub_end;
        }
        if(imm_end) {
            im_space.end  = imm_end;
        }
    }

    bool process(void *env);  // returns true if it processed at least one object
    void global_gc_get_pn_lives(void *env);
    void forbid_compaction_here(Block_Store *block_store);
    void mark_objects_in_block(void) {
        pub_space.mark_objects_in_block();
        im_space.mark_objects_in_block();
    }
	void mark_phase_process(GC_Thread *gc_thread);
};

class pn_space;
class PN_cheney_info;

class cheney_spaces : public ExpandInPlaceArray<cheney_space_pair> {
protected:
	pn_space *target_two_space;
public:
	cheney_spaces(void) : target_two_space(NULL) {}

	void mark_phase_process(GC_Thread *gc_thread);

	pn_space *get_target_two_space(void) {
		return target_two_space;
	}
	void set_target_two_space(pn_space *pns) {
		target_two_space = pns;
	}

	void process(PN_cheney_info *pci);
};


class Garbage_Collector {
public:
	Garbage_Collector(POINTER_SIZE_INT, POINTER_SIZE_INT, unsigned int);
    virtual ~Garbage_Collector();

	void gc_v4_init();
	void gc_add_fresh_chunks(unsigned int);

	block_info *p_cycle_chunk(block_info *p_used_chunk,
							  bool returnNullOnFail,
							  bool immutable,
							  void *new_thread_owner /* NULL if a public block */,
							  GC_Thread_Info *current_thread);
	void uncleared_to_free_block(block_info *block);

	void reclaim_full_heap(unsigned int, bool, bool);
#ifdef CONCURRENT
	void reclamation_func(void);
	void fake_func(void);
	// returns the estimated free space
	void reclaim_full_heap_concurrent(GC_Nursery_Info *copy_to);
	unsigned estimate_free_heap_space(void);
	void process_weak_roots_concurrent(bool process_short_roots);
	int num_threads_remaining_until_next_phase;
	void wait_for_marks_concurrent(void);
	void wait_for_sweep_or_idle_concurrent(CONCURRENT_GC_STATE new_gc_state);
#endif // CONCURRENT

	void gc_internal_add_root_set_entry(Partial_Reveal_Object **);
    // TAA, 3/3/2004...adding weak root support
    void gc_internal_add_weak_root_set_entry(Partial_Reveal_Object **,int offset, Boolean is_short_weak);

    void gc_internal_block_contains_pinned_root(block_info *p_block_info) {
        orp_synch_enter_critical_section(protect_pinned_blocks);
        // Add this block to the set of blocks that are pinned for this GC.
        m_pinned_blocks.insert(p_block_info);
        orp_synch_leave_critical_section(protect_pinned_blocks);
    }

	Partial_Reveal_Object **get_fresh_root_to_trace();

	inline unsigned int get_gc_num() {
		return _gc_num;
	}

/////////////////////////////////////////////////////////////
	// NOT DONE!!!!
	// NEED TO HIDE THE LOS from allocation.cpp -- need to make it private with an interface
	volatile unsigned int _los_lock;
	// LOS
	block_info *_los_blocks;
	block_info *get_new_los_block();
/////////////////////////////////////////////////////////////

	volatile unsigned int _wpo_lock;
	std::vector<weak_pointer_object*> m_wpos;

	Partial_Reveal_Object *create_single_object_blocks(unsigned, Allocation_Handle);

	inline void coalesce_free_blocks() {
		_p_block_store->coalesce_free_blocks();
	}

/////////////////////////////////////////////////////////////

	unsigned int sweep_one_block(block_info *, sweep_stats &);
#ifdef CONCURRENT
	unsigned int sweep_one_block_concurrent(block_info *, GC_Nursery_Info *copy_to);
    bool clear_and_unmark(block_info *block,bool check_only);
#endif // CONCURRENT

/////////////////////////////////////////////////////////////

	unsigned int sweep_heap(GC_Thread *, sweep_stats &);

/////////////////////////////////////////////////////////////////////////////////////////////////////

	void setup_mark_scan_pools();

	// A particular GC thread wants to do mark/scan work....how can the GC assist it?!
	void mark_scan_pools(GC_Thread *);

	void scan_one_slot (Slot, GC_Thread *);

	inline bool wait_till_there_is_work_or_no_work() {
		return _mark_scan_pool->wait_till_there_is_work_or_no_work();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////

	inline bool is_compaction_block(block_info *block) {
		return _p_block_store->is_compaction_block(block);
	}

	inline void add_slot_to_compaction_block(Slot p_slot, block_info *p_obj_block, unsigned int gc_thread_id) {
		_p_block_store->add_slot_to_compaction_block(p_slot, p_obj_block, gc_thread_id);
	}

	inline block_info *get_block_for_sliding_compaction_allocation_pointer_computation(unsigned int id, block_info *curr) {
		return _p_block_store->get_block_for_sliding_compaction_allocation_pointer_computation(id, curr);
	}

	inline block_info *get_block_for_fix_slots_to_compaction_live_objects(unsigned int id, block_info *curr) {
		return _p_block_store->get_block_for_fix_slots_to_compaction_live_objects(id, curr);
	}

	inline block_info *get_block_for_slide_compact_live_objects(unsigned int id, block_info *curr) {
		return _p_block_store->get_block_for_slide_compact_live_objects(id, curr);
	}

	inline Remembered_Set *get_slots_into_compaction_block(block_info *block) {
		return _p_block_store->get_slots_into_compaction_block(block);
	}

#ifndef GC_NO_FUSE
    // Get a chunk and add it to _gc_chunks
	inline block_info *get_chunk_for_object_placement_destination() {
        unsigned int empty_chunk_index = 0;
		assert(empty_chunk_index < GC_MAX_CHUNKS);

		block_info *chunk = get_fresh_chunk_from_block_store(false); // Use blocks below the waterline.
        if (!chunk) {
            return NULL;
        }
		while (_gc_chunks[empty_chunk_index].chunk != NULL) {
            assert (_gc_chunks[empty_chunk_index].chunk != chunk);
			empty_chunk_index++;
		}
		while ( InterlockedCompareExchangePointer((void **)&(_gc_chunks[empty_chunk_index].chunk), chunk, NULL) != NULL ) {
            assert (_gc_chunks[empty_chunk_index].chunk != chunk);
            // This entry was taken before we could get to it try the next entry.
            empty_chunk_index++;
        }
        assert(_gc_chunks[empty_chunk_index].free_chunk == NULL);
        _gc_chunks[empty_chunk_index].free_chunk = chunk;
		assert(_gc_chunks[empty_chunk_index].chunk == chunk);
		return chunk; // Get a block not for the chunk store
	}

#endif

	inline void init_live_object_iterator_for_block(block_info *block) {
		_p_block_store->init_live_object_iterator_for_block(block);
	}
	inline Partial_Reveal_Object *get_next_live_object_in_block(block_info *block) {
		return _p_block_store->get_next_live_object_in_block(block);
	}

	inline unsigned int get_total_live_objects_in_this_block(block_info *block)	{
		return _p_block_store->get_total_live_objects_in_this_block(block);
	}

	inline block_info *iter_get_next_compaction_block_for_gc_thread(unsigned int thread_id, block_info *curr_block, void *owner, bool search_for_owner=false) {
		return _p_block_store->iter_get_next_compaction_block_for_gc_thread(thread_id, curr_block, owner, search_for_owner);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////
	inline void *get_gc_heap_base_address() {
		return _p_block_store->get_gc_heap_base_address();
	}

	inline void *get_gc_heap_ceiling_address() {
		return _p_block_store->get_gc_heap_ceiling_address();
	}

    inline bool is_in_heap(void *addr) {
        return _p_block_store->is_in_heap(addr);
    }

    inline bool is_in_private_nursery(void *addr) {
        return pn_ranges.is_in_range(addr);
    }

	bool obj_belongs_in_single_object_blocks(Partial_Reveal_Object *);

	bool block_is_invalid_or_free_in_block_store(block_info *block) {
		return _p_block_store->block_is_invalid_or_free_in_block_store(block);
	}

	// Indicate that this object will need to be finalized when it is no longer
	// reachable.
    void add_finalize_object(Partial_Reveal_Object *p_object,bool check_for_duplicates=false);
    // Indicates that this object should not be finalized when it goes out of scope
    // even though it has a finalizer...part of the CLI spec.
    void remove_finalize_object(Partial_Reveal_Object *p_object);

    void lock_finalize_list(void) {
        // take the finalize list lock
        while ((LONG) InterlockedCompareExchange( (LONG *)(&m_finalize_list_lock), (LONG) 1, (LONG) 0) == (LONG) 1);
    }

    void unlock_finalize_list(void) {
        // release the finalize list lock
        m_finalize_list_lock = 0;
    }

    void add_finalize_object_prelocked(Partial_Reveal_Object *p_object,bool check_for_duplicates) {
        std::list<Partial_Reveal_Object*>::iterator finalizer_iter;

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
    }

    //
    // Starting with a pointer in a group of block holding a single object
    // fumble back through the blocks until you find the one that starts
    // this group of blocks.
    //
    block_info * find_start_of_multiblock(void *pointer_somewhere_in_multiblock) {
        // find where the start of the block would be
        block_info *pBlockInfo = GC_BLOCK_INFO(pointer_somewhere_in_multiblock);
        // keep going backwards from block to block until you find one that starts the multiblock
        while (pBlockInfo->block_store_info_index >= GC_MAX_BLOCKS ||
               pBlockInfo != _p_block_store->get_block_info(pBlockInfo->block_store_info_index))
        {
            // any subtraction will do that puts us in the previous block
            pBlockInfo = GC_BLOCK_INFO(pBlockInfo - 1);
            assert(pBlockInfo >= _p_block_store->get_gc_heap_base_address());
        }
        return pBlockInfo;
    }

    // Given a GC one can iterate throught the blocks using this interface.
    // Initialize the iterator
    void init_block_iterator () {
        _p_block_store->init_block_iterator();
    }

    // Get the next block if one is available
    // otherwise return NULL to indicate the end of the blocks.
    block_info *get_next_block () {
        return _p_block_store->get_next_block();
    }
/////////////////////////////////////////////////////////////////////////////////////////////////////

	void parallel_clear_mark_bit_vectors(unsigned thread_id);
    void guarantee_multi_block_allocation(unsigned size,GC_Thread_Info *tls_for_gc);
	unsigned int return_free_blocks_to_block_store(int, bool, bool, bool has_block_store_lock=false);
	unsigned int return_free_blocks_to_block_store_prob(float, bool, bool, bool has_block_store_lock=false);

private:

	void reset_thread_nurseries(void);
    void reclaim_full_heap_from_gc_thread(unsigned int size_failed, bool force_gc, bool for_los);
	block_info *get_fresh_chunk_from_block_store(bool stay_above_waterline);

	block_info *get_free_chunk_from_global_gc_chunks(void *new_thread_owner, bool desired_chunk_mutability=false);

	unsigned int return_free_blocks_concurrent(void);

	void _get_orp_live_references();
	void resume_orp();
	void init_gc_threads();
	void reset_gc_threads(bool);
	unsigned int sweep_single_object_blocks(block_info *, sweep_stats &stats);
	void prepare_to_sweep_heap();
	void prepare_chunks_for_sweeps_during_allocation(bool);
	void prepare_root_containers();
	void roots_init();
	void get_gc_threads_to_begin_task(gc_thread_action);
	void wait_for_gc_threads_to_complete_assigned_task();
	void clear_all_mark_bit_vectors_of_unswept_blocks();

	/////////////////////////////////// N O N - C O N C U R R E N T  //////////////////////////////////////

    void enumerate_thread(volatile GC_Thread_Info *stopped_thread);
    void gc_enumerate_root_set_all_threads();
    void stop_the_world();
    void enumerate_the_world();
    void resume_the_world();

	/////////////////////////////////// C O M P A C T I O N  //////////////////////////////////////////////

	void repoint_all_roots_into_compacted_areas();
	void repoint_all_roots_with_offset_into_compacted_areas();

	//////////////////////////////// V E R I F I C A T I O N //////////////////////////////////////////////

	void verify_marks_for_all_lives();


	// DEBUG TRACING ...
	// Trace and verify the entire heap
	unsigned int trace_verify_heap( bool );
	// Trace and verify all objects reachable from this object
//	void trace_verify_sub_heap(Partial_Reveal_Object *, bool);
	void trace_verify_sub_heap(std::stack<Partial_Reveal_Object *,std::vector<Partial_Reveal_Object*> > &obj_stack, bool before_gc);

    /////////////////////////// W E A K   P O I N T E R S /////////////////////////////////////////////////

    // TAA, 3/3/2004, weak root support
    // The bool parameter is true if you want to process short weak roots, false for long weak roots.
	// compactions_this_gc is true if the GC will be compacting.
    void process_weak_roots(bool process_short_roots, bool compactions_this_gc);

    /////////////////////////// F I N A L I Z A T I O N ///////////////////////////////////////////////////

    // TAA, 3/4/2004
    void identify_and_mark_objects_moving_to_finalizable_queue(bool);
    void add_objects_to_finalizable_queue();

    void expand_root_arrays();

	///////////////////////////////////////////////////////////////////////////////////////////////////////

	void _verify_gc_threads_state();

	///////////////////////////////////////////////////////////////////////////////////////////////////////

public:
	Block_Store *_p_block_store;
    cheney_spaces this_gc_cheney_spaces;
private:

	// CHUNKS
	chunk_info _gc_chunks[GC_MAX_CHUNKS];

	// CHUNKS limit marker
	volatile int _free_chunks_end_index;

	// SOB -- Single Object Blocks
	block_info *_single_object_blocks;

	// GC THREADS
	GC_Thread **_gc_threads;

    unsigned num_root_limit;
	Partial_Reveal_Object ***_array_of_roots;
//	Partial_Reveal_Object **_array_of_roots[GC_MAX_ROOTS];

	unsigned int _num_roots;

	Partial_Reveal_Object ***_save_array_of_roots;
//	Partial_Reveal_Object **_save_array_of_roots[GC_MAX_ROOTS];

    // list of short weak roots
	std::list<MyPair> m_short_weak_roots;
    // list of long weak roots
	std::list<MyPair> m_long_weak_roots;

    // list of objects that will eventually need to be finalized
	std::list<Partial_Reveal_Object*> *m_listFinalize;
    // Lock to protect m_listFinalize
    volatile unsigned int m_finalize_list_lock;
    // list of objects that have been identified for finalization but not added to the finalizable queue yet.
	std::list<Partial_Reveal_Object*> m_unmarked_objects;

    // set of blocks that contain at least one pinned root
    std::set<block_info *> m_pinned_blocks;

	SynchEventHandle *_gc_thread_work_finished_event_handles;

    Hash_Table *dup_removal_enum_hash_table;

	Work_Packet_Manager *_mark_scan_pool;

    float last_young_gen_percentage;

	// Keeping track of time in the GC
	unsigned int _gc_num;
	unsigned int _total_gc_time;
	unsigned int _gc_num_time;

    PN_ranges pn_ranges;

#ifdef _WINDOWS
	LARGE_INTEGER _start_time, _end_time;
	LARGE_INTEGER _gc_start_time, _gc_end_time;
#elif defined LINUX
	struct timeval _start_time, _end_time;
	struct timeval _gc_start_time, _gc_end_time;
#endif // _WINDOWS

////////////////////////////////////////////////////////////////////////////
	unsigned int _num_live_objects_found_by_first_trace_heap;
	Object_List *_live_objects_found_by_first_trace_heap;
	unsigned int _num_live_objects_found_by_second_trace_heap;
	Object_List *_live_objects_found_by_second_trace_heap;
	Partial_Reveal_Object ***_verify_array_of_roots;
////////////////////////////////////////////////////////////////////////////

    SynchCriticalSectionHandle protect_roots;
    SynchCriticalSectionHandle protect_short_weak_roots;
    SynchCriticalSectionHandle protect_long_weak_roots;
    SynchCriticalSectionHandle protect_pinned_blocks;
}; // Garbage_Collector

extern Garbage_Collector *p_global_gc;
extern bool g_maximum_debug;

extern "C" int32 vector_get_length_with_vt(void *,void *);
extern "C" void *vector_get_element_address_ref_with_vt(void*,int32,void*);

#ifdef CONCURRENT
void add_to_grays_concurrent(std::vector<Partial_Reveal_Object*> &gray_list);
void make_object_gray_local(pn_info *local_collector,Partial_Reveal_Object *obj);
void add_to_grays_local(pn_info *local_collector,Partial_Reveal_Object *obj);
void make_object_gray_in_concurrent_thread(Partial_Reveal_Object *obj, std::deque<Partial_Reveal_Object *> &gray_list);
extern std::deque<Partial_Reveal_Object*> g_concurrent_gray_list;
#endif // CONCURRENT

extern Partial_Reveal_VTable *wpo_vtable;
#ifdef _DEBUG
extern unsigned indirections_removed;
#endif

typedef enum {
    RI_NOTHING = 0,
    RI_REPLACE_OBJ = 1,
    RI_REPLACE_NON_OBJ = 2
} REMOVE_INDIR_RES;

#if 0
// Returns true if we replaced an indirection with a tagged integer, i.e., a non-pointer.
#ifdef _MSC_VER
__forceinline
#else
inline
#endif
REMOVE_INDIR_RES remove_indirections(Partial_Reveal_Object *&p_obj, Slot p_slot, unsigned rmindir_mask) {
#ifdef _DEBUG
#if 0
    if(!(g_remove_indirections & (1 << rmindir_mask))) {
		return RI_NOTHING;
	}
#else
	if(g_remove_indirections) {
		--g_remove_indirections;
	}
#endif
#endif
    REMOVE_INDIR_RES ret = RI_NOTHING;
    if(g_remove_indirections && is_object_pointer(p_obj)) {
        // A => B1 => ... => Bn => C.  A is a regular object.  B1...Bn are indirection objects.  C is a regular object.
        while(1) {
            // Get the vtable of a possible B.
            struct Partial_Reveal_VTable *indir_vtable = p_obj->vt();
            // See if that vtable is an indirection vtable.
            if(pgc_is_indirection(indir_vtable)) {
                // Compute the address of the indirection slot within the indirection object.
                // B + indirection_offset
                Partial_Reveal_Object ** new_value = (Partial_Reveal_Object**)((char*)p_obj + pgc_indirection_offset(indir_vtable));
                // Scan C as a live object instead of B.
                p_obj = *new_value;
                // Update the slot in A to point to C.
                p_slot.unchecked_update(p_obj);

#ifdef _DEBUG
                ++indirections_removed;
#endif
                // If the new slot value isn't a pointer then return true.
                if(!is_object_pointer(p_obj)) {
                    ret = RI_REPLACE_NON_OBJ;
                    return ret;
                }
                ret = RI_REPLACE_OBJ;
            } else {
                return ret;
            }
        }
    }
    return ret;
}
#endif

#ifdef _MSC_VER
__forceinline
#else
inline
#endif
REMOVE_INDIR_RES remove_one_indirection(Partial_Reveal_Object *&p_obj, Slot p_slot, unsigned rmindir_mask) {
#ifdef _DEBUG
#if 0
    if(!(g_remove_indirections & (1 << rmindir_mask))) {
		return RI_NOTHING;
	}
#else
	if(g_remove_indirections) {
		--g_remove_indirections;
	}
#endif
#endif
    REMOVE_INDIR_RES ret = RI_NOTHING;
    if(g_remove_indirections && is_object_pointer(p_obj)) {
        // A => B1 => ... => Bn => C.  A is a regular object.  B1...Bn are indirection objects.  C is a regular object.
        // Get the vtable of a possible B.
        struct Partial_Reveal_VTable *indir_vtable = p_obj->vt();
        // See if that vtable is an indirection vtable.
        if(pgc_is_indirection(indir_vtable)) {
            // Compute the address of the indirection slot within the indirection object.
            // B + indirection_offset
            Partial_Reveal_Object ** new_value = (Partial_Reveal_Object**)((char*)p_obj + pgc_indirection_offset(indir_vtable));
            // Scan C as a live object instead of B.
            p_obj = *new_value;
            // Update the slot in A to point to C.
            p_slot.unchecked_update(p_obj);

#ifdef _DEBUG
            ++indirections_removed;
#endif
            // If the new slot value isn't a pointer then return true.
            if(!is_object_pointer(p_obj)) {
                ret = RI_REPLACE_NON_OBJ;
                return ret;
            }
            ret = RI_REPLACE_OBJ;
        } else {
            return ret;
        }
    }
    return ret;
}

inline bool alignment_vt(Partial_Reveal_VTable *vt) {
   	if((uintptr_t)vt < 1<<16) {
        return true;
	} else {
        return false;
    }
}

#endif // _garbage_collector_H_
