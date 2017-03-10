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
#include "tgc/mark.h"
#include "tgc/gcv4_synch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern void get_next_set_bit(set_bit_search_info *info);

extern bool get_num_consecutive_similar_bits(uint8 *, unsigned int, unsigned int *, uint8 *);

// used to debug/verify "get_num_consecutive_similar_bits" above....
extern bool verify_consec_bits_using_asm(uint8 *, unsigned int , unsigned int *, uint8 *);

extern void verify_get_next_set_bit_code(set_bit_search_info *);
unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt);

bool g_zero_dead = false;

//////////////////////////////////  S W E E P  /////////////////////////////////////////////////////////////////////////////////////

#ifdef CONCURRENT
extern bool no_use_sweep_ptr;
extern bool incremental_compaction;
extern unsigned g_num_blocks_available;
#endif

unsigned int Garbage_Collector::sweep_single_object_blocks(block_info *blocks, sweep_stats &stats) {
    block_info *this_block = _single_object_blocks;
    block_info *block = this_block;
    block_info *new_single_object_blocks = NULL;

	unsigned int reclaimed = 0;

    // ***SOB LOOKUP*** We clear the _blocks_in_block_store[mumble].is_single_object_block field in the link_free_blocks routine..

    while (block != NULL) {
        this_block = block;
        block = block->next_free_block;   // Get the next block before we destroy this block.
        assert (this_block->is_single_object_block);

   		// SOB blocks never get compacted
		assert(!is_compaction_block(this_block));

        Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)GC_BLOCK_ALLOC_START(this_block);

//		unsigned size = get_object_size_bytes(p_obj);
//       	unsigned int low_mark_index = (unsigned int) (GC_OBJECT_LOW_MARK_INDEX(p_obj, size));

		if (is_object_marked(p_obj) == false) {
            // It is free, relink the blocks onto the free list.
            assert (this_block->number_of_blocks);
			reclaimed += (this_block->number_of_blocks) * GC_BLOCK_SIZE_BYTES;
            _p_block_store->link_free_blocks (this_block, this_block->number_of_blocks);
        } else {
			// Used block
            // GC_MARK(p_obj, low_mark_index) = false; // Unmark the object in preparation for the next collection.
            this_block->next_free_block = new_single_object_blocks;
            new_single_object_blocks = this_block;
#ifdef CONCURRENT
			if(p_obj->isMarked()) {
				p_obj->unmark();
			} else {
				assert(0);
			}
#endif // CONCURRENT
        }
    }
    _single_object_blocks = new_single_object_blocks;
	stats.amount_recovered += reclaimed;
	stats.blocks_swept ++;
	return reclaimed;
}




void Garbage_Collector::prepare_chunks_for_sweeps_during_allocation(bool compaction_has_been_done_this_gc) {
//	unsigned int num_active_chunks = 0;

    // GC can create new chunks so adjust _free_chunk_end_index as needed.
    while (_gc_chunks[_free_chunks_end_index].chunk != NULL) {
         _free_chunks_end_index++;
 	}
	for (int chunk_index = 0; chunk_index <= _free_chunks_end_index; chunk_index++) {

		// chunks can be null if they have been returned to the block store previously
		if (_gc_chunks[chunk_index].chunk) {

			block_info *block = _gc_chunks[chunk_index].chunk;

			bool free_chunk = (block->get_nursery_status() == free_nursery);
			bool active_chunk = (block->get_nursery_status() == active_nursery);
			bool spent_chunk = (block->get_nursery_status() == spent_nursery);

			assert(free_chunk || active_chunk || spent_chunk);
			nursery_state chunk_state = block->get_nursery_status();
			//void *owner_thread_of_chunk = NULL;

/*
			if (active_chunk) {
				num_active_chunks++;
				assert(block->thread_owner);
				assert(orp_get_thread_alloc_chunk(block->thread_owner) == block);
				assert(orp_get_thread_curr_alloc_block(block->thread_owner));

				owner_thread_of_chunk = block->thread_owner;
				// Lets repoint the thread to allocate from the first block in this chunk
				orp_set_thread_alloc_chunk(block->thread_owner, block);
				orp_set_thread_curr_alloc_block(block->thread_owner, block);
			}
*/
			while (block) {

				// Each block in a chunk should have the same status
   				assert(block->get_nursery_status() == chunk_state);

#if 0
				// Should have been swept in the mutator execution cycle before the current ongoing GC
				// ONLY IF IT WAS SPENT AND RETURNED TO THE SYSTEM
				if (spent_chunk) {
					// Well gc_force_gc() might make this untrue
//					assert(block->block_has_been_swept == true);
				}
#endif

				if (block->get_nursery_status() == spent_nursery) {
					// Convert it to be allocatable since we will sweep only later
					block->set_nursery_status(spent_nursery,free_nursery);
				}

				// If any compaction has happened in this GC
				if (compaction_has_been_done_this_gc && is_compaction_block(block)){
					// Compacted blocks will have already been swept.
					assert(block->block_has_been_swept == true);
				} else {
					block->age = 1;
				    block->block_has_been_swept = false;
				}
/*
				if (active_chunk) {
					assert(block->thread_owner);
					assert(owner_thread_of_chunk == block->thread_owner);
				}
*/
				block = block->next_free_block;
			}

			// Make the chunk allocatable by the mutators by rehooking free_chunk to chunk
			// ONLY IF IT IS NOT OWNED CURRENTLY BY SOME THREAD
			if (!active_chunk) {
				_gc_chunks[chunk_index].free_chunk = _gc_chunks[chunk_index].chunk;
			}

		} else {
			assert(_gc_chunks[chunk_index].free_chunk == NULL);
		}

	} // for

#ifdef _DEBUG
	// Make sure that all the other chunk entries are empty.
	for (int j = _free_chunks_end_index + 1; j < GC_MAX_CHUNKS; j++) {
		assert(_gc_chunks[j].chunk == NULL);
		assert(_gc_chunks[j].free_chunk == NULL);
	}
#endif // _DEBUG

#if 0
	if (stats_gc) {
		orp_cout << "num_active_chunks discovered by GC is " << num_active_chunks << std::endl;
	}
#endif
}

