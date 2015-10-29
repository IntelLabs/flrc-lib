/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>
#include <fstream>

// GC header files
#include "gc_v4.h"
#include "remembered_set.h"
#include "garbage_collector.h"
#include "gc_thread.h"
#include "mark.h"
#include "descendents.h"
#include "mark_stack.h"
#include "micro_nursery.h"

#ifdef IGNORE_SOME_ROOTS
extern unsigned num_roots_ignored;
extern unsigned roots_to_ignore;
#endif // IGNORE_SOME_ROOTS

static void scan_one_object(Partial_Reveal_Object *, GC_Thread *);
extern Field_Handle get_field_handle_from_offset(Partial_Reveal_VTable *vt, unsigned int offset);
#ifdef _DEBUG
extern bool g_profile;
extern std::ofstream * profile_out;
extern std::ofstream * live_dump;
#endif

#ifdef WRITE_BUFFERING
__declspec(dllimport) extern uint32 object_has_shadow_copy(void *);
__declspec(dllimport) extern uint32 object_get_transaction_info_offset();
#endif //WRITE_BUFFERING

void scan_one_slot (Slot p_slot, GC_Thread *gc_thread, bool is_weak, Partial_Reveal_Object *p_slot_obj) {
	assert(p_slot.get_value());
    // is_null() also checks for tagged integers.
    if (p_slot.is_null()) {
        return;
    }
	Partial_Reveal_Object *p_obj = p_slot.dereference();

    if(is_young_gen_collection()) {
        if(!is_young_gen(p_obj)) {
            return;
        }
    }

#if 1
	if(!local_nursery_size) {
	    REMOVE_INDIR_RES rir;
		// keep do indirection replacement until there are no more
		while((rir = remove_one_indirection(p_obj, p_slot, 7)) == RI_REPLACE_OBJ);
		if(rir == RI_REPLACE_NON_OBJ) {
			// New value is not a pointer so return.
			return;
		}
	}
#else
    REMOVE_INDIR_RES rir;
#ifdef PUB_PRIV
    pub_priv_retry:
    GC_Thread_Info *tls_for_gc;
    if(g_use_pub_priv) {
        if(p_global_gc->is_in_heap((Partial_Reveal_Object*)p_slot.get_value())) {
            block_info *bi = GC_BLOCK_INFO(p_slot.get_value());
            tls_for_gc = (GC_Thread_Info *)bi->thread_owner;
	        if(tls_for_gc != NULL) {
                GC_Small_Nursery_Info *private_nursery = tls_for_gc->get_private_nursery();
                pn_info *local_collector = private_nursery->local_gc_info;
                switch(get_object_location(p_obj,local_collector)) {
                case PUBLIC_HEAP:
                    break;
                case GLOBAL:
                    break;
                case PRIVATE_HEAP:
                    break;
                case PRIVATE_NURSERY:
                    return;
                }

                rir = remove_one_indirection_not_pn(p_obj, p_slot, local_collector, 1);
                if(rir == RI_REPLACE_NON_OBJ) {
                    return;
                }
                if(rir == RI_REPLACE_OBJ) {
                    goto pub_priv_retry;
                }
            } else {
                rir = remove_one_indirection(p_obj, p_slot, 2);
                if(rir == RI_REPLACE_NON_OBJ) {
                    // New value is not a pointer so return.
                    return;
                }
                if(rir == RI_REPLACE_OBJ) {
                    goto pub_priv_retry;
                }
            }
        } else {
            rir = remove_one_indirection(p_obj, p_slot, 3);
            if(rir == RI_REPLACE_NON_OBJ) {
                // New value is not a pointer so return.
                return;
            }
            if(rir == RI_REPLACE_OBJ) {
                goto pub_priv_retry;
            }
        }
    } else {
        rir = remove_one_indirection(p_obj, p_slot, 4);
        if(rir == RI_REPLACE_NON_OBJ) {
            // New value is not a pointer so return.
            return;
        }
        if(rir == RI_REPLACE_OBJ) {
            goto pub_priv_retry;
        }
    }
#else
	// keep do indirection replacement until there are no more
	while((rir = remove_one_indirection(p_obj, p_slot, 7)) == RI_REPLACE_OBJ);
    if(rir == RI_REPLACE_NON_OBJ) {
        // New value is not a pointer so return.
        return;
    }
#endif // PUB_PRIV
#endif

    if(is_weak) {
        gc_thread->add_weak_slot((Partial_Reveal_Object**)p_slot.get_value());
        return;
    }

#ifdef _DEBUG
    if(live_dump) {
        *live_dump << p_global_gc->get_gc_num() << " 1 " << p_obj << " " << p_slot_obj << "\n";
    }
#endif

#ifdef FILTER_NON_HEAP
    if ( !p_global_gc->is_in_heap(p_obj) ) {
        // In cheney mode, you can have heap to private nursery pointers in the middle of a major GC.
        // Here we filter those because the main GC routine has already handled objects in the PN pointing
        // into the heap.
        if(g_cheney) {
            if(p_global_gc->is_in_private_nursery(p_obj)) {
                return;
            }
        }
#ifdef GC_DO_NOT_USE_MARK_STACK
		// Whichever thread marks an object, scans it too....
		scan_one_object(p_obj, gc_thread, false);
#else
		// Add to mark stack
		MARK_STACK *ms = gc_thread->get_mark_stack();
		if (push_bottom_on_local_mark_stack(p_obj, ms) == false) {
			printf("Mark stack overflowed\n");
			orp_exit(17020);
		}
#endif // GC_DO_NOT_USE_MARK_STACK
        return; // don't mark objects not in the heap
    }
#endif // FILTER_NON_HEAP

	gc_trace(p_obj, " scan_one_slot(): some slot pointed to this object...\n");

	// I will try to be the first to mark it. If success..I will add it to my mark
	// stack, since I will then be owning it.

	if (mark_object_in_block(p_obj)) {
		gc_thread->add_to_marked_objects(p_obj);

#ifdef GC_DO_NOT_USE_MARK_STACK
		// Whichever thread marks an object, scans it too....
		scan_one_object(p_obj, gc_thread, false);
#else
		// Add to mark stack
		MARK_STACK *ms = gc_thread->get_mark_stack();
		if (push_bottom_on_local_mark_stack(p_obj, ms) == false) {
			printf("Mark stack overflowed\n");
			orp_exit(17020);
		}
#endif // GC_DO_NOT_USE_MARK_STACK
	}

//	if (gc_thread->is_compaction_turned_on_during_this_gc()) {	// Collect slots as we go
	    block_info *p_obj_block_info = GC_BLOCK_INFO(p_obj);
		if (gc_thread->_p_gc->is_compaction_block(p_obj_block_info)) {
            // JMS 2003-04-22.  I believe it's OK to pass p_slot as as argument, regardless of
            // whether it's a Partial_Reveal_Object** or a uint32* pointer.
			gc_thread->_p_gc->add_slot_to_compaction_block(p_slot, p_obj_block_info, gc_thread->get_id());
			if (stats_gc) {
				gc_thread->increment_num_slots_collected_for_later_fixing();
			}
		}
//	}
} // scan_one_slot

