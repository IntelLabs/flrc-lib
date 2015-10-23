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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Override the add_entry to do check to ensure pointer is valid.

#if 0
//
// Clone myself.
//
Remembered_Set *
Remembered_Set::p_clone()
{
    Remembered_Set *p_cloned_rs = new Remembered_Set();

    rewind();
    
    Slot pp_obj_ref(NULL);
    while (pp_obj_ref.set(next().get_value()) != NULL) {
        p_cloned_rs->add_entry(pp_obj_ref);
    }
    rewind();
    p_cloned_rs->rewind();

    return p_cloned_rs;
}
#endif

#if 0 // UNNECESSARY ??
//
// This is a higher-speed version of the step-generation's
// _reflect_tenuring_in_write_barrier. It searches for entries
// into younger space that have to be updated due to the recent
// tenuring, and updates appropriately.
//
void Remembered_Set::reflect_tenuring(Partial_Reveal_Object *p_old,
                                      Train_Generation  *p_mature_generation) 
{       

    for (int index = 0; index < _size_in_entries; index++) {

        Partial_Reveal_Object **pp_ref = (Partial_Reveal_Object **)_table[index];

        if ((pp_ref != NULL) && (*pp_ref == p_old)) {
            //
            // OK, we got a match. Update this to reflect the recent tenuring.
            //
            update_reference_forwarded(pp_ref);
            //
            // Remove this entry from the remembered set.
            //
            _table[index] = 0;
            //
            // May need to update inter-car remembered sets
            //
            p_mature_generation->add_entry_to_generation_write_barriers(pp_ref,
                                                                        *pp_ref);
        }
    }
}
#endif




// end file gc\remembered_set.cpp


