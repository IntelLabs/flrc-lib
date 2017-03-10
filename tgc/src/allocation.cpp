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
#define GC_FAST_ALLOC

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
#include "tgc/compressed_references.h"
#include "tgc/micro_nursery.h"

#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <set>
#else
#include <..\stlport\set>
#endif
#include "pgc/pgc.h"

///////////////////////////////////////////////////////////////////////////////////

static int bytes_allocated = 0;
int num_los_objects = 0;
int num_large_objects = 0;

extern bool sweeps_during_gc;

bool use_finalization = true;
bool do_not_zero = false;

//unsigned local_nursery_size = 0; // now in c_export.c
extern bool separate_immutable;
extern bool g_treat_wpo_as_normal;

///////////////////////////////////////////////////////////////////////////////////

#ifdef CONCURRENT
#include "tgc/mark.h"
#endif // CONCURRENT

static inline bool gc_has_begun() {
    return active_gc_thread != 0;
}

static inline void *get_thread_curr_alloc_block(void *tp) {
    GC_Thread_Info *info = (struct GC_Thread_Info *)tp;
    return info->get_nursery()->curr_alloc_block;
}

static inline void *get_thread_alloc_chunk(void *tp) {
    GC_Thread_Info *info = (struct GC_Thread_Info *)tp;
    return info->get_nursery()->chunk;
}

static inline void set_thread_curr_alloc_block(void *tp, void *data) {
    GC_Thread_Info *info = (struct GC_Thread_Info *)tp;
    info->get_nursery()->curr_alloc_block = data;
}

static inline void set_thread_alloc_chunk(void *tp, void *data) {
    GC_Thread_Info *info = (struct GC_Thread_Info *)tp;
    info->get_nursery()->chunk = data;
}

///////////////////////////////////////////////////////////////////////////////////

unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt);

///////////////////////////////////////////////////////////////////////////////////


int obj_num = 0;


inline Partial_Reveal_Object *
fast_gc_malloc_from_thread_chunk_or_null_with_nursery(GC_Nursery_Info *gc_nursery,
													  unsigned size,
													  Allocation_Handle ah) {
    Partial_Reveal_Object *frontier = (Partial_Reveal_Object *)gc_nursery->tls_current_free;
	adjust_frontier_to_alignment(frontier, (Partial_Reveal_VTable*)ah);
    POINTER_SIZE_INT new_free = (size + (uintptr_t)frontier);
    // check that the old way and the new way are producing
    // the same pointers. This can go once we have it in place.

    if (new_free <= (uintptr_t) gc_nursery->tls_current_ceiling) {
        // success...

        // Skip for now so this logic is not visible...
        // stick the vtable in
        //frontier->vt = (Partial_Reveal_VTable *)p_vtable;
        frontier->set_vtable(ah);

        // increment free ptr and return object
        gc_nursery->tls_current_free = (void *) new_free;
        // Heap characterization is done only in the GCEXPORT routine
        gc_trace_allocation(frontier, "Allocated in fast_gc_malloc_from_thread_chunk_or_null");

        if((Partial_Reveal_VTable*)ah == wpo_vtable && !g_treat_wpo_as_normal) {
            if(!local_nursery_size) {
                while ((LONG) InterlockedCompareExchange( (LONG *)(&p_global_gc->_wpo_lock), (LONG) 1, (LONG) 0) == (LONG) 1);
                p_global_gc->m_wpos.push_back((weak_pointer_object*)frontier);
                p_global_gc->_wpo_lock = 0;
//                ls->m_wpos.push_back((weak_pointer_object*)frontier);
            } else {
//                printf("WPO vtable allocation in fast_gc_malloc_from_thread_chunk_or_null_with_nursery while using private nurseries.\n");
            }
        }

        if(use_finalization && class_is_finalizable(allocation_handle_get_class(ah))) {
            p_global_gc->add_finalize_object(frontier);
        }

        return frontier;
    } else {
        return NULL;
    }
} // fast_gc_malloc_from_thread_chunk_or_null_with_nursery

