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

#ifndef _block_store_H_
#define _block_store_H_

#include "tgc/gc_plan.h"
#include "tgc/gc_header.h"
#include "tgc/gc_globals.h"
#include "tgc/gc_v4.h"
#include "tgc/compressed_references.h"
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <set>
#else
#include <..\stlport\set>
#endif

extern void *_p_heap_base;
extern void *_p_heap_ceiling;
extern POINTER_SIZE_INT _heap_size_bytes;

class block_store_info {
public:
    // Will be turned on by the GC if this block needs to be sliding compacted.
    // If the block is pinned this had better not be set.
    bool is_compaction_block;
    bool is_single_object_block;

    // Set to true once this block has been evacuated of all of its "from" objects.
    // bool from_block_has_been_evacuated; This is maintained in the block->from_block_has_been_evacuated

	// Used during mark/scan when slots are inserted on a per block basis
	Remembered_Set **per_thread_slots_into_compaction_block;

    unsigned int bit_index_into_all_lives_in_block;

    unsigned int total_live_objects_in_this_block;

    // Is the block free ? i.e. can the block store hand it out to a requesting thread??
    bool block_is_free;

    // If this is a super-block, then it means the number of sub-blocks that it holds
    unsigned int number_of_blocks;

    // Points to the actual heap block that it points to
    block_info *block;

    // Clobbered when blocks are handed out during parallel allocation pointer computation phase
    block_info *block_for_sliding_compaction_allocation_pointer_computation;

    // Clobbered when blocks are handed out during parallel slot updates phase
    block_info *block_for_sliding_compaction_slot_updates;

    // Clobbered when blocks are handed out during parallel object slides phase
    block_info *block_for_sliding_compaction_object_slides;
};


extern void get_next_set_bit(set_bit_search_info *info);
extern unsigned int get_live_object_size_bytes_at_mark_byte_and_bit_index(block_info *block, const uint8 * const p_byte, unsigned int bit_index);

class Block_Store {
public:

    Block_Store(POINTER_SIZE_INT initial_heap_size, POINTER_SIZE_INT final_heap_size, unsigned int block_size_bytes);

    virtual ~Block_Store();

    block_info *p_get_multi_block(unsigned int size, bool for_chunks_pool, bool do_alloc = true);

    void link_free_blocks (block_info *freed_block, unsigned int blocks, bool has_block_store_lock=false);

    void coalesce_free_blocks();

    inline unsigned int get_num_free_blocks_in_block_store() {
        return _num_free_blocks;
    }

    inline unsigned int get_num_total_blocks_in_block_store() {
        return _number_of_blocks_in_heap;
    }

    void characterize_blocks_in_heap ();

    block_info *p_get_new_block(bool);

    inline unsigned int get_block_store_low_watermark_free_blocks() {
        unsigned int reserve;

        // 0.5% of the total heap blocks....but at least 16 blocks ** was .005 ***
        reserve = (unsigned int) (.005 * (float) _number_of_blocks_in_heap);
        return (reserve >= 16) ? reserve : 16;
    }

    inline unsigned int get_block_size_bytes() {
        return _block_size_bytes;
    }

    bool block_store_can_satisfy_request(unsigned int);

    //////////////////// S L I D I N G   C O M P A C T I O N ///////////////////////////////////

    inline void set_compaction_type_for_this_gc(gc_compaction_type comp_type) {
        _compaction_type_for_this_gc = comp_type;
    }

    void determine_compaction_areas_for_this_gc(std::set<block_info *> &pinned_blocks);

    void reset_compaction_areas_after_this_gc();

    void disable_compaction(int block_store_info_index) {
        _blocks_in_block_store[block_store_info_index].is_compaction_block = false;
    }

