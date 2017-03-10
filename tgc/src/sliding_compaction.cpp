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

// System header files
#include <iostream>
#include <fstream>

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
#include "tgc/gcv4_synch.h"
#include "tgc/gc_debug.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern bool verify_live_heap;
bool zero_after_compact = false;
void add_repointed_info_for_thread(Partial_Reveal_Object *p_old, Partial_Reveal_Object *p_new, unsigned int thread_id);
void dump_object_layouts_in_compacted_block(block_info *, unsigned int);
void close_dump_file();
unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _DEBUG
extern bool g_profile;
extern std::ofstream * profile_out;
extern std::ofstream * live_dump;
#endif

#ifdef IGNORE_SOME_ROOTS
extern std::set<Partial_Reveal_Object**> g_barrier_roots;
#endif // IGNORE_SOME_ROOTS

void fix_slots_to_compaction_live_objects(GC_Thread *gc_thread) {
	block_info *p_compaction_block = NULL;
	unsigned int num_slots_fixed = 0;

	while ((p_compaction_block = gc_thread->_p_gc->get_block_for_fix_slots_to_compaction_live_objects(gc_thread->get_id(), p_compaction_block))) {

		while (true) {

			Remembered_Set *some_slots = gc_thread->_p_gc->get_slots_into_compaction_block(p_compaction_block);
			if (some_slots == NULL) {
				// Done with fixing all slots
				break;
			}

#ifdef REMEMBERED_SET_ARRAY

            Remembered_Set::iterator iter;
			Slot one_slot(NULL);

            for(iter  = some_slots->begin();
                iter != some_slots->end();
                ++iter) {
                one_slot.set(iter.get_current());

#ifdef IGNORE_SOME_ROOTS
				if(g_barrier_roots.find((Partial_Reveal_Object**)one_slot.get_value()) != g_barrier_roots.end()) {
					// let the root update stuff take care when during testing we have
					// a slot and a root to the same spot.
					continue;
				}
#endif // IGNORE_SOME_ROOTS

				Partial_Reveal_Object *p_obj = one_slot.dereference();

				// This slot points into this compaction block
				assert(GC_BLOCK_INFO(p_obj) == p_compaction_block);

                if(p_obj->isForwarded()) {
                    Partial_Reveal_Object *p_new_obj = p_obj->get_forwarding_pointer();
                    if(p_new_obj != p_obj) {
				        // update slot
                        one_slot.update(p_new_obj);
				        num_slots_fixed++;

				        assert(GC_BLOCK_INFO(p_obj)->in_nursery_p == true);
				        assert(GC_BLOCK_INFO(p_new_obj)->in_nursery_p == true);
    			        // the block this moves into better have been flagged for compaction..
				        assert(GC_BLOCK_INFO(p_new_obj)->is_compaction_block);
                    }
                }
            }

#else  // REMEMBERED_SET_ARRAY
			some_slots->rewind();
			Slot one_slot(NULL);

			while (one_slot.set(some_slots->next().get_value())) {

#ifdef IGNORE_SOME_ROOTS
				if(g_barrier_roots.find((Partial_Reveal_Object**)one_slot.get_value()) != g_barrier_roots.end()) {
					// let the root update stuff take care when during testing we have
					// a slot and a root to the same spot.
					continue;
				}
#endif // IGNORE_SOME_ROOTS

				Partial_Reveal_Object *p_obj = one_slot.dereference();

//				gc_trace(p_obj, " fix_slots_to_compaction_live_objects(): a slot pointed to this object, but the slot is being repointed...\n");

				// This slot points into this compaction block
				assert(GC_BLOCK_INFO(p_obj) == p_compaction_block);
                assert(p_obj->isForwarded());
				//Partial_Reveal_Object *p_new_obj = (Partial_Reveal_Object *) p_obj->get_obj_info();
                Partial_Reveal_Object *p_new_obj = p_obj->get_forwarding_pointer();
				// all objects in a sliding compacted area are forwarded.
				//assert((POINTER_SIZE_INT)p_new_obj & FORWARDING_BIT_MASK);
				// update slot
                one_slot.update(p_new_obj);
				num_slots_fixed++;

//				gc_trace(p_new_obj,
//					" fix_slots_to_compaction_live_objects(): a slot is being repointed to this object...\n");

				assert(GC_BLOCK_INFO(p_obj)->in_nursery_p == true);
				assert(GC_BLOCK_INFO(p_new_obj)->in_nursery_p == true);
    			// the block this moves into better have been flagged for compaction..
				assert(GC_BLOCK_INFO(p_new_obj)->is_compaction_block);

			} // while (one_slot)
#endif // REMEMBERED_SET_ARRAY

			// Delete the remebered set since it is not needed anymore...all slot updates for this list are done
			delete some_slots;

		} // while (true)
	} // while

	if (stats_gc) {
        printf ("%u: fixed %u slots\n",  gc_thread->get_id(), num_slots_fixed );
//		orp_cout << gc_thread->get_id() << ": fixed " <<  num_slots_fixed << " slots\n";
	}
}