inline Partial_Reveal_Object * gc_malloc_from_thread_chunk_or_null_with_nursery(GC_Nursery_Info *gc_nursery, unsigned size, Allocation_Handle ah) {
    // Try to allocate an object from the current alloc block.
    assert(size);
    assert (ah);

#ifndef GC_FAST_ALLOC
    // If the object size is less than the minimum alloc area just return NULL, If we have a free area we
    // want to know that we can allocate any object that makes it past here in it.....
   	if (size > GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
    // PINNED????
        return NULL;
    }
#endif
    // Get the gc tls structure. Review of what each piece of the structure is used for.
    // chunk - This holds a list of blocks linked throuh the  next_free_block fields
    // Each block is an alloc_block and it has various areas where allocation can happen.
    // The block might not have been swept which means that the allocation areas have
    // not be initialized.
    // The tls holds the tls_current_free and the tls_current_ceiling which are used
    // to allocate within a given allocation area within a given allocation block within
    // a chunk.

    // The tls ceiling and free are set to zero to zero and all the blocks in the
    // allocation chunk are either swept by the GC or have their block_has_been_swept
    // set to false by the GC.

    // The fast case just grabs the tls free pointer and increments it by size. If it is
    // less than ceiling then the object fits and we install the vtable.

    // If the object does not fit, then we go the the tls and grab the alloc_block and
    // see if it has another area available. If it doesn't we move on to the next block in the
    // chunk. If we run our of blocks in the chunk we return NULL;

    // The corner case is what happens right after a GC. Since free and ceiling are set to 0
    // the fast case will add size to 0 and compare against a 0 for ceiling and fall through.
    // The GC will have reset alloc block to the first block in the chunk and that is where
    // we will look for a new area to allocate in.

    // Someday this will be part of the interface.

    Partial_Reveal_Object *p_return_object = NULL;

    p_return_object = fast_gc_malloc_from_thread_chunk_or_null_with_nursery(gc_nursery, size, ah);
    if (p_return_object) {
        // fast case was successful.
        // Heap characterization is done only in the GCEXPORT routine
        gc_trace_allocation(p_return_object, "Returned from gc_malloc_from_thread_chunk_or_null");
        return p_return_object;
    }

    // We need a new allocation area. Get the current alloc_block
    block_info *alloc_block = (block_info *)gc_nursery->curr_alloc_block;

    // If the object size is less than the minimum alloc area just return NULL, If we have a free area we
    // want to know that we can allocate any object that makes it past here in it.....
   	if (size > GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
        // PINNED????
        return NULL;
    }

    // Loop through the alloc blocks to see if we can find another allocation area.
    while (alloc_block) {
        if(g_gen) {
            assert(alloc_block->num_free_areas_in_block <= 1);
        }
        // We will sweep blocks only right before we start using it. This seems to have good cache benefits

		if (!sweeps_during_gc) {
			// Sweep the block
			if (alloc_block->block_has_been_swept == false) {
                sweep_stats stats;
				// Determine allocation areas in the block
				p_global_gc->sweep_one_block(alloc_block,stats);
				alloc_block->block_has_been_swept = true;
			}
		}

#ifdef CONCURRENT
        if(alloc_block->get_nursery_status() == concurrent_sweeper_nursery) {
            printf("Found block being swept while looking for free allocation area in gc_malloc_from_thread_chunk_or_null_with_nursery.\n");
        }
#endif // CONCURRENT

        // current_alloc_area will be -1 if the first area has not been used and if it exists is available.
        if ( (alloc_block->num_free_areas_in_block == 0) || ((alloc_block->current_alloc_area + 1) == alloc_block->num_free_areas_in_block) ){
            // No areas left in this block get the next one.
            alloc_block = alloc_block->next_free_block; // Get the next block and loop
            if(g_gen && alloc_block) {
                assert(alloc_block->num_free_areas_in_block <= 1);
                assert(alloc_block->is_empty());
            }
        } else {
            assert (alloc_block->current_alloc_area < alloc_block->num_free_areas_in_block);
            break; // This block has been swept and has an untouched alloc block.
        }
    } // end while (alloc_block)

    if (alloc_block == NULL) {
        // ran through the end of the list of blocks in the chunk
        gc_nursery->tls_current_ceiling = NULL;
        gc_nursery->tls_current_free = NULL;
        gc_nursery->curr_alloc_block = NULL; // Indicating that we are at the end of the alloc blocks for this chunk.
        // This is the last place we can return NULL from.
        return NULL;
    }

    assert(alloc_block->num_free_areas_in_block > 0);
    assert(alloc_block->block_has_been_swept);
    assert(alloc_block->current_alloc_area < alloc_block->num_free_areas_in_block);

    // We have a block that has been swept and has at least one allocation area big enough to fit this object so we will be
    // successful..

    alloc_block->current_alloc_area++; // Get the next current area. If it is the first one it will be 0.

    //XXX assert part of GC_SLOW_ALLOC routines.
    assert (alloc_block->current_alloc_area != -1);
    unsigned int curr_area = alloc_block->current_alloc_area;


    if (alloc_block->block_free_areas[curr_area].has_been_zeroed == false) {
        gc_trace_block(alloc_block, " Clearing the curr_area in this block.");
		if(!do_not_zero) {
	        // CLEAR allocation areas just before you start using them
		    memset(alloc_block->block_free_areas[curr_area].area_base, 0, alloc_block->block_free_areas[curr_area].area_size);
		}
        alloc_block->block_free_areas[curr_area].has_been_zeroed = true;
    }

    gc_nursery->tls_current_free = alloc_block->block_free_areas[curr_area].area_base;
    // JMS 2003-05-23.  If references are compressed, then the heap base should never be
    // given out as a valid object pointer, since it is being used as the
    // representation for a managed null.
    assert(gc_nursery->tls_current_free != Slot::managed_null());
#ifdef CONCURRENT
    if(alloc_block->get_nursery_status() == concurrent_sweeper_nursery) {
        printf("Found block being swept while looking for free allocation area in gc_malloc_from_thread_chunk_or_null_with_nursery.\n");
        assert(0);
        exit(-1);
    }
#endif // CONCURRENT
    gc_nursery->tls_current_ceiling = alloc_block->block_free_areas[curr_area].area_ceiling;
    gc_nursery->curr_alloc_block = alloc_block;

    p_return_object = fast_gc_malloc_from_thread_chunk_or_null_with_nursery(gc_nursery,size, ah);
    //    assert (p_return_object); // We can still fail to find an object even after all of this.

    // Heap characterization is done only in the GCEXPORT routine
    gc_trace_allocation(p_return_object, "Returned from gc_malloc_from_thread_chunk_or_null at bottom");

    return p_return_object;
} // gc_malloc_from_thread_chunk_or_null_with_nursery