void Garbage_Collector::parallel_clear_mark_bit_vectors(unsigned thread_id) {
	//unsigned int num_active_chunks = 0;

	unsigned int num_mark_bit_vectors_cleared = 0;

	unsigned portion = _free_chunks_end_index / g_num_cpus;
	unsigned start   = portion * thread_id;
	unsigned end     = (portion * (thread_id + 1)) - 1;
	if(thread_id == g_num_cpus - 1) {
		end = _free_chunks_end_index;
	}

	for (unsigned chunk_index = start; chunk_index <= end; chunk_index++) {
		// chunks can be null if they have been returned to the block store previously
		if (_gc_chunks[chunk_index].chunk) {
			block_info *block = _gc_chunks[chunk_index].chunk;
			while (block) {
				assert(block->in_nursery_p);
				if (block->block_has_been_swept == false) {
					GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
					num_mark_bit_vectors_cleared++;
				}
				block = block->next_free_block;
			}
		}
	} // for

	if(thread_id == 0) {
		if (_los_blocks) {
			block_info *block = _los_blocks;
			while (block) {
				assert(block->in_los_p);
				GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
				block = block->next_free_block;
			}
		}

		if (_single_object_blocks) {
			block_info *block = _single_object_blocks;
			while (block) {
				assert(block->is_single_object_block);
				GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
				block = block->next_free_block;
			}
		}
	}

	if (stats_gc) {
		orp_cout << "num_mark_bit_vectors_cleared before GC = " << num_mark_bit_vectors_cleared << std::endl;
	}
}

// This is needed because some blocks may not get swept during  mutuator execution, but their
// mark bit vectors need to be cleared before this GC can start
void Garbage_Collector::clear_all_mark_bit_vectors_of_unswept_blocks() {
//	unsigned int num_active_chunks = 0;

	unsigned int num_mark_bit_vectors_cleared = 0;

	for (int chunk_index = 0; chunk_index <= _free_chunks_end_index; chunk_index++) {
		// chunks can be null if they have been returned to the block store previously
		if (_gc_chunks[chunk_index].chunk) {
			block_info *block = _gc_chunks[chunk_index].chunk;
			while (block) {
#if 0
                if(!block->in_nursery_p) {
                    printf("Block is not in nursery, state is %d\n",block->get_nursery_status());
                }
#endif
				assert(block->in_nursery_p);
				if (block->block_has_been_swept == false) {
					GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
					num_mark_bit_vectors_cleared++;
				}
				block = block->next_free_block;
			}
		}
	} // for

	if (_los_blocks) {
		block_info *block = _los_blocks;
		while (block) {
			assert(block->in_los_p);
			GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
			block = block->next_free_block;
		}
	}

	if (_single_object_blocks) {
	    block_info *block = _single_object_blocks;
		while (block) {
			assert(block->is_single_object_block);
			GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
			block = block->next_free_block;
		}
	}

	if (stats_gc) {
		orp_cout << "num_mark_bit_vectors_cleared before GC = " << num_mark_bit_vectors_cleared << std::endl;
	}
}

inline void *
get_pointer_from_mark_byte_and_bit_index(block_info *block, const uint8 * const p_byte, unsigned int bit_index) {
	uint8 *mark_vector_base = &(block->mark_bit_vector[0]);
	unsigned int obj_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
	Partial_Reveal_Object *p_live_obj = (Partial_Reveal_Object *)
			((POINTER_SIZE_INT)block + GC_BLOCK_INFO_SIZE_BYTES +  (obj_bit_index * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES));
    return p_live_obj;
}


inline unsigned int
get_live_object_size_bytes_at_mark_byte_and_bit_index(block_info *block, const uint8 * const p_byte, unsigned int bit_index) {
	uint8 *mark_vector_base = &(block->mark_bit_vector[0]);
	unsigned int obj_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
	Partial_Reveal_Object *p_live_obj = (Partial_Reveal_Object *)
			((POINTER_SIZE_INT)block + GC_BLOCK_INFO_SIZE_BYTES +  (obj_bit_index * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES));
	unsigned int sz = get_object_size_bytes(p_live_obj);
	return sz;
}


#ifdef CONCURRENT
extern volatile void * g_sweep_ptr;
#endif // CONCURRENT