// We make available all bytes after we are finished sliding objects into this block.

POINTER_SIZE_INT sweep_slide_compacted_block(block_info *p_compaction_block, void *first_free_byte_in_this_block_after_sliding_compaction) {
	// Align first free byte to next 16-byte boundary
//	void *first_free_byte_in_this_block = (void *) ( (((POINTER_SIZE_INT) first_free_byte_in_this_block_after_sliding_compaction + (GC_BYTES_PER_MARK_BYTE - 1)) & (~(GC_BYTES_PER_MARK_BYTE - 1))));

    gc_trace_block (p_compaction_block, " calling clear_block_free_areas in sweep_slide_compacted_block.");
	// Clear all the free areas computed during the previous GC
	clear_block_free_areas(p_compaction_block);

	POINTER_SIZE_INT bytes_freed_by_compaction_in_this_block = 0;
#if 0 // If anything was slid into this block reserve the entire block...
	// Fix the one allocation area if it remains
	if ((GC_BLOCK_INFO(first_free_byte_in_this_block) == GC_BLOCK_INFO(p_compaction_block)) &&	// Are we still in the same block??!
		((POINTER_SIZE_INT)GC_BLOCK_CEILING(p_compaction_block) - (POINTER_SIZE_INT)first_free_byte_in_this_block) > (POINTER_SIZE_INT)GC_MIN_FREE_AREA_SIZE) {

		p_compaction_block->num_free_areas_in_block = 1;


        p_compaction_block->current_alloc_area = -1;

        p_compaction_block->curr_free = p_compaction_block->block_free_areas[0].area_base = first_free_byte_in_this_block;
		p_compaction_block->curr_ceiling = p_compaction_block->block_free_areas[0].area_ceiling = GC_BLOCK_CEILING(p_compaction_block);
		p_compaction_block->block_free_areas[0].area_size = (unsigned)((POINTER_SIZE_INT)GC_BLOCK_CEILING(p_compaction_block) - (POINTER_SIZE_INT)first_free_byte_in_this_block + 1);
		p_compaction_block->block_free_areas[0].has_been_zeroed = false;
		// this area has to be 16-byte times X
		assert(p_compaction_block->block_free_areas[0].area_size % GC_BYTES_PER_MARK_BYTE == 0);
		bytes_freed_by_compaction_in_this_block = p_compaction_block->block_free_areas[0].area_size ;

	} else {
#endif
        p_compaction_block->num_free_areas_in_block = 0;

        p_compaction_block->current_alloc_area = -1;

		bytes_freed_by_compaction_in_this_block = 0;
        p_compaction_block->curr_free = NULL;
		p_compaction_block->curr_ceiling = NULL;
#if 0
    }
#endif
	// We know that some objects in this block lives through GC
	//p_compaction_block->is_free_block = false;
	// No need to sweep this block during allocation
	p_compaction_block->block_has_been_swept = true;

    p_compaction_block->block_free_bytes = (unsigned int)bytes_freed_by_compaction_in_this_block;
    p_compaction_block->block_used_bytes = (unsigned int)(GC_BLOCK_ALLOC_SIZE - bytes_freed_by_compaction_in_this_block);

	// These blocks dont get swept during allocation after mutators are restarted.
	// Means we need to explicitly clear their mark bit vectors, since they are fully useless until they get
	// repopulated with bits in the next GC cycle
	GC_CLEAR_BLOCK_MARK_BIT_VECTOR(p_compaction_block);

	return bytes_freed_by_compaction_in_this_block;
}


