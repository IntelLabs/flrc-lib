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
#include "tgc/work_packet_manager.h"
#include "tgc/gc_thread.h"
#include "tgc/object_list.h"
#include "tgc/garbage_collector.h"
#include "tgc/descendents.h"
#include "tgc/mark.h"
#include "tgc/gc_plan.h"
#include "tgc/gc_globals.h"
#include "tgc/gcv4_synch.h"

#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <map>
#else
#include <..\stlport\map>
#endif

//////////////////////////////////  V E R I F Y ////////////////////////////////


int num_marked = 0;
#ifdef IGNORE_SOME_ROOTS
extern unsigned num_roots_ignored;
extern unsigned roots_to_ignore;
#endif // IGNORE_SOME_ROOTS

static inline bool mark_object_header(Partial_Reveal_Object *p_obj) {
	if(!p_obj->isMarked()) {
		p_obj->mark();
		return true;
	} else {
		return false;
	}
}

#ifdef WRITE_BUFFERING
    /* STB 10-Aug-2007: The transaction_info field of the object may point to a
     * shadow copy or inflated transaction record. This method detects that case and
     * treats it like any other slot.
     */

static inline Partial_Reveal_Object*
get_transaction_info_target(Partial_Reveal_Object *p_object){
    Partial_Reveal_Object* childObj = (Partial_Reveal_Object*) object_get_transaction_info_target((struct ManagedObject*)p_object);
#ifdef VERIFY_TXN_INFO_DEBUG
    if(childObj){
        fprintf(stderr,"GC{TrcVer}: Object %08.8x --> shadowed by --> %08.8x \n",p_object,(childObj));
        fflush(stderr);
    }
#endif //VERIFY_TXN_INFO_DEBUG
    return childObj;
}
#endif //WRITE_BUFFERING

void Garbage_Collector::trace_verify_sub_heap(std::stack<Partial_Reveal_Object *,std::vector<Partial_Reveal_Object *> > &obj_stack, bool before_gc) {
	while(!obj_stack.empty()) {
		Partial_Reveal_Object *p_obj = obj_stack.top();
		obj_stack.pop();

#ifdef FILTER_NON_HEAP
	    if ( !p_global_gc->is_in_heap(p_obj) ) {
		    continue; // don't trace objects not in the heap
		}
#endif // FILTER_NON_HEAP
		if (mark_object_header(p_obj)) {
			// verify the newly found object

			verify_object(p_obj, get_object_size_bytes(p_obj));

			if (before_gc) {
				_num_live_objects_found_by_first_trace_heap++;
				_live_objects_found_by_first_trace_heap->add_entry(p_obj);
			} else {
				_num_live_objects_found_by_second_trace_heap++;
				_live_objects_found_by_second_trace_heap->add_entry(p_obj);
			}
		} else {
			continue;
		}

#ifdef WRITE_BUFFERING
		/* STB 10-Aug-2007: If the STM uses shadow copies, then the transaction_info field
		 * may need to be treated as a slot
		 */
		Partial_Reveal_Object* txn_info_obj = get_transaction_info_target(p_obj);
		if(txn_info_obj) {
			obj_stack.push(txn_info_obj);
		}
#endif // WRITE_BUFFERING

		if (is_array(p_obj)) {
			if (is_array_of_primitives(p_obj)) {
				continue;
			}
			int32 array_length = vector_get_length_with_vt((Vector_Handle)p_obj,p_obj->vt());
			for (int32 i=array_length-1; i>=0; i--)
			{
				Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_obj, i, p_obj->vt()));
				if (!p_element.is_null()) {
					obj_stack.push(p_element.dereference());
				}
			}

		}

		unsigned int *offset_scanner = init_object_scanner (p_obj);
		Slot pp_target_object(NULL);

		while (pp_target_object.set(p_get_ref(offset_scanner, p_obj)) != NULL) {
			// Move the scanner to the next reference.
			offset_scanner = p_next_ref (offset_scanner);
			if (!pp_target_object.is_null()) {
				obj_stack.push(pp_target_object.dereference());
			}
		}
	}
}