inline void adjust_free_bits_for_live_object(block_info *block,
									  unsigned &num_free_bits,
									  const uint8 * const p_last_live_byte,
									  const unsigned last_live_byte_bit_index,
									  uint8 *& p_byte,
									  unsigned &bit_index) {
	// Get the size of the last live object (the one preceding the current free area) and jump
	// ahead by that many bytes to get to the REAL start of this free region
	unsigned int sz = get_live_object_size_bytes_at_mark_byte_and_bit_index(block, p_last_live_byte, last_live_byte_bit_index);
	assert(sz >= 4);
	unsigned int rem_obj_sz_bits = sz / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES - 1;	// 1 bit already included...
	// adjust the free area size
	num_free_bits -= rem_obj_sz_bits ;
	// roll ahead the byte and bit index beyond the live object..
	p_byte    += (rem_obj_sz_bits / GC_NUM_BITS_PER_BYTE);
	bit_index += (rem_obj_sz_bits % GC_NUM_BITS_PER_BYTE);
	if (bit_index >= GC_NUM_BITS_PER_BYTE) {
		p_byte++;
		bit_index -= GC_NUM_BITS_PER_BYTE;
	}
}

unsigned int Garbage_Collector::sweep_one_block(block_info *block, sweep_stats &stats) {
	unsigned int block_final_free_areas_bytes = 0;
	set_bit_search_info info;
	unsigned int block_fragment_too_small_to_use = 0;
    unsigned block_num_fragments = 0;

	//assert(!block->is_free_block);

#ifdef CONCURRENT
	nursery_state old_nursery_state = block->get_nursery_status();
	block->set_nursery_status(old_nursery_state,concurrent_sweeper_nursery);
//	block->set_nursery_status(spent_nursery,concurrent_sweeper_nursery);
//    if(block->get_nursery_status() != concurrent_sweeper_nursery) {
//		block->block_free_bytes = 0;
//		return 0;
//	}
#endif // CONCURRENT

	// Clear all the free areas computed during the previous GC
#ifdef CONCURRENT
    if(block->get_nursery_status() == concurrent_sweeper_nursery)
#endif // CONCURRENT
	clear_block_free_areas(block);

	free_area *areas = block->block_free_areas;
	assert(areas);
	unsigned int num_min_free_bits_for_free_area = (GC_MIN_FREE_AREA_SIZE / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES);
	unsigned int curr_area_index = 0;


	uint8 *mark_vector_base = &(block->mark_bit_vector[0]);
	uint8 *p_ceiling = (uint8 *) ((POINTER_SIZE_INT)mark_vector_base + GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);

	// This keeps track of the previous live object if any that was encountered...
	uint8 *p_last_live_byte = NULL;
	unsigned int last_live_byte_bit_index = 0;

	uint8 *p_byte = mark_vector_base;	// start search at the base of the mark bit vector
	unsigned bit_index = 0;
	info.p_ceil_byte = p_ceiling;		// stop searching when we get close the the end of the vector

    stats.blocks_swept++;

	while (true) {	// Keep looping until we have linearly reached the end of the block's mark table...

		info.p_start_byte    = p_byte;
		info.start_bit_index = bit_index;
		// DO the leg work of finding the next set set bit...from the current position
		get_next_set_bit(&info);
#ifndef CONCURRENT
        // The next set bit can validly change in concurrent mode so verification may find new set bits there that
        // weren't before so just diable this verification step in concurrent mode.
		if (verify_gc) {
			verify_get_next_set_bit_code(&info);
		}
#endif // !CONCURRENT
		uint8 *p_byte_with_some_bit_set = info.p_non_zero_byte;
		unsigned int set_bit_index = info.bit_set_index;
		assert(set_bit_index < GC_NUM_BITS_PER_BYTE);
		// if we found a set bit in some byte downstream, it better be within range and really "set".
		assert((p_byte_with_some_bit_set == NULL) ||
			((p_byte_with_some_bit_set >= p_byte)) && (p_byte_with_some_bit_set < p_ceiling) && ((*p_byte_with_some_bit_set & (1 << set_bit_index)) != 0));

		unsigned num_free_bits = 0;

		if (p_byte_with_some_bit_set != NULL) {
			// Some live object was found....make sure this is a valid object and get its size
#ifdef _DEBUG
			unsigned int size = get_live_object_size_bytes_at_mark_byte_and_bit_index(block, p_byte_with_some_bit_set, set_bit_index);
			assert(size >= 4);
#endif // _DEBUG
			// Calculate the size of the free bits initially
			num_free_bits = (unsigned int) ((p_byte_with_some_bit_set - p_byte) * GC_NUM_BITS_PER_BYTE + (set_bit_index) - (bit_index));
#ifdef CONCURRENT
			if(!no_use_sweep_ptr) {
				g_sweep_ptr = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));
			}
			Partial_Reveal_Object *cur_obj = (Partial_Reveal_Object*)GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));;
			if(cur_obj->isMarked()) {
				cur_obj->unmark();
			}
#endif // CONCURRENT
		} else {
			// this is the last free region in this block....
			num_free_bits = (unsigned int) ((p_ceiling - p_byte) * GC_NUM_BITS_PER_BYTE - (bit_index));
		}