void local_nursery_collection(GC_Thread_Info *tls_for_gc,struct PrtStackIterator *si,Partial_Reveal_Object *escaping_object,bool is_future);

unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt) {
	struct AlignmentInfo alignment = pgc_get_vtable_alignment((struct VTable*)vt);

#if 0
	if(alignment.alignArray) {
		if(is_vt_array(vt)) {
	        int first_elem_offset = pgc_get_array_offset((struct VTable*)vt);
			POINTER_SIZE_INT new_array_portion_loc = (POINTER_SIZE_INT)frontier + first_elem_offset;
	        new_array_portion_loc = ((new_array_portion_loc + ((1 << (alignment.powerOfTwoBaseFour+2)) - 1)) >> (alignment.powerOfTwoBaseFour+2) << (alignment.powerOfTwoBaseFour+2));
			frontier = (Partial_Reveal_Object *)(new_array_portion_loc - first_elem_offset);
		} else {
			// intentionally do nothing.  nothing to be done if array alignment requested on non-array object.
			return;
		}
	} else {
        frontier = (Partial_Reveal_Object *)(((POINTER_SIZE_INT)frontier + ((1 << (alignment.powerOfTwoBaseFour+2)) - 1)) >> (alignment.powerOfTwoBaseFour+2) << (alignment.powerOfTwoBaseFour+2));
	}
#else
	// first_elem_offset is 0 if this is not array alignment which is fine for base alignment.
	int first_elem_offset = (!!(unsigned)alignment.alignArray) * pgc_get_array_offset((struct VTable*)vt);
	POINTER_SIZE_INT new_location_offset = (uintptr_t)frontier + first_elem_offset;
	POINTER_SIZE_INT new_location_offset_adjust = ((new_location_offset + ((1 << (alignment.powerOfTwoBaseFour+2)) - 1)) >> (alignment.powerOfTwoBaseFour+2) << (alignment.powerOfTwoBaseFour+2));
	frontier = (Partial_Reveal_Object *)(new_location_offset_adjust - first_elem_offset);
    return new_location_offset_adjust - new_location_offset;
#endif
}

inline unsigned max_private_nursery_allocation_size(void) {
	return local_nursery_size / ( g_two_space_pn ? 4 : 2);
}

inline Partial_Reveal_Object * small_local_nursery_alloc_or_null(unsigned size, Allocation_Handle ah, GC_Thread_Info *tls_for_gc) {
    // Try to allocate an object from the current alloc block.
    assert(size);
    assert (ah);

	if (size > max_private_nursery_allocation_size()) {
        return NULL;
#if 0
		return (Partial_Reveal_Object*)gc_malloc_slow_no_constraints_with_nursery(size, ah, tls_for_gc, tls_for_gc->get_nursery()
#ifdef PUB_PRIV
    			, g_use_pub_priv
#endif // PUB_PRIV
                );
#endif
	}

    Partial_Reveal_Object *p_return_object = NULL;

	GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();

    Partial_Reveal_Object *frontier = (Partial_Reveal_Object *)private_nursery->tls_current_free;

	adjust_frontier_to_alignment(frontier, (Partial_Reveal_VTable*)ah);

    POINTER_SIZE_INT new_free = (size + (uintptr_t)frontier);
    // check that the old way and the new way are producing
    // the same pointers. This can go once we have it in place.
    if (new_free <= (uintptr_t) private_nursery->tls_current_ceiling) {
        // success...

        // Skip for now so this logic is not visible...
        // stick the vtable in
        //frontier->vt = (Partial_Reveal_VTable *)p_vtable;
        frontier->set_vtable(ah);

        // increment free ptr and return object
        private_nursery->tls_current_free = (void *) new_free;
		p_return_object = frontier;

        if((Partial_Reveal_VTable*)ah == wpo_vtable && !g_treat_wpo_as_normal) {
            pn_info *ls = private_nursery->local_gc_info;
            ls->m_wpos.push_back((weak_pointer_object*)p_return_object);
        }
        if(use_finalization && class_is_finalizable(allocation_handle_get_class(ah))) {
            pn_info *ls = private_nursery->local_gc_info;
            ls->m_finalize.push_back(p_return_object);
        }
    }

	return p_return_object;
} // small_local_nursery_alloc_or_null

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GCEXPORT(Managed_Object_Handle, gc_malloc_or_null) (unsigned size, Allocation_Handle ah) {
    //Partial_Reveal_VTable *p_vtable = (Partial_Reveal_VTable *) vth;
    // All requests for space should be multiples of 4 (IA32) or 8(IA64)
    assert((size % GC_OBJECT_ALIGNMENT) == 0);

#ifndef GC_FAST_ALLOC
    // This seems to make things more stable but needs to ba looked into.
    if (size > 2048) {
        return NULL;
    }
#endif
    //
    // Try to allocate an object from the current chunk.
    // This can fail if the object won't fit in what is left of the
    // chunk.

    Partial_Reveal_Object *p_return_object = NULL;
    if(local_nursery_size) {
        p_return_object = small_local_nursery_alloc_or_null(size, ah, get_gc_thread_local());
    } else {
        p_return_object = gc_malloc_from_thread_chunk_or_null_with_nursery(get_gc_thread_local()->get_nursery(), size, ah);
    }

    gc_trace_allocation(p_return_object, "gc_malloc_or_null returns this object");
    //
    // return the object we just allocated. It may be NULL and we fall into
    // the slow code of gc_malloc.
    //

    // Put the object in the finalize set if it needs a finalizer run eventually.
    // RLH
    // Since we do inline allocation this should never pop up as a hot spot. If it does
    // then we could change the size passed in to something that will always result in this path never
    // being taken. It is a bit hackish to overload size this way so that will have to be weighted against
    // the performane consideration and I hope it comes up lacking.
    // end RLH.

    // if MANGLEPOINTERS is defined this is used for debugging.
    p_return_object =  (Partial_Reveal_Object *)mangleBits((HeapObject *)p_return_object);

    return p_return_object;

} // gc_malloc_or_null



