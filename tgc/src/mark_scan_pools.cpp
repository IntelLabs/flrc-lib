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
#include "tgc/gc_thread.h"
#include "tgc/mark.h"
#include "tgc/descendents.h"
#include "tgc/gcv4_synch.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//#ifdef GC_MARK_SCAN_POOLS

#ifdef IGNORE_SOME_ROOTS
extern unsigned num_roots_ignored;
extern unsigned roots_to_ignore;
#endif // IGNORE_SOME_ROOTS

// Convert all roots into packets and drop them into the mark scan pool....
void Garbage_Collector::setup_mark_scan_pools() {
	assert(_mark_scan_pool);

	unsigned int num_root_objects_per_packet = _num_roots / g_num_cpus;

	Work_Packet *wp = _mark_scan_pool->get_output_work_packet();
	assert(wp);

	unsigned int roots_in_this_packet = 0;

	for (unsigned int i = 0; i < _num_roots; i++) {
		Partial_Reveal_Object **slot = _array_of_roots[i];
		if (*slot == NULL) {
			continue;
		}
		// Mark the object.
		if (mark_object_in_block(*slot)) {
			// Check if packet is empty
			if ((wp->is_full() == true) || (num_root_objects_per_packet == roots_in_this_packet)) {
				// return this packet and get a new output packet
				_mark_scan_pool->return_work_packet(wp);
				wp = _mark_scan_pool->get_output_work_packet();
				roots_in_this_packet = 0;
			}
			assert(wp && !wp->is_full());
			wp->add_unit_of_work(*slot);
			roots_in_this_packet++;
		}
		// the object may have a already been marked if an earlier root pointed to it.
		// In this case we dont duplicate this object reference in the mark scan pool
	}
	// return the retained work packet to the pool
	assert(wp);
	_mark_scan_pool->return_work_packet(wp);
}



void mark_all_mark_bits_for_this_object(Partial_Reveal_Object *p_obj) {
	assert(0);
}


void Garbage_Collector::scan_one_slot (Slot p_slot, GC_Thread *gc_thread) {
	assert(p_slot.get_value());

	if (p_slot.is_null()) {
		return;
	}
	Partial_Reveal_Object *p_obj = p_slot.dereference();

    REMOVE_INDIR_RES rir;
	while((rir = remove_one_indirection(p_obj, p_slot, 0)) == RI_REPLACE_OBJ);
    if(rir == RI_REPLACE_NON_OBJ) {
        // New value is not a pointer so return.
        return;
    }

	gc_trace(p_obj, " scan_one_slot(): some slot pointed to this object...\n");

	Work_Packet *o_wp = gc_thread->get_output_packet();
	assert(o_wp);

#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(p_obj) ) {
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP

    // I will try to be the first to mark it. If success..I will add it to my output packet
	if (mark_object_in_block(p_obj)) {
		if (o_wp->is_full() == true) {
			// output packet is full....return to pool and a get a new output packet
			_mark_scan_pool->return_work_packet(o_wp);
			gc_thread->set_output_packet(NULL);
			o_wp = _mark_scan_pool->get_output_work_packet();
			gc_thread->set_output_packet(o_wp);
		}
		assert(o_wp && !o_wp->is_full());
		o_wp->add_unit_of_work(p_obj);
		gc_thread->add_to_marked_objects(p_obj);
	}

	if (gc_thread->is_compaction_turned_on_during_this_gc()) {	// Collect slots as we go
	    block_info *p_obj_block_info = GC_BLOCK_INFO(p_obj);
		if (gc_thread->_p_gc->is_compaction_block(p_obj_block_info)) {
			gc_thread->_p_gc->add_slot_to_compaction_block(p_slot, p_obj_block_info, gc_thread->get_id());
			if (stats_gc) {
				gc_thread->increment_num_slots_collected_for_later_fixing();
			}
		}
	}
}

