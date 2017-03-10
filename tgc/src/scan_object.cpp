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

//
// This file implements interfaces of how to scan an object.
//

// System header files
#include <iostream>
#include <assert.h>

// GC header files
#include "tgc/gcv4_synch.h"
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_v4.h"
#include "tgc/descendents.h"

GCEXPORT(Boolean, object_has_gc_slots)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return p_obj->vt()->get_gcvt()->gc_object_has_slots;
}

GCEXPORT(const char*, object_get_class_name)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return class_get_name(p_obj->vt()->get_gcvt()->gc_clss);
}

GCEXPORT(Boolean, object_is_array) (Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return is_array(p_obj);
}

GCEXPORT(Boolean, object_is_array_of_primitives)(Managed_Object_Handle obj)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return is_array_of_primitives(p_obj);
}


#if 0 //According to JMS, use vector routine to replace array scanner
//
// Return the last offset in the array that points to a reference.
// This will be decremented with each p_next_ref call.
//

GCEXPORT(unsigned, object_init_array_scanner) (Managed_Object_Handle array)
{
    Partial_Reveal_Object *p_array = (Partial_Reveal_Object *)array;
    return init_array_scanner(p_array);
}

//
// Move the scanner to the next ref.
//
GCEXPORT(unsigned, object_next_array_ref) (unsigned offset)
{
    return next_array_ref(offset);
}

//
// Take the offset returned from init_array_scanner and
// return the reference it referes to. Decrement the offset
// since we go through array backwards.
//
GCEXPORT(Managed_Object_Handle*, object_get_array_ref)(Managed_Object_Handle obj, unsigned offset)
{
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return (Managed_Object_Handle*)p_get_array_ref(p_obj, offset);
}

#endif //#if 0 //According to JMS, use vector routine to replace array scanner

//
//  Return a pointer to the array of object offsets associated
//  with this object.
//
GCEXPORT(unsigned*, object_init_object_scanner) (Managed_Object_Handle obj) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object*)obj;
    return (unsigned*)init_object_scanner(p_obj);
}
//
// Given an offset and the pointer gotten from initObjectScanner return
// the next location of a pointer in the object. If none are left return null;
//

// Offsets are unsigned int which should be a structure that is independent
// of the word length.

GCEXPORT(unsigned*, object_next_ref) (unsigned *offset) {
    return p_next_ref(offset);
}

GCEXPORT(Managed_Object_Handle*, object_get_ref) (unsigned *offset, Managed_Object_Handle obj) {
    Partial_Reveal_Object *p_obj = (Partial_Reveal_Object *)obj;
    return (Managed_Object_Handle*)p_get_ref(offset, p_obj);
}

/////End of scan_object.cpp