POINTER_SIZE_INT sweep_free_block(block_info * p_compaction_block) {
    gc_trace_block (p_compaction_block, " Sweeping this compaction block");
    assert (!p_compaction_block->is_to_block);
//    orp_cout << "-sc- sweep_free_block." << std::endl;
	// Clear all the free areas computed during the previous GC

    gc_trace_block (p_compaction_block, " calling clear_block_free_areas in sweep_free_block.");
	clear_block_free_areas(p_compaction_block);

	// determine that there is exactly ONE large allocation area in this block
//	p_compaction_block->is_free_block = true; // // RLH Aug 04
	p_compaction_block->num_free_areas_in_block = 1;

#ifndef GC_SLOW_ALLOC
    p_compaction_block->current_alloc_area = -1;
#else
    p_compaction_block->current_alloc_area = 0;
#endif

	p_compaction_block->curr_free = p_compaction_block->block_free_areas[0].area_base = GC_BLOCK_ALLOC_START(p_compaction_block);
	p_compaction_block->curr_ceiling = p_compaction_block->block_free_areas[0].area_ceiling = GC_BLOCK_CEILING(p_compaction_block);
	p_compaction_block->block_free_areas[0].area_size = GC_BLOCK_ALLOC_SIZE; // This is how we declare that the entire block is avaliable. (contains fo object)

    if (zero_after_compact) {
        gc_trace_block(p_compaction_block, " Clearing the curr_area in this block after compaction.");
        memset(p_compaction_block->block_free_areas[0].area_base, 0, p_compaction_block->block_free_areas[0].area_size);
        p_compaction_block->block_free_areas[0].has_been_zeroed = true;
    } else {
		p_compaction_block->block_free_areas[0].has_been_zeroed = false;
	}
	p_compaction_block->block_has_been_swept = true;
    p_compaction_block->block_free_bytes = GC_BLOCK_ALLOC_SIZE;
    p_compaction_block->block_used_bytes = 0;


	// These blocks dont get swept during allocation after mutators are restarted.
	// Means we need to explicitly clear their mark bit vectors, since they are fully useless until they get
	// repopulated with bits in the next GC cycle
	GC_CLEAR_BLOCK_MARK_BIT_VECTOR(p_compaction_block);

	return GC_BLOCK_ALLOC_SIZE;
}



class saved_to_block_info {
public:
    block_info *bi;
    Partial_Reveal_Object *next_obj_start;

    saved_to_block_info(block_info *b, Partial_Reveal_Object *next) : bi(b), next_obj_start(next) {}
};

void allocate_forwarding_pointers_for_compaction_live_objects(GC_Thread *gc_thread) {
    //unsigned int problem_locks = 0; // placement code does not deal with this.
    //unsigned int total_live_bytes = 0;
    block_info *p_compaction_block = NULL;
    block_info *p_destination_block = NULL;
    Partial_Reveal_Object *next_obj_start = NULL;
    unsigned int num_forwarded = 0;

#ifdef PUB_PRIV
    std::map<void *,saved_to_block_info> owner_map;
#endif // PUB_PRIV

	gc_thread->m_forwards.reset();

    // Initialize the first block into which live objects will start sliding into
    p_destination_block = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), NULL, NULL);
    if (p_destination_block == NULL) {
        // There is no block for compaction for this thread...no work to do in this phase....
        assert(gc_thread->_p_gc->get_block_for_sliding_compaction_allocation_pointer_computation(gc_thread->get_id(), NULL) == NULL);
        return;
    }
    p_destination_block->is_to_block = true;
    gc_trace_block (p_destination_block, "is_to_block set to true.");
    next_obj_start = (Partial_Reveal_Object*)GC_BLOCK_ALLOC_START(p_destination_block);

    while ((p_compaction_block = gc_thread->_p_gc->get_block_for_sliding_compaction_allocation_pointer_computation(gc_thread->get_id(), p_compaction_block ))) {
        unsigned int num_forwarded_in_this_block = 0;
        assert(p_compaction_block->in_nursery_p == true);

#ifdef PUB_PRIV
        if(g_use_pub_priv && p_compaction_block->thread_owner != p_destination_block->thread_owner) {
            owner_map.insert(std::pair<void*,saved_to_block_info>(p_destination_block->thread_owner,saved_to_block_info(p_destination_block,next_obj_start)));
            std::map<void*,saved_to_block_info>::iterator owner_iter;
            owner_iter = owner_map.find(p_compaction_block->thread_owner);
            if(owner_iter != owner_map.end()) {
                p_destination_block = owner_iter->second.bi;
                next_obj_start = owner_iter->second.next_obj_start;
                owner_map.erase(owner_iter);
            } else {
                p_destination_block = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), NULL, p_compaction_block->thread_owner, true);
                if(!p_destination_block) {
                    printf("Couldn't find p_destination_block for a given thread in allocate_forwarding_pointers_for_compaction_live_objects.\n");
                    assert(0);
                    exit(-1);
                }
                p_destination_block->is_to_block = true;
                gc_trace_block (p_destination_block, "is_to_block set to true.");
                assert(p_destination_block);
                next_obj_start = (Partial_Reveal_Object*)GC_BLOCK_ALLOC_START(p_destination_block);
            }
        }