GCEXPORT(Managed_Object_Handle, gc_malloc_or_null_with_thread_pointer) (unsigned size, Allocation_Handle ah, void *tp) {
    //Partial_Reveal_VTable *p_vtable = (Partial_Reveal_VTable *) vth;
    // All requests for space should be multiples of 4 (IA32) or 8(IA64)
    assert((size % GC_OBJECT_ALIGNMENT) == 0);

#ifndef GC_FAST_ALLOC
    // This seems to make things more stable but needs to be looked into.
    if (size > 2048) {
    //        orp_cout << "Size = " << size <<std::endl;
        return NULL;
    }
#endif
    //
    // Try to allocate an object from the current chunk.
    // This can fail if the object won't fit in what is left of the
    // chunk.
    GC_Thread_Info *tls_for_gc = orp_local_to_gc_local(tp);
    Partial_Reveal_Object *p_return_object = NULL;
    if(local_nursery_size) {
        p_return_object = small_local_nursery_alloc_or_null(size, ah, tls_for_gc);
    } else {
        p_return_object = gc_malloc_from_thread_chunk_or_null_with_nursery(tls_for_gc->get_nursery(), size, ah);
    }

    gc_trace_allocation(p_return_object, "gc_malloc_or_null_with_thread_pointer returns this object");

    //
    // return the object we just allocated. It may be NULL and we fall into
    // the slow code of gc_malloc.
    //

    // if MANGLEPOINTERS is defined this is used for debugging.
    p_return_object =  (Partial_Reveal_Object *)mangleBits((HeapObject *)p_return_object);

    return p_return_object;
} // gc_malloc_or_null_with_thread_pointer

void reset_nursery(GC_Nursery_Info *nursery) {
    if (nursery->chunk) {
        block_info *spent_block;
        spent_block = (block_info*)nursery->chunk;

        while (spent_block) {
            assert(spent_block);
            assert(spent_block->get_nursery_status() == active_nursery);
            spent_block->set_nursery_status(active_nursery,spent_nursery);
            spent_block = spent_block->next_free_block;
        }
    }

	nursery->chunk = NULL;
	nursery->curr_alloc_block = NULL;
	nursery->tls_current_free = NULL;
	nursery->tls_current_ceiling = NULL;
}

Managed_Object_Handle gc_malloc_slow_no_constraints_with_nursery (
	unsigned size,
	Allocation_Handle ah,
	GC_Thread_Info *tls_for_gc,
	GC_Nursery_Info *nursery
#ifdef PUB_PRIV
	,bool private_heap_block
#endif // PUB_PRIV
	) {
    orp_initialized = 17;

    Partial_Reveal_Object *p_return_object;
restart:

    p_return_object = NULL;

    //
    // See if this is a large object which needs special treatment:
    //
    if (size >= GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
        // CLEANUP - remove this cast.
        p_return_object = (Partial_Reveal_Object *)gc_pinned_malloc(size,
            ah,
            false, // returnNullOnFail
            false,
			tls_for_gc
        );
        // Characterization done in GCEXPORT caller
        gc_trace_allocation(p_return_object, "gc_malloc_slow_no_constraints_with_nursery returns this object.");

        // if MANGLEPOINTERS is defined this is used for debugging.
        p_return_object =  (Partial_Reveal_Object *)mangleBits((HeapObject *)p_return_object);

        return p_return_object;
    }

    // End of LOS case.

    // We have a normal object free of the alignment or pinning
    // constraints listed above, however,  it could need to be
    // registered for finalization or be a weak ref.
    //
    p_return_object = gc_malloc_from_thread_chunk_or_null_with_nursery(nursery, size, ah);

	if(p_return_object) {
        // Characterization done in GCEXPORT caller
        gc_trace_allocation (p_return_object, "gc_malloc_slow_no_constraints_with_nursery returns this object.");
        return p_return_object;
    }

    // All allocation areas exhausted. Get new set from global chunk store
    assert(p_return_object == NULL);

	//
	// The chunk for this thread is full. Retrieve the exhausted chunk.
	//
    block_info *p_old_chunk = (block_info *)nursery->chunk;

    // Remove chunk from thread structure.
	nursery->chunk = NULL;

	// Allocate a new chunk for this thread's use.
    block_info *p_new_chunk = p_global_gc->p_cycle_chunk(p_old_chunk, false, false
#ifdef PUB_PRIV
		, private_heap_block ? tls_for_gc : NULL
#else
		, NULL
#endif // PUB_PRIV
		, tls_for_gc
		);

	assert (p_new_chunk);
	if(p_new_chunk->get_nursery_status() != active_nursery) {
		assert (p_new_chunk->get_nursery_status() == active_nursery);
	}

	nursery->chunk = p_new_chunk;
	nursery->curr_alloc_block = p_new_chunk;
    if(g_gen) {
        assert(p_new_chunk->is_empty());
    }
	nursery->tls_current_free = NULL;
	nursery->tls_current_ceiling = NULL;

	goto restart;
} // gc_malloc_slow_no_constraints_with_nursery