#ifdef WRITE_BUFFERING

static inline void
scan_object_transaction_info(Partial_Reveal_Object *p_object, GC_Thread *gc_thread) {
    if(object_get_transaction_info_target((struct ManagedObject*)p_object)){
        uint32 txnOffset = object_get_transaction_info_offset();
        void* txnRecPtr = (void*)((Byte*)p_object + txnOffset);
#ifdef SCAN_TXN_INFO_DEBUG
        fprintf(stderr,"GC{Scan}: Object %08.8x --> shadowed by --> %08.8x \n",p_object,(*(uint32*)txnRecPtr));
        fflush(stderr);
#endif //SCAN_TXN_INFO_DEBUG
        Slot txnInfoSlot(txnRecPtr);
        // Create a slot for the txn_info, and scan it!
        scan_one_slot(txnInfoSlot,gc_thread);
    }
}
#endif

static inline void scan_one_array_object(Partial_Reveal_Object *p_object, GC_Thread *gc_thread,Partial_Reveal_VTable *obj_vt,struct GC_VTable_Info *obj_gcvt) {
    // If array is an array of primitives, then there are no references, so return.
    assert(!obj_gcvt->is_array_of_primitives());

    Type_Info_Handle tih = class_get_element_type_info(obj_gcvt->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
        for (int32 i=array_length-1; i>=0; i--) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, obj_vt));
            scan_one_slot(p_element, gc_thread, false, p_object);
        }
    } else if(type_info_is_primitive(tih)) {
        // intentionally do nothing
    } else if(type_info_is_unboxed(tih)) {
        Class_Handle ech = type_info_get_class(tih);
        assert(ech);
        int first_elem_offset = vector_first_element_offset_unboxed(ech);
        int base_offset = (int)class_get_unboxed_data_offset(ech);
        int elem_size = class_element_size(obj_gcvt->gc_clss);
        int array_length = vector_get_length_with_vt((Vector_Handle)p_object,obj_vt);
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
                scan_one_slot (pp_target_object, gc_thread, false, p_object);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // handle weak refs
            offset_scanner = evt->get_gcvt()->gc_weak_ref_offset_array;
            while ((pp_target_object.set(p_get_ref(offset_scanner, (Partial_Reveal_Object*)cur_value_type_entry_as_object))) != NULL) {
                scan_one_slot (pp_target_object, gc_thread, true, p_object);
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }

            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else assert(!"Tried to scan an array of unknown internal type.");
}


