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
#include "tgc/gcv4_synch.h"

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
