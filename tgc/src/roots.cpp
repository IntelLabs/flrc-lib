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
#include "gcv4_synch.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


unsigned int num_roots_added = 0;
extern unsigned int g_root_index_hint;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* 
*  I have this root but the slot is only valid while the thread being enumerated is
*  is suspended so action. This means that the ref can't be saved, instead we must save
*  the *ref, which points to the object.
*
**/

#ifdef CONCURRENT
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <map>
#else
#include <..\stlport\map>
#endif
extern bool incremental_compaction;
#endif // CONCURRENT

extern bool separate_immutable;

void Garbage_Collector::gc_internal_add_root_set_entry(Partial_Reveal_Object **ref) {
    // Is is NULL.  The NULL reference is not interesting.
    if (*ref == Slot::managed_null()) {
        return;
    }

#ifdef CONCURRENT
	if(prtGetTls()) {
		printf("Got here for non-gc thread.\n");
	}

    if(separate_immutable && incremental_compaction) {
#ifndef NO_IMMUTABLE_UPDATES
		MovedObjectIterator moved_iter;
		moved_iter = g_moved_objects.find(*ref);
		if(moved_iter != g_moved_objects.end()) {
#ifdef _DEBUG
			char buf[100];
			sprintf(buf,"Updating relocated pointer giarse %p to %p.",*ref,moved_iter->second);
			gc_trace (*ref, buf);
			gc_trace (moved_iter->second.m_new_location, buf);
#endif // _DEBUG
			make_object_gray_in_concurrent_thread(*ref,g_concurrent_gray_list);
			*ref = moved_iter->second.m_new_location;
		}
#endif // NO_IMMUTABLE_UPDATES
	}
	make_object_gray_in_concurrent_thread(*ref,g_concurrent_gray_list);
#else  // CONCURRENT
    orp_synch_enter_critical_section(protect_roots);

    num_roots_added++;
    // Here I will just collect them in an array and then let the mark threads compete for it
    if (_num_roots >= num_root_limit) {
        expand_root_arrays();
    }
    
    // The eumeration needs to provide a set which means no duplicates. Remove duplicates here.
    
    assert (dup_removal_enum_hash_table->size() == _num_roots);
    if (dup_removal_enum_hash_table->add_entry_if_required((void *)ref)) {
        gc_trace_slot((void **)ref, (void *)*ref, "In gc_internal_add_root_set_entry.");
        gc_trace (*ref, "In gc_internal_add_root_set_entry with this object in a slot.");
        assert(*ref); //RLH
        _array_of_roots[_num_roots++] = ref;
    }

    orp_synch_leave_critical_section(protect_roots);
#endif // CONCURRENT
}

void Garbage_Collector::gc_internal_add_weak_root_set_entry(Partial_Reveal_Object **ref, int offset,Boolean is_short_weak) {
    // Is it NULL.  The NULL reference is not interesting.
    if ((Partial_Reveal_Object *)((char *)*ref - offset) == Slot::managed_null()) {
        return;
    }

    if(is_short_weak) {
        orp_synch_enter_critical_section(protect_short_weak_roots);
        m_short_weak_roots.push_back(MyPair((void **)ref, offset));
        orp_synch_leave_critical_section(protect_short_weak_roots);
    } else {
        orp_synch_enter_critical_section(protect_long_weak_roots);
        m_long_weak_roots.push_back(MyPair((void **)ref, offset));
        orp_synch_leave_critical_section(protect_long_weak_roots);
    }
}

Partial_Reveal_Object ** Garbage_Collector::get_fresh_root_to_trace() {
    // Search the array from left to right...
    for (unsigned int i = g_root_index_hint; i < _num_roots; i++) {
        volatile Partial_Reveal_Object **ref = (volatile Partial_Reveal_Object ** ) _array_of_roots[i];
        
        if (ref == NULL) {
            continue;
        }
        assert(*ref); //RLH
        // Atomically grab a root....multiple GC threads compete for roots
        if (LockedCompareExchangePOINTER_SIZE_INT( 
            (POINTER_SIZE_INT *)&_array_of_roots[i],
            (POINTER_SIZE_INT) NULL,
            (POINTER_SIZE_INT) ref
            ) == (POINTER_SIZE_INT) ref) {
            
			g_root_index_hint = i+1;
            assert(ref);
            if (ref) {
                gc_trace_slot((void **)ref, (void *)*ref, "In get_fresh_root_to_trace.");
                return (Partial_Reveal_Object **) ref;
            }
        }
    }
    
    return NULL;
}


void Garbage_Collector::prepare_root_containers() {
    // Zero out array of roots only as many were recived the last time around
    memset(_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * _num_roots);
    _num_roots = 0;
    // Clear the hash table used to remove duplicates from the enumerations.
    dup_removal_enum_hash_table->empty_all();
    // clear out the previous set of weak roots
    m_short_weak_roots.clear();
    m_long_weak_roots.clear();
}


void Garbage_Collector::roots_init() {
    memset(_array_of_roots, 0, sizeof(Partial_Reveal_Object **) * num_root_limit);
}