    inline bool is_compaction_block(block_info *block) {
#ifdef _DEBUG
        unsigned int block_store_block_number =
            (unsigned int) (((POINTER_SIZE_INT) block - (POINTER_SIZE_INT) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
        assert(block_store_block_number == block->block_store_info_index);
        assert(_blocks_in_block_store[block_store_block_number].is_compaction_block == block->is_compaction_block);
#endif // _DEBUG
        return block->is_compaction_block;
    }

    void init_live_object_iterator_for_block(block_info *block);

    Partial_Reveal_Object *get_next_live_object_in_block(block_info *block);

    inline void add_slot_to_compaction_block(Slot p_slot, block_info *p_obj_block, unsigned int gc_thread_id) {
        if (p_slot.is_null()) {
            return;
        }
        unsigned int block_store_block_number =
            (unsigned int) (((uintptr_t) p_obj_block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
        assert(_blocks_in_block_store[block_store_block_number].is_compaction_block);	// we are collecting slots into compacted block
        Remembered_Set *slots_list =
            _blocks_in_block_store[block_store_block_number].per_thread_slots_into_compaction_block[gc_thread_id];
        if (slots_list == NULL) {
            _blocks_in_block_store[block_store_block_number].per_thread_slots_into_compaction_block[gc_thread_id] = slots_list =
                new Remembered_Set();
        }
        slots_list->add_entry(p_slot);
        //gc_trace (*p_slot, "Slot pointing to this object added to compation block remset.");

	}

	inline unsigned int get_total_live_objects_in_this_block(block_info *block)	{
		unsigned int block_store_block_number =
						(unsigned int) (((uintptr_t) block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);
		assert(block_store_block_number == block->block_store_info_index);
		return _blocks_in_block_store[block_store_block_number].total_live_objects_in_this_block;
	}


	block_info *get_block_for_sliding_compaction_allocation_pointer_computation(unsigned int, block_info *);

	block_info *get_block_for_fix_slots_to_compaction_live_objects(unsigned int, block_info *);

	Remembered_Set *get_slots_into_compaction_block(block_info *);

	block_info *get_block_for_slide_compact_live_objects(unsigned int, block_info *);

	block_info *iter_get_next_compaction_block_for_gc_thread(unsigned int, block_info *, void *owner, bool search_for_owner=false);

	//////////////////////////////// V E R I F I C A T I O N //////////////////////////////////////////////

#ifndef REMEMBERED_SET_ARRAY
	void verify_no_duplicate_slots_into_compaction_areas();
#endif

	bool block_is_invalid_or_free_in_block_store(block_info *block) {
		unsigned int block_store_block_number = (unsigned int) (((uintptr_t) block - (uintptr_t) _p_heap_base) >> GC_BLOCK_SHIFT_COUNT);

        if (block_store_block_number != block->block_store_info_index) {
            // This isn't even a valid block so return that it is free.
            return true;
        }
        return _blocks_in_block_store[block_store_block_number].block_is_free;
    }


    ///////////////////////////////////////////////////////////////////////////////////////////////////////

    inline void *get_gc_heap_base_address() {
        return _p_heap_base;
    }

    inline void *get_gc_heap_ceiling_address() {
        return _p_heap_ceiling;
    }

    inline bool is_in_heap(void *addr) {
#if 0
        return (addr >= _p_heap_base) && (addr < _p_heap_ceiling);
#else
        return ((uintptr_t)addr - (uintptr_t)_p_heap_base) < (POINTER_SIZE_INT)_heap_size_bytes;
#endif
    }

    inline bool is_single_object_block(unsigned int i) {
        return _blocks_in_block_store[i].is_single_object_block;
    }

    block_info * get_block_info(unsigned int block_store_info_index) {
        return _blocks_in_block_store[block_store_info_index].block;
    }

    // Block iterator code,
    // This can be used in a stop the world setting to iterate through all ther block sequentially.
    void init_block_iterator() {
        current_block_index = 0;
    }
    // This retrieves the next block after init_block_iterator is called or NULL if there are no more blocks.
    block_info *get_next_block() {
        if (current_block_index >= _number_of_blocks_in_heap) {
            return NULL;
        }
        block_store_info *store_info = &_blocks_in_block_store[current_block_index];
        block_info *info = store_info->block;
        if (store_info->is_single_object_block) {
            current_block_index += info->number_of_blocks;
            assert (store_info->number_of_blocks != 1);
            assert(info->is_single_object_block);
        } else {
            current_block_index++;
        }
        return info;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////

    void get_block_type_stats(unsigned &regular, unsigned &single, unsigned &los) const {
        regular=0;
		single=0;
		los=0;
		for(unsigned int i=0;i<_number_of_blocks_in_heap;++i) {
			if(!_blocks_in_block_store[i].block) {
				continue;
			}
            if(_blocks_in_block_store[i].block->in_los_p) {
                ++los;
            } else if(_blocks_in_block_store[i].block->is_single_object_block) {
                ++single;
            } else if(_blocks_in_block_store[i].block->in_nursery_p) {
                ++regular;
            }
		}
    }

	void print_block_stats(void) const {
        unsigned regular=0,single=0,los=0;
        unsigned rf=0,ru=0,sf=0,su=0,lf=0,lu=0;
        unsigned free_flag=0;
		for(unsigned int i=0;i<_number_of_blocks_in_heap;++i) {
            if(_blocks_in_block_store[i].block_is_free) {
                ++free_flag;
            }
			if(!_blocks_in_block_store[i].block) {
				continue;
			}
            if(_blocks_in_block_store[i].block->in_los_p) {
                ++los;
                lf += _blocks_in_block_store[i].block->block_free_bytes;
                lu += _blocks_in_block_store[i].block->block_used_bytes;
            } else if(_blocks_in_block_store[i].block->is_single_object_block) {
                ++single;
                sf += _blocks_in_block_store[i].block->block_free_bytes;
                su += _blocks_in_block_store[i].block->block_used_bytes;
            } else if(_blocks_in_block_store[i].block->in_nursery_p) {
                ++regular;
                rf += _blocks_in_block_store[i].block->block_free_bytes;
                ru += _blocks_in_block_store[i].block->block_used_bytes;
            }
		}
        printf("Block information: regular = %d, single = %d, los = %d, used = %d, total = %d\n",regular,single,los,regular+single+los,_number_of_blocks_in_heap);
        printf("Regular - free = %d, used = %d\n",rf,ru);
        printf("Single  - free = %d, used = %d\n",sf,su);
        printf("LOS     - free = %d, used = %d\n",lf,lu);
        printf("Free = %d, free flag = %d\n",_num_free_blocks,free_flag);
    }

    void print_age_stats(void) const {
		unsigned young=0,old=0;
		for(unsigned int i=0;i<_number_of_blocks_in_heap;++i) {
			if(_blocks_in_block_store[i].block->age) {
				++old;
			} else {
				++young;
			}
		}
		printf("Elder generation = %d blocks, Ephemeral generation = %d blocks\n",old,young);
	}

	void print_survivability(void) const {
		unsigned num_surviving = 0, sum_surviving = 0;

		for(unsigned int i=0;i<_number_of_blocks_in_heap;++i) {
			if(_blocks_in_block_store[i].block->age == 0) {
				set_bit_search_info info;

				uint8 *mark_vector_base = &(_blocks_in_block_store[i].block->mark_bit_vector[0]);
				uint8 *p_ceiling = (uint8 *) ((uintptr_t)mark_vector_base + GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);

				uint8 *p_byte = mark_vector_base;	// start search at the base of the mark bit vector
				unsigned bit_index = 0;
				info.p_ceil_byte = p_ceiling;		// stop searching when we get close to the end of the vector

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

					if (p_byte_with_some_bit_set != NULL) {
						num_surviving++;
						sum_surviving += get_live_object_size_bytes_at_mark_byte_and_bit_index(_blocks_in_block_store[i].block, p_byte_with_some_bit_set, set_bit_index);
						p_byte = p_byte_with_some_bit_set;
						bit_index = set_bit_index + 1;
						if (bit_index == GC_NUM_BITS_PER_BYTE) {
							bit_index = 0;
							p_byte++;
						}
					} else {
						break;
					}
				}
			}
		}
		printf("Num surviving from ephemeral = %d, Size = %d\n",num_surviving,sum_surviving);
	}

private:

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void get_compaction_limits_based_on_compaction_type_for_this_gc(unsigned int *, unsigned int *);

    void _initialize_block_tables(void *p_start, POINTER_SIZE_INT heap_size_in_bytes);

    void set_free_status_for_all_sub_blocks_in_block(unsigned int, bool);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned int _block_size_bytes;

    unsigned int _number_of_blocks_in_heap;

    block_store_info _blocks_in_block_store[GC_MAX_BLOCKS];

    unsigned int _num_free_blocks;

    unsigned int _free_block_search_hint;

    ////////// C O M P A C T I O N  S T A T E ////////////////////////////////////////////////////////////////////////////////////

    unsigned int _heap_compaction_ratio;

    gc_compaction_type _compaction_type_for_this_gc;

    unsigned int _compaction_blocks_low_index;

    unsigned int _compaction_blocks_high_index;

    // NEW for cross-block compaction
    unsigned int *_per_gc_thread_compaction_blocks_low_index;
    unsigned int *_per_gc_thread_compaction_blocks_high_index;

    // Block iterator code
    unsigned int current_block_index;

public:
	unsigned get_heap_compaction_ratio(void) { return _heap_compaction_ratio; }
};

#endif // _Block_Store_H_