#ifdef CONCURRENT
        // Skip creating free areas for active nurseries
        if(block->get_nursery_status() == concurrent_sweeper_nursery) {
#endif // CONCURRENT
		if (num_free_bits >= num_min_free_bits_for_free_area) {
			// We have chanced upon a fairly large free area
			if (p_last_live_byte != NULL) {
				adjust_free_bits_for_live_object(block,num_free_bits,p_last_live_byte,last_live_byte_bit_index,p_byte,bit_index);
			}

			if (num_free_bits >= num_min_free_bits_for_free_area) {
				// FREE region found -- it begins at (p_byte, bit_index)......
				assert((*p_byte & (1 << bit_index)) == 0);
				unsigned int free_region_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
				void *area_base = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block, free_region_bit_index);
				assert(area_base >= GC_BLOCK_ALLOC_START(block));
				unsigned int area_size = num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
				assert(area_size >= GC_MIN_FREE_AREA_SIZE);
				areas[curr_area_index].area_base = area_base;
				assert(areas[curr_area_index].area_base < (void *) ((POINTER_SIZE_INT) block + GC_BLOCK_SIZE_BYTES));
				areas[curr_area_index].area_ceiling = (void *) ((POINTER_SIZE_INT)area_base + area_size - 1);
				assert(areas[curr_area_index].area_ceiling < (void *) ((POINTER_SIZE_INT) block + GC_BLOCK_SIZE_BYTES));
				areas[curr_area_index].area_size = area_size;
				block_final_free_areas_bytes += areas[curr_area_index].area_size;
				if(g_zero_dead) {
					memset(areas[curr_area_index].area_base,0,areas[curr_area_index].area_size);
					areas[curr_area_index].has_been_zeroed = true;
				} else {
					areas[curr_area_index].has_been_zeroed = false;
				}
#ifdef CONCURRENT
				memset(areas[curr_area_index].area_base,0,areas[curr_area_index].area_size);
#endif
				curr_area_index++;
			} else {
                if(num_free_bits) {
				    block_fragment_too_small_to_use += num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
                    ++block_num_fragments;
					if(g_zero_dead) {
						unsigned int free_region_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
						void *free_base = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block, free_region_bit_index);
						unsigned int free_size = num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;

						memset(free_base, 0, free_size);
					}
                }
			}
		} else {
            if(stats_gc && p_last_live_byte != NULL) {
                void *p1 = get_pointer_from_mark_byte_and_bit_index(block, p_last_live_byte, last_live_byte_bit_index);
                void *p2 = get_pointer_from_mark_byte_and_bit_index(block, p_byte_with_some_bit_set, set_bit_index);

                // Get the size of the last live object (the one preceding the current free area) and jump
				// ahead by that many bytes to get to the REAL start of this free region
				unsigned int sz = get_live_object_size_bytes_at_mark_byte_and_bit_index(block, p_last_live_byte, last_live_byte_bit_index);
				assert(sz >= 4);
				unsigned int rem_obj_sz_bits = sz / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES - 1;	// 1 bit already included...
				// adjust the free area size
				num_free_bits -= rem_obj_sz_bits ;
			}
            if(num_free_bits) {
    			block_fragment_too_small_to_use += num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
                ++block_num_fragments;
				if(g_zero_dead) {
					if (p_last_live_byte != NULL) {
						adjust_free_bits_for_live_object(block,num_free_bits,p_last_live_byte,last_live_byte_bit_index,p_byte,bit_index);
					}
					unsigned int free_region_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
					void *free_base = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block, free_region_bit_index);
					unsigned int free_size = num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;

					memset(free_base, 0, free_size);
				}
            }
		}
#ifdef CONCURRENT
        }
#endif // CONCURRENT

		if (p_byte_with_some_bit_set) {
			// Record the live found at the end of the free region....
			p_last_live_byte = p_byte_with_some_bit_set;
			last_live_byte_bit_index = set_bit_index;
			// Roll ahead the byte and bit pointers ahead of the live that was found to be ready for the new search...(iter)
			p_byte = p_byte_with_some_bit_set;
			bit_index = set_bit_index + 1;
			if (bit_index == GC_NUM_BITS_PER_BYTE) {
				bit_index = 0;
				p_byte++;
			}
		} else {
			// we went off the edge
			break;	// DONE
		}

	} // while

	// Done with processing...finish up with the block
	if (curr_area_index == 0) {
		// Didnt find even one free region
		block->num_free_areas_in_block = 0;

#ifndef GC_SLOW_ALLOC
        block->current_alloc_area = -1;
#else
		block->current_alloc_area = 0;
#endif
		block->curr_free    = NULL;
		block->curr_ceiling = NULL;
	} else {
		block->num_free_areas_in_block = curr_area_index;
		// Start allocating at the first one.

#ifndef GC_SLOW_ALLOC
        block->current_alloc_area = -1;
#else
		block->current_alloc_area = 0;
#endif
		block->curr_free    = block->block_free_areas[0].area_base;
		block->curr_ceiling = block->block_free_areas[0].area_ceiling;
	}
#if 0 // // RLH Aug 04 Check explicitly as we try to return the blocks.
	if (block->block_free_areas[0].area_size == GC_BLOCK_ALLOC_SIZE) {
		// Entire block is free
		assert(block->num_free_areas_in_block == 1);
		//block->is_free_block = true;
	} else {
		assert(block->block_free_areas[0].area_size < GC_BLOCK_ALLOC_SIZE);
		//block->is_free_block = false;
	}
#endif // RLH Aug 04

	block->block_free_bytes = block_final_free_areas_bytes;

	// Clear the mark bit vector since we have determined allocation regions already
	GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);

#ifdef CONCURRENT
    if(block->get_nursery_status() == concurrent_sweeper_nursery) {
		if(old_nursery_state == spent_nursery) {
			block->set_nursery_status(concurrent_sweeper_nursery,free_nursery);
		} else {
			block->set_nursery_status(concurrent_sweeper_nursery,old_nursery_state);
		}
	}
//	if(block_final_free_areas_bytes != 61440) {
	if(stats_gc) {
//		printf("Block %d Freed %d Too Small %d\n",block->block_store_info_index,block_final_free_areas_bytes,block_fragment_too_small_to_use);
	}