unsigned int Garbage_Collector::trace_verify_heap(bool before_gc) {
    Partial_Reveal_Object *p_obj = NULL;
    // Reset instead of deleting and reallocating.
    if (before_gc) {
        _num_live_objects_found_by_first_trace_heap = 0;
        if (_live_objects_found_by_first_trace_heap == NULL) {
            _live_objects_found_by_first_trace_heap = new Object_List();
        }
        _live_objects_found_by_first_trace_heap->reset();
        assert(_live_objects_found_by_first_trace_heap);
    } else {
        _num_live_objects_found_by_second_trace_heap = 0;
        if (_live_objects_found_by_second_trace_heap == NULL) {
            _live_objects_found_by_second_trace_heap = new Object_List();
        }
        _live_objects_found_by_second_trace_heap->reset();
        assert(_live_objects_found_by_second_trace_heap);
    }

	std::stack<Partial_Reveal_Object *,std::vector<Partial_Reveal_Object *> > obj_stack;

    unsigned int root_index = 0;
    for (root_index = 0; root_index < _num_roots; root_index++) {
        p_obj = *(_verify_array_of_roots[root_index]);
        if (p_obj) {
#ifdef FILTER_NON_HEAP
            if ( p_global_gc->is_in_heap(p_obj) ) {
#endif // FILTER_NON_HEAP
                if(!(GC_BLOCK_INFO(p_obj)->in_nursery_p || GC_BLOCK_INFO(p_obj)->in_los_p || GC_BLOCK_INFO(p_obj)->is_single_object_block)) {
                    assert(0);
                }
#ifdef FILTER_NON_HEAP
            }
#endif // FILTER_NON_HEAP
			obj_stack.push(p_obj);
            trace_verify_sub_heap(obj_stack, before_gc);
			assert(obj_stack.empty());
        }
    } // for

    p_obj = NULL;

    unsigned int result = 0;
    if (before_gc) {
        _live_objects_found_by_first_trace_heap->rewind();
        while ((p_obj = _live_objects_found_by_first_trace_heap->next())) {
            assert((p_obj->isMarked()));
            p_obj->unmark();
        }
        result = _num_live_objects_found_by_first_trace_heap;
    } else {
        _live_objects_found_by_second_trace_heap->rewind();
        while ((p_obj = _live_objects_found_by_second_trace_heap->next())) {
            assert((p_obj->isMarked()));
            p_obj->unmark();
        }
        result = _num_live_objects_found_by_second_trace_heap;
    }

    return result;
}


void verify_marks_for_live_object(Partial_Reveal_Object *p_obj) {
    unsigned int object_index_byte = 0;
    unsigned int bit_index_into_byte = 0;
    get_mark_byte_and_mark_bit_index_for_object(p_obj, &object_index_byte, &bit_index_into_byte);
    uint8 *p_byte = &(GC_BLOCK_INFO(p_obj)->mark_bit_vector[object_index_byte]);

    // the base of the object is marked...
    if ((*p_byte & (1 << bit_index_into_byte)) == 0) {
        // We have a problem, lets try to focus in on it first - is this a legal object?
        // This is never called while collecting characterizations.
        verify_object(p_obj, get_object_size_bytes(p_obj));
        orp_cout << "verify_marks_for_live_object() failed for base of object " << p_obj << std::endl;
        orp_cout << "In block " << GC_BLOCK_INFO(p_obj) << std::endl;
        int x = *p_byte;
        orp_cout << "The byte is " << x << " Now verify object and draw line." << std::endl;
        verify_object(p_obj, get_object_size_bytes(p_obj));
        orp_cout << "-----------------------------" << std::endl;
    }
    return;
}



void Garbage_Collector::verify_marks_for_all_lives() {
#ifdef _DEBUG
    // Use the object lists collected by each thread to get to all the marked objects..
    for (unsigned int j = 0; j < g_num_cpus; j++) {
        for (unsigned int k = 0; k < _gc_threads[j]->get_num_marked_objects(); k++) {
            verify_marks_for_live_object(_gc_threads[j]->get_marked_object(k));
        }
    }
#endif

    if (verify_gc) {
        // If the verifier is turned on, use the trace heap utility.
        if (_live_objects_found_by_first_trace_heap) {
            _live_objects_found_by_first_trace_heap->rewind();
            Partial_Reveal_Object *p_obj = NULL;
            while ((p_obj = _live_objects_found_by_first_trace_heap->next())) {
                verify_marks_for_live_object(p_obj);
            } // while
        } // if
    } // if
}


//////////////////////////////////  D U M P   R O U T I N E S //////////////////////////////////


const int NUM_OBJECTS_PER_LINE = 4;

static FILE *fp = NULL;

void close_dump_file() {
    assert(fp);
    fclose(fp);
    fp = NULL;
}

// dumps to the screen all lives in a compacted block after compaction is complete..
void dump_object_layouts_in_compacted_block(block_info *block, unsigned int num_lives) {
    // This is not used and is broken since pair tables are now SSB structures that
    // don't remove duplicates
    assert (0);
}


//////////////////////////////////  T R A C E   R O U T I N E S ////////////////////////////////

// gc_v4.h references these routines for debug, for non-debug they are inlined into empty routines
#ifdef _DEBUG
// If GC_DEBUG is 0 then this is an inline empty latency free routine..

// We have a selection of 4 objects that are normally NULL.
// If we want to trace an object we set the address of one
// of the objectn to the object we want to trace.