//    std::map<void *,block_info *> owner_map;
#endif // PUB_PRIV

        Partial_Reveal_Object *p_prev_obj = NULL;
        gc_thread->_p_gc->init_live_object_iterator_for_block(p_compaction_block);
        Partial_Reveal_Object *p_obj = gc_thread->_p_gc->get_next_live_object_in_block(p_compaction_block);

        while (p_obj) {
            assert(!p_compaction_block->from_block_has_been_evacuated); // This block has not been evacuated yet.

            gc_trace (p_obj, "Examining object to see if it should be compacted");
            assert(GC_BLOCK_INFO(p_obj) == p_compaction_block);
            // Only objects in compaction blocks get forwarded
            assert(gc_thread->_p_gc->is_compaction_block(GC_BLOCK_INFO(p_obj)));
            // Assert that the list is pre-sorted
            assert(p_obj > p_prev_obj);
            unsigned int p_obj_size = get_object_size_bytes(p_obj);
//            total_live_bytes += p_obj_size;

			adjust_frontier_to_alignment(next_obj_start,(Partial_Reveal_VTable*)p_obj->vt());

            // Check for possible overflow in destination block where p_obj could go...
            if ((void *) ((POINTER_SIZE_INT) next_obj_start + p_obj_size) > GC_BLOCK_CEILING(p_destination_block)) {
                // this object will not fit in current destination block...get a new one..
#ifdef PUB_PRIV
                if(g_use_pub_priv) {
                    p_destination_block = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), p_destination_block, p_destination_block->thread_owner, true);
                } else {
                    p_destination_block = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), p_destination_block, p_destination_block->thread_owner);
                }
#else  // PUB_PRIV
                p_destination_block = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), p_destination_block, p_destination_block->thread_owner);
#endif // PUB_PRIV
                p_destination_block->is_to_block = true;
                gc_trace_block (p_destination_block, "is_to_block set to true.");
                assert(p_destination_block);
                next_obj_start = (Partial_Reveal_Object*)GC_BLOCK_ALLOC_START(p_destination_block);
            }
            // next_obj_start is where p_obj will go and it surely belongs in p_destination_block
            assert(GC_BLOCK_INFO(next_obj_start) == p_destination_block);

            // clobber the header with the new address.
            // This needs to be done atomically since some other thread may try to steal it to do colocation.!!!
            bool success = false;
            assert ((POINTER_SIZE_INT)next_obj_start + p_obj_size < (POINTER_SIZE_INT)(GC_BLOCK_CEILING(next_obj_start))); // Check overflow.

            gc_thread->m_forwards.add_entry(ForwardedObject(p_obj->vt(),next_obj_start));

#ifdef _DEBUG
            if(profile_out) {
                *profile_out << p_global_gc->get_gc_num() << " 1 " << p_obj << " " << next_obj_start << "\n";
            }
            if(live_dump) {
                *live_dump << p_global_gc->get_gc_num() << " 2 " << p_obj << " " << next_obj_start << "\n";
            }
#endif

            p_obj->set_forwarding_pointer(gc_thread->m_forwards.get_last_addr());
            success = true; // We always succeed if we are not colocating objects.
            gc_trace (next_obj_start, " In allocate_forwarding_pointers_for_compaction_live_objects forwarding *to* this location. (vtable not yet legal)");
            gc_trace(p_obj, " was forwarded...\n");
            if (verify_live_heap && success) {
                add_repointed_info_for_thread(p_obj, (Partial_Reveal_Object *) next_obj_start, gc_thread->get_id());
            }
            num_forwarded++;
            num_forwarded_in_this_block++;
            if (success) {
                // Compute the address of the next slid object
                next_obj_start = (Partial_Reveal_Object*)((POINTER_SIZE_INT) next_obj_start + p_obj_size);
            }
            p_prev_obj = p_obj;
            p_obj = gc_thread->_p_gc->get_next_live_object_in_block(p_compaction_block);
        } // end of while that gets the next object in a block, some could have been colocated.

    } // while

    if (stats_gc) {
        printf ("%u: allocated forwarding pointers for %u objects\n",  gc_thread->get_id(), num_forwarded);
    }

} // allocate_forwarding_pointers_for_compaction_live_objects