//	}
#endif // CONCURRENT

    stats.amount_recovered += block_final_free_areas_bytes;
    stats.amount_in_fragments += block_fragment_too_small_to_use;
    stats.num_fragments += block_num_fragments;
	return block_final_free_areas_bytes;
} // Garbage_Collector::sweep_one_block

#ifdef CONCURRENT

extern bool sweeps_during_gc;
extern bool do_not_zero;
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <map>
#else
#include <..\stlport\map>
#endif
extern SynchCriticalSectionHandle moved_objects_cs;

bimap<Partial_Reveal_Object*,ImmutableMoveInfo> g_moved_objects_2;

void gc_copy_to_immutable_nursery(Partial_Reveal_Object *p_obj,
	    						  GC_Nursery_Info * &copy_nursery,
								  unsigned _gc_num)
{
    Partial_Reveal_Object *p_return_object = NULL;
//	return;

#if 1
	orp_synch_enter_critical_section(moved_objects_cs);
	MovedObjectIterator moved_iter;
	MovedObjectInverseIterator moved_inverse_iter;
	moved_iter = g_moved_objects.find(p_obj);
	if(moved_iter != g_moved_objects.end()) {
//		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Moving a moved object whose original move is not finished.\n");
		orp_synch_leave_critical_section(moved_objects_cs);
		return; // don't move objects that haven't finished moving yet
	}
	moved_inverse_iter = g_moved_objects.inverse_find(p_obj);
	if(moved_inverse_iter != g_moved_objects.inverse_end()) {
//		printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Moving a moved target whose original move is not finished.\n");
		orp_synch_leave_critical_section(moved_objects_cs);
		return; // don't move objects that haven't finished moving yet
	}
	orp_synch_leave_critical_section(moved_objects_cs);
#endif

	while(!p_return_object) {
	    Partial_Reveal_Object *frontier = (Partial_Reveal_Object *)copy_nursery->tls_current_free;
		adjust_frontier_to_alignment(frontier,(Partial_Reveal_VTable*)p_obj->vt());
	    POINTER_SIZE_INT new_free = (get_object_size_bytes(p_obj) + (POINTER_SIZE_INT)frontier);

	    if (new_free <= (POINTER_SIZE_INT) copy_nursery->tls_current_ceiling) {
		    copy_nursery->tls_current_free = (void *) new_free;
			p_return_object = frontier;
	    }

	    if (!p_return_object) {
		    // We need a new allocation area. Get the current alloc_block
			block_info *alloc_block = (block_info *)copy_nursery->curr_alloc_block;

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

		        // current_alloc_area will be -1 if the first area has not been used and if it exists is available.
				if ( (alloc_block->num_free_areas_in_block == 0) || ((alloc_block->current_alloc_area + 1) == alloc_block->num_free_areas_in_block) ) {
		            // No areas left in this block get the next one.
				    alloc_block = alloc_block->next_free_block; // Get the next block and loop
		        } else {
				    assert (alloc_block->current_alloc_area < alloc_block->num_free_areas_in_block);
		            break; // This block has been swept and has an untouched alloc block.
				}
			} // end while (alloc_block)

		    if (alloc_block == NULL) {
				// ran through the end of the list of blocks in the chunk
		        copy_nursery->tls_current_ceiling = NULL;
				copy_nursery->tls_current_free = NULL;
				copy_nursery->curr_alloc_block = NULL; // Indicating that we are at the end of the alloc blocks for this chunk.
			} else {
    		    alloc_block->current_alloc_area++; // Get the next currenct area. If it is the first one it will be 0.

			    unsigned int curr_area = alloc_block->current_alloc_area;

				if (alloc_block->block_free_areas[curr_area].has_been_zeroed == false) {
				    gc_trace_block(alloc_block, " Clearing the curr_area in this block.");
					if(!do_not_zero) {
				        // CLEAR allocation areas just before you start using them
					    memset(alloc_block->block_free_areas[curr_area].area_base, 0, alloc_block->block_free_areas[curr_area].area_size);
					}
					alloc_block->block_free_areas[curr_area].has_been_zeroed = true;
				}

			    copy_nursery->tls_current_free = alloc_block->block_free_areas[curr_area].area_base;
				copy_nursery->tls_current_ceiling = alloc_block->block_free_areas[curr_area].area_ceiling;
				copy_nursery->curr_alloc_block = alloc_block;
			}

			if(!copy_nursery->curr_alloc_block) {
				block_info *p_old_chunk = (block_info*)copy_nursery->chunk;

				copy_nursery->chunk = NULL;

				// Allocate a new chunk for this thread's use.
				block_info *p_new_chunk = p_global_gc->p_cycle_chunk(p_old_chunk, true, true, p_old_chunk->thread_owner, (struct GC_Thread_Info*)p_old_chunk->thread_owner);

				if(p_new_chunk) {
					if(p_new_chunk->get_nursery_status() != active_nursery) {
						assert (p_new_chunk->get_nursery_status() == active_nursery);
					}
				}

				copy_nursery->chunk = p_new_chunk;
				copy_nursery->curr_alloc_block = p_new_chunk;
				copy_nursery->tls_current_free = NULL;
				copy_nursery->tls_current_ceiling = NULL;

				if(copy_nursery->chunk == NULL) {
					return;
				}
			}
		}
	}