// If there are no alignment or pinning constraints but possible finalization
// or weak ref constraints then one can use this routine.

Managed_Object_Handle gc_malloc_slow_no_constraints (
	unsigned size,
	Allocation_Handle ah,
	GC_Thread_Info *tls_for_gc
#ifdef PUB_PRIV
, bool private_heap_block
#endif // PUB_PRIV
)
{
    orp_initialized = 17;
    Partial_Reveal_Object *p_return_object;

	if(local_nursery_size) {
		GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();

		// This code should never be called from a local nursery collection.
		// That code should call gc_malloc_slow_no_constraints_with_nursery.
		assert(private_nursery->local_gc_info->gc_state != LOCAL_MARK_ACTIVE);

        // If the object is very large then allocate it in a global large block.
        if (size >= max_private_nursery_allocation_size()) {
            p_return_object = (Partial_Reveal_Object *)gc_pinned_malloc(size,
                ah,
                false, // returnNullOnFail
                false,
				tls_for_gc
            );
            // Characterization done in GCEXPORT caller
            gc_trace_allocation(p_return_object, "gc_malloc_slow_no_constraints returns this object.");

            // if MANGLEPOINTERS is defined this is used for debugging.
            p_return_object =  (Partial_Reveal_Object *)mangleBits((HeapObject *)p_return_object);

            return p_return_object;
        }

		unsigned i;
		for(i=0;i<2;++i) {
			p_return_object = small_local_nursery_alloc_or_null(size,ah,tls_for_gc);
			// Allocation failed to move the survivors from the small local nursery into the real
			// GC nursery and try again.

			if(p_return_object) {
				return p_return_object;
            }

			local_nursery_collection(tls_for_gc, NULL, NULL, false);
		}
		assert(0); // only one local_nursery_collection should be necessary to get enough space.  If not then assert.
		printf("No space was freed for normal sized object after call to local_nursery_collection in gc_malloc_slow_no_constraints.\n");
		exit(-1);
	} else {
	    p_return_object = (Partial_Reveal_Object *)gc_malloc_slow_no_constraints_with_nursery(size, ah, tls_for_gc, tls_for_gc->get_nursery()
#ifdef PUB_PRIV
			, private_heap_block
#endif // PUB_PRIV
			);

        return p_return_object;
	}
} // gc_malloc_slow_no_constraints



Partial_Reveal_Object * Garbage_Collector::create_single_object_blocks(unsigned size, Allocation_Handle ah) {
    num_large_objects++;

    Partial_Reveal_Object *result = NULL;
    // ***SOB LOOKUP*** p_get_multi_block sets the _blocks_in_block_store[sob_index + index].is_single_object_block to true.
    block_info *block = _p_block_store->p_get_multi_block (size, false);   // Do not extend the heap yet.

    if (block == NULL) {
        return NULL;
    }
    assert(block->block_free_areas == NULL);

    block->in_los_p = false;
    block->in_nursery_p = false;
    block->is_single_object_block = true;

    block->thread_owner    = NULL;
    block->next_free_block = _single_object_blocks;
    _single_object_blocks  = block;

    // Use this to keep track....block->number_of_blocks is total together
    block->los_object_size = size;
    Partial_Reveal_Object *p_obj_start = (Partial_Reveal_Object *)GC_BLOCK_ALLOC_START(block);
	if(!do_not_zero) {
        memset (p_obj_start, 0, size);  // Clear the new object.
	}
    result = p_obj_start;
    result->set_vtable(ah);
    assert (result);

    block->curr_free    = (void *) ((uintptr_t)p_obj_start + size);
    block->curr_ceiling = (void *) ((uintptr_t)(GC_BLOCK_INFO (block->curr_free)) + GC_BLOCK_SIZE_BYTES - 1);

    // Characterization done in GCEXPORT caller
    gc_trace_allocation (result, "create_single_object_blocks returns this object.");

    return result;
} // Garbage_Collector::create_single_object_blocks





block_info * Garbage_Collector::get_new_los_block() {
    block_info *block = _p_block_store->p_get_new_block (false);

    if (block == NULL) {
        return NULL;
    }
    block->in_nursery_p = false;
    block->in_los_p = true;
    block->is_single_object_block = false;

    // Blocks in BS always dont have this
    assert(block->block_free_areas == NULL);

    // Allocate free areas per block
    block->size_block_free_areas = GC_MAX_FREE_AREAS_PER_BLOCK(GC_MIN_FREE_AREA_SIZE);
    block->block_free_areas = (free_area *)malloc(sizeof(free_area) * block->size_block_free_areas);
    if (!(block->block_free_areas)) {
        dprintf("malloc failed\n");
        exit (411);
    }

    gc_trace_block (block, " calling clear_block_free_areas in get_new_los_block.");
    // Initialize free areas for block....
    clear_block_free_areas(block);

    // JUST ONE LARGE FREE AREA initially
    free_area *area = &(block->block_free_areas[0]);
    area->area_base = GC_BLOCK_ALLOC_START(block);
    area->area_ceiling = (void *)((uintptr_t)GC_BLOCK_ALLOC_START(block) + (POINTER_SIZE_INT)GC_BLOCK_ALLOC_SIZE - 1);
    area->area_size = (unsigned int)((uintptr_t) area->area_ceiling - (uintptr_t) area->area_base + 1);
    block->num_free_areas_in_block = 1;
    block->current_alloc_area = 0;

    // Start allocation in this block at the base of the first and only area
    block->curr_free = area->area_base;
    block->curr_ceiling = area->area_ceiling;

    return block;
}



