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
#include "descendents.h"
#include "gcv4_synch.h"
#include "compressed_references.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int object_lock_count = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////


//
// Verify slot
//
static void verify_slot(Slot p_slot) {
    if (p_slot.is_null()) {
        return;
    }
    Partial_Reveal_Object *the_obj = p_slot.dereference();
    block_info *target_block_info = GC_BLOCK_INFO(the_obj);
    
    // verify the block is in the heap and not on the free list and 
    // that the target object appears valid.
    assert (the_obj->vt());
    // The class is in the pinned single block large object space

    if (!((target_block_info->in_los_p) 
//        || (target_block_info->in_sos_p)
        )) {
#ifdef NO_GC_V4
		assert (target_block_info->free > the_obj);
#endif // GC_V4
    }
}


void verify_object (Partial_Reveal_Object *p_object, POINTER_SIZE_INT size) {
    Partial_Reveal_VTable *obj_vt = p_object->vt();
    assert (obj_vt); 
    gc_trace(p_object, "Verifying this object.");

    unsigned int *offset_scanner; 
    Slot pp_target_object(NULL);

#ifdef WRITE_BUFFERING
    if(object_get_transaction_info_target((struct ManagedObject*)p_object)){
        uint32 txnOffset = object_get_transaction_info_offset();
        void* txnRecPtr = (void*)((Byte*)p_object + txnOffset);
#ifdef TXN_INFO_DEBUG
        fprintf(stderr,"GC{Verify}: Object %08.8x --> shadowed by --> %08.8x \n",p_object,(*(uint32*)txnRecPtr));
        fflush(stderr);
#endif //TXN_INFO_DEBUG
        pp_target_object.set(txnRecPtr);
        verify_slot(pp_target_object);
    }
#endif // WRITE_BUFFERING
    
    // Loop through slots in array or objects verify what is in the slot
    if (is_array(p_object)) {
        Type_Info_Handle tih = class_get_element_type_info(obj_vt->get_gcvt()->gc_clss);
        if (type_info_is_primitive(tih)) {
            return;
        }
        if (!type_info_is_unboxed(tih)) {
            printf("Don't know how to verify arrays of anything but unboxed.\n");
            assert(0);
        }

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

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                verify_slot(pp_target_object);
				// Move the scanner to the next reference.
				offset_scanner = p_next_ref (offset_scanner);
            }

            // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
            // handle weak refs
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;

            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                verify_slot(pp_target_object);
				// Move the scanner to the next reference.
				offset_scanner = p_next_ref (offset_scanner);
            }
            
            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } // end while for arrays

    // It isn't an array, it is an object.
    offset_scanner = init_object_scanner (p_object);
    while (pp_target_object.set(p_get_ref(offset_scanner, p_object)) != NULL) {
        // Move the scanner to the next reference.
        offset_scanner = p_next_ref (offset_scanner);
        verify_slot (pp_target_object);
    } // end while for objects

    // Make sure the object info field has a valid entry, if it holds a lock update the counter.
    if (verify_object_header((void *)p_object)) {
        object_lock_count++;
    }
    // We are done.
}

//
// Called from sweep code....to make sure that the sweep logic is accurate...
// 

bool verify_consec_bits_using_asm(uint8 *p_byte_start, unsigned int bit_index_to_search_from, unsigned int *num_consec_bits, uint8 *p_ceil) {
#ifndef _IA64_
#ifndef ORP_POSIX

	unsigned int len = 0;
	bool is_zero_str = ((*p_byte_start) & (1 << bit_index_to_search_from)) ? false : true;
	unsigned int max_bits_to_search = (p_ceil - p_byte_start) * GC_NUM_BITS_PER_BYTE - bit_index_to_search_from;

	if (is_zero_str) {
		__asm 
		{
			mov eax, [p_byte_start]
			mov ecx, bit_index_to_search_from
			inc ecx							// start search from the next bit onwards
			mov ebx, max_bits_to_search
			dec ebx							// as above

_SEARCH_FOR_ZERO_STR_:
			clc				
			bt [eax], ecx			
			jc _ALL_DONE_ZERO_STR
			inc ecx							// one more '0'
			dec ebx
			jz _ALL_DONE_ZERO_STR
			jmp _SEARCH_FOR_ZERO_STR_
_ALL_DONE_ZERO_STR:
			mov len, ecx
		}
	} else {
		__asm 
		{
			mov eax, [p_byte_start]
			mov ecx, bit_index_to_search_from	
			inc ecx							
			mov ebx, max_bits_to_search		
			dec ebx

_SEARCH_FOR_ONE_STR_:
			clc				
			bt [eax], ecx			
			jnc _ALL_DONE_ONE_STR
			inc ecx							// one more '1'
			dec ebx
			jz _ALL_DONE_ONE_STR
			jmp _SEARCH_FOR_ONE_STR_
_ALL_DONE_ONE_STR:
			mov len, ecx
		}
	}

	// look at code above....;-)
	*num_consec_bits = len - bit_index_to_search_from;
	return is_zero_str ? false : true;

#endif // ORP_POSIX
#endif // _IA64_

	return false;
}