void slide_cross_compact_live_objects_in_compaction_blocks(GC_Thread *gc_thread) {
	block_info *p_compaction_block = NULL;
	unsigned int num_slided = 0;
	POINTER_SIZE_INT bytes_freed_by_compaction_by_this_gc_thread = 0;
	unsigned int num_actually_slided = 0;
	Partial_Reveal_Object *p_prev_obj = NULL;

	if(stats_gc) {
		printf("Used forwarding slots = %d\n",gc_thread->m_forwards.size());
	}

	while ((p_compaction_block = gc_thread->_p_gc->get_block_for_slide_compact_live_objects(gc_thread->get_id(), p_compaction_block))) {
		// assume the block will be completely emptied.  if source and destination blocks are the same
		// then the if statement about 18 lines below will set age back to 1.
		p_compaction_block->age = 0;

        gc_trace_block (p_compaction_block, "Memmoving objects out of this block.");
        assert(p_compaction_block->in_nursery_p == true);

		gc_thread->_p_gc->init_live_object_iterator_for_block(p_compaction_block);
		Partial_Reveal_Object *p_obj = 	gc_thread->_p_gc->get_next_live_object_in_block(p_compaction_block);

		while (p_obj) {
			assert(GC_BLOCK_INFO(p_obj) == p_compaction_block);

			gc_trace(p_obj, " slide_compact_live_objects_in_compaction_blocks(): this object will be slided up...\n");
			// Grab the eventual location of p_obj
			Partial_Reveal_Object *p_new_obj = p_obj->get_forwarding_pointer();

			struct block_info *new_obj_block = GC_BLOCK_INFO(p_new_obj);
			if(new_obj_block->age == 0) {
				new_obj_block->age = 1;
			}
//			new_obj_block->age = new_obj_block->age>=(p_compaction_block->age+1) ? new_obj_block->age:(p_compaction_block->age+1);

			Partial_Reveal_VTable *p_orig_vtable = p_obj->vt();
			gc_trace(p_new_obj, " some object is being slid into this spot: slide_compact_live_objects_in_compaction_blocks():...\n");

            // We move the object to the forwarding pointer location *not* to the next available location....
            if (p_obj != p_new_obj) {
                if ( !(GC_BLOCK_INFO(p_new_obj))->from_block_has_been_evacuated ) {
                    // We might be sliding inside the same block
                    assert ((GC_BLOCK_INFO(p_new_obj)) == (GC_BLOCK_INFO(p_obj)));
                    // Make sure we are sliding downward.
                    assert ((POINTER_SIZE_INT)p_new_obj < (POINTER_SIZE_INT)p_obj);
                }
                assert( (GC_BLOCK_INFO(p_new_obj))->is_to_block);
                memmove(p_new_obj, p_obj, get_object_size_bytes(p_obj));
                gc_trace(p_new_obj, "This new object just memmoved into place.");
                num_actually_slided++;
            }
            num_slided++;
            // clear the object header since the use for the forwarding pointer is finished
			p_new_obj->set_vtable((Allocation_Handle)p_orig_vtable);
            gc_trace(p_new_obj, " slide_compact_live_objects_in_compaction_blocks(): is clearing the just moved object obj_info field...\n");
            p_prev_obj = p_new_obj;
            p_obj = gc_thread->_p_gc->get_next_live_object_in_block(p_compaction_block);
            assert (p_obj != p_prev_obj);
		} // while

//		dump_object_layouts_in_compacted_block(p_compaction_block, num_slided_in_block); 		// 0x17470000 == item,
        // Signify that it is save to move objects into this block without concern.
        p_compaction_block->from_block_has_been_evacuated = true;
        gc_trace_block (p_compaction_block, "from_block_has_been_evacuated now set to true, block empty.");
	} // while

    block_info *p_block_to_set = NULL;
    // Sweep all blocks owned by this GC thread if it is a to block do not make it available otherwise declare it free.
    // HOW ABOUT SETTING THESE UP FOR ALLOCATION SWEEPS??!!!!!!!!!!!!!! this will reduce stop-the-world time!!
    while ((p_block_to_set = gc_thread->_p_gc->iter_get_next_compaction_block_for_gc_thread(gc_thread->get_id(), p_block_to_set, NULL))) {
        if (p_block_to_set->is_to_block) {
            sweep_slide_compacted_block(p_block_to_set, NULL);
        } else {
    		bytes_freed_by_compaction_by_this_gc_thread += sweep_free_block(p_block_to_set); // is this a hotspot??
        }
	}

	if (stats_gc) {
        printf("%u: slide compacted %u objects, num_actually_slided = %u\n", gc_thread->get_id(), num_slided, num_actually_slided);
		printf("%u: bytes_freed_by_compaction_by_this_gc_thread = %uK\n", gc_thread->get_id(), (unsigned int) (bytes_freed_by_compaction_by_this_gc_thread / 1024));
	}

//	close_dump_file();

} // slide_cross_compact_live_objects_in_compaction_block