void *object1 = (void *)0x0;                // The bogus object
void *object2 = (void *)0x0;                // the object that should be moved to the bogus object    //
void *object3 = (void *)0x0;                // The object that obliterate the object to be moved to the bogus object. ..
void *object4 = (void *)0x0;                // The bogus slot in company points to the middle of this object
void *object5 = (void *)0x0;
void *object6 = (void *)0x0;
void *object7 = (void *)0x0;

// Set this object to be traced if we aren't already tracing 4 objects.
void trace_object (void *obj_to_trace) {
    if (!object1) {
        object1 = obj_to_trace;
    } else if (!object2) {
        object2 = obj_to_trace;
    } else if (!object3) {
        object3 = obj_to_trace;
    } else if (!object4) {
        object4 = obj_to_trace;
    } else {
        orp_cout << "We can trace only 4 objects for now, this one ignored. " << std::endl;
    }
}

// This is called from a lot of places and if the object passed in
// and non-NULL and equal one of the objects being traced then
// string_x is printed out along with the object.
void gc_trace (void *object, const char *string_x) {
    // The NULL object is not interesting
    if (!object) {
        return;
    }
    if ((object==object1)||(object==object2)||(object==object3)||(object==object4)||(object==object5)||(object==object6)||(object==object7)) {
        orp_cout << " GC Trace " << object << " " << string_x << std::endl;
#if 0
        if (((Partial_Reveal_Object *)object)->vt()) {
            printf ( "The vt is %p.\n", ((Partial_Reveal_Object *)object)->vt());
            // This breaks with the new ->() in ways the debugger can't deal with.
            //printf (" of type %s \n", ((Partial_Reveal_Object *)object)->vt()->gc_class_name );
        } else {
            // On IA32, some of the JITs don't pass the base of the object instead
            // they pass the slot. This is not what the interface says but for historical
            // reasons (Changing the JIT would require too much effort) this has not
            // been done.
            orp_cout << " This object has a null vtable, it might be broken." << std::endl;
        }
#endif
    }
    return;
}

// Trace all allocations in a specific block. This is valuable when trying to find objects allocated in the same block as a problem
// object or perhaps a bogus address.
block_info *block_alloc_1 = (block_info *)0x0; //0x000006fbf6ff0000; // The interesting compress enumeration bug items

void gc_trace_allocation (void *object, const char *string_x) {
    if (object) {
        if (GC_BLOCK_INFO(object) == block_alloc_1) {
            orp_cout << "In gc trace block allocation with object " << object << string_x << std::endl;
        }
        gc_trace (object, string_x);
    }
}

void **object_slot1 = (void **)0x0;             // 00006fbfdb80118; // These are the interesting compress enumeration bug items
void **object_slot2 = (void **)0x0;
void **object_slot3 = (void **)0x0;
void **object_slot4 = (void **)0x0;
void **object_slot5 = (void **)0x0;

void gc_trace_slot (void **object_slot, void *object, const char *string_x) {

    if ((object_slot==object_slot1)||(object_slot==object_slot2)||(object_slot==object_slot3)
        ||(object_slot==object_slot4)||(object_slot==object_slot5)) {
        if (!object) {
            object = (void *) "NULL";
        }
        orp_cout << " GC Slot Trace " << object_slot << " with value " << object << " " << string_x << std::endl;
    }
    return;
}

block_info *block1 = (block_info *)0x0;  // (block_info *)0x000006fbf6ff0000; // These are the interesting compress enumeration bug items
block_info *block2 = (block_info *)0x0;
block_info *block3 = (block_info *)0x0;
block_info *block4 = (block_info *)0x0;

void gc_trace_block (void *the_block, const char *string_x) {
    assert (the_block);
    if (((block_info *)the_block == block1) || ((block_info *)the_block == block2) ||
        ((block_info *)the_block == block3) || ((block_info *)the_block == block4)) {
        orp_cout << (void *)the_block << " " << string_x  << "------ block trace --------" << std::endl;
    }
}

#endif // _DEBUG



bool is_vtable(Partial_Reveal_VTable *p_vtable) {
    if (p_vtable == 0) {
        return false;
    }

    if ((POINTER_SIZE_INT)p_vtable < 0x0100) {
        // Pick up random low bits.
        return false;
    }

    // Best guess is that it is legal.
    return true;
}

unsigned int get_object_size_bytes_with_vt(Partial_Reveal_Object *p_obj, Partial_Reveal_VTable *vt) {
	if((uintptr_t)vt < 1<<16) {
		return (uintptr_t)vt;
	}

    bool arrayp = is_vt_array(vt);
    if (arrayp) {
        unsigned int sz;
        sz = orp_vector_size(vt->get_gcvt()->gc_clss, vector_get_length_with_vt((Vector_Handle)p_obj,vt));
        return sz;
    } else {
        return vt->get_gcvt()->gc_allocated_size;
    }
}

// end file misc.cpp