static void scan_one_object(Partial_Reveal_Object *p_obj, GC_Thread *gc_thread) {
    bool in_heap = true;
	if ( !p_global_gc->is_in_heap(p_obj) ) {
        in_heap = false;
        std::pair<std::set<Partial_Reveal_Object*>::iterator,bool> res = gc_thread->m_global_marks.insert(p_obj);
        if(!res.second) {
            // object was already marked so just return
            return;
        }
    }

    Partial_Reveal_VTable *obj_vt   = p_obj->vt();
    struct GC_VTable_Info *obj_gcvt = obj_vt->get_gcvt();

#ifdef _DEBUG
    if(gc_mark_profiler) {
        gc_mark_profiler(p_global_gc->get_gc_num(), gc_thread->get_id(), p_obj);
    }
#endif // _DEBUG

    if(in_heap) {
#ifdef _DEBUG
        if(profile_out) {
            *profile_out << p_global_gc->get_gc_num() << " 0 " << obj_vt << " " << p_obj << " " << get_object_size_bytes_with_vt(p_obj,obj_vt) << "\n";
        }
#endif
	    // Object had better be marked.
	    assert(is_object_marked(p_obj) == true);

#ifdef WRITE_BUFFERING
        /* STB 09-Aug-2007: The STM may be using shadow copies that are heap allocated,
           but potentially only reachable from the transaction_info field of the object.
           check if the transaction_info contains a forwarding pointer, and scan the slot
           if appropriate
         */
        scan_object_transaction_info(p_obj,gc_thread);
#endif // WRITE_BUFFERING
    }

    if (obj_gcvt->has_slots()) {
        if (obj_gcvt->is_array() && !obj_gcvt->is_array_of_primitives()) {
            scan_one_array_object(p_obj, gc_thread,obj_vt,obj_gcvt);
        }

		unsigned int *offset_scanner = init_object_scanner (p_obj);
        Slot pp_target_object(NULL);
        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            scan_one_slot (pp_target_object, gc_thread, false, p_obj);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }

        // handle weak refs
		offset_scanner = init_object_scanner_weak (p_obj);
        while ((pp_target_object.set(p_get_ref(offset_scanner, p_obj))) != NULL) {
            // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
            // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
            // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
            // and last time...
            // If parent is not a delinquent type we are not interested in this edge at all....
            scan_one_slot (pp_target_object, gc_thread, true, p_obj);
            // Move the scanner to the next reference.
            offset_scanner = p_next_ref (offset_scanner);
        }
    }

	gc_trace(p_obj, " scan_one_object(): this object has been scanned...\n");
} // scan_one_object

void process_mark_stack(GC_Thread *gc_thread, MARK_STACK *ms) {
    Partial_Reveal_Object *p_obj;

    p_obj = pop_bottom_object_from_local_mark_stack(ms);
    while (p_obj != NULL) {
        if(is_young_gen_collection()) {
            if(!is_young_gen(p_obj)) {
                p_obj = pop_bottom_object_from_local_mark_stack(ms);
                continue;
            }
        }
        gc_trace(p_obj, " in mark_scan_heap taking object off mark stack, about to scan it.");
        scan_one_object(p_obj, gc_thread);
        p_obj = pop_bottom_object_from_local_mark_stack(ms);
    }
}

