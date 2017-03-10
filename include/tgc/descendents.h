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

#ifndef _DESCENDENTS_H_
#define _DESCENDENTS_H_

//
// Format of the descendents of an object.
//
#include "tgc/gc_header.h"
#include "tgc/remembered_set.h"
#include "tgc/object_list.h" // needed by garbage_collector.h
#include "tgc/garbage_collector.h" // for the declaration of p_global_gc

// inline them for release code where GC_DEBUG>0.

//
// Return the last offset in the array that points to a reference.
// This will be decremented with each p_next_ref call.
//

inline unsigned int
init_array_scanner (Partial_Reveal_Object *p_array) {
    assert(!is_array_of_primitives(p_array));

    assert (is_array(p_array));

    unsigned the_number_of_descendents = vector_get_length_with_vt((Vector_Handle)p_array,p_array->vt());
    Slot last_element(vector_get_element_address_ref_with_vt((Vector_Handle)p_array, the_number_of_descendents - 1, p_array->vt()));
    return (unsigned int) (((char *) last_element.get_value()) - ((char *) p_array));
}

//
// Move the scanner to the next ref.
//

inline unsigned int next_array_ref (unsigned offset) {
    return (offset - sizeof (Managed_Object_Handle));
}

//
//  Return a pointer to the array of object offsets associated
//  with this object.
//

inline unsigned int *
p_next_ref (unsigned *offset) {
    return (unsigned int *)((Byte *)offset + sizeof (unsigned));
}

extern bool g_treat_wpo_as_normal;

inline unsigned int *
init_object_scanner_from_vt (struct Partial_Reveal_VTable *vt) {
    unsigned int *ret = vt->get_gcvt()->gc_ref_offset_array;
    if(!g_treat_wpo_as_normal) {
        while(vt == wpo_vtable && *ret != 0 && *ret < (4*sizeof(void*))) {
            ret = p_next_ref (ret);
        }
    }
    return ret;
}

inline unsigned int *
init_object_scanner (Partial_Reveal_Object *the_object) {
    return init_object_scanner_from_vt(the_object->vt());
}

inline unsigned int *
init_object_scanner_weak_from_vt (struct Partial_Reveal_VTable *vt) {
    unsigned int *ret = vt->get_gcvt()->gc_weak_ref_offset_array;
    if(!g_treat_wpo_as_normal) {
        while(vt == wpo_vtable && *ret != 0 && *ret < (4*sizeof(void*))) {
            ret = p_next_ref (ret);
        }
    }
    return ret;
}

inline unsigned int *
init_object_scanner_weak (Partial_Reveal_Object *the_object) {
    return init_object_scanner_weak_from_vt(the_object->vt());
}

//
// Given an offset and the pointer gotten from initObjectScanner return
// the next location of a pointer in the object. If none are left return null;
//

// Offsets are unsigned int which should be a structure that is independent
// of the word length.

inline void *
p_get_ref(unsigned *offset, Partial_Reveal_Object *myObject) {
    if (*offset == 0) { // We are at the end of the offsets.
        return NULL;
    }
    // extract the next location
    return (void *) ((Byte *) myObject + *offset);
}


#ifndef GC_NO_FUSE
//
// Initials interator for fusing objects.
//
inline unsigned int *
init_fused_object_scanner (Partial_Reveal_Object *the_object) {
    assert(the_object->vt()->get_gcvt()->gc_fuse_info != NULL);
    return the_object->vt()->get_gcvt()->gc_fuse_info;
}
#endif

#endif // _DESCENDENTS_H_