// This verifies the C code for get_next_set_bit() defined in gc_utils.cpp

void verify_get_next_set_bit_code(set_bit_search_info *info) { 
#ifndef _IA64_
#ifndef ORP_POSIX
	// Verification code only for IA32 windows....

	uint8 *p_byte = info->p_start_byte;
	unsigned int start_bit_index = info->start_bit_index;
	uint8 *p_ceil = info->p_ceil_byte;

	uint8 *p_non_zero_byte = NULL;
	unsigned int set_bit_index = 0;

	__asm {	

				mov eax, [p_byte]			// eax holds byte ptr
				mov ecx, start_bit_index	// ecx holds bit index into byte
				mov edx, [p_ceil]			// limit ptr 

_LOOP_:			clc				
				bt [eax], ecx				// bit test
				jc _FOUND_SET_BIT_			// carry bit contains the bit value at eax[ecx]
				add ecx, 1					// '0' ...,move to next bit
				cmp ecx, GC_NUM_BITS_PER_BYTE					
				jge _INCR_BYTE_PTR_
				jmp _LOOP_

_INCR_BYTE_PTR_:add eax, 1					// into next byte..increment byte ptr...
				mov ecx, 0					// start search at bit index 0
				cmp eax, edx				// have we reached p_ceil ?
				jge _HIT_THE_CEILING_	
				jmp _LOOP_

_FOUND_SET_BIT_:mov [p_non_zero_byte], eax	// found a '1' within range...
				mov set_bit_index, ecx
				jmp _ALL_DONE_
				
_HIT_THE_CEILING_:
				mov [p_non_zero_byte], 0	// hit p_ceil
				mov set_bit_index, 0
				
_ALL_DONE_:		
	}

	// CHECKS
	if(p_non_zero_byte != info->p_non_zero_byte) {
		assert(p_non_zero_byte == info->p_non_zero_byte);
	}
	if(set_bit_index != info->bit_set_index) {
		assert(set_bit_index == info->bit_set_index);
	}

	if ((p_non_zero_byte != info->p_non_zero_byte) || (set_bit_index != info->bit_set_index)) {
		orp_cout << "verify_get_next_set_bit_code() failed.....\n";
		orp_exit(17040);
	}

	return;

#endif // ORP_POSIX
#endif // _IA64_
}


#ifndef REMEMBERED_SET_ARRAY
void Block_Store::verify_no_duplicate_slots_into_compaction_areas() {
	for (unsigned int i = 0; i < _number_of_blocks_in_heap; i++) {

		if (_blocks_in_block_store[i].is_compaction_block == true) {

			assert(_blocks_in_block_store[i].block->in_nursery_p);
			assert(_blocks_in_block_store[i].block->is_compaction_block);

			Remembered_Set *test_rem_set = new Remembered_Set();
			// verify that there are no duplicate slots into this block
			for (unsigned int j = 0; j < g_num_cpus; j++) {
				Remembered_Set *some_slots = _blocks_in_block_store[i].per_thread_slots_into_compaction_block[j];
				if (some_slots) {
					some_slots->rewind();
					Slot one_slot(NULL);
					while ((one_slot.set(some_slots->next().get_value()))) {
						if (test_rem_set->is_present(one_slot)) {
							// this is BAD!!!
							orp_cout	<< "verify_no_duplicate_slots_into_compaction_areas(): found duplicate slot -- " 
										<< one_slot.get_value() << " into compaction block : " << _blocks_in_block_store[i].block << std::endl;
							assert(0);
							orp_exit(17041);
						} else {
							// keep accumulating unique slots...
							test_rem_set->add_entry(one_slot);
						}
					} // while for one rem set
					some_slots->rewind();
				} // if there exist some slots
			} // for (num_cpus)
			// done with the use of this
			delete test_rem_set;
		}
	} // for blocks inside compaction limits
	// DONE!!!!
} // verify_no_duplicate_slots_into_compaction_areas()
#endif


// ***SOB LOOKUP*** Do the lookup in the _blocks_in_block_store. 

bool Garbage_Collector::obj_belongs_in_single_object_blocks(Partial_Reveal_Object *p_obj) {
    unsigned int block_store_block_number = (unsigned int) (((POINTER_SIZE_INT) p_obj - (POINTER_SIZE_INT) get_gc_heap_base_address()) >> GC_BLOCK_SHIFT_COUNT);
    bool new_result = _p_block_store->is_single_object_block(block_store_block_number);
#if 0
#ifdef _DEBUG
    // This is the old way we used to do this check. Remove in sept 03 if we don't see any problems.
    for (block_info *block = _single_object_blocks; block != NULL; block = block->next_free_block) {
        assert (block->is_single_object_block);
        if (p_obj == GC_BLOCK_ALLOC_START(block)) {
    	    assert (new_result);
            return true;
        }
    }
    assert (!new_result);
#endif
#endif

    return new_result;
}