void process_object(Partial_Reveal_Object *p_obj, GC_Thread *gc_thread, MARK_STACK *ms) {
    if(is_young_gen_collection()) {
        if(!is_young_gen(p_obj)) {
            return;
        }
    }

    bool do_mark = true;
#ifdef FILTER_NON_HEAP
	// THIS IS LARGELY UNTESTED IN STANDARD TGC builds but should be ok.
	// IF YOU GET A PROBLEM TRY REMOVING THIS!
	if ( !p_global_gc->is_in_heap(p_obj) ) {
        scan_one_object(p_obj, gc_thread);
        do_mark = false;
	}
#endif // FILTER_NON_HEAP
    // Mark the object. Means, it will be added to the mark stack, and later scanned
    if (do_mark && mark_object_in_block(p_obj)) {
        gc_thread->add_to_marked_objects(p_obj);
        gc_trace(p_obj, " in mark_scan_heap putting object onto mark stack.");
        scan_one_object(p_obj, gc_thread);
    }

    process_mark_stack(gc_thread, ms);
} // process_object


// Get one root at a time from GC and mark/scan it
void mark_scan_heap(GC_Thread *gc_thread) {
    MARK_STACK *ms = gc_thread->get_mark_stack();
    Partial_Reveal_Object **pp_slot;
    pp_slot = gc_thread->_p_gc->get_fresh_root_to_trace();
    while (pp_slot != NULL) {
        Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)((POINTER_SIZE_INT)(*pp_slot) & ~(0x3));

        gc_trace(p_obj, " mark_scan_heap(): this object was found to be a root...\n");

#ifdef _DEBUG
        if(live_dump) {
            *live_dump << p_global_gc->get_gc_num() << " 0 " << p_obj << "\n";
        }
#endif

        process_object(p_obj, gc_thread, ms);

        assert(mark_stack_is_empty(ms));

        // MARK STACK IS EMPTY....GO AND GET SOME MORE ROOTS FROM GC
        pp_slot = gc_thread->_p_gc->get_fresh_root_to_trace();
    } // while (true)

	gc_thread->_p_gc->this_gc_cheney_spaces.mark_phase_process(gc_thread);

#if 0
    if (stats_gc) {
        printf ("%u: collected %u slots for later fixing\n",  gc_thread->get_id(), gc_thread->get_num_slots_collected_for_later_fixing() );
        //		orp_cout << gc_thread->get_id() << ": collected " << gc_thread->get_num_slots_collected_for_later_fixing() << " slots for later fixing\n";
    }
#endif
}

void cheney_spaces::mark_phase_process(GC_Thread *gc_thread) {
    cheney_spaces::iterator cspace_iter;
    for(cspace_iter  = begin();
        cspace_iter != end();
        ++cspace_iter) {
		cspace_iter.get_current().mark_phase_process(gc_thread);
    }
}

void cheney_space_pair::mark_phase_process(GC_Thread *gc_thread) {
	if(!marked) {
        // Atomically grab a root....multiple GC threads compete for roots
        if (LockedCompareExchangePOINTER_SIZE_INT(
            (POINTER_SIZE_INT *)&marked,
            (POINTER_SIZE_INT) 1,
            (POINTER_SIZE_INT) 0
            ) == (POINTER_SIZE_INT) 0) {
			pub_space.mark_phase_process(gc_thread);
			im_space.mark_phase_process(gc_thread);
        }
	}
}

void cheney_space::mark_phase_process(GC_Thread *gc_thread) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)begin;
    MARK_STACK *ms = gc_thread->get_mark_stack();

    while(p_obj < end) {
        Partial_Reveal_VTable *vt = p_obj->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
        if(!alignment_vt(vt)) {
#ifdef _DEBUG
	        if(live_dump) {
		        *live_dump << p_global_gc->get_gc_num() << " 0 " << p_obj << "\n";
			}
#endif

			process_object(p_obj, gc_thread, ms);

			assert(mark_stack_is_empty(ms));
        }
        p_obj = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
    }
}

#if 0
void pn_space::mark_phase_process(GC_Thread *gc_thread) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)start;
    MARK_STACK *ms = gc_thread->get_mark_stack();

    while(p_obj < end) {
        Partial_Reveal_VTable *vt = p_obj->vt();
        unsigned obj_size = get_object_size_bytes_with_vt(p_obj,vt);
        if(!alignment_vt(vt)) {
#ifdef _DEBUG
	        if(live_dump) {
		        *live_dump << p_global_gc->get_gc_num() << " 0 " << p_obj << "\n";
			}
#endif

			process_object(p_obj, gc_thread, ms);

			assert(mark_stack_is_empty(ms));
        }
        p_obj = (Partial_Reveal_Object*)(((char*)p_obj) + obj_size);
    }
}
#endif

//////////////////////////////////  M A K E    P U B L I C   R O U T I N E S ////////////////////////////////