// May cause GC if it cant get an LOS block or a multi block from the block store.

void get_chunk_lock(struct GC_Thread_Info *gc_info);
void release_chunk_lock(struct GC_Thread_Info *gc_info);

Partial_Reveal_Object  * gc_pinned_malloc(unsigned size, Allocation_Handle ah, bool return_null_on_fail, bool double_align, GC_Thread_Info *tls_for_gc) {
    while ((LONG) InterlockedCompareExchange( (LONG *)(&p_global_gc->_los_lock), (LONG) 1, (LONG) 0) == (LONG) 1) {
        // spin on the lock until we have the los lock
        // A thread may have caused GC in here if the LOS couldnt get a new block or a LARGE block.
        // So check if GC has begun, enable GC and block on GC lock if needed.

        if (gc_has_begun()) {
            orp_gc_lock_enum();
            orp_gc_unlock_enum();
        }
    }


    if (size > GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
        Partial_Reveal_Object *p_obj = NULL;
        while (p_obj == NULL) {
            p_obj = p_global_gc->create_single_object_blocks(size, ah);

            if (p_obj == NULL) {
                // Do a collection
                get_chunk_lock(tls_for_gc);
                orp_gc_lock_enum();
                if (stats_gc) {
                    // create_single_object_blocks block allocate causing GC
                    dprintf("create_single_object_blocks() failed...calling reclaim_full_heap()\n");
                }
                p_global_gc->reclaim_full_heap(size, false, true);

                orp_gc_unlock_enum();
                release_chunk_lock(tls_for_gc);
            } else {
				adjust_frontier_to_alignment(p_obj,(Partial_Reveal_VTable*)ah);
			}
        }
        p_global_gc->_los_lock = 0;
        // heap characterization done in GCEXPORT caller
        gc_trace_allocation (p_obj, "gc_pinned_malloc returns this object.");
#ifdef CONCURRENT
		GC_Small_Nursery_Info *private_nursery = ((struct GC_Thread_Info *)get_gc_thread_local())->get_private_nursery();
		pn_info *local_collector = private_nursery->local_gc_info;
		switch(GetLocalConcurrentGcState(private_nursery)) {
		case CONCURRENT_IDLE:
			// intentionally do nothing
			break;
		case CONCURRENT_MARKING:
			// Make object black
            mark_header_and_block_atomic(p_obj);
			break;
		case CONCURRENT_SWEEPING:
			if(p_obj >= local_collector->sweep_ptr_copy) {
#ifndef NO_GRAY_SWEEPING
				bool has_slots = p_obj->vt()->get_gcvt()->gc_object_has_slots;
                bool mark_res = mark_header_and_block_atomic(p_obj);
				if(mark_res && has_slots) {
					assert(sizeof(p_obj) == 4);
					add_to_grays_local(local_collector,p_obj);
				}
#endif // NO_GRAY_SWEEPING
			}
			break;
		default:
			assert(0);
		}
#endif // CONCURRENT
        // Put the object in the finalize set if it needs a finalizer run eventually.
        if(use_finalization && class_is_finalizable(allocation_handle_get_class(ah))) {
            p_global_gc->add_finalize_object(p_obj);
        }

        return p_obj;
    }

    num_los_objects++;

    if (p_global_gc->_los_blocks == NULL) {
        p_global_gc->_los_blocks = p_global_gc->get_new_los_block();
    }

    assert(p_global_gc->_los_blocks);

    Partial_Reveal_Object *p_return_object = NULL;

    while (p_return_object == NULL) {

        block_info *los_block = p_global_gc->_los_blocks;
        /// XXX GC_SLOW_ALLOC
        if (los_block->current_alloc_area == -1){
            los_block->current_alloc_area = 0;
        }
        /// XXXXXXXX GC_SLOW_ALLOC
        assert(los_block->current_alloc_area != -1);
        if ((los_block->num_free_areas_in_block == 0) ||
            (los_block->current_alloc_area == los_block->num_free_areas_in_block)) { // Topped out on this block
            // Get new LOS block and hook it in
            block_info *block = NULL;
            while (block == NULL) {
                block = p_global_gc->get_new_los_block();
                if (block == NULL) {
                    // do a collection since cant get a block

                    orp_gc_lock_enum();

                    if (stats_gc) {
                        // get_new_los_block() failed....block allocate causing GC
                        dprintf("get_new_los_block() failed...calling reclaim_full_heap()\n");
                    }
                    // If we get here then maybe we are seeing a lot of pinned allocations and since pinned allocations
                    // need free blocks, we pass true as the last parameter to reclaim_full_heap and this triggers the
                    // GC to put every free block on the free list.
                    p_global_gc->reclaim_full_heap(size, false, true);
                    orp_gc_unlock_enum();
                }
            }

            block->next_free_block = p_global_gc->_los_blocks;
            p_global_gc->_los_blocks = block;
            // Go ahead with the new block
            los_block = p_global_gc->_los_blocks;
            assert(los_block->num_free_areas_in_block > 0);
        }

        assert(los_block->curr_free);
        assert(los_block->curr_ceiling);
        // = is possible if the previously allocated object exactly finished off the region
        assert(los_block->curr_free <= los_block->curr_ceiling);
        // XXXXXXXXXXXX GC_SLOW_ALLOC
        if (los_block->current_alloc_area == -1) {
            los_block->current_alloc_area = 0;
        }

        do {
			Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)los_block->curr_free;
			adjust_frontier_to_alignment(p_obj,(Partial_Reveal_VTable*)ah);
            POINTER_SIZE_INT new_free = (size + (uintptr_t)p_obj);
//            POINTER_SIZE_INT new_free = (size + (POINTER_SIZE_INT)los_block->curr_free);

            if (new_free <= (uintptr_t) los_block->curr_ceiling) {
                // success...

                p_return_object = (Partial_Reveal_Object *) los_block->curr_free;
				if(!do_not_zero) {
                    memset(p_return_object, 0, size); // This costs me 2%
				}

                // stick the vtable in
                //p_return_object->vt = (Partial_Reveal_VTable *)p_vtable;
                p_return_object->set_vtable(ah);
                // increment free ptr and return object
                los_block->curr_free = (void *) new_free;
                // Free LOS lock and leave
                p_global_gc->_los_lock = 0;
                // heap characterization done in GCEXPORT caller
                gc_trace_allocation (p_return_object, "gc_pinned_malloc returns this object.");
#ifdef CONCURRENT
				GC_Small_Nursery_Info *private_nursery = ((struct GC_Thread_Info *)get_gc_thread_local())->get_private_nursery();
				pn_info *local_collector = private_nursery->local_gc_info;
				switch(GetLocalConcurrentGcState(private_nursery)) {
				case CONCURRENT_IDLE:
					// intentionally do nothing
					break;
				case CONCURRENT_MARKING:
					// Make object black
                    mark_header_and_block_atomic(p_return_object);
					break;
				case CONCURRENT_SWEEPING:
					if(p_return_object >= local_collector->sweep_ptr_copy) {
#ifndef NO_GRAY_SWEEPING
						bool has_slots = p_return_object->vt()->get_gcvt()->gc_object_has_slots;
                        bool mark_res = mark_header_and_block_atomic(p_return_object);
						if(mark_res && has_slots) {
							assert(sizeof(p_return_object) == 4);
							add_to_grays_local(local_collector,p_return_object);
						}
#endif // NO_GRAY_SWEEPING
					}
					break;
				default:
					assert(0);
				}
#endif // CONCURRENT
                // Put the object in the finalize set if it needs a finalizer run eventually.
                if(use_finalization && class_is_finalizable(allocation_handle_get_class(ah))) {
                    p_global_gc->add_finalize_object(p_return_object);
                }

                return p_return_object;

            } else {
                // Move to next allocation area in block
                los_block->current_alloc_area++;
                los_block->curr_free = los_block->block_free_areas[los_block->current_alloc_area].area_base;
                los_block->curr_ceiling = los_block->block_free_areas[los_block->current_alloc_area].area_ceiling;
            }

        } while (los_block->current_alloc_area < los_block->num_free_areas_in_block);

    } // while

    assert(0);	// cant get here
    printf("Got to an impossible spot in gc_pinned_malloc.\n");
    exit(-1);
}