#ifdef _DEBUG
	char buf[100];
	sprintf(buf,"Relocating %p to %p.",p_obj,p_return_object);
	gc_trace (p_obj, buf);
	gc_trace (p_return_object, buf);
#endif // _DEBUG

    memmove(p_return_object,p_obj,get_object_size_bytes(p_obj));

	// If we move the object higher while in sweeping mode we need to mark it so that the subsequent
	// sweep of that block won't free it.
	if(p_return_object > p_obj) {
		((block_info*)copy_nursery->chunk)->block_immutable_copied_to_this_gc = true;
        mark_header_and_block_atomic(p_return_object);
	}

#if 1
#ifndef NO_IMMUTABLE_UPDATES
//	orp_synch_enter_critical_section(moved_objects_cs);
//	printf("%p\n",p_obj);
	g_moved_objects.insert(std::pair<Partial_Reveal_Object*,ImmutableMoveInfo>(p_obj,ImmutableMoveInfo(p_return_object,_gc_num)));
//	orp_synch_leave_critical_section(moved_objects_cs);
#endif // NO_IMMUTABLE_UPDATES
#endif // 0/1
} // gc_copy_to_immutable_nursery

bool is_block_concurrent_compaction(block_info *block);

unsigned int
Garbage_Collector::sweep_one_block_concurrent(block_info *block,
											  GC_Nursery_Info *copy_to)
{
	unsigned int block_final_free_areas_bytes = 0;
	set_bit_search_info info;
	//unsigned int block_fragment_too_small_to_use = 0;
	unsigned return_value = 0;

	//assert(!block->is_free_block);

    //void *block_bottom  = GC_BLOCK_ALLOC_START(block);
    void *block_ceiling = (void *)((POINTER_SIZE_INT)GC_BLOCK_ALLOC_START(block) + (POINTER_SIZE_INT)GC_BLOCK_ALLOC_SIZE);

	nursery_state old_nursery_state = block->get_nursery_status();
	block->set_nursery_status(old_nursery_state,concurrent_sweeper_nursery);

	bool do_compaction = false;

	// Clear all the free areas computed during the previous GC
    if(block->get_nursery_status() == concurrent_sweeper_nursery) {
		clear_block_free_areas(block);

		if(is_block_concurrent_compaction(block) &&
		   copy_to->chunk &&
		   copy_to->chunk != block) {
		   do_compaction = true;
//		   printf("Compacting block %d\n",block->block_store_info_index);
		}
	}

	unsigned num_moved=0; // num objects moved during this sweep
	if(do_compaction) {
//		printf("block: %d, gc_num: %d, destination: %d\n",block->block_store_info_index,_gc_num,copy_to?((block_info*)copy_to->chunk)->block_store_info_index:-1);
	}

    block->block_immutable_copied_to_this_gc = false;
	block->last_sweep_num_holes = 0;

    if (!block->block_free_areas)  {
        block->size_block_free_areas = GC_MAX_FREE_AREAS_PER_BLOCK(GC_MIN_FREE_AREA_SIZE);
        block->block_free_areas = (free_area *)malloc(sizeof(free_area) * block->size_block_free_areas);
        assert(block->block_free_areas);
    }
	free_area *areas = block->block_free_areas;
	assert(areas);
	unsigned int num_min_free_bits_for_free_area = (GC_MIN_FREE_AREA_SIZE / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES);
	unsigned int curr_area_index = 0;


	uint8 *mark_vector_base = &(block->mark_bit_vector[0]);
	uint8 *p_ceiling = (uint8 *) ((POINTER_SIZE_INT)mark_vector_base + GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);

	// This keeps track of the previous live object if any that was encountered...
//	uint8 *p_last_live_byte = NULL;
//	unsigned int last_live_byte_bit_index = 0;

	uint8 *p_byte = mark_vector_base;	// start search at the base of the mark bit vector
	unsigned bit_index = 0;
	info.p_ceil_byte = p_ceiling;		// stop searching when we get close the the end of the vector

	while (true) {	// Keep looping until we have linearly reached the end of the block's mark table...

		info.p_start_byte = p_byte;
		info.start_bit_index = bit_index;
		// DO the leg work of finding the next set set bit...from the current position
		get_next_set_bit(&info);
		uint8 *p_byte_with_some_bit_set = info.p_non_zero_byte;
		unsigned int set_bit_index = info.bit_set_index;
		assert(set_bit_index < GC_NUM_BITS_PER_BYTE);
		// if we found a set bit in some byte downstream, it better be within range and really "set".
		assert((p_byte_with_some_bit_set == NULL) ||
			((p_byte_with_some_bit_set >= p_byte)) && (p_byte_with_some_bit_set < p_ceiling) && ((*p_byte_with_some_bit_set & (1 << set_bit_index)) != 0));

		unsigned int num_free_bits = 0;
		//unsigned last_object_size;

		if (p_byte_with_some_bit_set != NULL) {
			// Some live object was found....make sure this is a valid object and get its size
			// Calculate the size of the free bits initially
			num_free_bits = (unsigned int) ((p_byte_with_some_bit_set - p_byte) * GC_NUM_BITS_PER_BYTE + (set_bit_index) - (bit_index));
			if(!no_use_sweep_ptr) {
				g_sweep_ptr = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));
			}
			Partial_Reveal_Object *cur_obj = (Partial_Reveal_Object*)GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));;
			if(cur_obj->isMarked()) {
				cur_obj->unmark();
			}
			if(do_compaction && copy_to->chunk) {
				num_moved++;
				gc_copy_to_immutable_nursery(cur_obj,copy_to,_gc_num);
			}
		} else {
			// this is the last free region in this block....
			num_free_bits = (unsigned int) ((p_ceiling - p_byte) * GC_NUM_BITS_PER_BYTE - (bit_index));
		}