void Garbage_Collector::mark_scan_pools(GC_Thread *gc_thread) {
	// thread has no output packet yet
	assert(gc_thread->get_output_packet() == NULL);

	unsigned int num_objects_scanned_by_thread = 0;

	// get a thread a new output packet
	Work_Packet *o_wp = _mark_scan_pool->get_output_work_packet();
	assert(o_wp);
	gc_thread->set_output_packet(o_wp);

	while (true) {
		// try to get work-packet from common mark/scan pool
		Work_Packet *i_wp = _mark_scan_pool->get_input_work_packet();

		if (i_wp == NULL) {
			//...return output packet if non-empty
			Work_Packet *o_wp = gc_thread->get_output_packet();
			if (o_wp->is_empty() == false) {
				_mark_scan_pool->return_work_packet(o_wp);
				gc_thread->set_output_packet(NULL);
				o_wp = _mark_scan_pool->get_output_work_packet();
				gc_thread->set_output_packet(o_wp);
				continue;	// try to get more work
			} else {
				_mark_scan_pool->return_work_packet(o_wp);
				gc_thread->set_output_packet(NULL);

				// NO TILL NO WORK......leave..or else keep working...
				bool there_is_work = wait_till_there_is_work_or_no_work();
				if (there_is_work == false) {
					// No more work left.....
					break;
				} else {
					o_wp = _mark_scan_pool->get_output_work_packet();
					gc_thread->set_output_packet(o_wp);
					// there seems to be some work...try again...
					continue;
				}
			}
		}
		if (i_wp->is_empty() == true) {
			// try to get some real work again..
			continue;
		}

		assert(gc_thread->get_input_packet() == NULL);
		gc_thread->set_input_packet(i_wp);

		// iterate through the work packet
		i_wp->init_work_packet_iterator();
		Partial_Reveal_Object *p_obj = NULL;

		while ((p_obj = (Partial_Reveal_Object *)i_wp->remove_next_unit_of_work())) {

			// Object had better be marked....since it is grey
			assert(is_object_marked(p_obj) == true);

			num_objects_scanned_by_thread++;

#if 1
    if (p_obj->vt()->get_gcvt()->gc_object_has_slots) {
        if (is_array(p_obj)) {
            Type_Info_Handle tih = class_get_element_type_info(p_obj->vt()->get_gcvt()->gc_clss);
            if(type_info_is_reference(tih) ||
               type_info_is_vector(tih) ||
               type_info_is_general_array(tih)) {
                // Initialize the array scanner which will scan the array from the
                // top to the bottom. IE from the last element to the first element.

                int32 array_length = vector_get_length_with_vt((Vector_Handle)p_obj,p_obj->vt());
                for (int32 i=array_length-1; i>=0; i--) {
                    Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_obj, i, p_obj->vt()));
                    scan_one_slot(p_element, gc_thread);
                }
            } else if(type_info_is_primitive(tih)) {
                // intentionally do nothing
            } else if(type_info_is_unboxed(tih)) {
                Class_Handle ech = type_info_get_class(tih);
                assert(ech);
                int first_elem_offset = vector_first_element_offset_unboxed(ech);
                int base_offset = (int)class_get_unboxed_data_offset(ech);
                int elem_size = class_element_size(p_obj->vt()->get_gcvt()->gc_clss);
                int array_length = vector_get_length_with_vt((Vector_Handle)p_obj,p_obj->vt());
                Partial_Reveal_VTable *evt = (Partial_Reveal_VTable *)class_get_vtable(ech);
                unsigned int *offset_scanner = NULL;
                // Offsets assume that the object starts with vtable pointer.  We'll set our
                // fake object to scan 4 bytes before the start of the value type to adjust.
                void *cur_value_type_entry_as_object = (Byte*)p_obj + first_elem_offset - base_offset;
                for(int i = 0; i < array_length; i++) {
                    // taken from init_object_scanner except based on Partial_Reveal_VTable rather than object
                    offset_scanner = evt->get_gcvt()->gc_ref_offset_array;
                    Slot pp_target_object(NULL);
                    while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                        scan_one_slot (pp_target_object, gc_thread);
                        // Move the scanner to the next reference.
                        offset_scanner = p_next_ref (offset_scanner);
                    }
                    // advance to the next value struct
                    cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
                }
            } else assert(!"Tried to scan an array of unknown internal type.");
        }

		unsigned int *offset_scanner = init_object_scanner (p_obj);
        Slot pp_target_object(NULL);
        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            scan_one_slot (pp_target_object, gc_thread);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }
#else
#ifdef WRITE_BUFFERING
            if(object_get_transaction_info_target((struct ManagedObject*)p_obj)){
                uint32 txnOffset = object_get_transaction_info_offset();
                void* txnRecPtr = (void*)((Byte*)p_obj + txnOffset);
#ifdef SCAN_TXN_INFO_DEBUG
                fprintf(stderr,"GC{Scan}: Object %08.8x --> shadowed by --> %08.8x \n",p_object,(*(uint32*)txnRecPtr));
                fflush(stderr);
#endif //SCAN_TXN_INFO_DEBUG
                Slot txnInfoSlot(txnRecPtr);
                // Create a slot for the txn_info, and scan it!
                scan_one_slot(txnInfoSlot,gc_thread);
            }
#endif // WRITE_BUFFERING

            if (p_obj->vt()->get_gcvt()->gc_object_has_slots) {
				if (is_array(p_obj)) {
					if (is_array_of_primitives(p_obj)) {
						break;	// no references here..
					}
					int32 array_length = vector_get_length_with_vt((Vector_Handle)p_obj,p_obj->vt());
                        for (int32 i = array_length - 1; i >= 0; i--)	{
                            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_obj, i, p_obj->vt()));
                            scan_one_slot(p_element, gc_thread);
                        }
                } else {
                    // not an array
                    unsigned int *offset_scanner = init_object_scanner (p_obj);
                    Slot pp_target_object(NULL);
                    while (pp_target_object.set(p_get_ref(offset_scanner, p_obj)) != NULL) {
                        // Move the scanner to the next reference.
                        offset_scanner = p_next_ref (offset_scanner);
                        scan_one_slot (pp_target_object, gc_thread);
                    }
                }
            } // if
#endif
		} // while (unit)

		// we have run through the input work packet...lets return it to the shared pool
		assert(i_wp->is_empty());
		_mark_scan_pool->return_work_packet(i_wp);
		assert(gc_thread->get_input_packet() == i_wp);
		gc_thread->set_input_packet(NULL);

	} // while(true)

	// we need to return any input and output packets that we still hold
	assert(gc_thread->get_input_packet() == NULL);
	assert(gc_thread->get_output_packet() == NULL);

	if (stats_gc) {
		printf("thread -- %d : scanned %d objects...\n", gc_thread->get_id(), num_objects_scanned_by_thread);
	}
}

//#endif // 	GC_MARK_SCAN_POOLS

void Garbage_Collector::_verify_gc_threads_state() {
	for (unsigned int i = 0; i < g_num_cpus ; i++) {
		GC_Thread *gc_thr = _gc_threads[i];
		if (gc_thr->get_input_packet() != NULL) {
			printf("GC thread %d has remaining possibly unprocessed input packet with %d units of work...\n", gc_thr->get_id(), gc_thr->get_input_packet()->get_num_work_units_in_packet());
			orp_exit(17021);
		}
		if (gc_thr->get_output_packet() != NULL) {
			printf("GC thread %d has remaining possibly unprocessed output packet with %d units of work...\n", gc_thr->get_id(), gc_thr->get_output_packet()->get_num_work_units_in_packet());
			orp_exit(17022);
		}
	}
}