GCEXPORT(Boolean, gc_can_allocate_without_collection)(unsigned size, void *tp) {
    GC_Thread_Info *gc_tls = orp_local_to_gc_local(tp);
    GC_Nursery_Info *main_nursery = gc_tls->get_primary_nursery();
    if((char*)main_nursery->tls_current_free + size < main_nursery->tls_current_ceiling) {
        return TRUE;
    } else {
        return FALSE;
    }
}

GCEXPORT(void, gc_require_allocate_without_collection)(unsigned size, void *tp) {
    GC_Thread_Info *tls_for_gc = orp_local_to_gc_local(tp);
	if(local_nursery_size && size < local_nursery_size) {
		GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();

        local_nursery_collection(tls_for_gc, NULL, NULL, false);
        if((char*)private_nursery->tls_current_free + size >= private_nursery->tls_current_ceiling) {
            printf("gc_require_allocate_without_collection failed to free enough space to perform requested allocation.\n");
            exit(-1);
        }
	} else {
restart:
        if(gc_can_allocate_without_collection(size,tp)) {
            return;
        }

        if (size >= GC_MAX_CHUNK_BLOCK_OBJECT_SIZE) {
            printf("gc_require_allocation_without_collection cannot guarantee allocation space of more than %d bytes.\n",GC_MAX_CHUNK_BLOCK_OBJECT_SIZE);
            assert(0);
            exit(-1);

            // This code doesn't work because to do the actual allocation would require the slow path which is what we
            // are trying to avoid.
            while ((LONG) InterlockedCompareExchange( (LONG *)(&p_global_gc->_los_lock), (LONG) 1, (LONG) 0) == (LONG) 1) {
                // spin on the lock until we have the los lock
                // A thread may have caused GC in here if the LOS couldnt get a new block or a LARGE block.
                // So check if GC has begun, enable GC and block on GC lock if needed.

                if (gc_has_begun()) {
                    orp_gc_lock_enum();
                    orp_gc_unlock_enum();
                }
            }

            p_global_gc->guarantee_multi_block_allocation(size,tls_for_gc);
            p_global_gc->_los_lock = 0;
            return;
        }

        GC_Nursery_Info *gc_nursery = tls_for_gc->get_nursery();

        // We need a new allocation area. Get the current alloc_block
        block_info *alloc_block = (block_info *)gc_nursery->curr_alloc_block;

        // Loop through the alloc blocks to see if we can find another allocation area.
        while (alloc_block) {
            // We will sweep blocks only right before we start using it. This seems to have good cache benefits

		    if (!sweeps_during_gc) {
			    // Sweep the block
			    if (alloc_block->block_has_been_swept == false) {
                    sweep_stats stats;
				    // Determine allocation areas in the block
				    p_global_gc->sweep_one_block(alloc_block,stats);
				    alloc_block->block_has_been_swept = true;
			    }
		    }

#ifdef CONCURRENT
            if(alloc_block->get_nursery_status() == concurrent_sweeper_nursery) {
                printf("Found block being swept while looking for free allocation area in gc_malloc_from_thread_chunk_or_null_with_nursery.\n");
            }
#endif // CONCURRENT

            // current_alloc_area will be -1 if the first area has not been used and if it exists is available.
            if ( (alloc_block->num_free_areas_in_block == 0) || ((alloc_block->current_alloc_area + 1) == alloc_block->num_free_areas_in_block) ){
                // No areas left in this block get the next one.
                alloc_block = alloc_block->next_free_block; // Get the next block and loop
            } else {
                assert (alloc_block->current_alloc_area < alloc_block->num_free_areas_in_block);
                break; // This block has been swept and has an untouched alloc block.
            }
        } // end while (alloc_block)

        if (alloc_block == NULL) {
            // ran through the end of the list of blocks in the chunk
            gc_nursery->tls_current_ceiling = NULL;
            gc_nursery->tls_current_free = NULL;
            gc_nursery->curr_alloc_block = NULL; // Indicating that we are at the end of the alloc blocks for this chunk.

	        //
	        // The chunk for this thread is full. Retrieve the exhausted chunk.
	        //
            block_info *p_old_chunk = (block_info *)gc_nursery->chunk;

            // Remove chunk from thread structure.
	        gc_nursery->chunk = NULL;

	        // Allocate a new chunk for this thread's use.
            block_info *p_new_chunk = p_global_gc->p_cycle_chunk(p_old_chunk, false, false, NULL, tls_for_gc);

	        assert (p_new_chunk);
	        if(p_new_chunk->get_nursery_status() != active_nursery) {
		        assert (p_new_chunk->get_nursery_status() == active_nursery);
	        }

	        gc_nursery->chunk = p_new_chunk;
	        gc_nursery->curr_alloc_block = p_new_chunk;
	        gc_nursery->tls_current_free = NULL;
	        gc_nursery->tls_current_ceiling = NULL;

	        goto restart;
        }

        assert(alloc_block->num_free_areas_in_block > 0);
        assert(alloc_block->block_has_been_swept);
        assert(alloc_block->current_alloc_area < alloc_block->num_free_areas_in_block);

        // We have a block that has been swept and has at least one allocation area big enough to fit this object so we will be
        // successful..

        alloc_block->current_alloc_area++; // Get the next currenct area. If it is the first one it will be 0.

        //XXX assert part of GC_SLOW_ALLOC routines.
        assert (alloc_block->current_alloc_area != -1);
        unsigned int curr_area = alloc_block->current_alloc_area;

        if (alloc_block->block_free_areas[curr_area].has_been_zeroed == false) {
            gc_trace_block(alloc_block, " Clearing the curr_area in this block.");
		    if(!do_not_zero) {
	            // CLEAR allocation areas just before you start using them
		        memset(alloc_block->block_free_areas[curr_area].area_base, 0, alloc_block->block_free_areas[curr_area].area_size);
		    }
            alloc_block->block_free_areas[curr_area].has_been_zeroed = true;
        }

        gc_nursery->tls_current_free = alloc_block->block_free_areas[curr_area].area_base;
        // JMS 2003-05-23.  If references are compressed, then the heap base should never be
        // given out as a valid object pointer, since it is being used as the
        // representation for a managed null.
        assert(gc_nursery->tls_current_free != Slot::managed_null());
#ifdef CONCURRENT
        if(alloc_block->get_nursery_status() == concurrent_sweeper_nursery) {
            printf("Found block being swept while looking for free allocation area in gc_malloc_from_thread_chunk_or_null_with_nursery.\n");
            assert(0);
            exit(-1);
        }
#endif // CONCURRENT
        gc_nursery->tls_current_ceiling = alloc_block->block_free_areas[curr_area].area_ceiling;
        gc_nursery->curr_alloc_block = alloc_block;

        goto restart;
    }
}

void Garbage_Collector::guarantee_multi_block_allocation(unsigned size,GC_Thread_Info *tls_for_gc) {
    block_info *block = _p_block_store->p_get_multi_block (size, false, false /* just check for alloc */);

    if (block == NULL) {
        // Do a collection
        get_chunk_lock(tls_for_gc);
        orp_gc_lock_enum();
        if (stats_gc) {
            // create_single_object_blocks block allocate causing GC
            dprintf("create_single_object_blocks() failed...calling reclaim_full_heap()\n");
        }
        p_global_gc->reclaim_full_heap(size, false, false);

        orp_gc_unlock_enum();
        release_chunk_lock(tls_for_gc);
	}
}