#if 0
		if(p_last_live_byte) {
			last_object_size = get_live_object_size_bytes_at_mark_byte_and_bit_index(block, p_last_live_byte, last_live_byte_bit_index);
			assert(last_object_size >= 4);
			unsigned object_size_in_bits = last_object_size / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES - 1;

            if(num_free_bits >= object_size_in_bits) {
			    num_free_bits -= object_size_in_bits;
		    }
		} else {
			last_object_size = 0;
		}
#endif

		if(num_free_bits) {
			block->last_sweep_num_holes++;
		}

		// Skip creating free areas for active nurseries
		if(block->get_nursery_status() == concurrent_sweeper_nursery) {
				// We have chanced upon a fairly large free area
				if (num_free_bits >= num_min_free_bits_for_free_area) {
					// FREE region found -- it begins at (p_byte, bit_index)......
					assert((*p_byte & (1 << bit_index)) == 0);
				void *area_base = get_pointer_from_mark_byte_and_bit_index(block, p_byte, bit_index);
//				unsigned int free_region_bit_index = (unsigned int) ((p_byte - mark_vector_base) * GC_NUM_BITS_PER_BYTE + bit_index);
//				void *area_base = GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block, free_region_bit_index);
					assert(area_base >= GC_BLOCK_ALLOC_START(block));
					unsigned int area_size = num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
					assert(area_size >= GC_MIN_FREE_AREA_SIZE);
					areas[curr_area_index].area_base = area_base;
					assert(areas[curr_area_index].area_base < (void *) ((POINTER_SIZE_INT) block + GC_BLOCK_SIZE_BYTES));
					areas[curr_area_index].area_ceiling = (void *) ((POINTER_SIZE_INT)area_base + area_size - 1);
					if(areas[curr_area_index].area_ceiling >= (void *) ((POINTER_SIZE_INT) block + GC_BLOCK_SIZE_BYTES)) {
						assert(0);
					}
					assert(areas[curr_area_index].area_ceiling < (void *) ((POINTER_SIZE_INT) block + GC_BLOCK_SIZE_BYTES));
					areas[curr_area_index].area_size = area_size;
					areas[curr_area_index].has_been_zeroed = false;
					block_final_free_areas_bytes += areas[curr_area_index].area_size;
                assert((char*)areas[curr_area_index].area_base + areas[curr_area_index].area_size <= (char*)block_ceiling);
					memset(areas[curr_area_index].area_base,0,areas[curr_area_index].area_size);
					curr_area_index++;
				} else {
#ifdef DLG_DEBUG // FIX FIX FIX..only for debug
				void *area_base = get_pointer_from_mark_byte_and_bit_index(block, p_byte, bit_index);
				unsigned int area_size = num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
                if((char*)area_base + area_size <= (char*)block_ceiling) {
				    memset(area_base,0,area_size);
				}
#endif
//				block_fragment_too_small_to_use += num_free_bits * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES;
			}
        }

#if 0
			if (p_last_live_byte != NULL) {
				// roll ahead the byte and bit index beyond the live object..
				p_byte += (last_object_size / GC_NUM_BITS_PER_BYTE);
				bit_index += (last_object_size % GC_NUM_BITS_PER_BYTE);
				if (bit_index >= GC_NUM_BITS_PER_BYTE) {
					p_byte++;
					bit_index -= GC_NUM_BITS_PER_BYTE;
				}
			}
#endif

		if (p_byte_with_some_bit_set) {
//			Partial_Reveal_Object *cur_obj = (Partial_Reveal_Object*)GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));
			unsigned last_object_size = get_live_object_size_bytes_at_mark_byte_and_bit_index(block, p_byte_with_some_bit_set, set_bit_index);
			assert(last_object_size >= 4);
			unsigned object_size_in_bits = last_object_size / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES - 1;

			// Roll ahead the byte and bit pointers ahead of the live that was found to be ready for the new search...(iter)
			p_byte = p_byte_with_some_bit_set;
			bit_index = set_bit_index + object_size_in_bits + 1;
			while (bit_index >= GC_NUM_BITS_PER_BYTE) {
				bit_index -= GC_NUM_BITS_PER_BYTE;
				p_byte++;
			}
		} else {
			// we went off the edge
			break;	// DONE
		}

	} // while

    if(block->get_nursery_status() == concurrent_sweeper_nursery) {
	    // Done with processing...finish up with the block
	    if (curr_area_index == 0) {
		    // Didnt find even one free region
		    block->num_free_areas_in_block = 0;

#ifndef GC_SLOW_ALLOC
            block->current_alloc_area = -1;
#else
		    block->current_alloc_area = 0;
#endif
		    block->curr_free = NULL;
		    block->curr_ceiling = NULL;
	    } else {
		    block->num_free_areas_in_block = curr_area_index;
		    // Start allocating at the first one.

#ifndef GC_SLOW_ALLOC
            block->current_alloc_area = -1;
#else
		    block->current_alloc_area = 0;
#endif
		    block->curr_free = block->block_free_areas[0].area_base;
		    block->curr_ceiling = block->block_free_areas[0].area_ceiling;
	    }

	    block->block_free_bytes = block_final_free_areas_bytes;

		if(old_nursery_state == spent_nursery) {
			block->set_nursery_status(concurrent_sweeper_nursery,free_nursery);
//			++g_num_blocks_available;
			return_value = 1;
		} else {
			block->set_nursery_status(concurrent_sweeper_nursery,old_nursery_state);
		}
	}

	// Clear the mark bit vector since we have determined allocation regions already