//
// These are not at all optimized but do have the funcationality required.
// Optimization can obviously avoid the call to orp_get_stm_properties.
//
Boolean is_public(Partial_Reveal_Object *obj) {
    TgcStmProperties props;
    orp_get_stm_properties(&props);
    uint32 offset = props.transaction_info_offset;
    uint32 *transaction_info_slot;
    if (obj == NULL) {
        return TRUE; // NULL is considered public.
    }
    transaction_info_slot = (uint32 *)((char *)obj + offset);
    if (*transaction_info_slot != props.private_object_indicator) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void set_public(Partial_Reveal_Object *obj) {
    TgcStmProperties props;
    orp_get_stm_properties(&props);
    uint32 offset = props.transaction_info_offset;
    uint32 *transaction_info_slot = (uint32 *)((char *)obj + offset);
    *transaction_info_slot = (((uintptr_t)obj) + props.initial_lock_value) & props.initial_lock_value_mask;
}

/*
 * gc_make_public takes an object that makes all the slots in it public.
 * It uses two utility routines is_public and set_public.
 *
 */

/*
 * We do not inline a lot of the obvious things here since they haven't popped up on
 * any of our VTune reports. If this routine does pop up then look to inline a lot of this
 * code.
 */
static void make_one_array_public(Partial_Reveal_Object *p_object, mark_stack_container *ms) {
    // If array is an array of primitives, then there are no references, so return.
    if (is_array_of_primitives(p_object)) {
        return;
    }

    Type_Info_Handle tih = class_get_element_type_info(p_object->vt()->get_gcvt()->gc_clss);
    if(type_info_is_reference(tih) ||
       type_info_is_vector(tih) ||
       type_info_is_general_array(tih)) {
        // Initialize the array scanner which will scan the array from the
        // top to the bottom. IE from the last element to the first element.

        int32 array_length = vector_get_length_with_vt((Vector_Handle)p_object,p_object->vt());
        for (int32 i=array_length-1; i>=0; i--) {
            Slot p_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_object, i, p_object->vt()));
            if ( !is_public(p_element.dereference()) ) {
                set_public(p_element.dereference());
                mark_stack_push(ms, p_element.dereference());
            }
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
                if (!is_public(pp_target_object.dereference())) {
                    set_public(pp_target_object.dereference());
                    mark_stack_push(ms, pp_target_object.dereference());
                }
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
            // advance to the next value struct
            cur_value_type_entry_as_object = (Byte*)cur_value_type_entry_as_object + elem_size;
        }
    } else {
        assert(!"Tried to scan an array of unknown internal type.");
    }
    return;
}

static void make_one_object_public(Partial_Reveal_Object *p_object, mark_stack_container *ms) {
    // Object had better be marked.
    assert(is_public(p_object));

    if (p_object->vt()->get_gcvt()->gc_object_has_slots) {
        if (is_array(p_object)) {
            make_one_array_public(p_object, ms);
        } else {
            unsigned int *offset_scanner = init_object_scanner (p_object);
            Slot pp_target_object(NULL);
            while ((pp_target_object.set(p_get_ref(offset_scanner, p_object))) != NULL) {
                // Each live object in the heap gets scanned exactly once...so each live EDGE in the heap gets looked at exactly once...
                // So, the logic here is independent of whether the object pointed to by this slot has already been reached and
                // marked by the collector...we still need to collect edge counts since this edge is being visited for the first
                // and last time...
                // If parent is not a delinquent type we are not interested in this edge at all....
                if (!is_public(pp_target_object.dereference())) {
                    set_public(pp_target_object.dereference());
                    mark_stack_push(ms, pp_target_object.dereference());
                }
                // Move the scanner to the next reference.
                offset_scanner = p_next_ref (offset_scanner);
            }
        }
    }

    gc_trace(p_object, " scan_one_object(): this object has been scanned...\n");

    return;
}

static void make_public_helper(Partial_Reveal_Object *obj, mark_stack_container *ms) {
    Partial_Reveal_Object *current_obj = obj;
    assert (is_public(obj));
    while (current_obj) {
        make_one_object_public(current_obj, ms);
        current_obj = mark_stack_pop(ms);
    }

    mark_stack_cleanup(ms);
}

GCEXPORT(void, gc_make_public) (Managed_Object_Handle object) {
    mark_stack_container ms_container;
    mark_stack ms;
    Partial_Reveal_Object *obj = (Partial_Reveal_Object *)object;

    mark_stack_init (&ms_container, &ms);
    set_public(obj);      /* Set to avoid infinite recursion */
    make_public_helper(obj, &ms_container);
}