//	GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);

	if(stats_gc) {
//		printf("Block %d Freed %d Too Small %d\n",block->block_store_info_index,block_final_free_areas_bytes,block_fragment_too_small_to_use);
	}

	if(do_compaction) {
//		printf("block: %d, gc_num: %d, destination: %d, moved: %d\n",block->block_store_info_index,_gc_num,copy_to?((block_info*)copy_to->chunk)->block_store_info_index:-1,num_moved);
	}

	return return_value;
} // sweep_one_block_concurrent

bool
Garbage_Collector::clear_and_unmark(block_info *block,bool check_only) {
    bool unmarked_something = false;

	set_bit_search_info info;

	uint8 *mark_vector_base = &(block->mark_bit_vector[0]);
	uint8 *p_ceiling = (uint8 *) ((POINTER_SIZE_INT)mark_vector_base + GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);

	uint8 *p_byte = mark_vector_base;	// start search at the base of the mark bit vector
	unsigned bit_index = 0;
	info.p_ceil_byte = p_ceiling;		// stop searching when we get close the the end of the vector

	while (true) {	// Keep looping until we have linearly reached the end of the block's mark table...
		info.p_start_byte    = p_byte;
		info.start_bit_index = bit_index;
		// DO the leg work of finding the next set set bit...from the current position
		get_next_set_bit(&info);
		uint8 *p_byte_with_some_bit_set = info.p_non_zero_byte;
		unsigned int set_bit_index = info.bit_set_index;

		assert(set_bit_index < GC_NUM_BITS_PER_BYTE);
		// if we found a set bit in some byte downstream, it better be within range and really "set".
		assert((p_byte_with_some_bit_set == NULL) ||
			((p_byte_with_some_bit_set >= p_byte)) && (p_byte_with_some_bit_set < p_ceiling) && ((*p_byte_with_some_bit_set & (1 << set_bit_index)) != 0));

		if (p_byte_with_some_bit_set) {
            if(check_only) {
                printf("BIG PROBLEM.  Clear_and_unmark found a block mark bit at the end of idle mode.\n");
            }
			Partial_Reveal_Object *cur_obj = (Partial_Reveal_Object*)GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block,(unsigned int) ((p_byte_with_some_bit_set - mark_vector_base) * GC_NUM_BITS_PER_BYTE + set_bit_index));;
			if(cur_obj->isMarked()) {
//                printf("Found a case where an object was unmarked by clear_and_unmark.\n");
    			cur_obj->unmark();
                unmarked_something = true;
			}

			p_byte = p_byte_with_some_bit_set;
			bit_index = set_bit_index + 1;
			while (bit_index >= GC_NUM_BITS_PER_BYTE) {
				bit_index -= GC_NUM_BITS_PER_BYTE;
				p_byte++;
			}
		} else {
            break;
        }
	} // while

    if(!check_only) {
        GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block);
    }

    return unmarked_something;
} // clear_and_mark

#ifdef _DEBUG

extern void *object1;
extern void *object2;
extern void *object3;
extern void *object4;
extern void *object5;
extern void *object6;
extern void *object7;

#endif

unsigned remove_unmarked_moved_objects(unsigned cur_gc_num) {
	unsigned k=0;
	MovedObjectIterator cur, next;
	next = g_moved_objects.begin();
	while(next != g_moved_objects.end()) {
		cur = next;
		++next;

		if(cur->second.m_gc_move_number + 3 < cur_gc_num || !cur->first->isMarked()) {
#ifdef _DEBUG
#if 0
			if(cur->second.m_gc_move_number + 3 < cur_gc_num && cur->first->isMarked()) {
				if(object1 == cur->first) continue;
				else if(object2 == cur->first) continue;
				else if(object3 == cur->first) continue;
				else if(object4 == cur->first) continue;
				else if(object5 == cur->first) continue;
				else if(object6 == cur->first) continue;
				else if(object7 == cur->first) continue;

				if(object1 == NULL) {
					object1 = cur->first;
					printf("object1 = %p\n",object1);
					continue;
				} else if(object2 == NULL) {
					object2 = cur->first;
					printf("object2 = %p\n",object2);
					continue;
				} else if(object3 == NULL) {
					object3 = cur->first;
					printf("object3 = %p\n",object3);
					continue;
				} else if(object4 == NULL) {
					object4 = cur->first;
					printf("object4 = %p\n",object4);
					continue;
				} else if(object5 == NULL) {
					object5 = cur->first;
					printf("object5 = %p\n",object5);
					continue;
				} else if(object6 == NULL) {
					object6 = cur->first;
					printf("object6 = %p\n",object6);
					continue;
				} else if(object7 == NULL) {
					object7 = cur->first;
					printf("object7 = %p\n",object7);
					continue;
				} else {
					g_moved_objects.erase(cur);
				}
			} else {
				g_moved_objects.erase(cur);
			}
#else
			g_moved_objects.erase(cur);
#endif
#else
			g_moved_objects.erase(cur);
#endif
		} else {
//			printf("remove_unmarked_moved_objects...a moved object was marked.\n");
		}
	}
	return k;
}

#endif // CONCURRENT
